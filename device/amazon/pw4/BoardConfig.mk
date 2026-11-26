# SPDX-License-Identifier: Apache-2.0
# DRAFT, step 1 of porting/android.md: the PW3 board as it is. The CPU is the
# same family (Cortex-A9, i.MX6SL vs SLL) and so is the panel (1072x1448).
include device/amazon/pw3/BoardConfig.mk

# /data is 2 GiB on the PW4, not the PW3's 1.75 GiB (the owner's decision,
# 2026-11-26): the PW4 has an 8 GB eMMC whose user volume is about 6.3 GB
# (porting/README.md, storage), so the PW3's arithmetic -- a 3.27 GB volume on
# which 256 MiB more would keep readers with books from installing at all --
# does not apply, and 2 GiB is what the development images ran with. A clean
# install then needs 512 MiB + 2 GiB + 64 MiB = 2.5625 GiB (2.75 GB) free.
# It is the PW4's own data format (boot/profiles/pw4-5.18.1.1.1.json,
# entry.data_format), so an installer never takes one model's data image for
# the other's; the PW3's size and format are unchanged.
BOARD_USERDATAIMAGE_PARTITION_SIZE := 2147483648

# PW4 policy additions (sepolicy/pw4_hardware.te).
BOARD_SEPOLICY_DIRS += device/amazon/pw4/sepolicy

# Wi-Fi: the Broadcom BCM43430 (bcmdhd) in place of the PW3's ath6kl; wificond
# waits for this module and this driver (patches/aosp-wificond/0008).
BOARD_PW3_WIFI_MODULE := bcmdhd
BOARD_PW3_WIFI_DRIVER := bcmsdh_sdmmc

# The build takes system.prop from the device's own directory, and the PW4 has
# none: without the PW3's, ART ran from a relocated copy of the boot image in
# /data/dalvik-cache and dex2oat kept its watchdog, which stops a full compile
# of a large application after 570 s (WeRead: "dex2oat did not finish").
TARGET_SYSTEM_PROP := device/amazon/pw3/system.prop

# Bluetooth (bluetooth/bluetooth.mk). BOARD_HAVE_BLUETOOTH is only a listed
# board variable in 8.1 (build/make/core/product.mk:287); libbt-vendor is
# built only with BOARD_HAVE_BLUETOOTH_BCM (hardware/broadcom/libbt/
# Android.mk:3), and USE_BLUETOOTH_BCM4343 (same file :17-19) lets a ROM name
# with only "4343" in it be read as a chip name (src/hardware.c:681-688).
# BOARD_CUSTOM_BT_CONFIG replaces vnd_<device>.txt (libbt vnd_buildcfg.mk:4-12).
# BOARD_BLUETOOTH_BDROID_BUILDCFG_INCLUDE_DIR goes to Soong as
# BtConfigIncludeDir (build/make/core/soong_config.mk:90 ->
# system/bt/build/fluoride.go:50-58) and to libbt (Android.mk:7-13).
BOARD_HAVE_BLUETOOTH := true
BOARD_HAVE_BLUETOOTH_BCM := true
USE_BLUETOOTH_BCM4343 := true
BOARD_CUSTOM_BT_CONFIG := device/amazon/pw4/bluetooth/vnd_pw4.txt
BOARD_BLUETOOTH_BDROID_BUILDCFG_INCLUDE_DIR := device/amazon/pw4/bluetooth/include
DEVICE_MANIFEST_FILE += device/amazon/pw4/bluetooth/manifest-bluetooth.xml
