# PW4 移植 · 第 0 阶段：GPL 源码与补丁试打

只在开发机上做，**没有碰设备**。目的是在动手前把两件事量清楚：亚马逊为 PW4 公布的源码够不够用，Readom 的 85 个内核补丁落到 PW4 的 4.1.15 上是什么情况。上机实测资料见同目录的 `README.md`。

- 日期：2026-11-23
- 源码包：`Kindle_src_5.18.1.1.1_4769680023.tar.gz`（`https://kindledownloads.s3.amazonaws.com/` 下），393 771 851 字节，SHA-256 `dba51e8ea149859289613c54828150e303412c760456828a87d1c8abef0a2bca`，`gzip -t` 通过。和设备固件 5.18.1.1.1（4769680023）对应
- 内核在 `gplrelease/linux-4.1.15.tar.gz`；`gplrelease/modules-out-of-tree/` 是空目录
- 源码包和解出来的内核树按仓库规矩不进仓库

## 一、结论

1. **PW4 上要用到的驱动源码都在包里**，可以照着改，不用逆向。
2. **亚马逊留了一份 `arch/arm/configs/imx_rex_android_defconfig`**（rex 是 PW4 的板子代号，见 `README.md` 第二节），里面开了 `CONFIG_ANDROID`、`CONFIG_ANDROID_BINDER_IPC`、`CONFIG_ASHMEM`、`CONFIG_SECURITY_SELINUX`。配置片段可以从它和设备上读出的 `data/stock-kernel-config.txt` 两份出发。
3. **这棵 4.1.15 已经带着大部分 Android 内核功能**（第三节）。3.0.35 上靠回移补的东西，这里多数现成。还缺的主要是 Oreo 要的新版 binder。
4. 85 个补丁逐个试打（第四节），初判：可去掉 11 个、待核实 14 个、要移植 30 个、要按 PW4 硬件重写 29 个，另有根目录的 perl 补丁 1 个。
5. 开发机上**还没有合适的交叉编译器**。原厂内核由 gcc 4.9.1 编出（`README.md` 第二节）；Readom 用 AOSP 预编译的 `arm-linux-androideabi-4.9`（`source-lock.json` 里锁定了版本）。下一步要先装好它，用原厂配置原样编一遍，核对 vermagic 和 `Module.symvers`，交接模块能不能加载取决于这一步。

## 二、PW4 驱动在源码里的位置

| 部件 | 源码 |
|---|---|
| Wi-Fi，博通 bcmdhd | `drivers/net/wireless/bcmdhd/` |
| EPDC v2 及波形解析 | `drivers/video/fbdev/mxc/mxc_epdc_v2_fb.c`、`mxc_epdc_v2_adwbf.c`、`epdc_v2_regs.h` |
| EPD 电源 FP9929 | 没有单独驱动，写在 `drivers/video/fbdev/mxc/mxc_epdc_fb_lab126.c` 和 `mxc_epdc_v2_fb.c` 里 |
| PXP v3 | `drivers/dma/pxp/pxp_dma_v3.c` |
| 触摸 Goodix GT9xx | `drivers/input/touchscreen/gt9xx.c`、`gt9xx_update.c` |
| PMIC BD71827 | `drivers/mfd/bd71827.c`、`drivers/power/bd71827-power.c`、`drivers/rtc/rtc-bd71827.c`、`drivers/gpio/gpio-bd71827.c`、`drivers/leds/leds-bd71827.c` |
| 前光 LM3695 | `drivers/video/backlight/lm3695_bl.c` |
| 充电检测 MAX14656 | `drivers/power/max14656_charger_detector.c` |
| 霍尔传感器 | `drivers/char/rex_hall.c` |
| Falcon | `drivers/falcon/`、`drivers/pinctrl/pinctrl-falcon.c` |

defconfig：`imx_rex_defconfig`、`imx_rex_android_defconfig`，另有 `imx6sll_evk_falcon_defconfig`。`arch/arm/boot/dts/` 里没有 rex 板的 dts（有 `imx6sll-evk*`、`imx6sll-jaeger-wfo.dts`），设备树以设备上读出的 `data/stock.dts` 为准。

## 三、树里已经有的 Android 内核功能

| 功能 | 位置 | 状态 |
|---|---|---|
| binder | `drivers/android/binder.c`（不在 staging，3721 行） | 有，但是旧版：没有多设备（hwbinder / vndbinder）、scatter-gather、fd 数组。Readom 的 `0003`、`0008`、`0900`、`0901`、`1038` 要按新路径重做，或者整体换成更新的 binder |
| ashmem、lowmemorykiller、ion | `drivers/staging/android/` | 有 |
| qtaguid、quota2、IDLETIMER | `net/netfilter/` | 有 |
| uid_cputime | `drivers/misc/uid_cputime.c` | 有 |
| interactive 调频 | `drivers/cpufreq/cpufreq_interactive.c` | 有 |
| wakeup_reason | `kernel/power/wakeup_reason.c` | 有 |
| MTP、accessory、configfs gadget | `drivers/usb/gadget/function/f_mtp.c`、`f_accessory.c`、`configfs.c` | 有 |
| uid 范围路由 | `include/uapi/linux/fib_rules.h` 有 `FRA_UID_START` | 有一部分，和 Readom 的 `0333` 不是同一版，要对 |
| `accept_ra_rt_table`、`tcp_default_init_rwnd` | `net/ipv6/addrconf.c`、`net/ipv4/sysctl_net_ipv4.c` | 有 |
| seccomp TSYNC、selinux xperms、`CLOCK_BOOTTIME_ALARM`、`EPOLLWAKEUP`、`process_vm_readv` | 主线 | 有 |
| ambient capabilities | `include/uapi/linux/prctl.h` 里没有 `PR_CAP_AMBIENT` | **缺**，`1050` 要移植 |
| ARM mmap 随机位（`ARCH_MMAP_RND_BITS`） | `arch/arm/Kconfig` 里没有 | **缺**，`1010` 要移植 |
| sdcardfs | 没有 | Readom 也没用 |

## 四、85 个补丁的试打结果

做法：在原样解出的 4.1.15 树上，对 `patches/android-core/` 的每个补丁单独 `patch -p1 -N -t --dry-run`（只有 `0000` 实际打上，其余互不叠加），统计每个补丁的块数（`@@`）。

- **能打上**：块可以干净应用
- **跳过**：patch 认为已经打过（反向检测），或者目标文件在 4.1 里不存在、整块被忽略。两种情况都算在这一列，要和「缺文件」一列一起看：缺文件为 0、跳过很多的，多半是树里已经有了
- **失败**：上下文对不上
- 共 960 块：能打上 153、跳过 554、失败 253

分类是按补丁内容和上面第三节的核对做的**初判**，还没有逐行审：

- **去掉**：4.1 主线或这棵树里已经有同样的东西
- **待核实**：大部分已经有，或者看起来已有、但要逐行比对才能定
- **移植**：不碰硬件，要在 4.1 上重新落位（binder、网络权限、capabilities、ASLR，以及 Readom 自己修的 qtaguid、binder、wakeup 问题）
- **重写**：碰 PW3 的硬件驱动（EPDC、USB、max77696、wario hall、ath6kl、Cypress 触摸、板级），在 PW4 上要对着第二节的驱动按原意重写。分类标准沿用移植文档第二节：`compat-wireless` 里的 cfg80211 不算硬件，所以 `1032`、`1047` 归在「移植」

根目录的 `0001-timeconst-current-perl.patch` 不在表里：它修的是 3.0.35 用 perl 生成时间常数的脚本，4.1 已经改用 `kernel/time/timeconst.bc`，预计不需要。

| 补丁 | 块 | 能打上 | 跳过 | 失败 | 缺文件 | 分类 |
|---|---|---|---|---|---|---|
| `0000-enable-staging-kconfig` | 1 | 0 | 1 | 0 | 0 | 去掉 |
| `0001-aosp30-binder-ashmem-selinux-hooks` | 21 | 12 | 2 | 7 | 0 | 移植 |
| `0002-lineage30-selinux-policy30-xperms` | 89 | 8 | 71 | 10 | 0 | 待核实 |
| `0003-rinando-oreo-binder-contexts-sg` | 139 | 3 | 135 | 1 | 2 | 移植 |
| `0004-rinando-lmk-oom-score-adj` | 8 | 5 | 1 | 2 | 0 | 移植 |
| `0005-seccomp-filter-tsync-arm-nnp` | 48 | 9 | 5 | 34 | 1 | 去掉 |
| `0006-seccomp-task-struct-type` | 1 | 0 | 1 | 0 | 0 | 去掉 |
| `0007-arm-separate-seccomp-trace-flags` | 1 | 0 | 0 | 1 | 0 | 去掉 |
| `0008-binder-fdset-freezer-size-compat` | 5 | 2 | 2 | 1 | 1 | 移植 |
| `0100-timerfd-alarm-boottime` | 40 | 0 | 40 | 0 | 0 | 去掉 |
| `0101-pm-userspace-wakeup-sources` | 6 | 2 | 1 | 3 | 0 | 去掉 |
| `0102-epoll-wakeup-capability` | 24 | 6 | 0 | 18 | 0 | 去掉 |
| `0103-timekeeping-boottime-nsec-64bit-sum` | 1 | 0 | 0 | 1 | 0 | 待核实 |
| `0200-android-power-supply-abi` | 3 | 0 | 2 | 1 | 2 | 重写 |
| `0201-wario-hall-lid-input` | 11 | 0 | 10 | 1 | 1 | 重写 |
| `0202-android-frontlight-blanking` | 3 | 0 | 2 | 1 | 1 | 重写 |
| `0290-guard-lab126-fadvise-extension` | 1 | 0 | 0 | 1 | 0 | 移植 |
| `0291-guard-lab126-uptime-fields` | 2 | 0 | 0 | 2 | 0 | 移植 |
| `0300-mount-namespace-proc-setns` | 18 | 5 | 0 | 13 | 0 | 去掉 |
| `0301-proc-hidepid-gid-enforcement` | 14 | 8 | 3 | 3 | 0 | 去掉 |
| `0310-android-ath6kl-firmware-policy` | 6 | 0 | 6 | 0 | 4 | 重写 |
| `0311-android-disable-touch-loader` | 3 | 0 | 2 | 1 | 1 | 重写 |
| `0320-android-fg-readonly-snapshot` | 5 | 0 | 5 | 0 | 1 | 重写 |
| `0330-android-qtaguid-and-socket-lookup` | 20 | 7 | 2 | 11 | 1 | 待核实 |
| `0331-android-quota2` | 4 | 2 | 2 | 0 | 0 | 待核实 |
| `0332-android-idletimer-rev1` | 19 | 0 | 19 | 0 | 1 | 待核实 |
| `0333-android81-uidrange-routing` | 63 | 10 | 11 | 42 | 1 | 移植 |
| `0334-ipv6-accept-ra-rt-table` | 22 | 1 | 21 | 0 | 0 | 去掉 |
| `0400-device-owned-epdc-waveform` | 27 | 2 | 25 | 0 | 3 | 重写 |
| `0401-epdc-valid-temperature` | 21 | 1 | 20 | 0 | 3 | 重写 |
| `0402-android-network-gid-capabilities` | 9 | 4 | 1 | 4 | 0 | 移植 |
| `0403-epdc-first-picture-not-black` | 1 | 0 | 1 | 0 | 1 | 重写 |
| `0490-kbuild-gnu-make-hash-escape` | 2 | 0 | 0 | 2 | 0 | 待核实 |
| `0500-android-bounded-display-suspend` | 41 | 8 | 22 | 11 | 1 | 重写 |
| `0600-android-inet-group-permission` | 5 | 0 | 0 | 5 | 0 | 移植 |
| `0700-device-bound-wbf-wrf-envelope` | 9 | 0 | 9 | 0 | 2 | 重写 |
| `0710-android-proc-eink-private` | 1 | 0 | 1 | 0 | 1 | 重写 |
| `0800-ashmem-serialize-size-publication` | 1 | 0 | 1 | 0 | 1 | 移植 |
| `0810-qtaguid-socket-tag-lifetime` | 16 | 13 | 0 | 3 | 0 | 移植 |
| `0811-qtaguid-control-reader-privacy` | 5 | 0 | 0 | 5 | 0 | 移植 |
| `0812-idletimer-producer-stop-and-work-drain` | 5 | 4 | 0 | 1 | 0 | 移植 |
| `0813-quota2-zero-complete-log-payload` | 1 | 0 | 0 | 1 | 0 | 移植 |
| `0820-android-pmic-complete-i2c-transfers` | 4 | 1 | 3 | 0 | 1 | 重写 |
| `0900-binder-poll-waitqueue-lifetime` | 4 | 0 | 4 | 0 | 1 | 移植 |
| `0901-binder-private-buffer-metadata` | 16 | 0 | 16 | 0 | 1 | 移植 |
| `1000-android-private-idme-proc` | 2 | 1 | 1 | 0 | 1 | 重写 |
| `1010-arm-mmap-base-aslr` | 10 | 5 | 0 | 5 | 0 | 移植 |
| `1011-selinux-genfs-per-file-labels` | 5 | 0 | 4 | 1 | 0 | 待核实 |
| `1012-signal-allow-self-siginfo` | 2 | 0 | 0 | 2 | 0 | 待核实 |
| `1013-process-vm-access` | 10 | 3 | 1 | 6 | 0 | 去掉 |
| `1014-proc-use-shared-mm-access` | 1 | 0 | 0 | 1 | 0 | 待核实 |
| `1015-cpuset-cap-sys-nice-delegation` | 6 | 2 | 0 | 4 | 0 | 移植 |
| `1020-epdc-sleepable-dma-allocation` | 2 | 0 | 2 | 0 | 1 | 重写 |
| `1021-epdc-honor-device-dma-mask` | 2 | 0 | 2 | 0 | 1 | 重写 |
| `1022-usb-optin-bounded-state-observation` | 9 | 0 | 9 | 0 | 1 | 重写 |
| `1023-usb-explicit-debug-active-mode` | 6 | 0 | 6 | 0 | 1 | 重写 |
| `1024-usb-debug-clock-release-after-work` | 2 | 0 | 2 | 0 | 1 | 重写 |
| `1030-epdc-android-queue-snapshot-requests` | 1 | 0 | 1 | 0 | 1 | 重写 |
| `1031-busfreq-initial-cpu-setpoint` | 1 | 0 | 1 | 0 | 1 | 重写 |
| `1032-cfg80211-interface-mac-address` | 1 | 0 | 1 | 0 | 1 | 移植 |
| `1033-exec-domain-linux32` | 1 | 0 | 0 | 1 | 0 | 移植 |
| `1035-input-wakeup-events-hall-onkey` | 16 | 0 | 16 | 0 | 2 | 重写 |
| `1036-usb-host-keeps-system-awake` | 12 | 0 | 12 | 0 | 2 | 重写 |
| `1037-power-off-is-full-shutdown` | 2 | 0 | 2 | 0 | 1 | 重写 |
| `1038-binder-pool-wait-no-spurious-restart` | 1 | 0 | 0 | 1 | 0 | 移植 |
| `1039-usb-cable-wakes-device-controller` | 3 | 0 | 3 | 0 | 1 | 重写 |
| `1040-android-gadget-mtp-ptp-functionfs` | 9 | 4 | 1 | 4 | 0 | 移植 |
| `1041-wakeup-source-stats-follow-android` | 16 | 1 | 2 | 13 | 0 | 移植 |
| `1042-usb-no-keep-awake-debug` | 6 | 0 | 6 | 0 | 1 | 重写 |
| `1043-printk-ring-survives-watchdog-reset` | 8 | 0 | 8 | 0 | 1 | 移植 |
| `1044-usb-gadget-ops-keep-off-stopped-controller` | 13 | 0 | 13 | 0 | 2 | 重写 |
| `1045-wifi-enable-waits-for-card-detection` | 1 | 0 | 1 | 0 | 1 | 重写 |
| `1046-usb-cable-events-in-kernel-log` | 5 | 0 | 5 | 0 | 2 | 重写 |
| `1047-cfg80211-regdb-answers-without-crda` | 5 | 0 | 5 | 0 | 1 | 移植 |
| `1048-no-kernel-line-per-user-fault` | 1 | 0 | 0 | 1 | 0 | 移植 |
| `1049-evdev-clock-selection` | 6 | 1 | 0 | 5 | 0 | 待核实 |
| `1050-capabilities-ambient` | 19 | 12 | 1 | 6 | 1 | 移植 |
| `1051-tcp-sysfs-window-knobs` | 2 | 1 | 1 | 0 | 0 | 待核实 |
| `1052-tcp-default-init-rwnd` | 7 | 1 | 0 | 6 | 0 | 待核实 |
| `1053-uid-cputime` | 3 | 2 | 0 | 1 | 0 | 待核实 |
| `1054-wifi-data-frame-filters` | 12 | 7 | 0 | 5 | 0 | 移植 |
| `1055-ptrace-proc-sub-threads` | 2 | 0 | 1 | 1 | 0 | 待核实 |
| `1056-tcpreset-registered-once` | 1 | 0 | 0 | 1 | 0 | 移植 |
| `1057-serial-dcd-change-without-a-tty` | 3 | 0 | 0 | 3 | 0 | 移植 |

## 五、下一步（第 1 阶段）

1. 装 `arm-linux-androideabi-4.9`（版本见 `source-lock.json` 的 `cross_compiler`），用 `data/stock-kernel-config.txt` 把原厂内核原样编一遍，核对 vermagic（以设备上原厂模块 `modinfo` 读到的为准）和 `Module.symvers`。设备开着 `CONFIG_MODVERSIONS`，交接模块要对着它编
2. 用 `imx_rex_android_defconfig` 加 Readom 的 `config/android-core.config` 做出 PW4 的配置片段（对应移植文档第六节「配置片段分两半」）
3. 先处理「去掉」和「待核实」两类，再做 binder 升级和其余「移植」类，编出第一版能跑 Android 用户空间的内核
4. 交接模块按 `README.md` 第四、七节改（DTB 代替 ATAG，私有页表保留模块区，L2 回调换成 4.1 的名字）

## 六、工期估计（更新）

| 阶段 | 估计 | 说明 |
|---|---|---|
| 1. 编出内核 | 1.5–3 周 | Android 的基础功能多数现成，主要是 binder 升级和「移植」类 |
| 2. 交接跑通 | 2–4 周 | 风险最高，DT、2G/2G 页表、L2、Falcon 都要处理 |
| 3. 显示、触摸、Wi-Fi、电源、USB | 4–8 周 | 外设全换了，但源码都在 |
| 4. 测量、合并 | 2–4 周 | |

合计业余时间约 2–4 个月。
