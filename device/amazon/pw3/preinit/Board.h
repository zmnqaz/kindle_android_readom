/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef PW3_PREINIT_BOARD_H
#define PW3_PREINIT_BOARD_H
/* The facts about the eMMC that PID 1 checks, per model. PW3 is the default
 * and compiles to what it always did; the PW4 port (device/amazon/pw4)
 * builds with -DPREINIT_BOARD_PW4 (Android.mk, by TARGET_DEVICE).
 *
 * Both keep the user FAT 8192 bytes into one partition of the eMMC, which is
 * the one partition left writable (with the whole disk); every other one,
 * the boot areas included, is set read only before anything is mapped. */
#ifdef PREINIT_BOARD_PW4
/* i.MX6SLL, 4.1.15: the eMMC is the second host (mmc0 is the Wi-Fi SDIO),
 * ten partitions, the user store in the last, and the 4.1 kernel lists the
 * RPMB area as a block device too. */
#define BOARD_DISK "mmcblk1"
#define BOARD_USER_PART "mmcblk1p10"
#define BOARD_RELEASE_TAG "-pw4-android81"
#define BOARD_BLOCKS { "mmcblk1", "mmcblk1p1", "mmcblk1p2", "mmcblk1p3", "mmcblk1p4", "mmcblk1p5", \
                       "mmcblk1p6", "mmcblk1p7", "mmcblk1p8", "mmcblk1p9", "mmcblk1p10", \
                       "mmcblk1boot0", "mmcblk1boot1", "mmcblk1rpmb" }
#define BOARD_BLOCK_COUNT 14U
#else
#define BOARD_DISK "mmcblk0"
#define BOARD_USER_PART "mmcblk0p4"
#define BOARD_RELEASE_TAG "-pw3-android81"
#define BOARD_BLOCKS { "mmcblk0", "mmcblk0p1", "mmcblk0p2", "mmcblk0p3", \
                       "mmcblk0p4", "mmcblk0boot0", "mmcblk0boot1" }
#define BOARD_BLOCK_COUNT 7U
#endif
#define BOARD_DISK_LEN (sizeof(BOARD_DISK) - 1)
#endif
