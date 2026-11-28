/* SPDX-License-Identifier: GPL-2.0-only */
/* The RPMB line of pw3-preinit.conf, as the Android kernel lists the area.
 *
 * On the stock system the PW4's eMMC is Amazon's storage driver, which lists
 * no RPMB device, so the device side (boot/devicedata, android_rpmb_sectors)
 * can only write the size the maintainer's PW4 had: 8192 sectors. The size is
 * the eMMC's own (EXT_CSD RPMB_SIZE_MULT) and differs between chips, and
 * preinit compares every eMMC block device's size with the configuration
 * (device/amazon/pw3/preinit/TestFileInit.h), so on another chip Android
 * stopped before anything was mapped. The Android kernel is the first that
 * knows the size; its PID 1 (pw4-handoff-init.c) puts it in that one line
 * before preinit reads the file. Nothing else in the file is touched.
 *
 * RPMB is a hardware partition of its own, not a range of the user area, so
 * the overlap check the sizes exist for cannot be reached through it; preinit
 * still requires it present and read only. */
#ifndef PW4_RPMB_H
#define PW4_RPMB_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* A decimal sector count as sysfs prints it ("8192\n"); 0 if not one. */
static inline int pw4_rpmb_parse_size(const char *s, uint64_t *out)
{
	uint64_t v = 0;
	size_t n = 0;
	for (; s[n] >= '0' && s[n] <= '9'; ++n) {
		if (v > (UINT64_MAX - 9) / 10) return 0;
		v = v * 10 + (uint64_t)(s[n] - '0');
	}
	if (!n || (s[n] && !(s[n] == '\n' && !s[n + 1])) || !v) return 0;
	*out = v;
	return 1;
}

/* in[0..len): the configuration. With exactly one "block=<disk>rpmb:N:-"
 * line, writes the file with N replaced by sectors to out (cap bytes) and
 * returns 1, or 0 when N already is sectors (out is then a copy, too).
 * -1: no such line, more than one, a malformed one, or out too small. *old
 * is the N the file had, when there is one line. */
static inline int pw4_rpmb_rewrite(const char *in, size_t len, const char *disk, uint64_t sectors,
				   char *out, size_t cap, size_t *outlen, uint64_t *old)
{
	char key[48], num[24];
	int k = snprintf(key, sizeof(key), "block=%srpmb:", disk);
	if (k <= 0 || (size_t)k >= sizeof(key) || !sectors) return -1;
	int m = snprintf(num, sizeof(num), "%llu", (unsigned long long)sectors);
	const char *hit = 0, *hit_end = 0;
	for (size_t i = 0; i < len;) {
		const char *line = in + i, *nl = memchr(line, '\n', len - i);
		size_t l = nl ? (size_t)(nl - line) : len - i;
		if (l >= (size_t)k && !memcmp(line, key, (size_t)k)) {
			if (hit) return -1;
			hit = line;
			hit_end = line + l;
		}
		i += l + (nl != 0);
	}
	if (!hit) return -1;
	/* The rest of the line is N, then ":-". */
	const char *p = hit + k, *colon = memchr(p, ':', (size_t)(hit_end - p));
	if (!colon || colon == p || hit_end - colon != 2 || colon[1] != '-') return -1;
	char was[24];
	if ((size_t)(colon - p) >= sizeof(was)) return -1;
	memcpy(was, p, (size_t)(colon - p));
	was[colon - p] = 0;
	if (!pw4_rpmb_parse_size(was, old)) return -1;
	size_t before = (size_t)(p - in), after = len - (size_t)(colon - in);
	if (before + (size_t)m + after > cap) return -1;
	memcpy(out, in, before);
	memcpy(out + before, num, (size_t)m);
	memcpy(out + before + m, colon, after);
	*outlen = before + (size_t)m + after;
	return *old != sectors;
}
#endif
