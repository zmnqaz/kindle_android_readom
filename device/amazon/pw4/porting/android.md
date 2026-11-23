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
