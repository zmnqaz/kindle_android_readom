/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef PW4_FDT_H
#define PW4_FDT_H
/* Device-tree counterpart of the ATAG rules in pw3_plan.h, for Kindles whose
 * stock kernel boots from a flattened device tree (PW4: 4.1.15, i.MX6SLL).
 * Pure data validation, like pw3_plan.c: no syscalls, no physical mapping. */
#include "pw3_plan.h"

#define PW_FDT_CAP (128u * 1024u)
#define PW_FDT_MAGIC 0xd00dfeedu
#define PW_FDT_MAX_DEPTH 32u

struct pw_fdt {
    pw_u32 totalsize, mem_count, rsv_count, has_initrd, has_bootargs;
    struct pw_range mem[PW_MAX_RANGES];
    struct pw_range rsv[PW_MAX_RANGES];
    struct pw_range initrd;
};

/* Accepts one well-formed version 17 blob of at most `bytes` (the rest zero
 * padding), with #address-cells = #size-cells = 1 at the root, memory nodes
 * whose reg fits 32 bits, at most one /chosen, and in it exactly one bootargs
 * equal to `cmdline` (when it is not null) and either both or neither of
 * linux,initrd-start/-end. The blob's /memreserve/ entries and the reg of
 * /reserved-memory's children are reported in rsv. Anything else is refused,
 * not repaired. */
int pw_fdt_parse(const pw_u8 *data, pw_u32 bytes, const char *cmdline,
                 struct pw_fdt *out);

/* pw_plan_validate() for a device-tree boot: the kernel is entered with
 * r1 = ~0 and r2 = the blob's physical address (the 4.1 head.S contract), so
 * the blob may sit anywhere in RAM below identity_limit that the decompressor
 * and the other segments leave alone; the stock machine_kexec rule that pins
 * ATAGs at entry - 0x7000 does not apply. */
int pw_plan_validate_fdt(const struct pw_plan *plan, const pw_u8 *fdt,
                         pw_u32 fdt_bytes, const char *cmdline,
                         const pw_u8 *zimage, pw_u32 zimage_bytes);
#endif
