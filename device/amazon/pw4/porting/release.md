# PW4 的一键安装入口（发行入口）

和 PW3 一样：使用者只往 Kindle 的 `documents` 里拷一个 `Readom.sh`，在书库点 "Readom"，安装、升级、启动都在设备上完成
（设计见 `installer/README.md`、`docs/design/release-build.md`）。这一页记 PW4 和 PW3 不同的地方、怎么构建、
哪些已在电脑上验过、哪些要上真机验。开发用的跳转（`pack-jump.sh`、`pack-initramfs.py`、`make-device-archive.py`、
`probe/ReadomJump*.sh`、`pw4-probe`）由它取代，留在仓库里只作开发记录。

**状态（2026-11-26）：构建侧和设备端程序写完，电脑上验过；没有上过真机。**

## 和 PW3 的差别

| | PW3 | PW4 |
|---|---|---|
| 机型配置 | `boot/profiles/pw3-5.16.2.1.1.json` | `boot/profiles/pw4-5.18.1.1.1.json`（`module_profile` 3） |
| 交接后的 PID 1 | `pw3-handoff-init`（复原 PMIC、EPDC 驱动、启动画面） | `pw4-handoff-init`（挂 `mmcblk1p2` 读面板波形、加载 `mxc_epdc_v2_fb.ko` 和 `gt9xx-ts.ko`、启动画面）。两者都由 `build_entry.py` 现编，不再拷开发包里的二进制 |
| 启动参数 | ATAG（`dd_atags`） | 设备树（`dd_fdt`：原厂那份换掉 `/chosen`） |
| 认机型 | `/proc/cpuinfo` 里的 `Wario` | 设备树根节点 `compatible` 的第一项 `amzn,imx6sll-rex`；再核对三个原厂固件文件的大小和 SHA256 |
| 设备那半 initramfs | 面板波形、AR6003 校准和固件、存储入口配置 | EPDC 通用固件、bcmdhd 固件和 NVRAM、蓝牙补丁、存储入口配置（`dd_tree_archive`，见下） |
| eMMC / 用户盘 | `mmcblk0`，`p4` | `mmcblk1`，`p10`（FAT 在偏移 8192 字节处，经 `loop0` 挂在 `/mnt/base-us`） |
| 命令行（原厂那段之后） | `loglevel=1 panic=5 rdinit=… androidboot.hardware=pw3 androidboot.init_rc=…` | `loglevel=1 rdinit=… androidboot.hardware=pw3 androidboot.init_rc=…`（`panic=5` 原厂段里已有）；没有 `pw4_lockup`、`imx.bt_dma`、TCP shell、adb 公钥、启动属性 |
| USB 检查（E104） | `fsl-usb2-udc/gadget/connected` | `/sys/class/udc/ci_hdrc.0/state` 为 `default`/`addressed`/`configured`/`suspended`；读不到时记一行日志、照常往下走（模块本身还会拒绝没停的 USB 控制器） |
| 交接前 | — | 用原厂开关关 Wi‑Fi（`lipc-set-prop com.lab126.cmd wirelessEnable 0`，等 3 秒）；没交接成就开回来 |
| PROBE 和提交 | 各一次，失败就停 | 最多三轮：挂起在回调前就忙（`pm_result=-16`、没有挂起采样）、模块不放行（时钟还开着）、提交时回调拒绝（`EBUSY`），都等 5 秒再关一次 Wi‑Fi 重来；模块因 USB 控制器不放行时直接停在 E104 |
| 日志允许清单 | `mmc0`、`fsl-usb2-udc`、`g_file_storage` | `mmc1`、`ci_hdrc`、`g_mass_storage`、`pw4-handoff:`；去掉 `mmc0`（PW4 上是 Wi‑Fi 的 SDIO） |
| `/data` | 1.75 GiB，数据格式 1 | **2 GiB，数据格式 41**（`device/amazon/pw4/BoardConfig.mk`，2026-11-26 维护者决定） |

PW3 不受影响：PW4 的代码都在 `#if CHECK_PROFILE == 3` / `#ifdef PW_DEVICE_TREE` 里，或者只由 PW4 的配置打开。
2026-11-26 在电脑上把改动前后的 PW3 版辅助程序（profile 1，带安装器）和 QEMU 版（profile 2）逐个目标文件比过：
`helper.c`、`userlog.c`、`devicedata.c`、`kindle.c`、`install.c` 等全部逐字节相同。

## 设备那半（`boot/devicedata/devicedata.c`，`dd_tree_archive`）

每次启动从原厂文件系统现场读，不进仓库、不进发行包：

| initramfs 里 | 来源（原厂） | 校验 |
|---|---|---|
| `lib/firmware/imx/epdc/epdc_ED060KC4.fw`（0644） | `/lib/firmware/imx/epdc/epdc_ED060KC4.fw` | 2,327,256 字节，SHA256 锁定 |
| `firmware/bcmdhd/fw_bcmdhd_sr.bin`（0640） | `/opt/brcm_4343w/wifi/firmware/fw_bcmdhd_sr.bin` | 369,119 字节，SHA256 锁定 |
| `firmware/bcmdhd/nvram_wfo.txt`（0640） | `/opt/brcm_4343w/wifi/nvram/nvram_wfo.txt` | 不锁定（是否逐台不同没法查），只要求非空文本、≤16 KiB |
| `firmware/bluetooth/BCM4343A1.hcd`（0644） | `/opt/brcm_4343w/bluetooth/firmware/BCM4343A1_001.002.009.0152.0515.hcd` | 35,568 字节，SHA256 锁定 |
| `pw3-preinit.conf`（0400） | 构建写的那几行 + 现场的 `fat_offset`、`fat_sizelimit`、`block=mmcblk1…` | 与 PW3 相同的一致性检查 |
| `pw3-testdata.conf`（0400） | 现场读 `android/data.img` 超级块的 UUID | 大小必须是 2 GiB |

三个锁定值取自开发包用过的、从这台设备拷下来的文件。锁定的文件不对时报 E101（说明这台的固件不是这一版），文件不在时报一般错误。

## 构建

在 PW3 构建机之外的主机上，用一份主机文件（`boot/build_host.py` 的键）告诉脚本工具和源码树在哪，例如：

```json
{"hostname": "<本机名>", "arm_tools": "<AOSP gcc 4.9>/bin/arm-linux-androideabi-",
 "musl_gcc": "<musl 1.2.5>/bin/musl-gcc",
 "android_kernel_source": "<PW4 安卓内核源码>", "android_kernel_build": "<它的 O= 目录>",
 "module_kernel_source": "<原厂 GPL 内核源码>", "module_kernel_output": "<它的构建目录>",
 "entry_work": "<临时目录>", "aosp_out": "<AOSP>/out"}
```

`musl_gcc` 可以用 AOSP 的 gcc 4.9 编 `source-lock.json` 的 musl 1.2.5（`CC="arm-linux-androideabi-gcc -mno-android"
./configure --target=arm-linux-musleabi --disable-shared CFLAGS='-Os -march=armv7-a -mfloat-abi=soft'`），
包装脚本里同样带 `-mno-android`，否则 `__ANDROID__` 会被定义。

顺序（`READOM_BUILD_HOST=<主机文件>` 下）：

1. 编安卓内核；`stage-pw4-hardware.py` 把它的模块放进 AOSP 树；**再**编系统（发行用 `user`，见下）。系统镜像里
   `/vendor/lib/modules` 必须和第 2 步导出的模块逐字节相同，`build_entry.py` 会核对。
2. `device/amazon/pw4/porting/export-kernel.py --kernel-build <O= 目录> --strip <arm strip> --output <内核导出>`
3. 系统导出（`build-result.json`、`system.raw.img`、`ramdisk.img`）。`tools/export-aosp-images.py` 现在只认 PW3 构建机和
   `pw3` 产品，**PW4 还没有正式的导出脚本**。
4. `installer/build_payload.py --product pw4 --export <导出> --data-format 41 --version <ver> --output <载荷>`
5. `installer/screens/render_screens.py --profile boot/profiles/pw4-5.18.1.1.1.json …`（E101 写的是 PW4 的机型名）
6. `boot/build_entry.py --profile boot/profiles/pw4-5.18.1.1.1.json --system-path android/system.img
   --data android/data.img:2147483648 --installer <载荷> --screens <画面> … --output <入口>`；连建两次，`Readom.sh` 逐字节一致。

机型配置的 `stock_files`（eips、原厂 init）还没读，所以现在第 6 步只能加 `--dev-unpinned`：这样构建出来的是开发入口
（回执里 `"entry": "development (--dev-unpinned)"`），辅助程序把这些文件的大小和 SHA256 打进日志，不做比对；
发出去的入口不能是这种。读法见「待真机读取的常量」。

## 待真机读取的常量

`boot/profiles/pw4-5.18.1.1.1.json` 的 `stock_files`：

```sh
ls -l /usr/sbin/eips /sbin/init /etc/upstart/init.sh /sbin/init.exe
sha256sum /usr/sbin/eips /sbin/init /etc/upstart/init.sh /sbin/init.exe
stat -c '%s %n' /usr/sbin/eips /sbin/init /etc/upstart/init.sh /sbin/init.exe
```

或者装一次开发入口，看 `android/logs/<时间>/launcher.log` 里的 `PW3_STOCK_UNPINNED` 行。注意：检查用 `O_NOFOLLOW`
打开，**符号链接不算**（日志里是 `absent errno=40`）；PW4 上哪个是真文件、哪个是链接，填配置时以 `ls -l` 为准，
只填真文件，没有的路径从 `original_boot` 里删掉。

## `/data`：2 GiB，数据格式 41

- 首次安装要的空间：系统 512 MiB + 数据 2 GiB + 给原厂留的 64 MiB = 2,751,463,424 字节（2.5625 GiB，约 2.75 GB）。
  PW3 是 2.49 GB。
- 数据格式 41 只在 PW4 上用（PW3 仍是 1）。`build_launcher.py` 核对载荷清单的格式和大小与机型配置一致，
  `build_entry.py` 核对 `--data` 的大小。
- 把一个已有的 2 GiB 数据镜像（例如开发时的 `pw4-android/data.img`）手动换进正式路径要满足：
  - 放在 `/mnt/us/android/data.img`（用户盘的 `android/data.img`，和 `system.img` 同一目录），大小正好 2,147,483,648 字节，
    ext4（超级块魔数 `0xEF53`）。
  - UUID 不用改：`pw3-testdata.conf` 每次启动由入口从这个文件的超级块现场生成，preinit 按它认镜像，开发包里那份固定
    UUID 的 conf 不再用。
  - `android/state` 里如果已经有 `data=ready 41` 而文件是换进去的：大小对就照用。没有 `state`、或 `state` 里没有 `data=`
    这一行：安装器按大小判断，大小对就保留并补写 `data=ready 41`。`state` 写着 `data=writing` 时安装器会重新生成、覆盖它，
    所以要换就别在安装进行到一半时换。
  - 开发数据是 `test-keys` 签的系统生成的，换到发行密钥签的系统上，第一次开机共享 UID 的 13 个包数据被清、
    system_server 重启一次（`docs/known-issues.md` 第十一节），使用者装的应用和书不受影响。

## 发行版要用 `user` 构建：PW4 这边要改的

只列，没做：

1. `build-aosp-pw4.sh` 是 `lunch pw4-userdebug`；`tools/build-aosp-system.sh`、`run-aosp-build-job.sh`、
   `export-aosp-images.py` 写死了 `pw3` 产品和 PW3 构建机。要么让它们按产品参数化，要么给 PW4 一套等价的作业和导出
   （回执里要有 `variant`、`boot_image`、`build_completed`、`private_firmware_injected`）。
2. `pw3.mk` 在 `user` 构建时要 `PW3_RELEASE_KEY_DIR`（PW4 继承它，报错信息写的是 `pw3-user`）；要用发行密钥。
3. `pw4.mk` 的 `PRODUCT_BRAND := PW4LocalPort`、`PRODUCT_MODEL := Kindle Paperwhite 4 development port`：会出现在
   「关于」和 USB 产品名里，发行要换。
4. `vendor/amazon/pw4-hardware` 要从发行用的内核重新暂存（现在系统镜像里的 5 个模块来自 11-23 的内核，和 11-26 的 lpm2
   内核不是同一次构建，`build_entry.py` 会拒绝）。内核本身也别带 `+`（脏树）版本号。
5. PW4 的 sepolicy（`device/amazon/pw4/sepolicy`）没有 `userdebug_or_eng`/`su` 规则，但从没在 `user` 下编过，
   `neverallow` 要等第一次 `user` 构建才知道。
6. 开发时靠的 root 通道在 `user` 里都没有：`su`、`pw4tcpsh`、`--adb-keys`、`ReadomSSH`。`user` 版上的调试只剩
   开发者选项里的 USB 调试（下节）和 `android/logs` 里的日志。
7. `pw4-handoff-init.c` 的 `usb_watch`（插线卡死时期的诊断，安卓下留一个进程 30 分钟、每 0.2 s 读 USB 时钟寄存器）
   还在，发行可以去掉或编译期关掉。

## USB：MTP 和有线 adb 在发行版上

目标沿用 PW3 发行版（`docs/design/usb-mtp.md`）：默认关 adb，开发者选项里打开，和 MTP 同时用，电脑第一次连上时
Kindle 上弹授权框；不开 TCP、不带公钥、不给 root。PW4 这条路现在**不成立**，差在框架：

- **PW4 上没有 UsbService。** `frameworks/base` 补丁 `0032` 只在 `/sys/class/android_usb` 存在时启动它；PW3 的 3.0 内核
  有 android_usb，PW4 的 4.1 是 configfs gadget，而 PW4 内核配置里 `CONFIG_USB_CONFIGFS_UEVENT` 没开
  （`android-build-lpm2/.config`），所以没有 `/sys/class/android_usb/android0`。Settings 补丁 `0010` 就是为这个加的。
  后果：
  - 开发者选项里「USB 调试」写的是 `Settings.Global.ADB_ENABLED`，读它、改 `sys.usb.config` 的是 UsbDeviceManager，
    所以开关不起作用；
  - adbd 要求授权（`ro.adb.secure=1`），弹框的是 UsbDebuggingManager（UsbDeviceManager 里），没有它电脑永远是
    `unauthorized`；
  - MTP 的用户空间（MediaProvider 的 MtpService）是收到 UsbDeviceManager 的 `USB_STATE` 广播才起的；没有它，内核的
    MTP 功能能枚举，但电脑上看不到文件。开机时 `persist.sys.usb.config=mtp` 只把 gadget 配成 mtp。
- **最小改法（待做、待验）**：PW4 内核打开 `CONFIG_USB_CONFIGFS_UEVENT`（`drivers/usb/gadget/configfs.c` 里
  `android_device_create` 的代码在 lpm2 源码里已经有），它会建 `/sys/class/android_usb/android0`（`state`）并发
  `DEVPATH=/devices/virtual/android_usb/android0` 的 uevent，这正是 8.1 UsbDeviceManager 认的；`0032` 的条件随之成立，
  不用改框架。要查：sepolicy 里 `/sys/class/android_usb` 的标签（PW3 有）在 PW4 的 genfs 上也对；Settings `0010` 那条
  「没有 UsbService」的分支此后不再走到。
- `pw3-usb-check` 和 `init.pw3.usb.rc` 按 `g_android`/`android_usb` 核对的地方，在 PW4 上不经过：PW4 装的是自己的
  `init.pw4.usb.rc`（configfs，`sys.usb.configfs=1`）和 `init.pw4.usb.modules.rc`，`pw3-usb-check` 只跑
  `configfs-serial` 这一步（`UsbGate.cpp` 里的 `Configfs()`：configfs、libcomposite 模块在、内核认 functionfs）。
  Readom 自己的 adb 门（`pw3-ffs-check` → `pw3-init-check usb`：要求开着授权、FunctionFS 是真的挂载）PW4 照样走。
  adbd 仍是 `PW3_USB_ONLY_ADBD`（`BoardConfig` 继承 PW3 的），没有 TCP 监听；`service.adb.tcp.port` 设成 -1。
- 上机要验（`user` 构建）：关于 → 连点版本号 → 开发者选项 → USB 调试；接电脑：`adb devices` 先是 `unauthorized`，
  Kindle 上弹「允许 USB 调试吗」，点允许后变 `device`；`adb shell id` 是 `uid=2000(shell)`，`adb root` 报
  `adbd cannot run as root in production builds`；关掉 USB 调试后 `adb devices` 看不到；同时 Windows/macOS 能用 MTP
  看到并拷文件；`adb tcpip 5555` 不起作用。

## 上真机要验的（入口本身）

1. 拷 `Readom.sh` 到 `documents/`，书库点 Readom：日志在 `android/logs/<时间>-<pid>/launcher.log`。
   - `PW3_FIRST_KERNEL_USB_STATE not attached`（拔线时）；接电脑时应停在 E104。
   - `PW3_FIRST_KERNEL_WIFI was_on=1 off=1 again=0`；没交接成时 `restored=1`。
   - `core-return.txt` 里 `module_ready=1`，`pm_result=0`；忙时有 `PW3_FIRST_KERNEL_RETRY attempt=1 reason=…` 和
     `core-2-return.txt`。
   - `PW3_DEVICE_DATA atags=<设备树字节数，约 32 KB> archive=<约 2.8 MB>`。
2. 首次安装：下载或 `documents/` 里的载荷 → `android/system.img`、2 GiB 的 `android/data.img` → 启动画面 → Android。
3. 回原厂后再点：读到 `previous-kernel.txt`（`pw4-handoff:`、`mmc1` 行在，`mmc0` 行不在）和 `android-report.txt`。
4. 换一台型号或固件不对的 Kindle（或改 `/proc` 无法模拟，只能看代码）：E101。
5. 错误屏停住等点击：gt9xx 报 `BTN_TOUCH`（源码里有），独占触屏这一套在 PW4 上没实测过。
6. 交接后回到原厂系统时 Wi‑Fi 是关着的（原厂开关被入口关了，原厂会记住），和开发跳转一样。
