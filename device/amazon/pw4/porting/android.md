# PW4：让安卓本身跑起来（第 3 阶段计划）

前提已在设备上验证（`handoff.md`）：交接、内核启动到用户空间、PMIC/电池、eMMC（HS200，10 个分区）、
墨水屏（面板波形现场读、GC16 全屏刷新）、跨重启日志、Wi-Fi SSH。

## 和 PW3 不同、要改的地方

| | PW3（Readom 现状） | PW4 |
|---|---|---|
| 用户存储 | FAT 在 `mmcblk0p4` | FAT 在 `mmcblk1p10` 里偏移 8192 扇区（原厂用 `loop0` 挂；`dmesg`: `Setup loop device /dev/loop0 for /dev/mmcblk1p10 + 8192`） |
| 面板波形 | 面板 flash 里的 WBF + 原厂生成的 WRF，装进设备那半 initramfs | `mmcblk1p2`（FAT）`waveform_to_use/*.wrf.gz`，驱动 `waveform=` 自己读；另需原厂 `/lib/firmware/imx/epdc/epdc_ED060KC4.fw` |
| 墨水屏 ABI | 3.0 Lab126 `mxcfb_update_data` | 4.1 Lab126 版本（多 `dither_mode`、`quant_bit`、`alt_buffer_data`、`hist_*`；ioctl 号相同），gralloc/HWC 要按它改 |
| 触摸 | Cypress cyttsp4 | Goodix gt9xx（`goodix-ts`，多点 B 协议） |
| Wi-Fi | ath6kl（AR6003） | bcmdhd（BCM43430，`fw_bcmdhd_sr.bin` + `nvram_wfo.txt`，SDIO `mmc0`） |
| PMIC / 电量 / 背光 | MAX77696 | BD71827；背光 LM3695 |
| eMMC 驱动 | 原生 | 标准 `sdhci-esdhc-imx`（`nofalcon`，补丁 0012） |
| 内核 | 3.0，单核 | 4.1，SMP_ON_UP，3G/1G |

## 步骤

1. **AOSP 产品 `pw4`**：从 `device/amazon/pw3` 复制，先只改名字，确认整套能编；再逐项换硬件相关部分（rc、ueventd、
   sepolicy、HAL、内核模块组件）。
2. **启动链**：用 Readom 的 preinit（PID 1）。它的存储配置（块设备、FAT 偏移）本来就由设备端生成的
   `pw3-preinit.conf` 给出，PW4 改成 `mmcblk1p10` + 8192 扇区。设备那半 initramfs 由设备端程序现场装入：
   面板波形、通用 EPDC 固件、Wi-Fi 固件和校准。
3. **第一个目标**：安卓 `init` 起来、`logd` 把日志写到能读回的地方；屏幕先由 preinit 画一张启动画面。
4. 之后：SurfaceFlinger/墨水屏、触摸、Wi-Fi、USB、电量。

始终不写原厂分区：系统镜像和数据镜像是用户存储里的文件，`preinit` 对 FAT 只读映射。

## 进展

**第一次安卓启动（Jump 11，2026-11-23）**：用户存储里放 `pw4-android/system.img`（512 MB，原始 ext4）和 `data.img`
（1 GB，`make_ext4fs`），设备那半 initramfs 放 `pw3-preinit.conf`（14 个分区，含 4.1 才有的 `mmcblk1rpmb`）和
`pw3-testdata.conf`。preinit 把 14 个原厂分区设为只读，核对两个文件的簇和区段（system 76 段、data 7 段）、FAT 无交叉，
映射为 dm，`/data` 读写、`/system` 只读挂上，2.17 s 执行 AOSP init；SELinux 策略加载、第二阶段开始解析 rc。
约 1 s 后 `hwservicemanager`、`vndservicemanager` 的 `binder_mmap` 全部失败（`vmap_page_range_noflush` WARN，-12），
init 重启。原因是 binder 移植（`0002`）在栈上的 `vm_struct` 上调 `map_vm_area()`，`flags` 未初始化，带 `VM_NO_GUARD` 时
映射两页、第二页为空；内核补丁 `0013` 改为 4.1 自己 binder 的 `map_kernel_range_noflush`。
Wi-Fi 上 SSH 写用户存储只有约 0.3 MB/s，大文件走 USB（1.5 GB 用 153 s）。

**binder 修好后（Jump 11 第三轮）**：binder 不再报错，`hwservicemanager`、`servicemanager`、`vndservicemanager`、`vold`
都起来了。init 在 `pw3-init-check`（storage 模式）处停下：它写死了 `/dev/block/mmcblk0p4`。已把它的分区名、背光
（PW4 是 `bl`）、电源键（`bd71827-power`）、触摸（`goodix-ts`）都改为读 `preinit/Board.h`。

下一步（按 PW3 的 `tools/kernel-hardware` 做 PW4 的硬件组件）：
- `post-fs-data` 里只读挂 `mmcblk1p2` 到 `/mnt/wfm`，`insmod mxc_epdc_v2_fb.ko waveform=…`；通用 EPDC 固件
  （原厂 `/lib/firmware/imx/epdc/epdc_ED060KC4.fw`）由设备端在启动时取，放进设备那半 initramfs；
- 触摸 gt9xx、Wi-Fi bcmdhd 的模块和参数（`platform_variables` 里的 `firmware_path`/`nvram_path`）；
- `pw3-init-check` 的 `CheckEpdcContract` 要 Readom 给 PW3 内核加的 `MXCFB_GET_ANDROID_INFO`：墨水屏那几个内核补丁
  （`0400`、`0401`、`0403`、`0500`）要对着 4.1 的 `mxc_epdc_v2_fb` 重写。

**PW4 交接 PID 1（Jump 12，2026-11-23）**：`boot/handoff-init/pw4-handoff-init.c` 放在 `/pw3-testfile-init`，Readom 的
preinit 改名 `.real`（同 `boot/build_entry.py`），公开 initramfs 由 `pack-initramfs.py` 打包（`fstab.pw3` 用
`fstab.pw3-testfile`、日志节点、`/pw4-handoff/` 下的驱动）。它只读挂 `mmcblk1p2`、按 `waveform_to_use/` 里的文件名
加载 `mxc_epdc_v2_fb.ko`（1.75 s）、卸载，画"正在启动 Android"（GC16 一次 572 ms），删掉设备那半带来的
`epdc_ED060KC4.fw`，再执行 preinit。存储检查通过；停在 `CheckEpdcContract`（4.1 驱动不认 `MXCFB_GET_ANDROID_INFO`）。

**安卓桌面（Jump 13，2026-11-23）**：内核补丁 `0014` 给 4.1 EPDC 驱动加上 `MXCFB_GET_ANDROID_INFO` 和 PW3 的 72 字节
`MXCFB_SEND_UPDATE`；PW4 面板波形 mode_version 0x41，`EpdcContract.h` 在 PW4 上另认；交接 PID 1 另外加载原厂同样
无参数加载的 `gt9xx-ts.ko`。init 检查全过，47 s `sys.boot_completed=1`，桌面显示、触摸可用，healthd 读到电量 95%。
插上 USB 线后整机卡死，日志在 108 s 无任何报错地中断（Lab126 缓冲没写满也没回绕）。Wi-Fi 还在按 PW3 加载 `ath6kl_sdio`。

下一步：USB（插线卡死、FunctionFS adb）、Wi-Fi bcmdhd。

**PW4 硬件组件（2026-11-23 晚，未上机）**：
- USB：4.1 内核是 ChipIdea（`ci_hdrc.0`，内建）+ configfs gadget，不是 PW3 的 ARC + android_usb。`rootdir/init.pw4.usb.rc`
  （装成 `/init.pw3.usb.rc`）按 AOSP `init.usb.configfs.rc` 写 adb/mtp/ptp，保留 Readom 的 `pw3-ffs-check` 门；
  `hardware/init.pw4.usb.modules.rc` 在 post-fs 加载 `configfs`、`libcomposite`、`usb_f_fs`、`usb_f_mtp`。USB 序列号暂空。
- Wi-Fi：`hardware/init.pw4.hardware.rc` 按原厂 `platform_variables` 的参数 `insmod bcmdhd.ko`，固件和 NVRAM 由设备那半
  放到 `/firmware/bcmdhd/`（`make-device-archive.py`）；bcmdhd 在 insmod 时以 init 的身份自己读文件，`sepolicy/pw4_hardware.te`
  允许 init 读 `pw3_private_firmware_file`。
- 模块由 `stage-pw4-hardware.py` 从内核编译目录放到 `vendor/amazon/pw4-hardware`（不入库），`pw4.mk` 引用。
- 插线卡死：同一份内核代码在原厂系统上不卡，USB 相关配置完全相同，差别只能是原厂休眠留下、新内核没重设的硬件状态。
  `pw4-handoff-init` 在交接开始和交出前各记一次 USB 相关寄存器（时钟门、PLL3、模拟 USB1、PHY1、门开时的 OTG1/USBNC，
  `boot/handoff-init/pw4-usb-regs.h`），并留一个进程在安卓下每 0.2 s 看 VBUS/时钟/PLL3，有变化就写一行进内核日志——
  再卡死时日志最后一行就是卡住时的状态。原厂（无线、无 USB 线）读数：CCGR6=ffffff3c（usboh3 门关）、PLL_USB1=80003040、
  REG_3P0=00010f74、USB1_VBUS_DET_STAT=1。

**其余驱动（Jump 15，2026-11-23 夜，未上机）**：
- Wi-Fi：Readom 的 wificond（`patches/aosp-wificond/0001`）等的是 `/sys/module/ath6kl_sdio`、驱动 `ath6kl_sdio`，在 PW4 上
  会加载 bcmdhd、空等 15 s、再卸掉。新补丁 `0008` 让板子给名字：PW4 的 `BoardConfig.mk` 设 `bcmdhd`、`bcmsdh_sdmmc`
  （原厂上实测的模块和 SDIO 驱动名）；不设时还是 PW3 的，PW3 构建不变。
- 前光：`lm3695`（`/sys/class/backlight/bl`，0–2047）。lights HAL 在 PW4 上用原厂 libfrontlight 的表：设备能力里没有 LED
  类型时用的那张，唯一一张第 11 档是 653（原厂当时的读数）；最后的 4095 截到驱动最大值。灭屏靠内核补丁 `0015` 让驱动认
  `bl_power`，power HAL 写 `/sys/class/backlight/bl/bl_power`。
- 电池：`bd71827_bat`/`bd71827_ac`，单位本来就是 µV/µA/µAh，`0015` 只把温度改成 0.1 °C；healthd 的电量计数路径改到
  `bd71827_bat/charge_now`。充电灯用内核触发器：琥珀色充电中、绿色充满。
- 按键/触摸：`input/bd71827-power.kl`（116 POWER WAKE）、`input/goodix-ts.idc`。
- SELinux：`sepolicy/genfs_contexts` 给 PW4 的前光、`bl_power`、电池、充电器路径打上 PW3 同名标签。
- USB 序列号：`pw3-usb-check` 加 `configfs-serial` 一步（configfs、libcomposite、usb_f_fs 都已加载、内核认 functionfs 后，
  做出或沿用 `persist.pw3.usb.serial`），PW4 的 `init.pw3.usb.rc` 在建 gadget 前跑它，写进 `strings/0x409/serialnumber`。
- 低电量：沿用 PW3 的 3450 mV 不改。PW4 驱动 3500 mV 报低电、3400 mV 报严重（原厂到这里关机），电量 0% 也定在 3400 mV，
  3450 正在两者之间。
- 蓝牙：原厂不用 Linux 蓝牙栈，是 Broadcom 的 `bsa_server` 走 `/dev/ttymxc2`、加载 `.hcd` 固件。Readom 本来没有蓝牙，
  要做是新增功能（蓝牙 HAL、协议栈、应用），先不做。
- 还没动：`pw3-fg-state`（读 PW3 的 `wario_battery`，PW4 上只会记一行读不到）；休眠（`/sys/power/state` 有 `mem`）未测。

**Jump 15–17（2026-11-24）：Wi-Fi、充电灯**。电源键、前光正常。
- Wi-Fi：bcmdhd 的 insmod 成功，但下载固件的是 SDIO probe 的内核工作线程（`kernel` 域），SELinux 拒绝它读
  `/firmware`（`pw3_private_firmware_file`）。`sepolicy/pw4_hardware.te` 给 `kernel` 也放行。Jump 17 起 Wi-Fi 正常。
- 充电灯触发器每次充电器事件都报 `scheduling while atomic`（`bd71827_led_set` 在读锁下走 I²C）→ 内核 `0016`。

## USB 插拔卡死（Jump 13–35，已解决）

**现象**：插或拔 USB 线时整机冻结，没有 panic、没有任何日志，约 60 s 后硬件看门狗重启。插线时偶发，
已连上电脑后拔线几乎必现；只供电的线、墙充很少触发。

**根因**：交接发生在原厂 `pm_test=core` 挂起的 syscore 回调里，所有驱动的挂起回调都已执行、恢复回调永远
不会执行，它们留下的硬件状态都带进了安卓内核。`serial_imx_suspend()` 给 UART1 置了 `UCR3_AWAKEN`（异步唤醒
中断）。线一动，RXD 上出现边沿，中断触发；安卓命令行没有 `console=`（原厂有 `console=ttymxc0`），端口没打开、
时钟关着，`imx_int()` 清不掉标志，中断无限重入（约 2 万次/秒）。GIC 里 UART1 的中断号比定时器小，单核上
定时器中断再也进不来：没有日志、软锁死检测也不触发，看起来和总线挂死一样。

**修复**：内核 `0022`，probe 时屏蔽端口的全部中断源。Jump 35 插拔 5 个来回（7 次 VBUS 接入）不冻，
`/proc/interrupts` 里 UART1 计数为 0。

**怎么找到的**：内核 `0021` 的死机检测器。EPIT2 每 0.5 s 以 GIC 最高优先级中断一次，jiffies 停住就打印计数
增长最快的中断。Jump 34 第一次插线就抓到 `pw4-lockup: irq 18 hw 26 (2020000.serial) +12633`。在这之前的
二十来次上机都是逐项排除，没有一项是原因：

| 排除的假设 | 依据 |
|---|---|
| DDR 调频（busfreq） | 关掉调频、PLL_USB1 全程不变（Jump 29）仍冻 |
| CPU 调频、`cpufreq_override` | 固定频率、关 override（Jump 19、20、29）仍冻 |
| cpuidle（WAIT / LOW-POWER-IDLE） | `cpuidle.off=1`（Jump 28）仍冻 |
| USB 运行时 PM | 禁止运行时挂起（Jump 29）仍冻 |
| PMIC、电源、时钟配置 | anatop、CCM、GPC 寄存器、稳压器和时钟树与原厂逐项一致（`PMU_REG_CORE` 0x00482012，USBPHY1 由 ldo3 供电）；时钟树唯一的差别正是 uart1，当时当成无关项放过了 |
| USB 高速通信本身 | 高速枚举、30 MB adb 读写校验正常；先软件断开再拔线不冻 |
| UDC 关中断忙等死循环 | 全部加次数上限（`0020`）后没有一条触发 |
| Wi-Fi | 关掉 Wi-Fi、卸载驱动后拔线仍冻过 |

排查中另外发现并修掉的、同样是挂起中途交接留下的问题：
- `bd71827_i2c_suspend_late` 把 PMIC 测试寄存器 0x42 设成 0x4C（关闭关机时序），`resume_early` 才改回 0x44
  → 内核 `0017` 在 probe 时恢复（日志 `power sequence register was 0x4c`）。恢复之后，看门狗重启时 PMIC 会
  短暂断掉 DDR 供电，Lab126 持久日志里会有位翻转，这是正常现象，不是内存不稳。
- DVS 初始化把 buck1/buck2 的 RUN 电压写成设备树的 1.1 V，而交接时 CPU 已在 996 MHz（原厂 1.275 V），要到
  第一次调频才升压 → 内核 `0019` 保留寄存器里已有的更高值。原厂里 `cpu0-arm` 是 buck2、`cpu0-soc` 是 buck1。

对照用的事实：VBUS 有效是 max14656 驱动经 anatop 覆盖位写进去的（`VBUS_DETECT` 0x1c→0xac），拔线后约 1.3 s
才撤掉，原厂也一样；`vdd3p0` 在原厂也始终关着。

**排查用的工具**（留在仓库里，默认不启用）：
- `pack-initramfs.py --tcp-shell PORT`：开机完成后在该 TCP 端口开一个 root shell（`toybox nc` 经 `su`；init →
  shell 域 → su）。Readom 的 adbd 编译时去掉了 TCP（`PW3_USB_ONLY_ADBD`），`service.adb.tcp.port` 不起作用。
- 启动参数 `pw4_lockup=1`：上面的死机检测器。
- `--cmdline-extra`、`--boot-prop`、`--adb-keys`：给诊断用的 Jump 加内核参数、开机属性、本机 adb 公钥（公钥只在本地打包，不进仓库）。

排查期间的内核打印、调试开关和每毫秒寄存器监视（原补丁 `0016`–`0030` 里的诊断部分）在 2026-11-25 整理时
去掉了，补丁重新编号为 `0016`–`0022`，代码树与 Jump 36 的内核一致。逐次上机的记录在 git 历史里
（`88ddf27` 及之前的 `android.md`）。

## 已知情况

- **微信读书慢**：和 PW3 一样受 1 GHz 单核限制（`docs/known-issues` 第 12 条）。刚装好时是 `quicken`（解释
  执行），后台编译任务要设备息屏空闲约 71 分钟才跑；PW4 上休眠还没调通，没验证它能否自己触发，可以手动
  `cmd package compile -m speed -f com.tencent.weread.eink`（约 20 分钟）。
- **原厂长时间运行后跳转被拒**：原厂连续运行十几小时、用过 U 盘模式后，`pm_test=core` 挂起时很多时钟门没关
  （`gates_clear=0`，`ccgr4=ffffffff`），模块拒绝跳转（`RESULT REFUSED`）。重启原厂后再点就能跳，哪个驱动
  没关时钟没有查。
- **插着 USB 按电源键息屏会 panic**（Jump 36，2026-11-25）：息屏时安卓请求挂起，被固件缓存重新请求 EPDC 固件
  （`Wakeup pending, aborting suspend`）中止；中止后的卡探测把 Wi-Fi 芯片当成拔出，bcmdhd 移除时 `BUG()`
  在 `sdio_claim_host`。内核 `0023`（Jump 37）。挂起本身仍会被固件缓存中止，所以现在息屏不省电，见休眠计划。
- **休眠（进行中，2026-11-25）**：Jump 38 关掉固件回退（`CONFIG_FW_LOADER_USER_HELPER_FALLBACK`），挂起不再被中止。
  `pm_test` 五级都返回 0、UART1 中断仍为 0；看门狗 `WCR=0x773d`（`WDZST` 已置位）。随后发现：挂起中止会让 Wi-Fi
  失效（内核 `0024`）；eMMC 恢复后出现 CRC 错误和超时，Jump 39 上 `/data` 日志中止、变只读（`data.img` 在电脑上
  `e2fsck` 修复，无文件丢失），内核 `0025`。唤醒后 bcmdhd 重建网络接口，wificond 按设计中止并重启，之后
  WifiStateMachine 把 Wi-Fi 留在关闭状态，所以 PW4 改成息屏即关 Wi-Fi（wifi 补丁 `0005`，`ro.pw3.wifi_idle_ms=0`）。
  自动挂起起初每次都被中止：`mmc_rescan` 提前返回时漏放唤醒锁（内核 `0026`），输入核心在挂起时无条件发
  `SYN_REPORT`、占住触摸屏的 evdev 唤醒锁（内核 `0027`）；息屏时电源 HAL 写 `/proc/touch` 锁住触摸。
  **Jump 42（2026-11-25）：按电源键息屏后进入深度休眠（`Turn off M/F mix!`），安卓定时闹钟能经 SNVS RTC 唤醒、
  0.1 s 内重新睡下，电源键唤醒，Wi-Fi 重连、触摸解锁，`/data` 无错误。** 待做：整夜耗电、插 USB 时的休眠。
  **测试 Jump 之前先在电脑上备份 `data.img`。**
- 未做：蓝牙、`pw3-fg-state`。
- **PW4 没有带上 PW3 的 `system.prop`**（2026-11-25 发现并补上）：构建只取设备自己目录下的 `system.prop`，PW4 目录
  里没有，于是 `dalvik.vm.extra-opts=-Xnorelocate` 和 `dalvik.vm.dex2oat-flags=--no-watch-dog ...` 都没生效：ART 用的是
  `/data/dalvik-cache` 里搬过地址的启动镜像副本，`cmd package compile -m speed` 微信读书在 570 s 被 dex2oat 的看门狗
  杀掉。`BoardConfig.mk` 现在用 `TARGET_SYSTEM_PROP` 指向 PW3 的那份。已有的数据镜像里要删一次
  `/data/dalvik-cache/arm/system@framework@boot*`：副本还在时 ART 仍优先用它，`-Xnorelocate` 不起作用。

## 蓝牙（2026-11-26，分支 `port/pw4-bt`，内核分支 `bt-prep`）

- **Jump 44**（只换内核）：`bt_pwr_ctrl` 的 rfkill（`/sys/devices/soc0/bt_pwr_ctrl/rfkill/rfkill0`）开关 10 次，UART1、UART3 中断不动；
  ttymxc2 上 HCI 复位 200 次无失败，芯片名 `BCM43430A1`；加载原厂 btd 用的 `BCM4343A1_001.002.009.0152.0515.hcd`
  后名字变为 "BCM4343A1 26MHz Class 1.5 Amazon eReader-0152"，921600 和 3 Mbaud 各 300 次复位无失败。
- **Jump 45**（加蓝牙协议栈）：固件目录要全员可读（根文件系统此时只读，rc 里改属组无效）；还要声明
  `android.hardware.bluetooth_le`，否则 BluetoothManagerService 停在 BLE_ON 不绑 GattService。"已连接的设备"页
  因为没有 UsbService 崩溃（Settings 补丁 `0010`）。之后能打开蓝牙、配对 Bose 耳机（选不带 `LE-` 的条目），
  微信读书听书从耳机出声，音频欠载 0；播放时单核基本占满（微信读书 80%、解码 22%、蓝牙 17%）。

