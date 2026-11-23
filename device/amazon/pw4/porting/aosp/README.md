# AOSP 8.1 编译（第一步：先用 Readom 的 PW3 配置编通）

目的：在原生 x86_64 Linux 上把 Readom 的 AOSP 补丁和 PW3 产品配置完整编一遍，确认补丁能打上、整套能编过，再换 PW4 配置。
只是编译验证：产物不刷机，也不能刷（没有内核模块，见下）。

## 结果（2026-11-23）

- `android-8.1.0_r81`（清华镜像，manifest `d4ab87d2`），Readom 163 个 AOSP 补丁全部用 `tools/apply-patch-series.py` 打上，无冲突
- `lunch pw3-userdebug` → `init adbd pw3-* sepolicy`、`systemimage`、`ramdisk` 全部成功
- `system.img` 455MB（sparse），installed-files 1457 项，含 WebView、EinkBro、Launcher3Go 及其 odex
- 16 核 / 45GB 内存，几轮修环境的编译累计约 30 分钟

## 和 Readom 原流程的区别

Readom 在 ARM64 虚拟机里用 x86 chroot + QEMU 编译；这里是原生 x86_64，省掉了 native jack / native clang / 模拟器相关步骤。

| 问题 | 处理 |
|---|---|
| 宿主机没有 Python 2 | 在 Docker 里编（`Dockerfile`，Ubuntu 18.04 + OpenJDK 8） |
| Jack 用 TLSv1，新版 JDK 8 默认禁用 | 镜像里从 `jdk.tls.disabledAlgorithms` 去掉 TLSv1/1.1 |
| jack-admin 需要 curl | 镜像装 curl |
| `add-emoji-variation-sequences.py` 在 Python 3.6 + `LC_ALL=C` 下按 ASCII 读文件 | 镜像里 `python3` 换成 3.8（C locale 自动转 UTF-8） |
| Readom 的 blueprint 补丁需要 Go 1.10 的 `testdeps.ImportPath` | 按 `source-lock.json` 下载 go1.10.8（sha256 校验），设 `PW3_BUILD_GOROOT` |
| dex2oat：`ANDROID_DATA not set and /data does not exist` | 设 `ANDROID_DATA` 到 out 下 |
| dex2oat：`Failed to mmap at expected address`（宿主内核 7.0） | 见下 |
| `vendor/amazon/pw3-hardware` 要从 PW3 内核编译结果导出 | `stub-pw3-hardware.py` 按原结构生成，但不含 `.ko` |
| WebView 从 googlesource 下载不动 | 从清华镜像 `platform/external/chromium-webview` 取同一提交的同一文件，sha256 与 `prebuilts.json` 一致 |

### dex2oat 映射地址问题

在 Linux 7.0 宿主机上，Android 8.1 的宿主 ART 把 boot 镜像（如 `boot-framework.art`，5MB）按提示地址 `mmap` 时，内核把它放到了高地址（2MB 对齐），ART 随即放弃。
`PR_SET_THP_DISABLE`（`run-nothp.py`）能修好匿名映射的测试，但对 dex2oat 本身无效；用 Python 复现同样的文件映射又都成功，根因未查清。
处理：`host-art-map-fixed-noreplace.patch`，只在宿主编译（`!kIsTargetBuild`）且给了期望地址时加 `MAP_FIXED_NOREPLACE`——要么精确映射到该地址，要么报错，不覆盖已有映射。设备上的 ART 不受影响。
这是编译环境补丁，不属于 Readom 的补丁系列，不进 `.pw3-applied-patches.json` 账本。

## 步骤

```sh
AOSP=/path/to/aosp-8.1 READOM=/path/to/readom GO=/path/to/go1.10.8
# 1. 补丁
grep -v '^#' $READOM/patches/projects.txt | while read -r p s; do [ -n "$p" ] &&
  python3 $READOM/tools/apply-patch-series.py $AOSP/$p $READOM/patches/$s/*.patch; done
git -C $AOSP/art apply $READOM/device/amazon/pw4/porting/aosp/host-art-map-fixed-noreplace.patch
# 2. 设备目录、硬件组件、预装 APK
python3 $READOM/tools/fetch-prebuilts.py
rsync -ac --delete $READOM/device/amazon/pw3/ $AOSP/device/amazon/pw3/
python3 $READOM/device/amazon/pw4/porting/aosp/stub-pw3-hardware.py $AOSP/vendor/amazon/pw3-hardware
# 3. 镜像和编译
docker build -t pw4_aosp81:18.04 $READOM/device/amazon/pw4/porting/aosp
docker run --rm -v $AOSP:$AOSP -v $READOM:$READOM -v $GO:$GO -v $HOME/.aosp-home:/jack-home \
  -e HOME=/jack-home -e AOSP=$AOSP -e GOROOT_1108=$GO pw4_aosp81:18.04 \
  python3 $READOM/device/amazon/pw4/porting/aosp/run-nothp.py \
  bash $READOM/device/amazon/pw4/porting/aosp/build-aosp-pw3.sh
```

## 下一步

建 `device/amazon/pw4`：从 pw3 复制，按 PW4 改 BoardConfig（i.MX6SLL / Cortex-A9）、分辨率与 EPDC、触摸（gt9xx）、Wi-Fi（bcmdhd）、电源（BD71827）、init/ueventd、sepolicy，然后 `lunch pw4-userdebug`。
