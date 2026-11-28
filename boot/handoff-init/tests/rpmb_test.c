/* SPDX-License-Identifier: GPL-2.0-only */
/* pw4-rpmb.h: the RPMB line of pw3-preinit.conf, rewritten to the size the
 * Android kernel lists, on configurations shaped like the device side writes
 * them (boot/devicedata/devicedata.c: the block= lines, RPMB last). */
#define _GNU_SOURCE
#include "../pw4-rpmb.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static const char head[] = "system_size=1073741824\nfat_offset=8192\n"
	"block=mmcblk1:15269888:-\nblock=mmcblk1p10:12000000:3000000\n"
	"block=mmcblk1boot0:8192:-\nblock=mmcblk1boot1:8192:-\n";

static int run(const char *in, uint64_t sectors, char *out, size_t cap, size_t *len, uint64_t *old)
{
	return pw4_rpmb_rewrite(in, strlen(in), "mmcblk1", sectors, out, cap, len, old);
}

int main(void)
{
	char in[1024], out[1024], want[1024];
	size_t len = 0;
	uint64_t old = 0, v = 0;

	/* Another chip's size: only that number changes. */
	snprintf(in, sizeof(in), "%sblock=mmcblk1rpmb:8192:-\n", head);
	snprintf(want, sizeof(want), "%sblock=mmcblk1rpmb:32768:-\n", head);
	CHECK(run(in, 32768, out, sizeof(out), &len, &old) == 1);
	CHECK(old == 8192 && len == strlen(want) && !memcmp(out, want, len));

	/* Smaller than the placeholder, and without the final newline. */
	snprintf(in, sizeof(in), "%sblock=mmcblk1rpmb:8192:-", head);
	snprintf(want, sizeof(want), "%sblock=mmcblk1rpmb:1024:-", head);
	CHECK(run(in, 1024, out, sizeof(out), &len, &old) == 1);
	CHECK(len == strlen(want) && !memcmp(out, want, len));

	/* The maintainer's chip: nothing to do. */
	snprintf(in, sizeof(in), "%sblock=mmcblk1rpmb:8192:-\n", head);
	CHECK(run(in, 8192, out, sizeof(out), &len, &old) == 0 && old == 8192);
	CHECK(len == strlen(in) && !memcmp(out, in, len));

	/* The line in the middle of the file. */
	snprintf(in, sizeof(in), "block=mmcblk1rpmb:8192:-\n%s", head);
	snprintf(want, sizeof(want), "block=mmcblk1rpmb:16384:-\n%s", head);
	CHECK(run(in, 16384, out, sizeof(out), &len, &old) == 1);
	CHECK(len == strlen(want) && !memcmp(out, want, len));

	/* Refused: no line, two lines, a malformed one, another disk's, no room, a zero size. */
	CHECK(run(head, 32768, out, sizeof(out), &len, &old) == -1);
	snprintf(in, sizeof(in), "%sblock=mmcblk1rpmb:8192:-\nblock=mmcblk1rpmb:8192:-\n", head);
	CHECK(run(in, 32768, out, sizeof(out), &len, &old) == -1);
	const char *bad[] = { "block=mmcblk1rpmb::-\n", "block=mmcblk1rpmb:8192\n", "block=mmcblk1rpmb:8192:0\n",
		"block=mmcblk1rpmb:8192:-x\n", "block=mmcblk1rpmb:x8192:-\n", "block=mmcblk1rpmb:0:-\n",
		"block=mmcblk1rpmb:99999999999999999999999:-\n", "block=mmcblk0rpmb:8192:-\n" };
	for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); ++i) {
		snprintf(in, sizeof(in), "%s%s", head, bad[i]);
		CHECK(run(in, 32768, out, sizeof(out), &len, &old) == -1);
	}
	snprintf(in, sizeof(in), "%sblock=mmcblk1rpmb:8192:-\n", head);
	CHECK(run(in, 32768, out, strlen(in), &len, &old) == -1);
	CHECK(run(in, 0, out, sizeof(out), &len, &old) == -1);

	/* Reading: a sysfs attribute stats as 4096 bytes and reads as a few (the
	 * rc1 bug); this host's own block devices are one. */
	char text[24], path[300];
	struct stat st;
	int sysfs_seen = 0;
	DIR *dir = opendir("/sys/class/block");
	for (struct dirent *e; dir && (e = readdir(dir)) && !sysfs_seen;) {
		if (e->d_name[0] == '.') continue;
		snprintf(path, sizeof(path), "/sys/class/block/%s/size", e->d_name);
		if (stat(path, &st) || st.st_size != 4096) continue;
		sysfs_seen = 1;
		CHECK(pw4_rpmb_read_text(path, text, sizeof(text)));
		CHECK(strlen(text) < 24 && text[strlen(text) - 1] == '\n');
	}
	if (dir) closedir(dir);
	if (!sysfs_seen) puts("rpmb: no sysfs block size file on this host; that case not run");
	char tmpl[] = "/tmp/rpmb-test-XXXXXX";
	int fd = mkstemp(tmpl);
	CHECK(fd >= 0 && write(fd, "32768\n", 6) == 6 && !close(fd));
	CHECK(pw4_rpmb_read_text(tmpl, text, sizeof(text)) && !strcmp(text, "32768\n"));
	CHECK(pw4_rpmb_parse_size(text, &v) && v == 32768);
	CHECK(pw4_rpmb_read_text(tmpl, text, 7) && !strcmp(text, "32768\n"));   /* exactly fits */
	CHECK(!pw4_rpmb_read_text(tmpl, text, 6));                               /* one byte short */
	unlink(tmpl);
	CHECK(!pw4_rpmb_read_text("/nonexistent/rpmb/size", text, sizeof(text)));

	/* The sysfs value. */
	CHECK(pw4_rpmb_parse_size("8192\n", &v) && v == 8192);
	CHECK(pw4_rpmb_parse_size("32768", &v) && v == 32768);
	CHECK(!pw4_rpmb_parse_size("", &v) && !pw4_rpmb_parse_size("0\n", &v) && !pw4_rpmb_parse_size("12 \n", &v) &&
	      !pw4_rpmb_parse_size("12\n\n", &v) && !pw4_rpmb_parse_size("-1\n", &v) &&
	      !pw4_rpmb_parse_size("99999999999999999999999\n", &v));

	if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
	puts("rpmb: ok");
	return 0;
}
