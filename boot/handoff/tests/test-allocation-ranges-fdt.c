/* SPDX-License-Identifier: GPL-2.0-only */
/* allocation-ranges.h on the device-tree layout (PW4): the MiB below the
 * initramfs holds the boot blob, so nothing may be allocated there; the rest
 * is the PW3 rule, checked by test-allocation-ranges.c. */
#define PWS_FDT_BOOT 1
#include <stdio.h>
#include "allocation-ranges.h"
static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) {printf("PW4_ALLOCATION_RANGES_FAIL line=%u\n",__LINE__);return 1;} } while(0)
int main(void)
{
    pw_u32 base=0x80000000u,end=base+0x1000000u,kernel=4u*1024u*1024u,initrd=8u*1024u*1024u;
    pw_u32 blob=base+PWS_FDT_OFFSET,rd=base+0x4000000u;
#define ALLOWED(address,bytes) pws_allocation_allowed(base,end,kernel,initrd,address,bytes)
    CHECK(PWS_FDT_OFFSET==63u*1024u*1024u);
    CHECK(ALLOWED(blob-4096,4096));
    CHECK(!ALLOWED(blob-4096,8192));
    CHECK(!ALLOWED(blob,4096));
    CHECK(!ALLOWED(blob+0xff000u,4096));
    CHECK(!ALLOWED(rd,4096));
    CHECK(ALLOWED(end,4096));
    CHECK(!ALLOWED(end-4096,4096));
    CHECK(ALLOWED(rd+initrd,4096));
    CHECK(pws_excluded_page_limit(base,end,kernel,initrd)==
          (end-base)/4096u+kernel/4096u+initrd/4096u+256u);
    printf("PW4_ALLOCATION_RANGES_PASS checks=%u\n",checks);
    return 0;
}
