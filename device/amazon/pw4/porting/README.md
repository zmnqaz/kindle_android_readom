# 移植到 Kindle Paperwhite 4：上机实测

这份文档记的是**这台 PW4 自己报告的事实**，来自越狱后在书库里运行的 `Readom Probe`（只读，不改系统）。
对照的是 `readom/docs/design/porting-to-other-models.md`（下称「移植文档」）里按 PW3 写的清单。
**还没有在这台机器上运行过 Readom 的任何代码**，这里只是把要改什么算清楚。

- 采集时间：2026-11-23 00:21（设备时间，GMT+8），root 身份运行
- 原始数据在本目录的 `data/`：`probe.txt`、`dmesg.txt`、`stock-kernel-config.txt`（`/proc/config.gz` 解出来的完整内核配置）、`dt-tree.txt`、`stock.dts`（引导程序交给内核的设备树 `/sys/firmware/fdt` 解成的文本）；采集脚本 `probe/ReadomProbe.sh`。第二轮（00:29）见第六节
- 二进制原件（`fdt.dtb`、内存转储）按仓库规矩不进仓库，留在采集者本机；`data/stock.dts` 和 `data/stock-functions.txt` 是它们的文本形式
- 解设备树：`python3 -I fdtdump.py fdt.dtb [/节点前缀 ...]`（采集用的 Windows 机器上没有 dtc，这是一个小解析器；有 dtc 就用 `dtc -I dtb -O dts`）。在 Git Bash 里传 `/chosen` 这类参数前要先 `export MSYS2_ARG_CONV_EXCL='*'`，否则会被改写成 Windows 路径
- 逐机信息已经去掉：`data/probe.txt` 里 cpuinfo 的 `Serial`、`data/stock.dts` 里 `/idme` 下全部 27 个值（序列号、`board_id`、MAC 等）都换成了 `<redacted>`

## 一、结论先说

1. **SoC 不是移植文档写的 i.MX7D，是 i.MX6SLL**，CPU 和 PW3 一样是单核 Cortex‑A9。移植文档第一节「4.1.15 的机型硬件更好（i.MX7D 双核 Cortex‑A7、8/32 GB 存储）」这句对 PW4 不成立，应当改掉（i.MX7D 是 Oasis 2/3 的情况，未核实）。好处是：AOSP 侧针对 Cortex‑A9 的那两个修正（`bionic`、`swiftshader`）照样适用。
2. **内核是 2G/2G 划分，`PAGE_OFFSET == PHYS_OFFSET == 0x80000000`**，模块区在 `0x7F000000`–`0x80000000`。这正是移植文档第六节「交接」里为 Oasis 1（duet）推出来的情形，PW4 是第一台**实机**确认它的机器：lowmem 本身就是恒等映射，`identity_limit` 应为 `0xA0000000`，`stage.c` 的 `BUILD_BUG_ON(TARGET_IDENTITY_LIMIT>MODULES_VADDR)` 会照设计拦下构建，要做的是让私有页表保留从活页表照抄的模块区。
3. **用设备树启动，不用 ATAG**。交接代码现在按 ATAG 传参、按机器号核对（`__machine_arch_type != TARGET_MACHINE` 就拒绝加载），这一层要改成传 DTB。
4. **板上外设几乎全换了**：PMIC、前光、触摸、Wi‑Fi、USB 控制器、EPDC 驱动版本都和 PW3 不同（见第三节）。移植文档里「碰硬件的 29 个补丁」在 PW4 上基本都要按意图重写，不是改改就能打上。
5. **对交接有利的条件都在**：模块不要求签名（`CONFIG_MODULE_SIG` 未开）、`modules_disabled=0`、`kptr_restrict=0`（kallsyms 给真实地址）、`pm_test_show` 存在、`/dev/mem` 可用且没有 `STRICT_DEVMEM`。

## 二、系统与内核

| 项目 | PW4 实测 | PW3（`boot/profiles/pw3-5.16.2.1.1.json` 等） |
|---|---|---|
| 固件 | 5.18.1.1.1（476968 023），`023-juno_18010101_moonshine_rex-476968` | 5.16.2.1.1 |
| 内核 | `4.1.15-lab126 #1 SMP PREEMPT Thu May 21 05:14:34 UTC 2026`，gcc 4.9.1 | `3.0.35-lab126 #8 PREEMPT` |
| 板子 | 设备树 `model = "Lab126 i.MX6SLL Board"`，`compatible = "amzn,imx6sll-rex", "fsl,imx6sll"`（代号 rex） | wario 板级文件，机器号 4091 |
| 启动方式 | 设备树（`CONFIG_USE_OF=y`，`CONFIG_ARCH_MULTIPLATFORM=y`）；`CONFIG_ATAGS=y` 也编进去了，但引导程序传的是 DTB | ATAG |
| 地址划分 | `CONFIG_VMSPLIT_2G`，`PAGE_OFFSET=0x80000000` | 3G/1G，`PAGE_OFFSET=0xC0000000` |
| 虚拟布局（dmesg） | lowmem `0x80000000`–`0xA0000000`；modules `0x7F000000`–`0x80000000`；vmalloc `0xA0800000`–`0xFF000000` | |
| SMP | `CONFIG_SMP=y`、`NR_CPUS=4`、`HOTPLUG_CPU=y`，实际只有 cpu0 | 非 SMP 内核（待核对） |
| HZ / 抢占 | 100 / `PREEMPT` | |
| 命令行 | `console=ttymxc0,115200 consoleblank=0 uart_at_4m root=/dev/mmcblk1p8 rootwait quiet secure_cpu=1 androidboot.secure_cpu=1 androidboot.prod=1 androidboot.unlocked_kernel=false` | 带 `video=mxcepdcfb:E60,bpp=8,x_mem=3M` 和 `log_pos_goal=` |
| 内置默认命令行 | `CONFIG_CMDLINE="noinitrd console=ttymxc0,115200"`（被 DTB 里的 bootargs 覆盖） | |
| 安全启动 | `secure_cpu=1`、`is_production=1, is_unlocked=0`：引导程序验签，**不能直接刷内核**，只能走交接这条路（和 PW3 一样） | 同 |
| initrd | 引导程序带一个约 6.3 MB 的 initrd（`0x9E6A5000`–`0x9ECEBE00`），它负责挂波形分区、加载 EPDC 驱动模块 | |

### 内存与保留区

`/proc/iomem`：System RAM `0x80000000`–`0x9AFFFFFF` 和 `0x9B300000`–`0x9FFFFFFF`；内核代码 `0x80008000`–`0x80B3B4A3`，数据 `0x80B96000`–`0x80CB5E8B`。设备树 `/memory` 是 `<0x80000000 0x20000000>`（512 MiB）。

| 区域 | 地址 | 来源 |
|---|---|---|
| EPDC 工作缓冲 | `0x9B000000` + 3 MiB（`no-map`） | `/reserved-memory/epdc_reserved` |
| printk 日志 | `0x9BE00000`，256 KiB | dmesg `log_goal_location` |
| Falcon BIOS | `0x9FFD0000` + 0x30000 | `/falcon/falcon-bios` |
| storage BIOS | `0x9FFC0000` + 0x10000 | `/falcon/storage-bios` |
| DTB 本身 | `0x83F077B0` + 0x8850 | `/memreserve/` |
| initrd | `0x9E6A5000` + 0x646E00 | `/memreserve/`、`/chosen` |
| ramoops | 节点在（`fsl,mxc_ramoops`，record 512 KiB），没有写地址 | `/ramoops_device` |

PW3 的规矩「目的地留在低 128 MiB（`0x80000000`–`0x88000000`）」在这里仍然避开了上面所有区域，**唯一例外是 DTB 落在 `0x83F077B0`**：新内核的 DTB 必须由我们自己放，不能指望原厂那份还在原处。

`MemTotal` 502340 kB，开机后常驻已经吃掉约 200 MB 匿名页；原厂也用 128 MiB zram 做 swap，和 PW3 一样紧。

### 存储

eMMC `mmcblk1`，7 634 944 KiB（约 7.3 GiB），另有 `boot0`/`boot1` 各 4 MiB。

| 分区 | 大小 | 用途（看挂载和 dmesg） |
|---|---|---|
分区名来自第二轮探针（`/sys/block/mmcblk1/*/uevent` 的 `PARTNAME`），起点和长度以 512 字节扇区计。

| 分区 | 名字 | 起点 / 扇区数 | 大小 | 用途（看挂载和 dmesg） |
|---|---|---|---|---|
| p1 | `kernel` | 2048 / 32768 | 16 MiB | 原厂内核（引导程序验签，不能改） |
| p2 | `wfm` | 34816 / 8192 | 4 MiB | 波形分区，initrd 临时挂到 `/mnt/wfm`，开机后卸掉 |
| p3 | `keys` | 43008 / 4096 | 2 MiB | `/keys`（ext3，只读） |
| p4、p5 | `reserved0`、`reserved1` | 47104、48128 / 各 1024 | 各 512 KiB | 未挂载 |
| p6 | `miscdata` | 49152 / 32768 | 16 MiB | 未挂载 |
| p7 | `snapshot` | 81920 / 524288 | 256 MiB | 未挂载；名字看是 Falcon 快速开机的休眠快照 |
| p8 | `system` | 606208 / 1048576 | 512 MiB | 根文件系统（ext3，只读，已用 94%） |
| p9 | `cache` | 1654784 / 524288 | 256 MiB | `/var/local`（ext3） |
| p10 | `userdata` | 2179072 / 13090749 | 约 6.24 GiB | 用户盘：偏移 8192 处是 FAT，经 loop0 挂成 `/mnt/base-us`，再由 fuse 挂成 `/mnt/us` |

用户盘当时空着约 6.0 GiB（`filler/` 已删）。移植文档要求腾出约 2.1 GiB，够用。

## 三、硬件：和 PW3 逐项对照

| 部件 | PW4 实测 | 证据 | 对移植的意思 |
|---|---|---|---|
| CPU | Cortex‑A9 r4p1（`414fc091`），单核，L2C‑310 256 KiB 16 路；工作点 996 MHz / 396 MHz | cpuinfo、dmesg、`/cpus/cpu@0` | 和 PW3 同一种核，AOSP 侧的 A9 修正沿用 |
| PMIC（电池、充电、稳压、RTC） | **ROHM BD71827**，I2C0 `0x4B`，Device ID 0x31；电源 `bd71827_ac`、`bd71827_bat` | i2c 列表、dmesg、`/sys/class/power_supply` | max77696 的 5 个补丁（电池、电量计、前光、I2C、关机）全部作废，按 BD71827 重写；`tools/power-log.c` 要改 |
| EPD 电源 | **FitiPower FP9929**，I2C0 `0x48` | i2c 列表 | PW3 上这部分应当在 max77696 里（未核对） |
| 前光 | **TI LM3695**，I2C0 `0x63`，`/sys/class/backlight/bl`，亮度 0–2047（11 位） | i2c 列表、backlight | lights HAL 节点路径和亮度换算要改 |
| 电池 | 设计容量 1507 mAh，满充 1503 mAh，4.35 V / 3.4 V，充电电流上限 900 mA；`bd71827_bat` 报电压、电流、电量、温度 | `power_supply/*/uevent` | 电池统计和功耗记录改读这里 |
| USB 充电检测 | **MAX14656**，I2C2 `0x35` | i2c 列表、dmesg | 新部件 |
| 触摸 | **Goodix GT9xx**（`gt9xx_ts.ko`，I2C1 `0x14`，输入设备名 `goodix-ts`，多点 B 协议）；配置区开头 X 最大 1072、Y 最大 1448，坐标与屏幕竖放方向一致。I2C1 `0x24` 上还登记了一个 `cyttsp5_i2c_adapter`，但没有对应输入设备；原厂 `/etc/upstart/touch` 按板子在 gt9xx、focaltech、goodix_core 等之间选，是同一固件给多个机型共用 | 模块、i2c、input、`/proc/gt9xx_config` | Cypress 触摸补丁作废；固件在 `/lib/firmware/goodixfw/` |
| 电源键 | SoC 自带的 SNVS（`20cc000.snvs:snvs-powerkey`） | input0 | PW3 走 PMIC；`bd71827-power` 另有一个 input1 |
| 霍尔传感器 | `amzn,rex_hall`，GPIO 中断 | `/rex_hall`、interrupts | `wario_hall` 补丁要换 |
| Wi‑Fi | **Broadcom BCM43430（4343W）**，SDIO 接在 `usdhc@2190000`（mmc0）；驱动是博通的 **bcmdhd**（模块，`CONFIG_BCMDHD=m`），主线的 `brcmfmac` 没开；芯片固件 7.45.102.16 | 模块、DT `/wifi_brcm`、dmesg | 和移植文档第六、七节的 Oasis 1 同一颗芯片；但 PW4 原厂用的是 bcmdhd，不是 brcmfmac。要确认 GPL 包里有没有这份 bcmdhd 源码 |
| Wi‑Fi 固件 / 校准 | 实际用的是 `/opt/brcm_4343w/wifi/firmware/fw_bcmdhd_sr.bin`（369 119 字节）和 `/opt/brcm_4343w/wifi/nvram/nvram_wfo.txt`（1166 字节，仅 Wi‑Fi 版；另有 `nvram_wan.txt` 给 3G 版、`fw_bcmdhd_mfg.bin` 是产线用的）。内核配置里写的 `nvram.txt` 不存在，是模块参数覆盖的。驱动版本 1.363.110.23，芯片 `a9a6 Rev 1` | config、`/sys/module/bcmdhd/parameters` | 都在根分区上，按项目规矩从这台机器现场读，不打包 |
| 蓝牙固件 | `/opt/brcm_4343w/bluetooth/firmware/BCM4343A1_*.hcd`，11 个版本 | 目录清单 | 本项目不用 |
| 蓝牙 | 同一颗芯片，`bt_pwr_ctrl`，有 `BT hostwake` 中断 | DT、interrupts | 本项目没用蓝牙，可不管 |
| USB 设备控制器 | **ChipIdea**（`ci_hdrc`，`usb@02184000`） | iomem、drivers | PW3 的 `arcotg_udc` 8 个补丁作废；好在 4.1 自带 configfs gadget，且原厂已开 `CONFIG_USB_CONFIGFS_F_FS`、`CONFIG_USB_CONFIGFS_F_MTP` |
| 显示控制器 | EPDC **v2 驱动**（`imx_epdc_v2_fb`，`compatible = "fsl,imx6sll-epdc", "fsl,imx7d-epdc"`），**编成模块**，由 initrd 加载；PXP 是 `imx-pxp-v3` | dmesg、DT | EPDC/PXP 的 9 个补丁要对着 v2 驱动重写 |
| 屏幕 | `mxc_epdc_fb`，1072×1448，8 bpp，虚拟尺寸 1088×6144（行跨度 1088）；`rotate = 3`，即 `FB_ROTATE_CCW` | fb0 | 分辨率**和旋转方向**都与 PW3 相同（`EpdcContract.h` 写的也是 1072×1448 + `FB_ROTATE_CCW`），尺寸、位图、密度都能沿用；行跨度是 1088 而不是 1072，要确认合成器按 stride 写 |
| 面板与波形 | 面板 ED060KC4；波形文件 `320_R278_AI4103_ED060KC4C3_TC.wrf.gz`（94 849 字节，gzip），放在 p2 的 `waveform_to_use/`；VCOM −2.73 V 来自 idme | dmesg | 波形格式是 `.wrf.gz`、读法与 PW3 不同；灰阶曲线要重新量 |
| GPU | `CONFIG_MXC_GPU_VIV=y`（galcore 驱动在，i.MX6SLL 本身没有 GPU 核心，应当用不上） | config | 维持 CPU 合成 |
| 串口 | `ttymxc0` 控制台，另有 `ttymxc2` | dmesg | |

## 四、交接（`boot/`）要改的地方

把 `boot/profiles/pw3-5.16.2.1.1.json` 的每个字段对到 PW4：

| 字段 | PW4 的值或状态 |
|---|---|
| `machine` | **不适用**：DT 启动，`__machine_arch_type` 不是一个板子号。`native_init()` 那句 `__machine_arch_type!=TARGET_MACHINE` 要换成按 DT `compatible`（`amzn,imx6sll-rex`）核对 |
| `boot_ram_base` | `0x80000000`（`stage.c` 里 `PHYS_OFFSET!=0x80000000` 的检查照样通过） |
| `identity_limit` | `0xA0000000`（lowmem 终点，由内核提供）。按移植文档第六节，会被 `BUILD_BUG_ON` 拦下，要按那里说的改私有页表 |
| `ram` | `[0x80000000, 0x20000000]` |
| `forbidden` | 沿用「低 128 MiB 之外都不放」即可覆盖上表的保留区 |
| `atag_kinds` | **不适用**，改成 DTB 段：交给新内核的应当是我们自己的 DTB（以原厂这份为底，改 `bootargs`、`memory`、`reserved-memory`），`r2` 指向它 |
| `cmdline_base` | 以原厂的 bootargs 为底，去掉 `root=`、`rootwait`；要不要保留 `androidboot.*` 待定 |
| `stock_kernel_release` / `version` | `4.1.15-lab126` / `#1 SMP PREEMPT Thu May 21 05:14:34 UTC 2026` |
| `stock_functions.pm_test_show` | `0x80069564`，116 字节，SHA‑256 `3bc6feff48a2461b0bb3d4298191e6e9cdeda3dd0f682fccb1a7ae2c938b6dda`（第七节） |
| `stock_functions.l2x0_*` | **4.1 里没有这些名字了**，换成 `outer_cache` 里实际挂的函数：`flush_all` = `l2c210_flush_all`，`disable` = `l2c310_disable`；`outer_cache_fns` 里已经没有 `inv_all`。`native_init()` 那段按三个名字比对的逻辑要按 4.1 的结构重写（第七节） |
| `module_kernel_source` | 需要亚马逊 PW4 5.18.1.1.1 的 GPL 源码；原厂开了 `CONFIG_MODVERSIONS`，模块必须对着同一份配置（就是这次拿到的 `config.txt`）编，vermagic 要一致 |
| 硬编码的外设地址 | `native-handoff.h` 里 `ioremap` 的 CCM `0x020C4000`、DCP `0x020FC000`、USB `0x02184000`：DCP 和 USB 在 PW4 的 iomem 里地址相同；CCM 在设备树里是 `ccm@020c4000`，地址也相同（iomem 没列出来）；寄存器含义是否与 i.MX6SL 一致还要对照参考手册 |
| SMP | 原厂是 SMP 内核（单核在线）。交接时的缓存与 TLB 维护、`ACTLR.SMP` 位，要确认对 SMP 编译的内核同样成立 |
| 挂起入口 | 交接挂在 `pm_suspend(PM_SUSPEND_MEM)` 上。`/sys/power/state` 是 `freeze standby mem`，`pm_test` 可选 `none core processors platform devices freezer`，这条路在原厂 4.1 上是开着的 |

另外两个和 PW3 不同、对交接有影响的事实：

- **EPDC 驱动是模块，由 initrd 加载**。我们的内核起来后，显示要靠自己的 initramfs 去挂 p2、读 `.wrf.gz` 波形、初始化 EPDC v2——原厂的这套流程可以照着学，但要看 GPL 包里 `mxc_epdc_v2_fb` 的源码在不在。
- `falcon` 模块是 `[permanent]` 且以 `newBSD` 许可证加载（污染内核），Falcon BIOS 占着 RAM 顶端 256 KiB。交接前它是否需要被安顿，和 PW3 一样要看。

## 五、原厂内核配置里和 Android 相关的几项

| 配置 | 原厂 | 意思 |
|---|---|---|
| `CONFIG_ANDROID`（binder 等） | 未开 | 4.1 的 staging 里有 binder/ashmem，打开即可，不用像 3.0.35 那样回移 |
| `CONFIG_SECCOMP` | 未开 | 4.1 自带，打开即可 |
| SELinux / `CONFIG_SECURITY` | 没有 | 要开 |
| `CONFIG_PSTORE` | 未开 | 设备树有 ramoops 节点，可以开来留崩溃日志 |
| `CONFIG_EXT4_FS` | 开 | |
| `CONFIG_F2FS_FS` | 未开 | |
| `CONFIG_INPUT_UINPUT` | 未开 | |
| `CONFIG_USB_CONFIGFS_F_MTP`、`F_FS` | 开（configfs 本身是模块） | MTP/adb 组合可以直接用 4.1 的 configfs gadget |
| `CONFIG_CPU_FREQ_GOV_INTERACTIVE` | 开 | |
| `CONFIG_NAMESPACES`/`PID_NS`/`NET_NS`/`USER_NS`、`CONFIG_MEMCG` | 开 | |
| `CONFIG_NETFILTER_XT_MATCH_QUOTA2` | 未开 | qtaguid/quota2 这一类要补 |

完整配置在 `data/stock-kernel-config.txt`，做配置片段时以它为底（对应移植文档第六节「配置片段分两半」的做法）。

## 六、第二轮探针（2026-11-23 00:29）

脚本 `probe/ReadomProbe2.sh`，结果在 `data/`：`probe2.txt`、完整的 `kallsyms.txt`（92 066 行，`kptr_restrict=0`，地址是真的）。脚本还从 `/dev/mem` 读了 8 段内存（每段是包含某个符号的连续两页），切出来的函数体以十六进制字写在 `data/stock-functions.txt`，带大小和 SHA‑256。这台机器 lowmem 是恒等映射，所以符号地址就是物理地址，`/dev/mem` 也没有 `STRICT_DEVMEM` 限制。

第一轮列的 11 项都读到了，结果已经填进上面各节（分区名、屏幕旋转、触摸坐标、Wi‑Fi 文件、电池、前光、挂起入口）。只有 `bcmdhd` 是谁加载的没找到：`/etc/upstart` 里 grep 不到它，它在开机 32 秒才加载，大概是 Wi‑Fi 守护进程自己 `insmod` 的。`g_mass_storage` 由 `filesystems_userstore.conf` 加载（`idVendor=0x1949`，`idProduct` 按板子取）。

## 七、交接要核对的原厂函数

### `outer_cache` 里实际挂的回调

`outer_cache` 在 `0x80B9A104`。按 4.1 的 `struct outer_cache_fns` 顺序解出来：

| 槽 | 地址 | 函数 |
|---|---|---|
| `inv_range` | `0x80021A50` | `l2c210_inv_range` |
| `clean_range` | `0x80021AB8` | `l2c210_clean_range` |
| `flush_range` | `0x80021B00` | `l2c210_flush_range` |
| `flush_all` | `0x800229B8` | `l2c210_flush_all` |
| `disable` | `0x80021CEC` | `l2c310_disable` |
| `sync` | `0x80021B48` | `l2c210_sync` |
| `resume` | `0x80022988` | `l2c310_resume` |
| `write_sec`、`configure` | 0 | 未设置 |

紧接着的两个字是 `v7wbi_flush_user_tlb_range`、`v7wbi_flush_kern_tlb_range`，说明结构到此为止、解法没错位。**没有 `inv_all` 这一槽**：PW3 交接用的「整体无效化 L2」在 4.1 上没有现成回调，要换成别的做法（比如 `flush_all` 之后再 `disable`，或者自己按 L2C‑310 的寄存器按路无效化），这一步要对照 PW3 的 `cache-ops.S` 和 `transition.S` 重新想。

### 函数体的大小和哈希

大小取「到下一个代码符号的距离」（PW3 那份是怎么取的要核对：如果 PW3 用的是 ELF 里的符号大小，两者在函数末尾有对齐填充时会不同）。哈希是对这段字节求 SHA‑256。`pm_test_show` 开头是 `e92d41f0`（`push {r4-r8, lr}`），确认读到的是真代码。

| 函数 | 地址 | 字节 | SHA‑256 |
|---|---|---|---|
| `pm_test_show` | `0x80069564` | 116 | `3bc6feff48a2461b0bb3d4298191e6e9cdeda3dd0f682fccb1a7ae2c938b6dda` |
| `l2c210_flush_all` | `0x800229B8` | 96 | `803eacc7b1dd385911364c088523a48821372a9f04a50175a2a3826ab2012ffd` |
| `l2c310_disable` | `0x80021CEC` | 40 | `b5337f06d4acbbb2581785f37189c91a4eb69b65ae87d83a7aa5f6d80ac30ef9` |
| `l2c210_sync` | `0x80021B48` | 40 | `c5bf463648249b2ca224e376d2abb8c474fc93dbd449a83b360f550739f6c1be` |
| `l2c310_resume` | `0x80022988` | 48 | `cdac8adc3ce37b80f06adb1baffedadd7b76d31ef66804956eedba4949a3c0ee` |
| `soft_restart` | `0x80012598` | 52 | `b8e98ad4525085cc74c8cce131668ef0bf1f3d0e0049ea1aa2ff001a75aa6146` |
| `cpu_v7_proc_fin` | `0x8002158C` | 20 | `1fcb26cc305c580e1625f0618b8de7f057c77e494cd43dbbd1b80bcc4ae79275` |

`l2c310_flush_all_erratum` 不存在（没开 PL310 的 727915 勘误），所以 `flush_all` 就是 `l2c210_flush_all`。

这些值只对这一版内核（`#1 SMP PREEMPT Thu May 21 05:14:34 UTC 2026`）成立。原厂一升级就要重读，这也是交接拒绝别的版本的原因（和 PW3 的 E101 同理）。

## 八、还没做的

上机能读的基本读完了。剩下的在机外：

- 下载亚马逊 PW4 5.18.1.1.1 的 GPL 源码包，确认里面有没有 `bcmdhd`、`mxc_epdc_v2_fb`、`gt9xx`、`bd71827`、`fp9929`、`lm3695`、`falcon` 的源码，以及哪个 defconfig 对应 rex；用它和 `config.txt` 编出 vermagic 一致的交接模块。
- 交接代码按第四、七节改：DT 代替 ATAG 和机器号；私有页表保留模块区；L2 回调换名，`inv_all` 另想办法。
- 改移植文档第一节里「4.1.15 = i.MX7D 双核 A7」那句（至少对 PW4 不对）。
