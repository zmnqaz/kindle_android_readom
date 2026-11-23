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
