# SPDX-License-Identifier: Apache-2.0
# DRAFT, step 1 of porting/android.md: the PW3 board as it is. The CPU is the
# same family (Cortex-A9, i.MX6SL vs SLL) and so is the panel (1072x1448).
include device/amazon/pw3/BoardConfig.mk

# PW4 policy additions (sepolicy/pw4_hardware.te).
BOARD_SEPOLICY_DIRS += device/amazon/pw4/sepolicy
