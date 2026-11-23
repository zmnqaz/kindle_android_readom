# SPDX-License-Identifier: Apache-2.0
# DRAFT, step 1 of porting/android.md: the PW3 board as it is. The CPU is the
# same family (Cortex-A9, i.MX6SL vs SLL) and so is the panel (1072x1448).
include device/amazon/pw3/BoardConfig.mk

# PW4 policy additions (sepolicy/pw4_hardware.te).
BOARD_SEPOLICY_DIRS += device/amazon/pw4/sepolicy

# Wi-Fi: the Broadcom BCM43430 (bcmdhd) in place of the PW3's ath6kl; wificond
# waits for this module and this driver (patches/aosp-wificond/0008).
BOARD_PW3_WIFI_MODULE := bcmdhd
BOARD_PW3_WIFI_DRIVER := bcmsdh_sdmmc
