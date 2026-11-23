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
#include <sys/klog.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/utsname.h>
#include <unistd.h>

static int kmsg = -1;

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
    dump("parts", "/proc/partitions", NULL, 20);
    say("<4>PW4_OBSERVER done; rebooting in 8 seconds\n");
    sleep(8);
    sync();
    say("<4>PW4_OBSERVER reboot now\n");
    reboot(RB_AUTOBOOT);
    /* Not reached; if it is, the armed watchdog resets the board. */
    for (;;) pause();
}
