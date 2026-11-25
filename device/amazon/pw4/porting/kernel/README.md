# PW4 内核 · 第 1 阶段进展

在开发机上做，**没有碰设备**。第 0 阶段的清点见 `../phase0.md`，上机实测见 `../README.md`。

这里的补丁是**草稿**：放在 `device/amazon/pw4/porting/kernel/` 下，不进 `patches/`。等 4.1.15 这一组（PW4、Oasis 2、Oasis 3、Kindle 10）的补丁系列稳定了，再按 CONTRIBUTING 的规矩决定放到哪里（大概是一个和 `patches/android-core/` 并列的 4.1 系列）。

## 一、环境

| 东西 | 来源 | 说明 |
|---|---|---|
| 内核源码 | `Kindle_src_5.18.1.1.1_4769680023.tar.gz` 里的 `linux-4.1.15.tar.gz` | 校验见 `../phase0.md` |
| 交叉编译器 | AOSP `platform/prebuilts/gcc/linux-x86/arm/arm-linux-androideabi-4.9`，提交 `0390252b6bcc6217966ade31d07f8b12f6f78f89` | 和 `source-lock.json` 的 `cross_compiler` 是同一个提交。清华镜像可拉：`https://mirrors.tuna.tsinghua.edu.cn/git/AOSP/platform/prebuilts/gcc/linux-x86/arm/arm-linux-androideabi-4.9` |
| AOSP 8.1 | `android-8.1.0_r81`，清华镜像（`REPO_URL=https://mirrors.tuna.tsinghua.edu.cn/git/git-repo`，`repo init -u https://mirrors.tuna.tsinghua.edu.cn/git/AOSP/platform/manifest -b android-8.1.0_r81 --depth=1 --no-clone-bundle -g default,-darwin`） | `.repo/manifests` 的提交是 `d4ab87d25f7aa942e79e4f06b229df1f10a6aceb`，和 `source-lock.json` 一致。同步完约 39 GB |
| 宿主机 | x86_64 Linux，gcc 较新 | 两处宿主机问题见第五节 |

编译命令（`O=` 指向构建目录）：

```bash
export PATH=<gcc-4.9>/bin:$PATH
make O=<out> ARCH=arm CROSS_COMPILE=arm-linux-androidkernel- \
  HOSTCFLAGS="-Wall -O2 -fomit-frame-pointer -std=gnu89 -fcommon" -j16 vmlinux modules
```

## 二、原厂内核原样编一遍

用设备上读出的 `../data/stock-kernel-config.txt` 原样编（`olddefconfig` 后配置零差异）：

- vmlinux 和 65 个模块都编出来了；`zImage` 因为宿主机没有 `lzop` 没打包，不影响下面的核对
- 模块的 vermagic 是 `4.1.15-lab126 SMP preempt mod_unload modversions ARMv7 p2v8`
- **函数地址和设备对不上**：拿 `System.map` 和设备的 `../data/kallsyms.txt` 比，34 291 个两边都唯一的代码符号里只有 21 个地址相同，`pm_test_show` 在设备上是 `0x80069564`、这里是 `0x8007274c`。设备内核由 gcc 4.9.1（亚马逊自己的 poky 工具链）编，这里是 AOSP 的 gcc 4.9 20150123，代码生成不同，所以**编不出逐字节相同的原厂内核**
- 这对交接没有影响：交接模块在运行时读原厂内核的地址和函数体（`../README.md` 第七节），不依赖编出来的 vmlinux。要紧的是模块能被原厂内核接受：vermagic 已经一致；`CONFIG_MODVERSIONS` 开着，符号 CRC 由源码和配置决定、与编译器无关，**还要用设备上的原厂模块核对一次**（第六节）

## 三、Android 配置草稿

`android-4.1-draft.config`：从 Readom 的 `config/android-core.config` 机械映射而来，再加 PW4 的几行。和原厂配置合并：

```bash
KCONFIG_CONFIG=<out>/.config ARCH=arm scripts/kconfig/merge_config.sh -m -O <out> \
  ../data/stock-kernel-config.txt android-4.1-draft.config
make O=<out> ARCH=arm olddefconfig
```

- 以**原厂配置**为底，不用 `imx_rex_android_defconfig`：原厂配置是设备上实际在跑的，设备树、initrd、Falcon 都和它对得上
- 原 125 项里有 26 项 4.1 没有：改名的映射过去（`CGROUP_MEM_RES_CTLR` → `MEMCG`，`NETFILTER_TPROXY` → `NETFILTER_XT_TARGET_TPROXY`），其余列在文件末尾。它们分三类：要靠 Readom 补丁加的（`ANDROID_*`、`LAB126_*`、`ARM_MMAP_BASE_RANDOMIZATION`）、PW3 的硬件（Cypress、max77696、wario hall、ARC USB）、上游已经取消的（`EXPERIMENTAL`、`RESOURCE_COUNTERS`、`IPV6_PRIVACY`、`EXT4_FS_XATTR`）
- 合并后请求的 117 项全部生效（核对方法同移植文档第六节）；`NETFILTER_XT_MATCH_QUOTA2_LOG` 要靠补丁 `0004` 才选得上
- PW4 和原厂、和 PW3 不同的几处：
  - `USB_GADGET` 保持原厂的 `y`。PW3 片段里的 `=m` 会把 ChipIdea 一起变成模块，`bd71827-power`、`max14656`、`mwan_rex` 这几个内建驱动就链接不上（`ci_hdrc_set_phy_vcc`、`usb_udc_connected`）
  - **内存划分用 3G/1G，不用原厂的 2G/2G**。Android 8.1 的 init（`system/core/init/init.cpp` 的 `set_mmap_rnd_bits_action`）在 32 位 ARM 上要求 `/proc/sys/vm/mmap_rnd_bits` 必须设到 16，否则 `security_failure()`，系统起不来；而上游对 2G/2G 的 ARM 上限是 15。交接是在原厂 2G/2G 内核里跑的，下一个内核用什么划分与它无关，所以和 PW3 一样用 3G/1G：512 MiB 内存两种划分下都全是 lowmem，用户空间还多 1 GiB。`drivers/falcon/` 里没有按 `PAGE_OFFSET` 写死的地址（搜到的 `0x80000000` 是标志位）。这一条要在真机上验证 Falcon 的 BIOS 调用
  - 打开 `ANDROID_PARANOID_NETWORK`（见第五节）
  - 关掉 `MXC_GPU_VIV`：i.MX6SLL 没有 GPU 核心，而 galcore 在开了 Android 同步框架后找不到 `sync.h`
- `LOCALVERSION="-lab126-pw4-android81"`，沿用 PW3 的命名规矩

用这份配置，加上下面十个补丁，**vmlinux 和模块都能编出来**（binder、hwbinder、vndbinder，ashmem，SELinux，ambient capabilities 都在）。还没在设备上跑过。

## 四、补丁（`patches/`，按顺序应用在原样的 4.1.15 上）

| 补丁 | 做了什么 | 对应 Readom 的 |
|---|---|---|
| `0001` uid_cputime | 亚马逊树里的 `uid_cputime.c` 用了 4.1 没有的 `task_struct.cpu_power`，原厂配置没开它所以没人发现。去掉功耗那一列，输出变成 `uid: utime stime`，和 Readom 的一样 | `1053` |
| `0002` binder | 把 4.1 自带的旧 binder 换成 Readom 给 PW3 用的 Oreo binder（`0001`、`0003`、`0008`、`0900`、`0901` 叠出来的那份），按 4.1 的接口改：fd 操作用 `__alloc_fd`/`__fd_install`/`__close_fd`，`kuid_t`，`task_active_pid_ns()`，三参数的 `hlist_for_each_entry`，`map_vm_area()` 直接传页数组；加 `ANDROID_BINDER_DEVICES` | `0001`、`0003`、`0008`、`0900`、`0901` 里的 binder 部分 |
| `0003` ambient capabilities | 直接用上游提交 `58319057b784`（v4.3），在 4.1.15 上只有偏移 | `1050` |
| `0004` quota2 LOG | `NETFILTER_XT_MATCH_QUOTA2_LOG` 依赖 `IP_NF_TARGET_ULOG=n` 并包含 `ipt_ULOG.h`，而 ULOG 在 3.17 已从上游删掉，这个选项在 4.1 上根本选不上。去掉依赖，在 `xt_quota2.c` 里按原布局定义 `ulog_packet_msg_t`（netd 读的就是这个结构）| PW3 不需要（3.0.35 还有 ULOG） |
| `0005` uid 范围路由 | 这棵树带的是老 Android 内核的 `FRA_UID_START`/`FRA_UID_END`（属性号 18、19），而 8.1 的 netd（`system/netd/server/RouteController.cpp`）只发上游的 `FRA_UID_RANGE = 20`，前面是 `FRA_PAD`、`FRA_L3MDEV`。把 netlink 这一侧换成上游的布局（uapi 枚举、`struct fib_rule_uid_range`、校验、解析、删除匹配、导出、消息长度），按 uid 匹配流的那部分原样保留 | `0333`（PW3 那边连 uid 的管道也要补，这里树里已经有） |
| `0006` ashmem | `ASHMEM_SET_SIZE`、`ASHMEM_GET_SIZE` 加 `ashmem_mutex`，和 `ashmem_mmap()` 对齐。路径从 `mm/ashmem.c` 换到 `drivers/staging/android/ashmem.c`，fuzz 1 | `0800` |
| `0007` quota2 清零 | `ulog_packet_msg_t` 只填了一部分字段就整个广播出去，未初始化的内核字节会到用户空间。按这棵树 `nlmsg_put()` 的写法手工重做 | `0813` |
| `0008` IDLETIMER | sysfs 读取不再拿 `list_mutex`（和删除互相等待会死锁）；创建时先初始化再挂出来；删除时先注销 PM 通知，再 `del_timer_sync`、`cancel_work_sync`。第 2–5 块带偏移打上，第 1 块手工重做（这棵树的 `idletimer_tg_show()` 在查不到时还会解引用空指针） | `0812` |
| `0009` qtaguid ctrl | `/proc/net/xt_qtaguid/ctrl` 不再把别的 UID 的标签和 `struct sock` 地址给没有读全量权限的进程看；全局计数要读全量权限。这棵树是 seq_file 写法，按原意重做 | `0811` |
| `0010` mmap 随机化 | 上游 `d07e22597d1d`、`e0c25d958f78`（v4.5）合成一个。`arch/Kconfig` 那块手工放在 `ARCH_HAS_ELF_RANDOMIZE` 后面：4.1 的上下文不同，模糊匹配会把它塞进 `CLONE_BACKWARDS2` 的帮助文字里。配置里设 16 位（见第三节） | `1010` |

补丁 `0011`（2026-11-23 加）：Readom `1043` 的 4.1 版。4.1 的 Lab126 实现在 `log_store` 里另外往 `0x9be00000` 复制一份文本；每复制一条就 `__cpuc_flush_dcache_area` + `outer_clean_range` 写回内存，开机清空固定区后也写回一次。没有它，看门狗复位时还在缓存里的日志全丢（第一、二次真跳转的日志都止于 0.26 s）。

补丁 `0012` 已撤回（2026-11-23）：给 `fsl_otp` 读序列号前后开关时钟，读完关门反而让随后经 syscon 读 OCOTP 的 `imx_thermal` 卡住。改由交接模块在跳转前恢复上电时的全开时钟门，驱动保持原样。

补丁 `0012`（新，2026-11-23）：`sdhci-esdhc-imx` 匹配 PW4 eMMC 节点的 `falcon_blk`，mmc 核心把 `no-removable` 当 `non-removable`；配合命令行 `nofalcon`，安卓内核不碰原厂已初始化过的 Falcon BIOS，用标准驱动读写 eMMC。

补丁 `0013`（2026-11-23）：binder 每页用 `map_kernel_range_noflush` 映射（`0002` 在栈上的 `vm_struct` 上调 `map_vm_area()`，`flags` 未初始化，第 11 次跳转时 `binder_mmap` 全部 -12）。

补丁 `0014`（2026-11-23）：`mxc_epdc_v2_fb` 回答 Readom 的 `MXCFB_GET_ANDROID_INFO`（`0400` 的 4.1 版；波形是面板自己 FAT 里的文件，驱动注册前已读入，控制器就绪即算就绪），并接受 PW3 3.0 的 72 字节 `MXCFB_SEND_UPDATE`（号相同、大小不同，按命令区分；PW3 的 `TEMP_USE_AUTO` 0x1001 换成本内核的 `TEMP_USE_AMBIENT`）。这样 Readom 的 gralloc 和 `pw3-init-check` 不用改 ABI。本机波形 `320_R278` 的 mode_version 是 0x41，`EpdcContract.h` 在 PW4 上另认这个值。

补丁 `0015`（2026-11-23）：`0200`/`0202` 的 PW4 版。前光是 `lm3695`（`ti-lmu-backlight`，`/sys/class/backlight/bl`，0–2047），原驱动不看 `bl_power`；开 `LAB126_ANDROID_BACKLIGHT_BLANKING` 后 `bl_power` 不是 unblank 或帧缓冲 blank 时输出 0，请求的亮度保留，power HAL 灭屏就靠它。`bd71827` 的电压/电流/电量本来就是 µV/µA/µAh、充电器 `online` 就是 DCIN 检测，只有温度是整度（原厂读出 20），开 `LAB126_ANDROID_POWER_SUPPLY_ABI` 后按 0.1 °C 报。

补丁 `0016`–`0022` 是从原厂挂起中途交接带来的问题的修复，2026-11-25 整理过一次：排查 USB 插拔卡死时加的打印、开关和寄存器监视（原 `0016`–`0030` 里的诊断部分）已全部去掉，只留下面这些。排查过程见 `../android.md`。

补丁 `0016`（2026-11-24）：`leds-bd71827` 的寄存器写改到工作队列里。power_supply 的充电/充满触发器在读锁下调用 `brightness_set`，原来直接走 I²C，每次充电器事件都报 `scheduling while atomic`（Jump 15 日志）。

补丁 `0017`（2026-11-24）：内核是从原厂挂起的 syscore 阶段起来的，此前 `bd71827_i2c_suspend_late` 已把 PMIC 测试寄存器 0x42 设成"关闭关机时序"（0x4C），原厂的 `resume_early` 不会再跑；probe 时改回 0x44 并记下原值。

补丁 `0018`（2026-11-24）：i.MX 胶水层关掉控制器时钟期间，ChipIdea 的寄存器读写直接跳过、记一行并打印调用栈（前 3 次），免得总线挂死。

补丁 `0019`（2026-11-24）：bd71827 的 DVS 初始化不再把 buck1/2 的 RUN 电压降到设备树的 1.1 V（寄存器里已有更高值时保留）。交接时 CPU 已在 996 MHz、电压 1.275 V，cpufreq 看不到频率变化，原先会在 1.1 V 下跑到第一次调频（35–90 s）。

补丁 `0020`（2026-11-25）：UDC 里关中断执行的忙等按次数设上限。单核关中断时 jiffies 不走，Lab126 原有的 200 ms 保护不生效；到上限时打印 `ci_hdrc udc: busy wait gave up in ...` 并退出。排查中没有实际触发过，作为防护保留。

补丁 `0021`（2026-11-25，诊断用，默认关闭）：启动参数 `pw4_lockup=1` 时，EPIT2 每 0.5 秒以 GIC 最高优先级中断一次，jiffies 停住时打印 `pw4-lockup:` 计数增长最快的中断和被打断处的寄存器。设备无日志卡死时用它区分中断风暴和总线挂死，下面 `0022` 的根因就是它抓到的。

补丁 `0022`（2026-11-25，**USB 插拔卡死的修复**）：交接发生在原厂挂起流程中，`serial_imx_suspend()` 已置位 `UCR3_AWAKEN`，之后不会有 resume。USB 插拔时 RXD 上的边沿触发异步唤醒中断，端口未打开、时钟关闭时（安卓命令行没有 `console=`）`imx_int()` 清不掉，形成中断风暴（约 2 万次/秒），单核被占满。probe 时屏蔽该端口的全部中断源。Jump 35 实测插拔 5 个来回、7 次 VBUS 接入，UART1 中断计数为 0。

补丁 `0023`（2026-11-25）：Wi-Fi 所在的 SDIO 主机标成不可拔出。每次系统挂起尝试结束（`PM_POST_SUSPEND`）都会重新探测卡，探测可能碰上 BCM4343 正在休眠，判定卡已拔出并移除，bcmdhd 的移除流程在 `sdio_claim_host()` 里触发 `BUG()`，内核 panic（插着 USB 按电源键息屏时出现）。原厂休眠前会关 Wi-Fi，安卓不关。`wifi_card_detect()` 每次给芯片上电时重新允许扫描一次。

补丁 `0024`（2026-11-25）：Wi-Fi 跨挂起中止和模块重载都能恢复。Lab126 的设计是 `suspend_late` 关 BCM4343、`resume_early` 开，恢复时重新初始化 SDIO 卡；挂起在卡已挂起、晚期阶段之前被中止（唤醒事件、2 s 内的闹钟，安卓里很常见）时芯片没有断电重启，重新初始化报 -110，Wi-Fi 直到重启都没了。现在 SDIO 核心在卡挂起和恢复时通知 `wifi-bcm4343`，没人做过断电重启就补做一次。另外 `0023` 把主机标成不可拔出后，关开 Wi-Fi 时旧卡不会被清掉；`wifi_card_detect(1)` 遇到没有驱动绑定的卡就先移除。

补丁 `0025`（2026-11-25）：eMMC 跨挂起保持唤醒，跑 High Speed。原厂从没用 MMC 核心挂起过这块 eMMC（由 Falcon 管）；恢复时重新初始化（CMD5 睡眠、断电、HS200 初始化和调谐）之后出现 CRC 错误和数据超时，有一次让 `/data` 的 ext4 日志中止、文件系统变只读（Jump 39）。现在 eMMC 不进 HS200（不需要调谐），挂起时不睡眠、不重新初始化（控制器恢复时重设参数，`SDHCI_QUIRK2_HOST_OFF_CARD_ON`），`bd71827-power` 在挂起态保持 BUCK5（eMMC VCC）。

补丁 `0026`（2026-11-25）：`mmc_rescan` 每条退出路径都释放 `mmc_delayed_work` 唤醒锁。调度检测工作时拿锁，原来两处提前返回（扫描被禁用、不可拔出的主机已扫过）不释放，要等下一次完整扫描。`0023` 把 Wi-Fi 主机标成不可拔出后，唤醒后的检测走这条路，锁一直挂着挡住自动挂起（一次 414 s）。

补丁 `0027`（2026-11-25）：挂起时不再发多余的 `EV_SYN`（同上游 `00159f19a505`）。4.1 的 `input_dev_release_keys()` 不管有没有按键松开都发 `SYN_REPORT`，系统每次挂起都送到 evdev，占住安卓给触摸屏开的 evdev 唤醒锁（`event2`），自动挂起每次都被中止（Jump 41：27 次全失败）。

**编译参数（2026-11-23 补）**：安卓内核要带 `KCFLAGS="-fno-pic -fno-pie"`，和 Readom 的 `tools/build-kernel.sh`、交接模块一样。AOSP 的 gcc 4.9 默认生成位置无关代码，不带这个参数时内核本身还能启动，但模块里会引用 `_GLOBAL_OFFSET_TABLE_`，加载时报 `Unknown symbol _GLOBAL_OFFSET_TABLE_`（第十次交接时 EPDC 驱动就是这样没加载上）。

## 五、第 0 阶段分类的修正

逐项核对后（`../phase0.md` 第四节的表还没改，以这里为准）：

- **再去掉 7 个**，4.1 里已经有：`0103`（timekeeping 已改写）、`1012`（自发 siginfo，主线 3.9）、`1014`（`mm_access` 已在 `kernel/fork.c`）、`1051`（`net/ipv4/sysfs_net_ipv4.c` 已有）、`1052`（`tcp_default_init_rwnd` 已有）、`1055`（`same_thread_group` 判断，主线 3.9）、`0332`（`xt_IDLETIMER` 已有 revision 1）
- **已做**：`1053`、`1050`、`1010`、`0333`、`0800`、`0811`、`0812`、`0813`、binder 那 5 个（见第四节）
- **换成配置项**：`0402`、`0600`（网络按组授权）。这棵树有 Android 原版的 `ANDROID_PARANOID_NETWORK`（`af_inet`、`af_inet6`、`commoncap`、`tun` 都接上了），原厂没开；草稿配置里打开它。4.1 的 netlink 权限检查走 `cap_capable`，也由 `commoncap` 里那段覆盖，所以不需要 PW3 上为 3.0 的 `cap_netlink_send` 写的那部分
- **要注意的**：
  - `0810` qtaguid 标签生命周期：改动集中在 `uid_tag_data_tree_lock` 和 `sock_tag_list_lock` 的加锁顺序上，要逐段对照这棵树的版本做，还没动
  - `0004` lmk：这棵树的 lowmemorykiller 已经按 `oom_score_adj` 工作，多半可以去掉，待核
- 宿主机的两个问题（不改源码）：较新的宿主 gcc 默认 `-fno-common`，`scripts/dtc` 链接时报 `yylloc` 重复定义，用 `HOSTCFLAGS=... -fcommon` 绕过；打包 `zImage` 要 `lzop`

## 六、设备 CRC 核对（已完成，2026-11-23）

用只读脚本 `Readom Probe 3`（书库里点 `.sh` 执行，只拷文件不加载）把设备 `/lib/modules/4.1.15-lab126/` 下全部 61 个原厂模块拷回开发机，逐个读 `__versions` 段，和这边原样编出的 `Module.symvers` 比：

- 3343 个符号引用，CRC **全部一致**，没有缺失的导出符号
- 61 个模块 vermagic 全部是 `4.1.15-lab126 SMP preempt mod_unload modversions ARMv7 p2v8`，和这边一致
- 设备 `CONFIG_MODVERSIONS=y`、`CONFIG_MODULE_SIG` 未开、`CONFIG_MODULE_FORCE_LOAD` 未开

结论：对着这份 GPL 源码 + 设备配置编出的模块（包括交接模块）能被原厂内核按版本校验接受，不需要手工填 CRC。
运行中加载的模块：`mxc_epdc_v2_fb`、`falcon`（专有，permanent）、`gt9xx_ts`、`configfs`、`libcomposite`、`usb_f_mass_storage`、`g_mass_storage`；Wi-Fi 关闭时 `bcmdhd` 不在。
原厂 `.ko` 是亚马逊的二进制，只留在本地，不进仓库。

## 六之二、没有串口时的日志通道（设备实测，2026-11-23）

原厂内核开了 `CONFIG_LAB126_PRINTK_BUFFER`：内核日志放在固定物理地址 `0x9be00000`（256 KB，`dmesg` 里 `Printk log placed at 0x9be00000`），下次开机从 `/proc/printkbuf` 读上一次的日志；正常重启时还会 `dump_lastk_to_mmc` 存一份到 eMMC。只读脚本 `Readom Probe 4` 两次实测：

| 上一次是怎么结束的 | `/proc/printkbuf` |
|---|---|
| 软件重启（`reboot: Restarting system`，经 imx2-wdt） | 完整保留，3604 行，从 0 秒到最后一行 |
| 长按电源键 40 秒强制重启 | 全是乱码（约 89% 的位为 1），DRAM 掉过电，日志丢失 |

结论（交接模块和安卓内核据此设计）：

- 新内核也开 `CONFIG_LAB126_PRINTK_BUFFER`、用同一地址，日志写进这块内存；
- 新内核出错时必须**自己重启**（`panic=5`、`softlockup_panic`/`hung_task_panic`，尽早喂/启用看门狗），不能停在需要长按电源键的状态，否则日志随 DRAM 一起丢；
- 交接模块在跳转前把自己的进度也写一份到这块内存里，失败时回到原厂系统能读出卡在哪一步。

Wi-Fi（同一次探测）：`/etc/sysconfig/platform_variables` 里由 `wifim` 执行 `modprobe bcmdhd iface_name=wlan0 firmware_path=/opt/brcm_4343w/wifi/firmware/fw_bcmdhd_sr.bin nvram_path=/opt/brcm_4343w/wifi/nvram/nvram_wfo.txt`（另有 `nvram_wan.txt` 一套），关闭是 `modprobe -r bcmdhd`；SDIO `02d0:a9a6`（BCM43430）挂在 `2190000.usdhc`（mmc0），电源 GPIO 47；固件 7.45.102.16，驱动 1.363.110.23。

## 七、下一步

1. 设备上拷一个原厂模块回来，做第六节的 CRC 核对
2. `0810` qtaguid 标签生命周期
3. 其余「移植」类：`0002` 剩下的 SELinux 部分、`0330`、`1015`、`1033`、`1040`、`1041`、`1043`、`1048`、`1054`、`1056`、`1057`
4. 开始写交接模块的 PW4 版本（DTB 代替 ATAG，2G/2G 页表，L2 回调），先在 QEMU 里跑
