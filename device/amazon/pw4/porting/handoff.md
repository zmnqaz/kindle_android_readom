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
| 原厂函数锁定 | `pm_test_show`、3 个 L2 函数的长度和哈希 | `pm_test_show`、2 个 L2 函数；长度取自设备 kallsyms（116、96、40 字节），**哈希待从设备内存取**，缺哈希时模块拒绝加载 |
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
- **没有在任何机器上加载或运行过**

## 上真机之前还差的

1. **原厂函数哈希**：第二轮探测在 Kindle 上导出了这几个函数所在的内存页（`readom-probe2/mem-*.bin`），
   按上面的长度切出来算 sha256，填进 profile。不能用自己编的内核算：编译器不同，字节不同。
2. **解压器占用**：`derive_kernel_footprint.py` 锁的是 3.0 的 `compressed/head.S`；按 4.1 的
   `head.S` 重新核对公式（`_stext` 为 `0x80008000`），加 `--profile pw4`。
3. **设备端生成设备树**（`boot/devicedata`）：从 `/sys/firmware/fdt` 读原厂 blob，换掉 `/chosen` 的
   bootargs 和 initrd 两项，去掉指向旧 DTB 和旧 initrd 的两条 `/memreserve/`（`0x83f077b0`、`0x9e6a5000`）。
4. **QEMU**：profile 4 的替身内核（4.1、2G/2G、RAM 在 `0x80000000`、Cortex-A9 或 A7），`qemu-pm.c` 跟着改。
5. **设备上的 PROBE**（会跑一次真实的 `pm_test=core` 挂起，先征得同意）：确认 PRRR/NMRR/MIDR、
   挂起后各门控的实际状态、USB 控制器寄存器，再据此锁定 USB 空闲判据。
6. 跳转之后：新内核要能自己重启（`panic=5`），日志写进 `0x9be00000`，回原厂系统读 `/proc/printkbuf`。
