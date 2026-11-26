/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * What only the device itself has, read from the running stock system at the
 * moment the Android entry starts, and put into the form the new kernel and the
 * storage entry take:
 *
 *   ATAGs     board revision and ID, MAC address, boot mode, RAM banks (the
 *             stock kernel's /proc copies of what its boot loader passed), with
 *             the build's command line and initramfs range;
 *   archive   a cpio (newc) appended to the public initramfs: the panel's
 *             waveform (its own WBF from the panel flash with the WRF the stock
 *             system made from it), the Wi-Fi calibration and firmware from the
 *             stock root file system, and the storage entry's configuration
 *             with this device's eMMC geometry and FAT offset.
 *
 * Nothing is stored: every start reads the device again, so nothing private
 * leaves it or goes stale. Paths are read below `root` ("/" on the device; a
 * directory of captured files in tests). Every function returns 0 or -1 and
 * then names the failed check in `why`.
 */
#ifndef PW3_DEVICEDATA_H
#define PW3_DEVICEDATA_H
#include <stddef.h>
#include <stdint.h>

struct dd_buf { unsigned char *data; size_t size, cap; };
void dd_free(struct dd_buf *b);

/* What one supported model on one stock firmware fixes. */
struct dd_profile {
    const char *stock_cmdline;   /* /proc/cmdline without root= and rootwait */
    const char *user_mount;      /* the stock mount of the user FAT */
    const char *wifi_board;      /* stock path of the Wi-Fi calibration */
    const char *wifi_firmware;   /* stock path of the Wi-Fi firmware */
    /* The stock kernel itself, as /proc/version and uname report it. The
     * handover calls that kernel's own pm_test_show and L2 cache functions and
     * checks their bodies byte for byte (boot/handoff/native-handoff.h), so
     * another build of the same kernel is not a thing this one can be used on.
     * Both strings are in the profile JSON too, and a test keeps them equal
     * (tools/tests/test_profile_matches_devicedata.py). */
    const char *kernel_release;  /* uname -r, and what /proc/version names */
    const char *kernel_version;  /* uname -v: the build number, date and time */
};
extern const struct dd_profile dd_pw3_5_16_2_1_1;
extern const struct dd_profile dd_pw4_5_18_1_1_1;

/* What the build fixes. */
struct dd_build {
    const char *cmdline;         /* complete kernel command line; starts with the profile's stock part */
    const char *preinit_conf;    /* pw3-preinit.conf without the fat_* and block= lines */
    const char *data_path;       /* data image, relative to the user FAT; NULL: no pw3-testdata.conf */
    uint32_t data_size;          /* its size in bytes */
};

/* `other_device` says the failure was the identity check: this build is for one
 * model on one firmware, and the running system is not it. The caller shows a
 * different screen for that, because telling someone with another Kindle to
 * restart and try again is telling them to do it for ever. */
struct dd_why { char text[160]; int other_device; };

/* Whether the running system is the model and firmware this build is for.
 * Cheap and side-effect free, for asking before anything is installed. */
int dd_device_matches(const char *root, const struct dd_profile *profile, struct dd_why *why);
int dd_atags(const char *root, const struct dd_profile *profile, const char *cmdline,
             uint32_t initrd_start, uint32_t initrd_size, struct dd_buf *out, struct dd_why *why);
/* PW4: the stock device tree with the build's /chosen (see devicedata.c).
 * Built with -DPW_DEVICE_TREE and boot/handoff/pw4_fdt.c + pw3_plan.c. */
int dd_fdt(const char *root, const struct dd_profile *profile, const char *cmdline,
           uint32_t initrd_start, uint32_t initrd_size, struct dd_buf *out, struct dd_why *why);

#ifdef PW_DEVICE_TREE
/* What a device-tree Kindle (PW4) fixes beyond struct dd_profile. Kept apart,
 * and only in -DPW_DEVICE_TREE builds, so that a PW3 build of this file is the
 * same object it was before the PW4 was added: nothing a PW3 runs changed.
 * Also needs boot/handoff/sha256.c. */
struct dd_pin { const char *path; size_t bytes; const char *sha256; };
struct dd_tree_profile {
    const struct dd_profile *base;   /* command line, user mount, Wi-Fi paths, kernel */
    const char *compatible;          /* the first entry of the root node's compatible */
    const char *disk;                /* the eMMC, e.g. mmcblk1 */
    const char *user_partition;      /* the partition the user FAT is in, e.g. mmcblk1p10 */
    /* Stock files the archive carries, read now and never stored: they are
     * Amazon's and Broadcom's, so they reach the Android kernel only from the
     * device they are on. Pinned by size and SHA256, read from a device: they
     * are part of the firmware this build is for, as the kernel is. */
    struct dd_pin epdc_firmware, wifi_firmware, bt_firmware;
    size_t nvram_max;                /* the Wi-Fi NVRAM (base->wifi_board) is not pinned */
};
extern const struct dd_tree_profile dd_pw4_rex_5_18_1_1_1;
/* dd_device_matches and dd_archive for such a Kindle. The archive carries the
 * EPDC firmware, the Wi-Fi firmware and NVRAM, the Bluetooth patch file and
 * the storage entry's configuration; no waveform: the Android kernel's PID 1
 * reads the panel's own from its partition (boot/handoff-init/pw4-handoff-init.c). */
int dd_tree_device_matches(const char *root, const struct dd_tree_profile *profile, struct dd_why *why);
int dd_tree_archive(const char *root, const struct dd_tree_profile *profile, const struct dd_build *build,
                    struct dd_buf *out, struct dd_why *why);
#endif
int dd_archive(const char *root, const struct dd_profile *profile, const struct dd_build *build,
               struct dd_buf *out, struct dd_why *why);

/* Parts, for tests. */
uint32_t dd_crc32(uint32_t crc, const unsigned char *data, size_t size);
int dd_envelope(const unsigned char *panel, size_t panel_size, const unsigned char *wrf, size_t wrf_size,
                struct dd_buf *out, struct dd_why *why);
int dd_gzip_stored(const unsigned char *data, size_t size, struct dd_buf *out);
int dd_newc(struct dd_buf *out, const char *name, uint32_t mode, const unsigned char *data, size_t size);
int dd_newc_end(struct dd_buf *out);
#endif
