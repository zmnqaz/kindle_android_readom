# PW4 内核 · 第 1 阶段进展

在开发机上做，**没有碰设备**。第 0 阶段的清点见 `../phase0.md`，上机实测见 `../README.md`。

这里的补丁是**草稿**：放在 `device/amazon/pw4/porting/kernel/` 下，不进 `patches/`。等 4.1.15 这一组（PW4、Oasis 2、Oasis 3、Kindle 10）的补丁系列稳定了，再按 CONTRIBUTING 的规矩决定放到哪里（大概是一个和 `patches/android-core/` 并列的 4.1 系列）。

## 一、环境

| 东西 | 来源 | 说明 |
|---|---|---|
| 内核源码 | `Kindle_src_5.18.1.1.1_4769680023.tar.gz` 里的 `linux-4.1.15.tar.gz` | 校验见 `../phase0.md` |
| 交叉编译器 | AOSP `platform/prebuilts/gcc/linux-x86/arm/arm-linux-androideabi-4.9`，提交 `0390252b6bcc6217966ade31d07f8b12f6f78f89` | 和 `source-lock.json` 的 `cross_compiler` 是同一个提交。清华镜像可拉：`https://mirrors.tuna.tsinghua.edu.cn/git/AOSP/platform/prebuilts/gcc/linux-x86/arm/arm-linux-androideabi-4.9` |
| AOSP 8.1 | `android-8.1.0_r81`，清华镜像 | `repo init` 后 `.repo/manifests` 的提交是 `d4ab87d25f7aa942e79e4f06b229df1f10a6aceb`，和 `source-lock.json` 一致 |
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
- 合并后请求的 113 项全部生效（核对方法同移植文档第六节）；`NETFILTER_XT_MATCH_QUOTA2_LOG` 要靠补丁 `0004` 才选得上
- PW4 和 PW3 不同的两处：
  - `USB_GADGET` 保持原厂的 `y`。PW3 片段里的 `=m` 会把 ChipIdea 一起变成模块，`bd71827-power`、`max14656`、`mwan_rex` 这几个内建驱动就链接不上（`ci_hdrc_set_phy_vcc`、`usb_udc_connected`）
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

## 五、第 0 阶段分类的修正

逐项核对后（`../phase0.md` 第四节的表还没改，以这里为准）：

- **再去掉 7 个**，4.1 里已经有：`0103`（timekeeping 已改写）、`1012`（自发 siginfo，主线 3.9）、`1014`（`mm_access` 已在 `kernel/fork.c`）、`1051`（`net/ipv4/sysfs_net_ipv4.c` 已有）、`1052`（`tcp_default_init_rwnd` 已有）、`1055`（`same_thread_group` 判断，主线 3.9）、`0332`（`xt_IDLETIMER` 已有 revision 1）
- **已做**：`1053`、`1050`、`0333`、`0800`、`0811`、`0812`、`0813`、binder 那 5 个（见第四节）
- **换成配置项**：`0402`、`0600`（网络按组授权）。这棵树有 Android 原版的 `ANDROID_PARANOID_NETWORK`（`af_inet`、`af_inet6`、`commoncap`、`tun` 都接上了），原厂没开；草稿配置里打开它。4.1 的 netlink 权限检查走 `cap_capable`，也由 `commoncap` 里那段覆盖，所以不需要 PW3 上为 3.0 的 `cap_netlink_send` 写的那部分
- **要注意的**：
  - `0810` qtaguid 标签生命周期：改动集中在 `uid_tag_data_tree_lock` 和 `sock_tag_list_lock` 的加锁顺序上，要逐段对照这棵树的版本做，还没动
  - `0004` lmk：这棵树的 lowmemorykiller 已经按 `oom_score_adj` 工作，多半可以去掉，待核
- 宿主机的两个问题（不改源码）：较新的宿主 gcc 默认 `-fno-common`，`scripts/dtc` 链接时报 `yylloc` 重复定义，用 `HOSTCFLAGS=... -fcommon` 绕过；打包 `zImage` 要 `lzop`

## 六、需要在设备上做的（只读）

核对符号 CRC，确认对着这份源码编的模块能被原厂内核接受：

```sh
# 在 Kindle 上（越狱后的 shell）
ls /lib/modules/$(uname -r)/
# 挑一个原厂模块，例如 bcmdhd.ko 或 gt9xx_ts.ko，拷回电脑
```

拿到 `.ko` 后在开发机上：`modprobe --dump-modversions <模块>.ko`，和这边原样编出来的 `Module.symvers` 比，CRC 全部一致才算数。

## 七、下一步

1. 设备上拷一个原厂模块回来，做第六节的 CRC 核对
2. `0810` qtaguid 标签生命周期，`1010` ARM mmap 随机化
3. 其余「移植」类：`0002` 剩下的 SELinux 部分、`0330`、`1015`、`1033`、`1040`、`1041`、`1043`、`1048`、`1054`、`1056`、`1057`
4. 开始写交接模块的 PW4 版本（DTB 代替 ATAG，2G/2G 页表，L2 回调），先在 QEMU 里跑
