/* SPDX-License-Identifier: GPL-2.0-only */
/* dd_tree_device_matches() and dd_tree_archive() (PW4) on a made-up device: a
 * temporary directory laid out like the PW4's stock system, with invented
 * values and invented file contents (no device's data, and none of Amazon's or
 * Broadcom's files: the pinned files here are pinned by a test profile to
 * what the test wrote). Checks what the archive holds, where, and with which
 * modes, and that each inconsistency stops the start -- with the other-device
 * screen only where the firmware or the model is not the build's. Built with
 * -DPW_DEVICE_TREE and boot/handoff/sha256.c. */
#define _GNU_SOURCE
#include "../devicedata.h"
#include "../../handoff/sha256.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char root[256];
static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static void put(const char *path, const void *data, size_t size) {
    char full[512], dir[512];
    snprintf(full, sizeof(full), "%s%s", root, path);
    strcpy(dir, full);
    for (char *p = dir + strlen(root) + 1; (p = strchr(p, '/')); ++p) { *p = 0; mkdir(dir, 0700); *p = '/'; }
    FILE *f = fopen(full, "wb");
    assert(f && fwrite(data, 1, size, f) == size && fclose(f) == 0);
}
static void puts_(const char *path, const char *text) { put(path, text, strlen(text)); }
static void hex_sha(const unsigned char *data, size_t size, char hex[65]) {
    SHA256_CTX ctx; unsigned char d[32];
    sha256_init(&ctx); sha256_update(&ctx, data, size); sha256_final(&ctx, d);
    for (int i = 0; i < 32; ++i) snprintf(hex + 2 * i, 3, "%02x", d[i]);
}

#define STOCK "console=ttymxc0,115200 consoleblank=0 uart_at_4m quiet secure_cpu=1 androidboot.secure_cpu=1 androidboot.prod=1 androidboot.unlocked_kernel=false"
#define EPDC "/lib/firmware/imx/epdc/epdc_TEST.fw"
#define WIFI "/opt/brcm_4343w/wifi/firmware/fw_bcmdhd_sr.bin"
#define BT "/opt/brcm_4343w/bluetooth/firmware/BCM4343A1_TEST.hcd"

static unsigned char epdc[5000], wifi[3000], bt[700];
static char epdc_sha[65], wifi_sha[65], bt_sha[65];
static struct dd_tree_profile test;

static void make_device(void) {
    puts_("/proc/cmdline", "console=ttymxc0,115200 consoleblank=0 uart_at_4m root=/dev/mmcblk1p8 rootwait quiet secure_cpu=1 "
                           "androidboot.secure_cpu=1 androidboot.prod=1 androidboot.unlocked_kernel=false\n");
    puts_("/proc/version", "Linux version 4.1.15-lab126 (build@lab126) (gcc version 4.9.1) #1 SMP PREEMPT Thu May 21 05:14:34 UTC 2026\n");
    put("/sys/firmware/devicetree/base/compatible", "amzn,imx6sll-rex\0fsl,imx6sll\0", 29);
    for (size_t i = 0; i < sizeof(epdc); ++i) epdc[i] = (unsigned char)(i * 13);
    for (size_t i = 0; i < sizeof(wifi); ++i) wifi[i] = (unsigned char)(i * 7 + 1);
    for (size_t i = 0; i < sizeof(bt); ++i) bt[i] = (unsigned char)(i * 3 + 2);
    put(EPDC, epdc, sizeof(epdc)); put(WIFI, wifi, sizeof(wifi)); put(BT, bt, sizeof(bt));
    puts_("/opt/brcm_4343w/wifi/nvram/nvram_wfo.txt", "# invented\nboardtype=0x0000\nmacaddr=00:00:00:00:00:00\n");
    puts_("/proc/self/mounts", "rootfs / rootfs ro 0 0\n/dev/loop/0 /mnt/base-us vfat rw,noexec 0 0\nfsp /mnt/us fuse.fsp rw 0 0\n");
    puts_("/sys/block/loop0/loop/backing_file", "/dev/mmcblk1p10\n");
    puts_("/sys/block/loop0/loop/offset", "8192\n");
    puts_("/sys/block/loop0/loop/sizelimit", "0\n");
    puts_("/sys/class/block/mmcblk1/size", "1000000\n");
    puts_("/sys/class/block/mmcblk1boot0/size", "8192\n");
    puts_("/sys/class/block/mmcblk1rpmb/size", "8192\n");
    puts_("/sys/class/block/mmcblk1p1/size", "32768\n");
    puts_("/sys/class/block/mmcblk1p1/start", "2048\n");
    puts_("/sys/class/block/mmcblk1p10/size", "600000\n");
    puts_("/sys/class/block/mmcblk1p10/start", "400000\n");
    puts_("/sys/class/block/mmcblk0/size", "4\n");      /* not this model's eMMC: left out */
    puts_("/sys/class/block/loop0/size", "1\n");
    unsigned char super[2048] = {0};
    super[1024 + 0x38] = 0x53; super[1024 + 0x39] = 0xef;
    for (int i = 0; i < 16; ++i) super[1024 + 0x68 + i] = (unsigned char)(0xb0 + i);
    put("/mnt/base-us/android/data.img", super, sizeof(super));
    char full[512];
    snprintf(full, sizeof(full), "%s/mnt/base-us/android/data.img", root);
    assert(truncate(full, 1 << 20) == 0);
    hex_sha(epdc, sizeof(epdc), epdc_sha); hex_sha(wifi, sizeof(wifi), wifi_sha); hex_sha(bt, sizeof(bt), bt_sha);
    test = dd_pw4_rex_5_18_1_1_1;
    test.epdc_firmware = (struct dd_pin){EPDC, sizeof(epdc), epdc_sha};
    test.wifi_firmware = (struct dd_pin){WIFI, sizeof(wifi), wifi_sha};
    test.bt_firmware = (struct dd_pin){BT, sizeof(bt), bt_sha};
}

struct entry { char name[128]; uint32_t mode, size; const unsigned char *data; };
static unsigned parse(const struct dd_buf *b, struct entry *e, unsigned max) {
    size_t pos = 0; unsigned n = 0;
    for (;;) {
        if (pos + 110 > b->size || memcmp(b->data + pos, "070701", 6)) return 0;
        uint32_t f[13];
        for (int i = 0; i < 13; ++i) { char hex[9]; memcpy(hex, b->data + pos + 6 + i * 8, 8); hex[8] = 0; f[i] = (uint32_t)strtoul(hex, NULL, 16); }
        const char *name = (const char *)b->data + pos + 110;
        if (strlen(name) + 1 != f[11]) return 0;
        size_t data = (pos + 110 + f[11] + 3) & ~(size_t)3;
        if (!strcmp(name, "TRAILER!!!")) return data == b->size ? n : 0;
        if (n == max) return 0;
        snprintf(e[n].name, sizeof(e[n].name), "%s", name);
        e[n].mode = f[1]; e[n].size = f[6]; e[n].data = b->data + data;
        ++n;
        pos = (data + f[6] + 3) & ~(size_t)3;
    }
}
static const struct entry *find(const struct entry *e, unsigned n, const char *name) {
    for (unsigned i = 0; i < n; ++i) if (!strcmp(e[i].name, name)) return &e[i];
    return NULL;
}

/* Replaces one file for one check and puts it back. */
static char saved[8192]; static size_t saved_size;
static void swap(const char *path, const void *data, size_t size) {
    char full[512];
    snprintf(full, sizeof(full), "%s%s", root, path);
    FILE *f = fopen(full, "rb"); assert(f); saved_size = fread(saved, 1, sizeof(saved), f); fclose(f);
    put(path, data, size);
}
static void restore(const char *path) { put(path, saved, saved_size); }

static void test_profile(void) {
    /* The release profile: the PW4's base profile, its board, its eMMC. */
    CHECK(dd_pw4_rex_5_18_1_1_1.base == &dd_pw4_5_18_1_1_1);
    CHECK(!strcmp(dd_pw4_rex_5_18_1_1_1.compatible, "amzn,imx6sll-rex"));
    CHECK(!strcmp(dd_pw4_rex_5_18_1_1_1.disk, "mmcblk1") && !strcmp(dd_pw4_rex_5_18_1_1_1.user_partition, "mmcblk1p10"));
    const struct dd_pin *pins[] = {&dd_pw4_rex_5_18_1_1_1.epdc_firmware, &dd_pw4_rex_5_18_1_1_1.wifi_firmware,
                                   &dd_pw4_rex_5_18_1_1_1.bt_firmware};
    for (unsigned i = 0; i < 3; ++i) {
        CHECK(pins[i]->path[0] == '/' && pins[i]->bytes > 0 && strlen(pins[i]->sha256) == 64);
        CHECK(strspn(pins[i]->sha256, "0123456789abcdef") == 64);
    }
    CHECK(!strcmp(dd_pw4_rex_5_18_1_1_1.wifi_firmware.path, dd_pw4_5_18_1_1_1.wifi_firmware));
}

static void test_matches(void) {
    struct dd_why why = {{0}, 0};
    CHECK(dd_tree_device_matches(root, &test, &why) == 0);
    /* The release profile on this invented device: the files are not the pinned ones. */
    memset(&why, 0, sizeof(why));
    CHECK(dd_tree_device_matches(root, &dd_pw4_rex_5_18_1_1_1, &why) == -1);

    /* Another board on the same SoC, and a root node whose compatible is
     * only the SoC: not this model. */
    swap("/sys/firmware/devicetree/base/compatible", "amzn,imx6sll-other\0fsl,imx6sll\0", 31);
    memset(&why, 0, sizeof(why));
    CHECK(dd_tree_device_matches(root, &test, &why) == -1 && why.other_device);
    restore("/sys/firmware/devicetree/base/compatible");
    swap("/sys/firmware/devicetree/base/compatible", "fsl,imx6sll\0", 12);
    memset(&why, 0, sizeof(why));
    CHECK(dd_tree_device_matches(root, &test, &why) == -1 && why.other_device);
    restore("/sys/firmware/devicetree/base/compatible");
    swap("/sys/firmware/devicetree/base/compatible", "amzn,imx6sll-rex", 16);   /* not terminated */
    memset(&why, 0, sizeof(why));
    CHECK(dd_tree_device_matches(root, &test, &why) == -1);
    restore("/sys/firmware/devicetree/base/compatible");

    /* Another kernel build. */
    static const char other_build[] = "Linux version 4.1.15-lab126 (build@lab126) #2 SMP PREEMPT Fri Jun 1 00:00:00 UTC 2027\n";
    swap("/proc/version", other_build, strlen(other_build));
    memset(&why, 0, sizeof(why));
    CHECK(dd_tree_device_matches(root, &test, &why) == -1 && why.other_device);
    restore("/proc/version");

    /* A pinned file that is another one: another firmware. One that is gone:
     * a stop, but not the other-device screen. */
    unsigned char other[sizeof(wifi)];
    memcpy(other, wifi, sizeof(other)); other[100] ^= 1;
    swap(WIFI, other, sizeof(other));
    memset(&why, 0, sizeof(why));
    CHECK(dd_tree_device_matches(root, &test, &why) == -1 && why.other_device);
    restore(WIFI);
    swap(BT, bt, sizeof(bt) - 1);
    memset(&why, 0, sizeof(why));
    CHECK(dd_tree_device_matches(root, &test, &why) == -1 && why.other_device);
    restore(BT);
    char full[512];
    snprintf(full, sizeof(full), "%s%s", root, EPDC);
    char moved[520];
    snprintf(moved, sizeof(moved), "%s.x", full);
    assert(rename(full, moved) == 0);
    memset(&why, 0, sizeof(why));
    CHECK(dd_tree_device_matches(root, &test, &why) == -1 && !why.other_device && why.text[0]);
    assert(rename(moved, full) == 0);
    CHECK(dd_tree_device_matches(root, &test, &why) == 0);
}

static void test_archive(void) {
    struct dd_buf a = {0};
    struct dd_why why = {{0}, 0};
    struct dd_build build = {STOCK " panic=5", "schema=1\nadb_key_sha256=none\n", "android/data.img", 1 << 20};
    CHECK(dd_tree_archive(root, &test, &build, &a, &why) == 0);
    struct entry e[32];
    unsigned n = parse(&a, e, 32);
    CHECK(n == 13);
    static const struct { const char *name; uint32_t mode; } dirs[] = {
        {"lib", 040755}, {"lib/firmware", 040755}, {"lib/firmware/imx", 040755}, {"lib/firmware/imx/epdc", 040755},
        {"firmware", 040755}, {"firmware/bcmdhd", 040750}, {"firmware/bluetooth", 040755}};
    for (unsigned i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i)
        CHECK(find(e, n, dirs[i].name) && find(e, n, dirs[i].name)->mode == dirs[i].mode);
    /* Under the names the Android side asks for, not the stock names. */
    const struct entry *f = find(e, n, "lib/firmware/imx/epdc/epdc_ED060KC4.fw");
    CHECK(f && f->mode == 0100644 && f->size == sizeof(epdc) && !memcmp(f->data, epdc, sizeof(epdc)));
    f = find(e, n, "firmware/bcmdhd/fw_bcmdhd_sr.bin");
    CHECK(f && f->mode == 0100640 && f->size == sizeof(wifi) && !memcmp(f->data, wifi, sizeof(wifi)));
    f = find(e, n, "firmware/bcmdhd/nvram_wfo.txt");
    CHECK(f && f->mode == 0100640 && f->size > 10 && !memcmp(f->data, "# invented", 10));
    f = find(e, n, "firmware/bluetooth/BCM4343A1.hcd");
    CHECK(f && f->mode == 0100644 && f->size == sizeof(bt) && !memcmp(f->data, bt, sizeof(bt)));
    /* The storage entry's lines are this model's eMMC, and only its. */
    f = find(e, n, "pw3-preinit.conf");
    static const char conf[] = "schema=1\nadb_key_sha256=none\nfat_offset=8192\nfat_sizelimit=0\n"
        "block=mmcblk1:1000000:-\nblock=mmcblk1boot0:8192:-\nblock=mmcblk1p1:32768:2048\n"
        "block=mmcblk1p10:600000:400000\nblock=mmcblk1rpmb:8192:-\n";
    CHECK(f && f->mode == 0100400 && f->size == strlen(conf) && !memcmp(f->data, conf, f->size));
    f = find(e, n, "pw3-testdata.conf");
    static const char data[] = "schema=2\ndata_path=android/data.img\ndata_size=1048576\ndata_uuid=b0b1b2b3b4b5b6b7b8b9babbbcbdbebf\n";
    CHECK(f && f->mode == 0100400 && f->size == strlen(data) && !memcmp(f->data, data, f->size));
    /* No PW3 names: no waveform, no AR6003 files. */
    for (unsigned i = 0; i < n; ++i) CHECK(!strstr(e[i].name, "image") && !strstr(e[i].name, "ath6k"));
    dd_free(&a);

    /* Each of these stops the start, and leaves no archive. */
    struct { const char *path, *text; size_t size; } bad[] = {
        {"/proc/self/mounts", "/dev/loop/1 /mnt/base-us vfat rw 0 0\n", 0},
        {"/sys/block/loop0/loop/backing_file", "/dev/mmcblk0p4\n", 0},     /* the PW3's partition */
        {"/sys/block/loop0/loop/backing_file", "/dev/mmcblk1p9\n", 0},
        {"/sys/block/loop0/loop/offset", "100\n", 0},
        {"/sys/class/block/mmcblk1p10/start", "1000000\n", 0},
        {"/opt/brcm_4343w/wifi/nvram/nvram_wfo.txt", "", 0},
        {"/opt/brcm_4343w/wifi/nvram/nvram_wfo.txt", "a\0b", 3},
        {WIFI, "short", 0},
    };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        swap(bad[i].path, bad[i].text, bad[i].size ? bad[i].size : strlen(bad[i].text));
        why.text[0] = 0;
        CHECK(dd_tree_archive(root, &test, &build, &a, &why) == -1 && why.text[0] && a.size == 0);
        restore(bad[i].path);
    }
    build.data_size = 4096;   /* not the size of the data image */
    CHECK(dd_tree_archive(root, &test, &build, &a, &why) == -1);
    build.data_path = NULL;   /* without one: no pw3-testdata.conf */
    CHECK(dd_tree_archive(root, &test, &build, &a, &why) == 0 && parse(&a, e, 32) == 12);
    dd_free(&a);
}

int main(void) {
    char tmpl[] = "/tmp/pw4-devicedata-tree-XXXXXX";
    assert(mkdtemp(tmpl));
    snprintf(root, sizeof(root), "%s", tmpl);
    make_device();
    test_profile();
    test_matches();
    test_archive();
    char cmd[300];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", root);
    if (system(cmd)) {}
    if (failures) return 1;
    printf("PW4_DEVICEDATA_TREE_PASS\n");
    return 0;
}
