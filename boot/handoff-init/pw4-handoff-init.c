/* SPDX-License-Identifier: GPL-2.0-only */
/* PW4 handoff restore - the first process of the Android initramfs on a
 * Kindle Paperwhite 4, in place of the storage entry it then executes
 * (pw3-handoff-init.c is the PW3 one; this is the same job on other hardware).
 *
 * The PW4 needs no PMIC registers put back (the panel takes updates after the
 * handover as it is, Jump 10). What it needs is the display driver, which
 * on this kernel reads the panel's waveform from a file:
 *   - mmcblk1p2 (a FAT the stock system mounts at /mnt/wfm) is mounted read
 *     only, the driver mxc_epdc_v2_fb.ko is loaded with the name of the
 *     waveform found in waveform_to_use/, and the FAT is unmounted again
 *     (the driver keeps what it read: read_mnt_wfm() copies it into memory);
 *   - the generic EPDC firmware the driver also asks for
 *     (imx/epdc/epdc_ED060KC4.fw) is in this initramfs only because the device
 *     side put it there; it is deleted once the driver is up.
 * Then "正在启动 Android" goes on the panel and the touch driver (gt9xx-ts.ko,
 * loaded by the stock system with no parameters, /etc/upstart/touch) is
 * loaded, since Android's interface check waits for its input device "goodix-ts".
 * Before that, the RPMB line of pw3-preinit.conf gets the size this kernel
 * lists (pw4-rpmb.h): the device side could only write the maintainer's.
 * Last the unmodified storage entry
 * is executed as PID 1 with the root as found but for that one line: no
 * mounts left, no files added.
 *
 * Nothing is written to any block device. */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <linux/fb.h>
#include "start-picture.h"
#include "pw4-usb-regs.h"
#include "pw4-rpmb.h"

#ifndef KERNEL_RELEASE
#define KERNEL_RELEASE "4.1.15-lab126-pw4-android81+"
#endif
#define REAL_ENTRY "/pw3-testfile-init.real"
#define REAL_ARGV0 "/pw3-testfile-init"
#define MODULE_FILE "/pw4-handoff/mxc_epdc_v2_fb.ko"
#define TOUCH_MODULE_FILE "/pw4-handoff/gt9xx-ts.ko"
#define STATUS_FILE "/pw4-handoff/status.txt"
#define FIRMWARE_DIRS { "/lib/firmware/imx/epdc", "/lib/firmware/imx", "/lib/firmware", "/lib" }
#define FIRMWARE_FILE "/lib/firmware/imx/epdc/epdc_ED060KC4.fw"
#define WFM_DISK "/dev/mmcblk1p2"
#define WFM_MOUNT "/mnt/wfm"

/* ---- Lab126 framebuffer ABI (PW4 4.1.15 include/uapi/linux/mxcfb.h) ---- */
struct rect { uint32_t top, left, width, height; };
struct update {
	struct rect region;
	uint32_t waveform, mode, marker;
	int32_t temperature;
	uint32_t flags;
	int32_t dither_mode, quant_bit;
	struct { uint32_t address, width, height; struct rect region; } alternate;
	uint32_t hist_bw, hist_gray;
};
struct marker { uint32_t value, collision; };
_Static_assert(sizeof(struct update) == 80, "Lab126 4.1 update ABI");
#define MXCFB_SET_AUTO_UPDATE_MODE _IOW('F', 0x2D, uint32_t)
#define MXCFB_SEND_UPDATE _IOW('F', 0x2E, struct update)
#define MXCFB_WAIT_FOR_UPDATE_COMPLETE _IOWR('F', 0x2F, struct marker)
#define MXCFB_SET_UPDATE_SCHEME _IOW('F', 0x32, uint32_t)
#define WAVEFORM_MODE_GC16 2u
#define UPDATE_MODE_FULL 1u
#define TEMP_USE_AMBIENT 0x1000

static int kmsgfd = -1, statusfd = -1;
static struct pw4_usb_map usbmap;
static int usbmapped;

static double now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

static void msleep(unsigned ms)
{
	struct timespec t = { ms / 1000, (long)(ms % 1000) * 1000000L };
	while (nanosleep(&t, &t) && errno == EINTR) { }
}

/* One line to the kernel log (the Lab126 buffer the stock system reads back)
 * and to the status file kept in the RAM root. */
static void P(const char *fmt, ...)
{
	char b[600];
	int n = snprintf(b, sizeof(b), "<4>pw4-handoff: ");
	va_list ap;
	va_start(ap, fmt);
	int m = vsnprintf(b + n, sizeof(b) - (size_t)n - 2, fmt, ap);
	va_end(ap);
	if (m < 0) return;
	if ((size_t)m > sizeof(b) - (size_t)n - 2) m = (int)(sizeof(b) - (size_t)n - 2);
	n += m;
	b[n++] = '\n';
	if (kmsgfd >= 0) (void)!write(kmsgfd, b, (size_t)n);
	if (statusfd >= 0) (void)!write(statusfd, b + 3, (size_t)n - 3);
}

static unsigned char *load_file(const char *path, size_t *len)
{
	struct stat st;
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0 || fstat(fd, &st) || st.st_size <= 0 || st.st_size > (12 << 20)) { if (fd >= 0) close(fd); return 0; }
	unsigned char *p = malloc((size_t)st.st_size);
	size_t used = 0;
	while (p && used < (size_t)st.st_size) {
		ssize_t k = read(fd, p + used, (size_t)st.st_size - used);
		if (k < 0 && errno == EINTR) continue;
		if (k <= 0) break;
		used += (size_t)k;
	}
	close(fd);
	if (!p || used != (size_t)st.st_size) { free(p); return 0; }
	*len = used;
	return p;
}

/* ---- the display driver, with the panel's waveform from mmcblk1p2 ---- */
static int load_driver(void)
{
	/* The eMMC is probed asynchronously; its partitions appear a few tenths
	 * of a second after PID 1 starts (Jump 9: 0.89 s). */
	struct stat st;
	double start = now();
	while (stat(WFM_DISK, &st) && now() - start < 15) msleep(100);
	P("WFM %s %s after %.0f ms", WFM_DISK, stat(WFM_DISK, &st) ? "absent" : "present", (now() - start) * 1000);
	mkdir("/mnt", 0755);
	int made = !mkdir(WFM_MOUNT, 0755);
	if (mount(WFM_DISK, WFM_MOUNT, "vfat", MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOEXEC, "")) {
		P("WFM mount failed errno=%d", errno);
		if (made) rmdir(WFM_MOUNT);
		return -1;
	}
	char name[80] = "", args[120];
	DIR *d = opendir(WFM_MOUNT "/waveform_to_use");
	for (struct dirent *e; d && (e = readdir(d)); )
		/* The driver builds the path in 96 bytes, 25 of them its directory. */
		if (strstr(e->d_name, ".wrf") && strlen(e->d_name) < 70) { memcpy(name, e->d_name, strlen(e->d_name) + 1); break; }
	if (d) closedir(d);
	P("WFM waveform='%s'", name);

	size_t ko_len = 0;
	unsigned char *ko = load_file(MODULE_FILE, &ko_len);
	int e = -1;
	double t0 = now();
	if (ko && name[0]) {
		snprintf(args, sizeof(args), "waveform=%s", name);
		e = syscall(SYS_init_module, ko, (unsigned long)ko_len, args) ? errno : 0;
	}
	P("DRIVER init_module errno=%d module_bytes=%zu seconds=%.2f", e, ko_len, now() - t0);
	free(ko);
	if (umount(WFM_MOUNT)) { P("WFM umount errno=%d; detaching", errno); umount2(WFM_MOUNT, MNT_DETACH); }
	if (made) rmdir(WFM_MOUNT);
	return e == 0 ? 0 : -1;
}

/* ---- the start picture, as on the PW3 (same panel size, 1072x1448) ---- */
struct screen {
	int fd;
	unsigned char *mem;
	struct fb_var_screeninfo v;
	struct fb_fix_screeninfo f;
};

static void px(struct screen *s, int x, int y, unsigned char c)
{
	if (x < 0 || y < 0 || (unsigned)x >= s->v.xres || (unsigned)y >= s->v.yres) return;
	s->mem[(size_t)((unsigned)y + s->v.yoffset) * s->f.line_length + (unsigned)x + s->v.xoffset] = c;
}

static void draw_start(struct screen *s)
{
	for (unsigned y = 0; y < s->v.yres; y++)
		memset(s->mem + (size_t)(y + s->v.yoffset) * s->f.line_length + s->v.xoffset, 255, s->v.xres);
	int left = ((int)s->v.xres - PW3_START_W) / 2;
	int top = (int)((uint64_t)s->v.yres * PW3_START_TOP / PW3_START_PANEL_H);
	unsigned x = 0, y = 0;
	for (size_t i = 0; i + 1 < sizeof(pw3_start_runs); i += 2)
		for (unsigned n = pw3_start_runs[i + 1]; n; n--) {
			px(s, left + (int)x, top + (int)y, pw3_start_runs[i]);
			if (++x == PW3_START_W) { x = 0; y++; }
		}
	P("SCREEN picture %dx%d at %d,%d pixels=%lu/%u", PW3_START_W, PW3_START_H,
	  left, top, (unsigned long)y * PW3_START_W + x, PW3_START_PIXELS);
}

static void show_start(void)
{
	struct screen s;
	memset(&s, 0, sizeof(s));
	s.fd = -1;
	for (int i = 0; i < 50 && s.fd < 0; i++) {
		s.fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
		if (s.fd >= 0 && ioctl(s.fd, FBIOGET_FSCREENINFO, &s.f)) { close(s.fd); s.fd = -1; }
		if (s.fd < 0) msleep(100);
	}
	if (s.fd < 0 || ioctl(s.fd, FBIOGET_VSCREENINFO, &s.v)) { P("SCREEN framebuffer unavailable errno=%d", errno); return; }
	if (s.v.bits_per_pixel != 8 ||
	    (uint64_t)s.v.xoffset + s.v.xres > (uint64_t)s.f.line_length ||
	    ((uint64_t)s.v.yoffset + s.v.yres) * (uint64_t)s.f.line_length > (uint64_t)s.f.smem_len) {
		P("SCREEN geometry not usable x=%u+%u stride=%u y=%u+%u len=%u",
		  s.v.xoffset, s.v.xres, s.f.line_length, s.v.yoffset, s.v.yres, s.f.smem_len);
		close(s.fd);
		return;
	}
	s.mem = mmap(0, s.f.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, s.fd, 0);
	if (s.mem == MAP_FAILED) { P("SCREEN mmap failed errno=%d", errno); close(s.fd); return; }
	uint32_t mode = 0;
	int a = ioctl(s.fd, MXCFB_SET_AUTO_UPDATE_MODE, &mode);
	mode = 2;
	int b = ioctl(s.fd, MXCFB_SET_UPDATE_SCHEME, &mode);
	int c = ioctl(s.fd, FBIOBLANK, FB_BLANK_UNBLANK);
	P("SCREEN fb=%s %ux%u stride=%u rotate=%u auto=%d scheme=%d unblank=%d", s.f.id, s.v.xres, s.v.yres,
	  s.f.line_length, s.v.rotate, a, b, c);
	draw_start(&s);
	struct update u;
	memset(&u, 0, sizeof(u));
	u.region = (struct rect){ 0, 0, s.v.xres, s.v.yres };
	u.waveform = u.hist_bw = u.hist_gray = WAVEFORM_MODE_GC16;
	u.mode = UPDATE_MODE_FULL;
	u.marker = 1;
	u.temperature = TEMP_USE_AMBIENT;
	double t0 = now();
	int rc = ioctl(s.fd, MXCFB_SEND_UPDATE, &u), e = rc < 0 ? errno : 0;
	struct marker m = { 1, 0 };
	int wrc = -1, we = 0;
	if (!rc) {
		do { wrc = ioctl(s.fd, MXCFB_WAIT_FOR_UPDATE_COMPLETE, &m); } while (wrc < 0 && errno == EINTR);
		we = wrc < 0 ? errno : 0;
	}
	P("SCREEN %s send=%d/%d wait=%d/%d total_ms=%.0f", rc || wrc < 0 ? "refresh failed" : "up",
	  rc, e, wrc, we, (now() - t0) * 1000);
	munmap(s.mem, s.f.smem_len);
	close(s.fd);
}

static int load_touch(void)
{
	size_t len = 0;
	unsigned char *ko = load_file(TOUCH_MODULE_FILE, &len);
	double t0 = now();
	int e = ko ? (syscall(SYS_init_module, ko, (unsigned long)len, "") ? errno : 0) : -1;
	P("TOUCH init_module errno=%d module_bytes=%zu seconds=%.2f", e, len, now() - t0);
	free(ko);
	return e == 0 ? 0 : -1;
}

static void usb_log(const char *tag)
{
	char line[700];
	if (!usbmapped) return;
	pw4_usb_format(&usbmap, 1, line, sizeof(line));
	P("USBREGS %s %s", tag, line);
}

/* Left running under Android: one line whenever the USB clock, PLL3 or the
 * VBUS/charger detection changes, from mappings made before Android started,
 * and never a controller register (those are behind a clock gate that the
 * driver turns on and off). If plugging the cable freezes the machine again,
 * the last line before the end of the log is the state it froze in. */
static void usb_watch(void)
{
	static const uint32_t watched[] = { 0x020C4080, 0x020C8010, 0x020C8120, 0x020C81C0, 0x020C81D0, 0x020C81F0, 0x020C9030 };
	uint32_t last[sizeof(watched) / sizeof(*watched)] = { 0 };
	if (!usbmapped || fork() != 0) return;
	setsid();
	if (statusfd >= 0) close(statusfd);
	statusfd = -1;
	double end = now() + 1800;
	unsigned lines = 0;
	while (now() < end && lines < 400) {
		int changed = 0;
		for (size_t i = 0; i < sizeof(watched) / sizeof(*watched); i++) {
			uint32_t v = 0;
			if (!pw4_usb_read(&usbmap, watched[i], &v) && v != last[i]) { last[i] = v; changed = 1; }
		}
		if (changed) {
			char line[700];
			pw4_usb_format(&usbmap, 0, line, sizeof(line));
			P("USBWATCH t=%.2f %s", now(), line);
			lines++;
		}
		msleep(200);
	}
	_exit(0);
}

/* The RPMB size preinit will compare, from this kernel (pw4-rpmb.h): the
 * device side could only write the maintainer's. The file is replaced whole,
 * by rename, so preinit reads either the old one or the new one. */
#define PREINIT_CONF "/pw3-preinit.conf"
#define RPMB_SIZE "/sys/class/block/mmcblk1rpmb/size"
static void fix_rpmb(void)
{
	size_t sl = 0, cl = 0, outlen = 0;
	unsigned char *s = load_file(RPMB_SIZE, &sl);
	char text[24];
	uint64_t sectors = 0, old = 0;
	if (!s) { P("RPMB none listed; configuration left as is"); return; }
	int ok = sl < sizeof(text);
	if (ok) { memcpy(text, s, sl); text[sl] = 0; ok = pw4_rpmb_parse_size(text, &sectors); }
	free(s);
	if (!ok) { P("RPMB size unreadable; configuration left as is"); return; }
	struct stat st;
	unsigned char *conf = load_file(PREINIT_CONF, &cl);
	if (!conf || stat(PREINIT_CONF, &st)) { free(conf); P("RPMB configuration unreadable"); return; }
	char *out = malloc(cl + sizeof(text));
	int rc = out ? pw4_rpmb_rewrite((const char *)conf, cl, "mmcblk1", sectors, out, cl + sizeof(text), &outlen, &old) : -1;
	free(conf);
	if (rc <= 0) {
		if (rc < 0) P("RPMB line not found; configuration left as is");
		else P("RPMB %llu sectors, as configured", (unsigned long long)sectors);
		free(out);
		return;
	}
	static const char tmp[] = PREINIT_CONF ".rpmb";
	int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, st.st_mode & 07777);
	size_t done = 0;
	while (fd >= 0 && done < outlen) {
		ssize_t k = write(fd, out + done, outlen - done);
		if (k < 0 && errno == EINTR) continue;
		if (k <= 0) break;
		done += (size_t)k;
	}
	free(out);
	int e = 0, bad = fd < 0 || done != outlen || fchmod(fd, st.st_mode & 07777);
	if (bad) e = errno;
	if (fd >= 0 && close(fd) && !bad) { bad = 1; e = errno; }
	if (!bad && rename(tmp, PREINIT_CONF)) { bad = 1; e = errno; }
	if (bad) {
		unlink(tmp);
		P("RPMB %llu sectors (configured %llu); rewrite failed errno=%d", (unsigned long long)sectors, (unsigned long long)old, e);
		return;
	}
	P("RPMB %llu sectors (configured %llu); configuration updated", (unsigned long long)sectors, (unsigned long long)old);
}

/* Everything that touches hardware; runs in a child so that it is bounded in time. */
static int work(void)
{
	int loaded = !load_driver();
	if (loaded) show_start();
	int touch = !load_touch();
	P("RESULT driver_loaded=%d touch_loaded=%d", loaded, touch);
	return 0;
}

int main(int argc, char **argv, char **envp)
{
	(void)argc; (void)argv;
	if (getpid() != 1) return 64; /* only ever the first process of its own initramfs */
	signal(SIGPIPE, SIG_IGN);
	int mp = !mount("proc", "/proc", "proc", 0, 0);
	int ms = !mount("sysfs", "/sys", "sysfs", 0, 0);
	int md = !mount("devtmpfs", "/dev", "devtmpfs", 0, 0);
	kmsgfd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
	statusfd = open(STATUS_FILE, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0444);
	struct utsname u;
	memset(&u, 0, sizeof(u));
	uname(&u);
	P("BEGIN release=%s uptime=%.2f mounts=%d%d%d", u.release, now(), mp, ms, md);
	usbmapped = !pw4_usb_map(&usbmap);
	usb_log("begin");

	if (!strcmp(u.release, KERNEL_RELEASE) && mp && ms && md) {
		pid_t child = fork();
		if (!child) _exit(work());
		double end = now() + 60;
		int status = -1, done = 0;
		while (child > 0 && now() < end) {
			if (waitpid(child, &status, WNOHANG) == child) { done = 1; break; }
			msleep(50);
		}
		if (child > 0 && !done) { kill(child, SIGKILL); waitpid(child, &status, 0); P("RESULT timed out; continuing the boot"); }
		while (waitpid(-1, 0, WNOHANG) > 0) { }
		/* A child killed mid-way may have left the FAT mounted. */
		if (!umount2(WFM_MOUNT, MNT_DETACH)) P("WFM was still mounted; detached");
		rmdir(WFM_MOUNT);
	} else P("RESULT skipped: unexpected kernel or mounts");
	if (ms) fix_rpmb();

	/* The firmware came from the device: it does not stay in the root. */
	if (unlink(FIRMWARE_FILE) && errno != ENOENT) P("FIRMWARE unlink errno=%d", errno);
	static const char *const dirs[] = FIRMWARE_DIRS;
	for (size_t i = 0; i < sizeof(dirs) / sizeof(*dirs); i++) rmdir(dirs[i]);

	usb_log("handover");
	usb_watch();
	P("END uptime=%.2f handing over to %s", now(), REAL_ENTRY);
	if (statusfd >= 0) { fsync(statusfd); close(statusfd); statusfd = -1; }
	if (kmsgfd >= 0) { close(kmsgfd); kmsgfd = -1; }
	if (md && umount("/dev")) umount2("/dev", MNT_DETACH);
	if (ms && umount("/sys")) umount2("/sys", MNT_DETACH);
	if (mp && umount("/proc")) umount2("/proc", MNT_DETACH);
	char *args[] = { REAL_ARGV0, 0 };
	execve(REAL_ENTRY, args, envp);

	/* The storage entry could not be executed: back to the stock system. */
	sync();
	reboot(RB_AUTOBOOT);
	for (;;) sleep(60);
}
