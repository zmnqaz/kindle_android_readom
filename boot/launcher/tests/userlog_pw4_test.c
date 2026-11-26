/* SPDX-License-Identifier: GPL-2.0-only */
/* userlog.c as the PW4's helper is built (-DCHECK_PROFILE=3): the lines its
 * allow-list keeps that the PW3's does not, and the other way round. The PW4's
 * eMMC is mmc1 and its Wi-Fi card is on mmc0, the opposite of the PW3, so the
 * one line that has to go is the Wi-Fi bus's -- the PW3's reason for leaving
 * mmc1 out (1,476 lines of SDIO enumeration, and a card that announces its
 * MAC address) is the PW4's reason for leaving mmc0 out. Everything the two
 * share is userlog_test.c's. Lines are invented, in the shape of the PW4's
 * kernel log (device/amazon/pw4/porting/data/dmesg.txt). */
#include "../userlog.h"
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { printf("PW4_USERLOG_FAIL line=%u\n", __LINE__); return 1; } } while (0)

static int keep(const char *line)
{
	return pw3_klog_keep(line, strlen(line));
}

int main(void)
{
	/* The eMMC and its partitions. */
	CHECK(keep("<6>[    1.234567] mmc1: new HS200 MMC card at address 0001"));
	CHECK(keep("<6>[    1.240000] mmcblk1: mmc1:0001 H8G4a2 7.28 GiB"));
	CHECK(keep("<3>[   40.000000] mmc1: Timeout waiting for hardware interrupt."));
	/* The Wi-Fi card's bus is not. */
	CHECK(!keep("<6>[   32.000000] mmc0: new high speed SDIO card at address 0001"));
	CHECK(!keep("<6>[   32.000000] mmc0: queuing unknown CIS tuple 0x80 (3 bytes)"));
	/* The cable: the ChipIdea controller and the stock gadget, not the PW3's. */
	CHECK(keep("<6>[    2.000000] ci_hdrc ci_hdrc.0: EHCI Host Controller"));
	CHECK(keep("<6>[   12.000000] g_mass_storage gadget: high-speed config #1: Linux File-Backed Storage"));
	CHECK(!keep("<6>[ 1707.376493] fsl-usb2-udc: bind to driver g_file_storage"));
	/* The PID 1 before Android. */
	CHECK(keep("<4>[    0.900000] pw4-handoff: DRIVER init_module errno=0 module_bytes=570308 seconds=0.41"));
	CHECK(keep("<4>[    1.900000] pw4-handoff: RESULT driver_loaded=1 touch_loaded=1"));
	/* Still not a line with an identifier on it, whatever its prefix. */
	CHECK(!keep("<4>[    1.000000] pw4-handoff: MAC 02:00:00:00:00:00"));
	CHECK(!keep("<6>[    1.300000] mmc1: serial 0123456789abcdef"));
	/* What both keep. */
	CHECK(keep("<6>[  160.699698] suspend debug: Waiting for 5 second(s)."));
	CHECK(keep("<6>[  159.885809] PM: suspend entry 2026-11-23 07:36:11.324517018 UTC"));
	CHECK(keep("<0>[  100.000000] Kernel panic - not syncing: Fatal exception"));
	printf("PW4_USERLOG_PASS checks=%u\n", checks);
	return 0;
}
