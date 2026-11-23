/* SPDX-License-Identifier: GPL-2.0-only */
/* The PW4's USB state in registers, read only, for one line of log each.
 *
 * Why: on the first Android boot (Jump 13) plugging the USB cable froze the
 * whole machine without a single kernel message, which is what an access to a
 * block whose clock or power is off does. The stock system on the same kernel
 * code does not freeze, and the difference between the two is what the stock
 * suspend left behind and the new kernel never set again. These are the
 * registers to compare: the clock gate, PLL3 (USB1), the analog USB1 block,
 * USB PHY1, and -- only while its clock gate is on, since reading it otherwise
 * is exactly the freeze being looked for -- the OTG1 controller and USBNC.
 *
 * i.MX6SLL addresses (imx6sll.dtsi, arch/arm/mach-imx/clk-imx6sll.c). */
#pragma once
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

struct pw4_reg { const char *name; uint32_t addr; };

static const struct pw4_reg pw4_regs_always[] = {
	{ "CCGR6", 0x020C4080 },
	{ "CCM_CCSR", 0x020C400C },
	{ "PLL_USB1", 0x020C8010 },
	{ "PLL_USB2", 0x020C8020 },
	{ "REG_3P0", 0x020C8120 },
	{ "ANA_MISC0", 0x020C8150 },
	{ "USB1_VBUS_DETECT", 0x020C81A0 },
	{ "USB1_CHRG_DETECT", 0x020C81B0 },
	{ "USB1_VBUS_DET_STAT", 0x020C81C0 },
	{ "USB1_CHRG_DET_STAT", 0x020C81D0 },
	{ "USB1_LOOPBACK", 0x020C81E0 },
	{ "USB1_MISC", 0x020C81F0 },
	{ "USBPHY1_PWD", 0x020C9000 },
	{ "USBPHY1_TX", 0x020C9010 },
	{ "USBPHY1_RX", 0x020C9020 },
	{ "USBPHY1_CTRL", 0x020C9030 },
	{ "USBPHY1_STATUS", 0x020C9040 },
	{ "USBPHY1_DEBUG", 0x020C9050 },
};
/* Behind the usboh3 gate (CCGR6 CG0). */
static const struct pw4_reg pw4_regs_gated[] = {
	{ "OTG1_USBCMD", 0x02184140 },
	{ "OTG1_USBSTS", 0x02184144 },
	{ "OTG1_USBINTR", 0x02184148 },
	{ "OTG1_PORTSC1", 0x02184184 },
	{ "OTG1_OTGSC", 0x021841A4 },
	{ "OTG1_USBMODE", 0x021841A8 },
	{ "USBNC_OTG1_CTRL", 0x02184800 },
	{ "USBNC_OTG1_PHY_CTRL0", 0x02184818 },
};

/* The four pages, mapped once: a watcher keeps reading them after Android's
 * init has taken over /dev and loaded its policy, without another syscall. */
#define PW4_USB_PAGES 4
static const uint32_t pw4_usb_page_addr[PW4_USB_PAGES] = { 0x020C4000, 0x020C8000, 0x020C9000, 0x02184000 };
struct pw4_usb_map { volatile uint32_t *page[PW4_USB_PAGES]; };

static int pw4_usb_map(struct pw4_usb_map *m)
{
	int fd = open("/dev/mem", O_RDONLY | O_SYNC | O_CLOEXEC);
	if (fd < 0) return -1;
	for (int i = 0; i < PW4_USB_PAGES; i++) {
		void *p = mmap(0, 4096, PROT_READ, MAP_SHARED, fd, pw4_usb_page_addr[i]);
		m->page[i] = p == MAP_FAILED ? 0 : p;
	}
	close(fd);
	return 0;
}

static int pw4_usb_read(const struct pw4_usb_map *m, uint32_t addr, uint32_t *v)
{
	for (int i = 0; i < PW4_USB_PAGES; i++)
		if (m->page[i] && (addr & ~0xFFFu) == pw4_usb_page_addr[i]) { *v = m->page[i][(addr & 0xFFFu) / 4]; return 0; }
	return -1;
}

/* "NAME=xxxxxxxx ..." for every register; the controller's only while its
 * clock gate is on and `gated` asks for them. */
static void pw4_usb_format(const struct pw4_usb_map *m, int gated, char *out, size_t len)
{
	size_t used = 0;
	uint32_t v, ccgr6 = 0;
	out[0] = 0;
	for (size_t i = 0; i < sizeof(pw4_regs_always) / sizeof(*pw4_regs_always) && used < len; i++) {
		if (pw4_usb_read(m, pw4_regs_always[i].addr, &v)) continue;
		if (i == 0) ccgr6 = v;
		used += (size_t)snprintf(out + used, len - used, "%s=%08x ", pw4_regs_always[i].name, v);
	}
	if (gated && (ccgr6 & 3u))
		for (size_t i = 0; i < sizeof(pw4_regs_gated) / sizeof(*pw4_regs_gated) && used < len; i++)
			if (!pw4_usb_read(m, pw4_regs_gated[i].addr, &v))
				used += (size_t)snprintf(out + used, len - used, "%s=%08x ", pw4_regs_gated[i].name, v);
	if (used && used < len) out[used - 1] = 0;
}
