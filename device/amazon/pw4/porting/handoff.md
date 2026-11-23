# PW4 交接模块（第 2 阶段，进行中）

交接模块在原厂内核里把 Readom 的内核、启动参数和 initramfs 放进内存，再跳过去。不写分区、不改引导。
PW4 版沿用 Readom PW3 的全部机制，以 `PWS_PROFILE` 3（PW4 真机）和 4（QEMU 替身）加进同一份代码。
PW3 的 profile 1、2 走原来的分支，行为不变：原有宿主机测试全部通过，改动都放在 `#if PWS_FDT_BOOT` 里。

## 和 PW3 的差别

| | PW3（3.0.35） | PW4（4.1.15，`boot/profiles/pw4-5.18.1.1.1.json`） |
|---|---|---|
| 启动参数 | ATAG，放在 `base+0x1000` | 设备树 blob，放在 `base+63 MiB`（initramfs 下面那 1 MiB），r1 = `~0`，r2 = blob 地址 |
| 校验 | `pw_atags_check` + `pw_plan_validate` | `pw4_fdt.c`：`pw_fdt_parse` + `pw_plan_validate_fdt` |
| 机型 | 机器号 4091 | 设备树 `compatible = "fsl,imx6sll"`，并要求只有 1 个 CPU |
| 内存划分 | 3G/1G，模块自建 1:1 窗口 | 2G/2G，`PAGE_OFFSET == PHYS_OFFSET`，lowmem 本身 1:1 |
| 私有页表 | 清 `[0, 0xbf0)`，抄 `[0xbf0, 4096)` | 清用户半边 `[0, 0x7f0)`，抄 `[0x7f0, 4096)`（模块区起），控制页所在段在抄过的部分里改写成 1:1 非缓存段（`private-table.h`） |
| SMP | 内核是 UP | 内核是 SMP（`SMP_ON_UP`），挂起回调里要求 `num_online_cpus() == 1`；TTBR 走表属性接受 UP 的 `0x59` 和 SMP 的 `0x6a` |
| L2 回调 | `flush_all` + `disable` + `inv_all` | 4.1 没有 `inv_all`：`l2c210_flush_all` + `l2c310_disable`（后者先按路清+无效再关；新内核 `l2c_enable` 开之前还会再无效一次） |
| 原厂函数锁定 | `pm_test_show`、3 个 L2 函数的长度和哈希 | `pm_test_show`、2 个 L2 函数；长度取自设备 kallsyms（116、96、40 字节），哈希取自设备内存 |
| `pm_mutex` | 导出 | 4.1 没导出，用 `kallsyms_lookup_name` 取 |
| DMA 主设备门控 | i.MX6SL 的 14 个 | i.MX6SLL 的 14 个（`clk-imx6sll.c`）：sdma、epdc、lcdif pix/apb、pxp、csi、ssi1-3、usboh3、usdhc1-3、spdif；DCP 只在 CCM 门开着时才读 |
| USB 空闲判据 | 锁定了 PW3 控制器修订号 | 还没锁定，所以 USB 门必须是关的 |

不变的部分：在 `pm_test=core` 的挂起里、所有驱动已挂起之后的 syscore 回调中动手；PROBE 走同一圈但不跳；
分配源页时避开解压器、目的地和 blob 所在的范围；拷贝表逐字核对；拷贝程序（`transition.S`）原样。

## 现在的状态

- 对着原厂内核原样编译的树（`Module.symvers` 和设备 CRC 一致）编出 `pw3_first_kernel.ko`，`-Werror` 通过，
  46 个引用的内核符号原厂全部导出，vermagic `4.1.15-lab126 SMP preempt mod_unload modversions ARMv7 p2v8`
- `tests/test_fdt.py`：22 条正反用例、原厂设备树实测、3000 个损坏 blob（ASan + UBSan 下只返回错误码）
  —— 需要 dtc：`python3 boot/handoff/tests/test_fdt.py --dtc <kernel-out>/scripts/dtc/dtc`
- `tests/test-allocation-ranges-fdt.c`（已进 `tools/run-tests.py`）：blob 那 1 MiB 不分配
- 在 QEMU 里端到端跑通（见下面第 4 条）；**没有在 Kindle 上加载过**

## 上真机之前还差的

1. ~~原厂函数哈希~~ 已填（2026-11-23）：从第二轮探测的 `/dev/mem` 页切出（2G/2G 下虚拟地址即物理地址），
   反汇编核对过边界：`pm_test_show` 116 字节（含字面量池）、`l2c210_flush_all` 96 字节（`BUG_ON` 中断、按路清+无效、
   同步）、`l2c310_disable` 40 字节（full-line-zero 检查后尾跳 `l2c_disable`）。同一份转储里 `outer_cache` 的
   `flush_all`/`disable` 正是这两个地址，`write_sec` 为空：L2 直接由安全态写，不经 SMC。
2. ~~解压器占用~~ 已完成：`derive_kernel_footprint.py --profile pw4`。4.1 的搬移代码和越界判断与 3.0 相同，
   差别是 `AUTO_ZRELADDR`（`r4 = (pc & 0xf8000000) + 0x8000`，zImage 放在 `base+0x8000` 时就是它本身）
   和镜像起点符号改为 `_text`；锁定 4.1 的 `head.S`、`vmlinux.lds.S` 哈希；安卓内核改用 gzip。
   当前安卓内核：zImage 6,953,832 字节，解压后 14,160,640 字节，占用 `0x80004000`–`0x8143d000`，
   与 63 MiB 处的 blob、64 MiB 处的 initramfs 不重叠。
3. ~~设备端生成设备树~~ 已完成：`dd_fdt()`（`boot/devicedata`，`-DPW_DEVICE_TREE` 编进去，PW3 的程序不变）
   读 `/sys/firmware/fdt`，核对 `/proc/cmdline` 是这台机器的，再由 `pw_fdt_rewrite()`（`pw4_fdt.c`）换掉
   `/chosen` 的 bootargs 和 initrd，去掉原厂引导给自己载荷做的 `/memreserve/`（碰到旧 initrd 的、落在交接区
   低 128 MiB 里的，即 `0x83f077b0` 那份旧 DTB；4.1 会自己保留它启动用的 blob），其余节点一字不改。
   用原厂设备树实测：输出只剩 EPDC 那条保留，整套计划（含真实解压占用）通过 `pw_plan_validate_fdt`。
   测试：`device-data-fdt`、`pw4-fdt`（改写幂等、补缺的 `/chosen`、拒绝坏输入）。
4. ~~QEMU~~ 已跑通（2026-11-23）：`mcimx6ul-evk` 上第一级 4.1（2G/2G、SMP_ON_UP）加载 profile 4 模块、拒绝四种坏输入、
   两次 PROBE、COMMIT，第二级安卓内核（3G/1G）拿到构建的命令行和两段 initramfs 起来。见 `qemu/README.md`。
   QEMU 的 i.MX6UL 自带挂起支持，不需要 `qemu-pm.c` 那个假的挂起提供者。
5. **设备上的 PROBE** —— 第一轮已跑（2026-11-23，试运行版模块，跳转未编入）：
   - 通过：设备上 `dd_fdt` 生成设备树（32,664 字节）；模块 SEAL（内核、命令行、initramfs、三个原厂函数哈希、计划、
     解压占用）和 PREPARE 全过，1707 个载荷页零碰撞；`pm_test=core` 挂起/恢复干净（Wi-Fi、Falcon 存储、触摸依次
     挂起、恢复）；挂起回调里页表、栈、CPU 模式检查 `backend_result=0`；卸载、`pm_test` 复原
   - 拒绝 1：PRRR 实测 `0x000a81a8`（`SMP_ON_UP` 在单核上走 UP 那套；TTBR 属性 `0x59`、ACTLR `0xe`），已改锁定值
   - 拒绝 2：DCP `CTRL=0x30c0000f`、通道全开，但 STAT 无活动、信号量全 0（没有排队的描述符）；PW4 原厂驱动开了上下文
     缓存（bit 22-23），按实测把"空闲"定为无任务、无活动
   - 拒绝 3：uSDHC1 门开着——它接 Wi-Fi（`2190000.usdhc`），bcmdhd 加载时挂起中保持供电。规则不放宽，启动器先关 Wi-Fi
   - USB 门关（拔线）；eMMC 所在 uSDHC2、EPDC、PXP、SDMA 等全部关
   - 第二轮（关 Wi-Fi）：CPU、DCP 放行；仍有两个门开着——`usboh3`（两个 OTG 控制器 ID `0xe4a1fa05`、
     run/stop 清零、无端点工作）和 uSDHC1（Wi-Fi 驱动已卸载，门仍开）。试运行程序当时打印的 `PROBE_OK`
     判据比模块宽，模块自己的结论是不放行；已改为直接报告模块的 `probe_ready`
   - 补上 PW4 的 USB 空闲判据（锁定实测修订号，要求停止、无端点工作）和 uSDHC1 空闲判据（门开时读
     `PRES_STATE`，命令/数据线、读写传输都不活动）；QEMU 回归通过
   - 第三轮：同样关 Wi-Fi，期望模块给出 `READY`
   原计划：（会跑一次真实的 `pm_test=core` 挂起，先征得同意）：确认 PRRR/NMRR/MIDR、
   挂起后各门控的实际状态、USB 控制器寄存器，再据此锁定 USB 空闲判据。
6. 跳转之后：新内核要能自己重启（`panic=5`），日志写进 `0x9be00000`，回原厂系统读 `/proc/printkbuf`。
