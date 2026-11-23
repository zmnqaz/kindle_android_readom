/* SPDX-License-Identifier: GPL-2.0-only */
/* PID 1 of the first handoff on a PW4: proves the Android kernel came up on
 * the device, then reboots so the stock system can read the log.
 *
 * It mounts nothing but proc, sysfs and devtmpfs, reads no partition and
 * writes nowhere except the kernel log, which on this machine is the Lab126
 * buffer at 0x9be00000 that the next (stock) boot shows in /proc/printkbuf.
 * It arms the i.MX watchdog before anything else, so that a hang after this
 * point still ends in a reset the log survives, then reboots itself. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <linux/watchdog.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/klog.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <linux/fb.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/utsname.h>
#include <unistd.h>

static int kmsg = -1;

/* ---- Lab126 framebuffer ABI (PW4 4.1.15 include/uapi/linux/mxcfb.h) ---- */
struct mxcfb_rect { uint32_t top, left, width, height; };
struct mxcfb_alt_buffer_data { uint32_t phys_addr, width, height; struct mxcfb_rect alt_update_region; };
struct mxcfb_update_data {
    struct mxcfb_rect update_region;
    uint32_t waveform_mode, update_mode, update_marker;
    int temp;
    unsigned int flags;
    int dither_mode, quant_bit;
    struct mxcfb_alt_buffer_data alt_buffer_data;
    uint32_t hist_bw_waveform_mode, hist_gray_waveform_mode;
};
struct mxcfb_update_marker_data { uint32_t update_marker, collision_test; };
#define MXCFB_SET_AUTO_UPDATE_MODE _IOW('F', 0x2D, uint32_t)
#define MXCFB_SEND_UPDATE _IOW('F', 0x2E, struct mxcfb_update_data)
#define MXCFB_WAIT_FOR_UPDATE_COMPLETE _IOWR('F', 0x2F, struct mxcfb_update_marker_data)
#define MXCFB_SET_UPDATE_SCHEME _IOW('F', 0x32, uint32_t)
#define WAVEFORM_MODE_GC16 0x2
#define UPDATE_MODE_FULL 0x1
#define TEMP_USE_AMBIENT 0x1000

static void say(const char *format, ...)
{
    char line[900];
    va_list a;
    va_start(a, format);
    int n = vsnprintf(line, sizeof(line), format, a);
    va_end(a);
    if (n <= 0) return;
    if (n >= (int)sizeof(line)) n = sizeof(line) - 1;
    if (kmsg >= 0) { if (write(kmsg, line, (size_t)n) < 0) {} }
}
/* One file, line by line, into the log; lines matching `only` when given. */
static void dump(const char *tag, const char *path, const char *only, int max_lines)
{
    static char buf[131072];
    int fd = open(path, O_RDONLY), lines = 0;
    if (fd < 0) { say("<4>PW4_OBSERVER %s: cannot open %s\n", tag, path); return; }
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n < 0) return;
    buf[n] = 0;
    for (char *save = NULL, *l = strtok_r(buf, "\n", &save); l && lines < max_lines; l = strtok_r(NULL, "\n", &save)) {
        if (only && !strstr(l, only)) continue;
        say("<4>PW4_OBSERVER %s: %s\n", tag, l);
        ++lines;
    }
}

/* The e-ink screen, as the stock system brings it up: the panel's own
 * waveform from the FAT on mmcblk1p2 (mounted read-only), the EPDC driver
 * loaded with its name, then one full GC16 update of a test picture. */
static void screen(void)
{
    char name[256] = "", args[300];
    DIR *d;
    struct dirent *e;
    mkdir("/mnt", 0755); mkdir("/mnt/wfm", 0755);
    /* The eMMC is probed asynchronously: its partitions appear about 0.3 s
     * after PID 1 starts (Jump 9: 0.89 s). Wait for the one we need. */
    struct stat ps;
    int waited = 0;
    while (stat("/dev/mmcblk1p2", &ps) && waited < 150) { usleep(100000); ++waited; }
    say("<4>PW4_OBSERVER epdc: mmcblk1p2 %s after %d ms\n", waited < 150 ? "present" : "absent", waited * 100);
    if (mount("/dev/mmcblk1p2", "/mnt/wfm", "vfat", MS_RDONLY, "") ) {
        say("<4>PW4_OBSERVER epdc: mount mmcblk1p2 failed errno=%d\n", errno); return;
    }
    d = opendir("/mnt/wfm/waveform_to_use");
    while (d && (e = readdir(d)))
        if (strstr(e->d_name, ".wrf")) { snprintf(name, sizeof(name), "%s", e->d_name); break; }
    if (d) closedir(d);
    say("<4>PW4_OBSERVER epdc: waveform '%s'\n", name);
    int fd = open("/mxc_epdc_v2_fb.ko", O_RDONLY);
    struct stat st;
    if (fd < 0 || fstat(fd, &st)) { say("<4>PW4_OBSERVER epdc: no module\n"); return; }
    void *ko = malloc(st.st_size);
    if (!ko || read(fd, ko, st.st_size) != st.st_size) { say("<4>PW4_OBSERVER epdc: read module failed\n"); return; }
    close(fd);
    snprintf(args, sizeof(args), "waveform=%s", name);
    long rc = syscall(SYS_init_module, ko, (unsigned long)st.st_size, args);
    say("<4>PW4_OBSERVER epdc: init_module rc=%ld errno=%d\n", rc, rc ? errno : 0);
    free(ko);
    int fb = -1;
    for (int i = 0; i < 100 && fb < 0; ++i) { fb = open("/dev/fb0", O_RDWR); if (fb < 0) usleep(100000); }
    if (fb < 0) { say("<4>PW4_OBSERVER epdc: no /dev/fb0\n"); return; }
    struct fb_var_screeninfo v; struct fb_fix_screeninfo f;
    if (ioctl(fb, FBIOGET_VSCREENINFO, &v) || ioctl(fb, FBIOGET_FSCREENINFO, &f)) { say("<4>PW4_OBSERVER epdc: fb info failed\n"); return; }
    say("<4>PW4_OBSERVER epdc: fb %s %ux%u virt %ux%u bpp %u gray %u rotate %u line %u smem %u\n", f.id,
        v.xres, v.yres, v.xres_virtual, v.yres_virtual, v.bits_per_pixel, v.grayscale, v.rotate, f.line_length, f.smem_len);
    uint8_t *p = mmap(NULL, f.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb, 0);
    if (p == MAP_FAILED || v.bits_per_pixel != 8) { say("<4>PW4_OBSERVER epdc: mmap or bpp unsupported\n"); return; }
    uint8_t *base = p + (size_t)v.yoffset * f.line_length;
    /* White page, black frame, 16 gray steps, a checkerboard: plainly a test. */
    for (uint32_t y = 0; y < v.yres; ++y) {
        uint8_t *row = base + (size_t)y * f.line_length;
        for (uint32_t x = 0; x < v.xres; ++x) {
            uint8_t c = 0xff;
            if (x < 24 || y < 24 || x >= v.xres - 24 || y >= v.yres - 24) c = 0x00;
            else if (y > v.yres / 4 && y < v.yres / 4 + 120) c = (uint8_t)(((x - 24) * 16 / (v.xres - 48)) * 17);
            else if (y > v.yres / 2 && y < v.yres / 2 + 400 && x > v.xres / 4 && x < v.xres * 3 / 4)
                c = (((x / 50) + (y / 50)) & 1) ? 0x00 : 0xff;
            row[x] = c;
        }
    }
    uint32_t mode = 0;
    ioctl(fb, MXCFB_SET_AUTO_UPDATE_MODE, &mode);
    mode = 2; ioctl(fb, MXCFB_SET_UPDATE_SCHEME, &mode);
    ioctl(fb, FBIOBLANK, FB_BLANK_UNBLANK);
    struct mxcfb_update_data u;
    memset(&u, 0, sizeof(u));
    u.update_region.width = v.xres; u.update_region.height = v.yres;
    u.waveform_mode = u.hist_bw_waveform_mode = u.hist_gray_waveform_mode = WAVEFORM_MODE_GC16;
    u.update_mode = UPDATE_MODE_FULL; u.update_marker = 1; u.temp = TEMP_USE_AMBIENT;
    int r = ioctl(fb, MXCFB_SEND_UPDATE, &u), err = r ? errno : 0;
    struct mxcfb_update_marker_data m = { 1, 0 };
    int w = ioctl(fb, MXCFB_WAIT_FOR_UPDATE_COMPLETE, &m);
    say("<4>PW4_OBSERVER epdc: send_update rc=%d errno=%d wait rc=%d errno=%d\n", r, err, w, w ? errno : 0);
    sleep(6);   /* long enough to be seen */
}

int main(void)
{
    struct utsname u;
    mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);
    mount("proc", "/proc", "proc", 0, NULL);
    mount("sysfs", "/sys", "sysfs", 0, NULL);
    kmsg = open("/dev/kmsg", O_WRONLY);

    /* First: a reset that does not need this program to keep running. */
    int wdt = open("/dev/watchdog", O_WRONLY), timeout = 60;
    if (wdt >= 0) ioctl(wdt, WDIOC_SETTIMEOUT, &timeout);
    say("<4>PW4_OBSERVER start watchdog=%s timeout=%d\n", wdt >= 0 ? "armed" : "unavailable", timeout);

    uname(&u);
    say("<4>PW4_OBSERVER uname %s %s\n", u.release, u.version);
    dump("cmdline", "/proc/cmdline", NULL, 2);
    dump("cpu", "/proc/cpuinfo", "Hardware", 2);
    dump("cpu", "/proc/cpuinfo", "CPU part", 2);
    dump("mem", "/proc/meminfo", "MemTotal", 1);
    dump("iomem", "/proc/iomem", "System RAM", 4);
    dump("iomem", "/proc/iomem", "Kernel", 4);
    dump("mounts", "/proc/mounts", NULL, 8);
    dump("marker", "/device-marker", NULL, 1);
    dump("fb", "/sys/class/graphics/fb0/name", NULL, 1);
    dump("mmc", "/proc/partitions", "mmcblk", 4);
    dump("power", "/sys/class/power_supply/bd71827_bat/capacity", NULL, 1);
    /* The clocks the stock suspend left behind: the first handoff hung in the
     * serial console, whose clock (uart, from pll3_80m or osc) was gated. */
    mount("debugfs", "/sys/kernel/debug", "debugfs", 0, NULL);
    dump("clk", "/sys/kernel/debug/clk/clk_summary", "uart", 12);
    dump("clk", "/sys/kernel/debug/clk/clk_summary", "pll3", 12);
    dump("clk", "/sys/kernel/debug/clk/clk_summary", " osc", 2);
    dump("clk", "/sys/kernel/debug/clk/clk_summary", "epdc", 6);
    dump("clk", "/sys/kernel/debug/clk/clk_summary", "usdhc", 6);
    /* The early boot (before the Lab126 region took over) from the ring:
     * the Falcon storage BIOS is found or refused there. */
    {
        static char ring[262144];
        int n = klogctl(3, ring, sizeof(ring) - 1), lines = 0;
        if (n > 0) {
            ring[n] = 0;
            for (char *save = NULL, *l = strtok_r(ring, "\n", &save); l && lines < 40; l = strtok_r(NULL, "\n", &save))
                if (strstr(l, "alcon") || strstr(l, "BIOS") || strstr(l, "dt: ") || strstr(l, "mmcblk") ||
                    strstr(l, "Memory policy") || strstr(l, "Virtual kernel") || strstr(l, "lowmem")) {
                    say("<4>PW4_OBSERVER early: %s\n", l);
                    ++lines;
                }
        }
    }
    dump("parts", "/proc/partitions", "mmcblk", 20);
    screen();
    dump("mmc1", "/sys/kernel/debug/mmc1/ios", NULL, 12);
    say("<4>PW4_OBSERVER done; rebooting in 8 seconds\n");
    sleep(8);
    sync();
    say("<4>PW4_OBSERVER reboot now\n");
    reboot(RB_AUTOBOOT);
    /* Not reached; if it is, the armed watchdog resets the board. */
    for (;;) pause();
}
