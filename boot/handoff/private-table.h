/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef PW3_PRIVATE_TABLE_H
#define PW3_PRIVATE_TABLE_H
/* The private translation table the handoff switches to after the caches are
 * off has three parts: the sections below PRIVATE_COPY_FROM are cleared, the
 * ones from there up are copied word for word from the live table (the
 * module's own code and the kernel are there and keep running across the
 * TTBR0 write), and the control page's section is written as a one-to-one
 * Normal non-cacheable section.
 *
 * PW3 (3.0, 3G/1G): PRIVATE_COPY_FROM is the module area, 0xbf000000, which
 * is also the top of the user half and of the window the module builds; the
 * control section is below it.
 * PW4 (4.1, 2G/2G): the module area starts at 0x7f000000 and lowmem is one to
 * one already, so the copy starts there and the control section, which is in
 * lowmem, is rewritten inside the copied part (design/porting-to-other-
 * models.md section 6). */
#include <linux/types.h>
#include <asm/memory.h>
#if PWS_PROFILE >= 3
#define PWS_FDT_BOOT 1
#define PRIVATE_COPY_FROM ((u32)MODULES_VADDR >> 20)
#else
#define PWS_FDT_BOOT 0
#define PRIVATE_COPY_FROM 0xbf0u
#endif
/* TTBR0 walk attributes the live table may carry: none, the UP ones
 * (IRGN_WB|RGN_OC_WB) and, on an SMP_ON_UP kernel, the SMP ones
 * (IRGN_WBWA|S|NOS|RGN_OC_WBWA). */
static inline int pws_ttbr_flags_ok(u32 ttbr)
{
    u32 f = ttbr & 0x3fffu;
#if PWS_FDT_BOOT
    if (f == 0x6au) return 1;
#endif
    return f == 0 || f == 0x59u;
}
#endif
