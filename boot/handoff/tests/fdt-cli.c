/* SPDX-License-Identifier: GPL-2.0-only */
/* Host driver for tests/test_fdt.py: runs pw4_fdt.c on a blob from disk.
 *   fdt-cli parse BLOB [CMDLINE]
 *   fdt-cli plan BLOB CMDLINE FDT_DEST INITRD_DEST INITRD_BYTES FOOTPRINT_END
 * Prints the result code and, for parse, what was found. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../pw4_fdt.h"

static unsigned char blob[PW_FDT_CAP + 8];

int main(int argc, char **argv)
{
    FILE *f;
    size_t n;
    unsigned int i;
    if (argc < 3) return 2;
    f = fopen(argv[2], "rb");
    if (!f) return 2;
    n = fread(blob, 1, sizeof(blob), f);
    fclose(f);
    n = (n + 3) & ~(size_t)3;   /* a slot is padded to whole words with zeros */
    if (!strcmp(argv[1], "parse")) {
        struct pw_fdt t;
        int rc = pw_fdt_parse(blob, (pw_u32)n, argc > 3 ? argv[3] : NULL, &t);
        printf("rc=%d", rc);
        if (!rc) {
            printf(" total=%u mem=%u", t.totalsize, t.mem_count);
            for (i = 0; i < t.mem_count; ++i) printf(" %#x+%#x", t.mem[i].start, t.mem[i].size);
            printf(" rsv=%u", t.rsv_count);
            for (i = 0; i < t.rsv_count; ++i) printf(" %#x+%#x", t.rsv[i].start, t.rsv[i].size);
            printf(" bootargs=%u initrd=%u", t.has_bootargs, t.has_initrd);
            if (t.has_initrd) printf(" %#x+%#x", t.initrd.start, t.initrd.size);
        }
        printf("\n");
        return 0;
    }
    if (!strcmp(argv[1], "plan") && argc == 8) {
        static unsigned char zimage[0x1000];
        struct pw_plan p;
        unsigned int base = 0x80000000u;
        memset(&p, 0, sizeof(p));
        memset(zimage, 0, sizeof(zimage));
        zimage[0x24] = 0x18; zimage[0x25] = 0x28; zimage[0x26] = 0x6f; zimage[0x27] = 0x01;
        zimage[0x2c] = 0x00; zimage[0x2d] = 0x10;   /* end 0x1000 > start 0 */
        p.entry = base + 0x8000; p.machine_id = p.expected_machine_id = 0xffffffffu;
        p.boot_ram_base = base; p.identity_limit = 0xa0000000u;
        p.ram_count = 1; p.ram[0].start = base; p.ram[0].size = 0x20000000u;
        p.forbidden_count = 1; p.forbidden[0].start = 0x88000000u; p.forbidden[0].size = 0x18000000u;
        p.footprint_count = 1; p.footprint[0].start = base;
        p.footprint[0].size = (unsigned int)strtoul(argv[7], 0, 0) - base;
        p.segment_count = 3;
        p.segments[0].kind = PW_ZIMAGE; p.segments[0].destination.start = base + 0x8000;
        p.segments[0].destination.size = 0x1000; p.segments[0].buffer_bytes = sizeof(zimage);
        p.segments[1].kind = PW_ATAGS; p.segments[1].destination.start = (unsigned int)strtoul(argv[4], 0, 0);
        p.segments[1].destination.size = ((unsigned int)n + 4095) & ~4095u; p.segments[1].buffer_bytes = (pw_u32)n;
        p.segments[2].kind = PW_INITRAMFS; p.segments[2].destination.start = (unsigned int)strtoul(argv[5], 0, 0);
        p.segments[2].buffer_bytes = (unsigned int)strtoul(argv[6], 0, 0);
        p.segments[2].destination.size = (p.segments[2].buffer_bytes + 4095) & ~4095u;
        printf("rc=%d\n", pw_plan_validate_fdt(&p, blob, (pw_u32)n, argv[3], zimage, sizeof(zimage)));
        return 0;
    }
    return 2;
}
