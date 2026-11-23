/* SPDX-License-Identifier: GPL-2.0-only */
/* dd_fdt() (PW4) on a made-up device: a temporary directory with a
 * /proc/cmdline and a /sys/firmware/fdt built here, with invented values (no
 * device's data). Checks the blob it makes and that each inconsistency stops
 * the start. Built with -DPW_DEVICE_TREE. */
#define _GNU_SOURCE
#include "../devicedata.h"
#include "../../handoff/pw4_fdt.h"
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

/* A small blob shaped like the PW4 one: root cells, /memory, /chosen with the
 * stock boot's initrd, /reserved-memory, and two /memreserve/ entries for the
 * stock boot's own blob and initrd. */
static unsigned char fdt[4096];
static size_t at;
static void be(uint32_t v) { fdt[at++] = v >> 24; fdt[at++] = v >> 16; fdt[at++] = v >> 8; fdt[at++] = v; }
static void pad(void) { while (at & 3) fdt[at++] = 0; }
static const char strings[] = "#address-cells\0#size-cells\0device_type\0reg\0bootargs\0linux,initrd-start\0linux,initrd-end\0no-map";
enum { S_AC = 0, S_SC = 15, S_DT = 27, S_REG = 39, S_BOOT = 43, S_IS = 52, S_IE = 71, S_NOMAP = 88 };
static void node(const char *name) { be(1); size_t n = strlen(name) + 1; memcpy(fdt + at, name, n); at += n; pad(); }
static void prop(uint32_t name, const void *v, uint32_t len) { be(3); be(len); be(name); memcpy(fdt + at, v, len); at += len; pad(); }
static void prop32(uint32_t name, uint32_t v) { unsigned char b[4] = {v >> 24, v >> 16, v >> 8, v}; prop(name, b, 4); }
static void prop2(uint32_t name, uint32_t a, uint32_t b_) {
    unsigned char b[8] = {a >> 24, a >> 16, a >> 8, a, b_ >> 24, b_ >> 16, b_ >> 8, b_}; prop(name, b, 8);
}
static size_t make_fdt(void) {
    memset(fdt, 0, sizeof(fdt));
    at = 40;
    be(0); be(0x83f077b0u); be(0); be(0x8850u);       /* the stock boot's blob */
    be(0); be(0x9e6a5000u); be(0); be(0x646e00u);     /* and its initrd */
    be(0); be(0); be(0); be(0);
    size_t s = at;
    node("");
    prop32(S_AC, 1); prop32(S_SC, 1);
    node("chosen");
    prop(S_BOOT, "stock line", 11);
    prop32(S_IS, 0x9e6a5000u); prop32(S_IE, 0x9e6a5000u + 0x646e00u);
    be(2);
    node("memory"); prop(S_DT, "memory", 7); prop2(S_REG, 0x80000000u, 0x20000000u); be(2);
    node("reserved-memory"); prop32(S_AC, 1); prop32(S_SC, 1);
    node("epdc"); prop2(S_REG, 0x9b000000u, 0x300000u); prop(S_NOMAP, "", 0); be(2);
    be(2);
    be(2); be(9);
    size_t strings_at = at;
    memcpy(fdt + at, strings, sizeof(strings)); at += sizeof(strings); pad();
    uint32_t total = (uint32_t)at;
    at = 0;
    be(PW_FDT_MAGIC); be(total); be((uint32_t)s); be((uint32_t)strings_at); be(40); be(17); be(16); be(0);
    be(sizeof(strings)); be((uint32_t)(strings_at - s));
    return total;
}

#define STOCK "console=ttymxc0,115200 consoleblank=0 uart_at_4m quiet secure_cpu=1 androidboot.secure_cpu=1 androidboot.prod=1 androidboot.unlocked_kernel=false"
#define CMDLINE STOCK " panic=5 rdinit=/init"

int main(void) {
    struct dd_buf out = {0};
    struct dd_why why;
    struct pw_fdt t;
    char tmpl[] = "/tmp/pw4-devicedata-XXXXXX";
    assert(mkdtemp(tmpl));
    snprintf(root, sizeof(root), "%s", tmpl);
    size_t n = make_fdt();
    struct pw_fdt src;
    CHECK(pw_fdt_parse(fdt, (pw_u32)n, NULL, &src) == 0 && src.rsv_count == 3);
    put("/sys/firmware/fdt", fdt, n);
    put("/proc/cmdline", "console=ttymxc0,115200 consoleblank=0 uart_at_4m root=/dev/mmcblk1p8 rootwait quiet secure_cpu=1 androidboot.secure_cpu=1 androidboot.prod=1 androidboot.unlocked_kernel=false\n",
        strlen("console=ttymxc0,115200 consoleblank=0 uart_at_4m root=/dev/mmcblk1p8 rootwait quiet secure_cpu=1 androidboot.secure_cpu=1 androidboot.prod=1 androidboot.unlocked_kernel=false\n"));

    memset(&why, 0, sizeof(why));
    CHECK(dd_fdt(root, &dd_pw4_5_18_1_1_1, CMDLINE, 0x84000000u, 0x123000u, &out, &why) == 0);
    CHECK(pw_fdt_parse(out.data, (pw_u32)out.size, CMDLINE, &t) == 0);
    CHECK(t.has_initrd && t.initrd.start == 0x84000000u && t.initrd.size == 0x123000u);
    /* Both stock payload reservations gone, the EPDC one kept. */
    CHECK(t.rsv_count == 1 && t.rsv[0].start == 0x9b000000u);
    CHECK(t.mem_count == 1 && t.mem[0].start == 0x80000000u && t.mem[0].size == 0x20000000u);

    memset(&why, 0, sizeof(why));
    CHECK(dd_fdt(root, &dd_pw4_5_18_1_1_1, "panic=5", 0x84000000u, 0x1000u, &out, &why) == -1 && !why.other_device);
    memset(&why, 0, sizeof(why));
    CHECK(dd_fdt(root, &dd_pw4_5_18_1_1_1, CMDLINE, 0x84000800u, 0x1000u, &out, &why) == -1);
    memset(&why, 0, sizeof(why));
    CHECK(dd_fdt(root, &dd_pw4_5_18_1_1_1, CMDLINE, 0x84000000u, 0, &out, &why) == -1);

    /* Another model's (or firmware's) command line: the other-device screen. */
    put("/proc/cmdline", "console=ttymxc0,115200 root=/dev/mmcblk0p1 rootwait\n", 52);
    memset(&why, 0, sizeof(why));
    CHECK(dd_fdt(root, &dd_pw4_5_18_1_1_1, CMDLINE, 0x84000000u, 0x1000u, &out, &why) == -1 && why.other_device);

    /* A damaged blob stops the start, it is not repaired. */
    put("/proc/cmdline", "console=ttymxc0,115200 consoleblank=0 uart_at_4m root=/dev/mmcblk1p8 rootwait quiet secure_cpu=1 androidboot.secure_cpu=1 androidboot.prod=1 androidboot.unlocked_kernel=false\n",
        strlen("console=ttymxc0,115200 consoleblank=0 uart_at_4m root=/dev/mmcblk1p8 rootwait quiet secure_cpu=1 androidboot.secure_cpu=1 androidboot.prod=1 androidboot.unlocked_kernel=false\n"));
    fdt[0] ^= 1;
    put("/sys/firmware/fdt", fdt, n);
    memset(&why, 0, sizeof(why));
    CHECK(dd_fdt(root, &dd_pw4_5_18_1_1_1, CMDLINE, 0x84000000u, 0x1000u, &out, &why) == -1);
    put("/sys/firmware/fdt", fdt, 0);
    CHECK(dd_fdt(root, &dd_pw4_5_18_1_1_1, CMDLINE, 0x84000000u, 0x1000u, &out, &why) == -1);

    dd_free(&out);
    char cmd[300];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", root);
    if (system(cmd)) {}
    if (failures) return 1;
    printf("PW4_DEVICEDATA_FDT_PASS\n");
    return 0;
}
