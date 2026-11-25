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

**插 USB 卡死的分析（2026-11-24 凌晨，未上机）**：Jump 13 卡死时日志里连 `bd71827` 充电器中断的
`usb_gadget_chg_det_sync` 都没有，所以卡在插线后最先发生的事上：i.MX 自己的 USB 唤醒。控制器在没插线时处于
低功耗（时钟关、`in_lpm`），`ci_irq` 收到 VBUS 唤醒中断就 `pm_runtime_get`，恢复路径
`imx_controller_resume` 先 `request_bus_freq(BUS_FREQ_HIGH)`（DDR 从低频切回高频，由 OCRAM 里的
`lpddr2_freq_imx6sll.S` 做），再开时钟、读写 USBNC/PHY——全程不打印。嫌疑最大的是 DDR 调频。
- 内核补丁 `0016`（诊断）：USB 唤醒中断、恢复的每一步、每次 DDR 调频（开始/完成）各打一行 `pw4-trace:`。
  Lab126 日志每条都写回内存（`0011`），卡死后最后一行就是卡住的那一步；平时 EPDC 刷新也会调频，日志同时说明
  调频本身在不在工作。
- 参数 `pw4_busfreq_off`：DDR 一直停在开机时的频率（原厂在进休眠前已切到高频，交接就从那里开始）。
- Jump 15 = 跟踪；Jump 16 = 跟踪 + `pw4_busfreq_off`（同一个系统镜像，只差命令行）。15 卡、16 不卡，就是调频。
  `pack-initramfs.py --cmdline-extra` 加这种参数，`cmdline_sha256` 随之改。

**Jump 15 上机（2026-11-24）**：电源键、前光正常。插 USB **没有卡死**：VBUS 唤醒 → DDR 切高频 → 时钟 → USBNC →
PHY 全部走完（`pw4-trace` 共 4 ms），电脑枚举到 `configured`，能充电。Jump 13 的卡死这次没复现，原因待定（`0016` 先留着）。
- Wi-Fi 仍不行：bcmdhd 的 insmod 成功，但下载固件的是 SDIO probe 的内核工作线程（`kernel` 域），SELinux 拒绝它
  读 `/firmware`（`pw3_private_firmware_file`）。`sepolicy/pw4_hardware.te` 给 `kernel` 也放行。
- 充电灯触发器每次充电器事件都报 `scheduling while atomic`（`bd71827_led_set` 在读锁下走 I²C）→ 内核 `0017`。
- Jump 17 = 这两处修复（不带 `pw4_busfreq_off`）。

**Jump 17 上机（2026-11-24）**：Wi-Fi、充电灯正常。插 USB 又卡死，约 70 s 后硬件看门狗（60 s）重启，
没有 panic 也没有 `Restarting`。最后一行是 `busfreq high done`；Jump 15 插线时同一位置下一行是 max14656 的
`usb_gadget_chg_det_sync`，它前面只有 `cpufreq_override(2)`：CPU 396 → 996 MHz（升压经 I²C 写 bd71827，再重设 PLL1）。
- 内核 `0018`（诊断）：DCIN 中断、max14656 检测、override、调频每一步都打 `pw4-trace`；参数 `pw4_cpufreq_override_off`。
- Jump 18 = 跟踪；Jump 19 = 跟踪 + `pw4_cpufreq_override_off=1`（同一个系统镜像）。

**Jump 18 上机（2026-11-24）**：插线一次就卡死。插线后**没有任何中断打印**（bd71827 DCIN、USB 唤醒都没有），
软锁死检测（`softlockup_panic=1`）也没触发，像是硬件层面的冻结。卡死时 CPU 在 996 MHz（最后一次调频是升频）；
Jump 17 卡死前刚升频；Jump 15 插线时 CPU 在 396 MHz，没卡。
- 内核 `0019`：参数 `pw4_cpufreq_low`。Jump 20 = 跟踪 + `pw4_cpufreq_low=1`。

**Jump 19 上机**：关掉 override 仍卡死。**读代码的结论（2026-11-24）**：交接在原厂 `pm_test=core` 挂起的 syscore 回调里，
所有驱动的挂起回调都已执行、恢复回调永远不会执行，它们留下的硬件状态都带进了安卓内核。与插线有关的：
- `bd71827_i2c_suspend_late`：进 PMIC 测试模式把 0x42 设成 0x4C（关闭关机时序），`resume_early` 才改回 0x44；
- ChipIdea（`CONFIG_USB_REX`）挂起时打开 PHY 唤醒、关掉 PHY 3.0V（`vdd3p0`），插线由 max14656 检测后再打开。
内核 `0020` 在 probe 时恢复 0x42，并加 `pw4_usbwatch`。Jump 21 = 0020（不带调频参数）。

**Jump 22–25（2026-11-24 下午，另一个助手做的）**：都带了 `pw4_busfreq_off`。`0020` 生效（日志 `power sequence register
was 0x4c, restore 0`，证实原厂留下了 0x4C）。插线后中断、max14656、USB 唤醒、枚举都走完（22、24），但几秒后冻结。
**这几次的日志都有位翻转**（22/23/24 分别 28/5/11 处被改坏的字符），不带 `pw4_busfreq_off` 的 15/17/18 一处都没有：
关掉调频后 DDR 并不在正确的高频设置上，内存出错。所以 22–25 的冻结不能算到 USB 头上，`pw4_busfreq_off` 不能再用。
Jump 21（`0020`，调频正常）跑过一次但日志没取就被覆盖了，要重测。

**更正（2026-11-24 晚）**：日志的位翻转从带 `0020` 的 Jump 21 起才有，与 `pw4_busfreq_off` 无关。恢复正常关机时序后，
看门狗重启时 PMIC 会短暂断掉 DDR 供电，内存里的日志掉位。Jump 22–25 的结果有效。

**Jump 21 上机**：插 PC 正常（中断、max14656、唤醒、枚举、配置都走完）；**拔线瞬间冻结**，1 ms 的监视和中断都来不及记录。
只供电的线、墙充插拔都不冻（另一个助手的测试）。VBUS 有效是 max14656 驱动经 anatop 覆盖位写进去的（`VBUS_DETECT`
0x1c→0xac），拔线后约 1.3 s 才撤掉，此间控制器不知道线已断。
**原厂对照**（只读，`devmem`）：插 PC、拔线时 anatop/USB 寄存器的变化和安卓一样，`vdd3p0` 在原厂也始终关着，拔线后同样
约 1.3 s 才撤覆盖。SoC 侧状态一致，下一步比 PMIC：原厂 `i2cdump -f -y 0 0x4b` 已存，Jump 26 用 `0023` 打出安卓下的。

**Jump 26 上机**：拔线仍冻结。PMIC 寄存器对比原厂：配置寄存器一致，只有 **buck1/buck2 RUN = 0x0c（1.1 V），原厂 0x13（1.275 V）**。
原因：DVS 初始化写 1.1 V，交接时 CPU 已在 996 MHz（原厂从 u-boot 的 792 MHz 起，cpufreq 必然切一次并升压），
所以要到第一次调频才升压。这是真问题（`0024` 修），但几次冻结都发生在第一次调频之后，不是拔线冻结的原因。
原厂里 `cpu0-arm` 是 buck2、`cpu0-soc` 是 buck1。Jump 27 = `0024`；接 PC 时 ADB 可用，改用 adb 在现场做对照。

**Jump 27 上机**：`0024` 生效（`keep run selector 0x13`）。插 PC 后 230 ms、枚举中（主机 `not accepting address`）冻结，
此时 DDR 在高频、电压正确。冻结与 USB 高速通信同时发生（枚举、通信中拔线），不只在 VBUS 跳变。
下一个对照：Jump 28 = Jump 27 + `cpuidle.off=1`（6SLL 的 WAIT/LOW-POWER-IDLE 会门控 ARM 时钟、在 OCRAM 里让 DDR 自刷新）。

**Jump 28 上机**：`cpuidle.off=1` 生效（`failed to register cpuidle driver`），插线正常、拔线仍冻结，排除 cpuidle。
为省上机次数，一次准备两个：Jump 29 = 内存调频、CPU 调频、USB 运行时 PM、空闲低功耗全关（`pw4_busfreq_off`
`pw4_cpufreq_hold` `pw4_usb_rpm_forbid` `pw4_cpuidle_wfi`）；不冻则用 adb 在运行中逐项打开再插拔。
Jump 30 = 只加 `pw4_udc_nopullup`：线上无 USB 通信，冻则与通信无关。

**Jump 29、30 上机**：29（四项全关，开关都确认生效）插线即冻结，四项排除。30（`pw4_udc_nopullup`，日志 3 次
`pullup refused`）插拔多次都不冻：**没有 USB 通信就不冻**，电源、PMIC、max14656 这一路排除。
Jump 31 = 30 的内核 + Wi-Fi adb（`pack-initramfs.py --default-prop service.adb.tcp.port=5555`、`--adb-keys`
本机公钥，只在本地打包），在不冻的状态下用 adb 运行中打开 D+ 上拉、换 USB 功能组合，逐项对照。

### Jump 32–34 (2026-11-25): Wi-Fi root shell, the unplug freeze is not a busy wait

- Readom's adbd is USB only (`PW3_USB_ONLY_ADBD`), so TCP adb never listens. The
  diagnostic shell is `toybox nc` under `su` (init → shell domain → su).
- With the gadget connected over the shell: HS enumeration and 30 MB adb push/pull
  are fine; a soft disconnect followed by unplugging is fine; unplugging while
  connected froze (Jump 32, 33), and once with Wi-Fi off (one other unplug with
  Wi-Fi off did not). Plugging in with the pull-up refused also froze once.
- Jump 33 bounded every UDC busy wait: no `udc stuck`, and the 1 ms watcher
  logged nothing after the cable moved — no PMIC VBUS interrupt, no UDC
  interrupt. Power, regulator and clock state match stock (PMU_REG_CORE
  0x00482012, USBPHY1 from ldo3).
- Jump 34 adds an EPIT2 lockup detector at top GIC priority to tell an
  interrupt storm from a bus hang.
