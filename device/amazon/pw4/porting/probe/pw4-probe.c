/* SPDX-License-Identifier: GPL-2.0-only */
/* First run of the handoff module on a PW4, PROBE only: runs on the stock
 * system as root, from /tmp (the user FAT is noexec).
 *
 *   pw4-probe DIR REPORT [jump]
 *
 * DIR holds pw3_first_kernel.ko (built --probe-only: the jump is not compiled
 * in), zImage, public-initramfs.gz and cmdline.txt. It makes the device tree
 * from this Kindle's /sys/firmware/fdt the way the launcher will (dd_fdt),
 * uploads everything, lets the module check it, runs one pm_test=core suspend
 * cycle (PWS_NATIVE_PROBE) and writes what the module saw to REPORT. Whatever
 * happens it then discards, unloads the module and puts pm_test back.
 * Writes nothing but REPORT; touches no partition.
 *
 * With "jump", the module must be a full build (not --probe-only); after a
 * PROBE the module calls READY, the report is put on disk and the same open
 * store is committed: the Android kernel starts and this program does not
 * return. Any refusal on the way ends as a plain probe does. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <sys/utsname.h>
#include <unistd.h>
#include "stage_abi.h"
#include "native-abi.h"
#include "sha256.h"
#include "devicedata.h"

static FILE *report;
static char saved_test[32] = "none";
static int module_loaded, device_fd = -1;

static void say(const char *format, ...)
{
    va_list a, b;
    va_start(a, format); va_copy(b, a);
    vfprintf(stdout, format, a); fflush(stdout);
    if (report) { vfprintf(report, format, b); fflush(report); }
    va_end(a); va_end(b);
}
static int write_text(const char *path, const char *text)
{
    int fd = open(path, O_WRONLY), ok;
    if (fd < 0) return -1;
    ok = write(fd, text, strlen(text)) == (ssize_t)strlen(text);
    return close(fd) == 0 && ok ? 0 : -1;
}
static int read_text(const char *path, char *out, size_t size)
{
    int fd = open(path, O_RDONLY);
    ssize_t n;
    if (fd < 0) return -1;
    n = read(fd, out, size - 1);
    close(fd);
    if (n < 0) return -1;
    out[n] = 0;
    return 0;
}
static void cleanup(void)
{
    if (device_fd >= 0) { ioctl(device_fd, PWS_DISCARD, 0); close(device_fd); device_fd = -1; }
    if (module_loaded && syscall(SYS_delete_module, "pw3_first_kernel", O_NONBLOCK) == 0) module_loaded = 0;
    say("cleanup module_unloaded=%d pm_test_restored=%d (%s)\n", !module_loaded,
        write_text("/sys/power/pm_test", saved_test) == 0, saved_test);
}
static void stop(const char *what)
{
    say("STOP %s errno=%d (%s)\n", what, errno, strerror(errno));
    cleanup();
    exit(1);
}
static unsigned char *read_blob(const char *dir, const char *name, unsigned *bytes)
{
    char path[512];
    struct stat st;
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    int fd = open(path, O_RDONLY);
    if (fd < 0 || fstat(fd, &st) || st.st_size <= 0 || st.st_size > (48 << 20)) stop(path);
    unsigned char *p = malloc((size_t)st.st_size + 4);
    if (!p) stop("out of memory");
    for (off_t done = 0; done < st.st_size;) {
        ssize_t n = read(fd, p + done, (size_t)(st.st_size - done));
        if (n <= 0) stop(path);
        done += n;
    }
    close(fd);
    *bytes = (unsigned)st.st_size;
    return p;
}
/* A newc archive with one file: what the launcher appends is the device's
 * part (waveform, Wi-Fi data); here only a marker, since nothing boots. */
static unsigned char *marker_archive(unsigned *bytes)
{
    static const char name[] = "device-marker", trailer[] = "TRAILER!!!";
    unsigned char *p = calloc(1, 1024);
    unsigned at = 0;
    if (!p) stop("out of memory");
    at += sprintf((char *)p + at, "070701%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x",
                  1, 0100644, 0, 0, 1, 0, 6, 0, 0, 0, 0, (unsigned)sizeof(name), 0);
    memcpy(p + at, name, sizeof(name)); at += sizeof(name); at = (at + 3) & ~3u;
    memcpy(p + at, "probe\n", 6); at += 6; at = (at + 3) & ~3u;
    at += sprintf((char *)p + at, "070701%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x",
                  0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, (unsigned)sizeof(trailer), 0);
    memcpy(p + at, trailer, sizeof(trailer)); at += sizeof(trailer); at = (at + 3) & ~3u;
    *bytes = at;
    return p;
}

int main(int argc, char **argv)
{
    struct utsname u;
    char text[4096], cmdline[1100];
    unsigned kb, pb, mb, ab, ib, cb;
    int jump = argc == 4 && !strcmp(argv[3], "jump");
    if (argc != 3 && !jump) { fprintf(stderr, "usage: pw4-probe DIR REPORT [jump]\n"); return 2; }
    report = fopen(argv[2], "w");
    if (!report) { perror(argv[2]); return 2; }
    if (getuid() || uname(&u)) stop("not root");
    say("pw4-probe v2 mode=%s\nuname %s %s %s\n", jump ? "jump" : "probe", u.release, u.version, u.machine);
    if (strcmp(u.release, dd_pw4_5_18_1_1_1.kernel_release) || strcmp(u.version, dd_pw4_5_18_1_1_1.kernel_version))
        { errno = 0; stop("not the 5.18.1.1.1 stock kernel"); }
    if (read_text("/sys/power/pm_test", text, sizeof(text))) stop("pm_test");
    char *l = strchr(text, '['), *r = l ? strchr(l, ']') : NULL;
    if (!l || !r || r - l - 1 >= (long)sizeof(saved_test)) { errno = 0; stop("pm_test unreadable"); }
    memcpy(saved_test, l + 1, (size_t)(r - l - 1)); saved_test[r - l - 1] = 0;
    say("pm_test was [%s]\n", saved_test);

    unsigned char *kernel = read_blob(argv[1], "zImage", &kb);
    unsigned char *pub = read_blob(argv[1], "public-initramfs.gz", &pb);
    unsigned char *module = read_blob(argv[1], "pw3_first_kernel.ko", &mb);
    unsigned char *cmd = read_blob(argv[1], "cmdline.txt", &cb);
    if (cb >= sizeof(cmdline)) { errno = 0; stop("cmdline too long"); }
    memcpy(cmdline, cmd, cb); cmdline[cb] = 0;
    if ((l = strchr(cmdline, '\n'))) *l = 0;
    /* The device's part of the initramfs: a prepared archive when DIR has
     * one (device-archive.cpio, newc), else the one-file marker. */
    char archive_path[512];
    struct stat ast;
    unsigned char *archive;
    snprintf(archive_path, sizeof(archive_path), "%s/device-archive.cpio", argv[1]);
    if (!stat(archive_path, &ast)) archive = read_blob(argv[1], "device-archive.cpio", &ab);
    else archive = marker_archive(&ab);
    say("device archive %u bytes (%s)\n", ab, stat(archive_path, &ast) ? "marker" : "prepared");
    unsigned start = (pb + 3u) & ~3u;
    ib = start + ab;
    unsigned char *initramfs = calloc(1, ib);
    if (!initramfs) stop("out of memory");
    memcpy(initramfs, pub, pb); memcpy(initramfs + start, archive, ab);

    struct dd_buf tree = {0};
    struct dd_why why = {{0}, 0};
    if (dd_fdt("/", &dd_pw4_5_18_1_1_1, cmdline, 0x84000000u, ib, &tree, &why))
        { errno = 0; say("dd_fdt: %s (other_device=%d)\n", why.text, why.other_device); stop("device tree"); }
    say("device tree %zu bytes, initramfs slot %u bytes, kernel %u bytes\n", tree.size, ib, kb);

    if (syscall(SYS_init_module, module, (size_t)mb, "")) stop("init_module");
    module_loaded = 1;
    if (read_text("/sys/module/pw3_first_kernel/parameters/probe_only", text, sizeof(text)) ||
        strcmp(text, jump ? "0\n" : "1\n"))
        { errno = 0; stop(jump ? "jump needs a full module build" : "the module is not a probe-only build"); }
    unsigned major_num, minor_num;
    if (read_text("/sys/class/misc/pw3-first-kernel/dev", text, sizeof(text)) ||
        sscanf(text, "%u:%u", &major_num, &minor_num) != 2) stop("misc device");
    unlink("/tmp/pw3-first-kernel");
    if (mknod("/tmp/pw3-first-kernel", S_IFCHR | 0600, makedev(major_num, minor_num))) stop("mknod");
    device_fd = open("/tmp/pw3-first-kernel", O_RDWR);
    unlink("/tmp/pw3-first-kernel");
    if (device_fd < 0) stop("open");

    void *slot[3] = {kernel, tree.data, initramfs};
    unsigned size[3] = {kb, (unsigned)tree.size, ib};
    struct pws_begin b = {.version = 1};
    for (int i = 0; i < 3; ++i) {
        SHA256_CTX h; sha256_init(&h); sha256_update(&h, slot[i], size[i]); sha256_final(&h, b.images[i].sha256);
        b.images[i].bytes = size[i];
    }
    if (ioctl(device_fd, PWS_BEGIN, &b)) stop("BEGIN");
    for (int i = 0; i < 3; ++i)
        for (unsigned off = 0; off < size[i];) {
            unsigned n = size[i] - off > 65536 ? 65536 : size[i] - off;
            struct pws_put p = {.slot = (unsigned)i, .offset = off, .bytes = n,
                                .user_buffer = (uintptr_t)((unsigned char *)slot[i] + off)};
            if (ioctl(device_fd, PWS_PUT, &p)) stop("PUT");
            off += n;
        }
    if (ioctl(device_fd, PWS_SEAL, 0)) stop("SEAL (the build's pins or the device tree rules)");
    if (ioctl(device_fd, PWS_PREPARE, 0)) stop("PREPARE");
    struct pws_status st = {0};
    struct pws_plan_status ps = {0};
    if (ioctl(device_fd, PWS_STATUS, &st) || ioctl(device_fd, PWS_PLAN_STATUS, &ps)) stop("STATUS");
    say("prepared: payload_pages=%u control_pages=%u collision_pages=%u list_pages=%u footprint_verified=%u\n",
        st.payload_pages, st.control_pages, st.collision_pages, ps.list_pages, ps.boot_footprint_verified);

    if (write_text("/sys/power/pm_test", "core")) stop("pm_test core");
    say("PROBE: one pm_test=core suspend cycle\n");
    sync();
    int rc = ioctl(device_fd, PWS_NATIVE_PROBE, 0), err = errno;
    write_text("/sys/power/pm_test", saved_test);
    struct pws_native_report n = {0};
    if (ioctl(device_fd, PWS_NATIVE_REPORT, &n)) stop("REPORT");
    say("probe rc=%d errno=%d\n", rc, rc ? err : 0);
    say("report version=%u profile=%u pm_called=%u pm_result=%d suspend_samples=%u resume_samples=%u\n",
        n.version, n.profile, n.pm_called, n.pm_result, n.suspend_samples, n.resume_samples);
    say("cpu irq_fiq_masked=%u cpu_profile_valid=%u registers_valid=%u hardware_sampled=%u\n",
        n.irq_fiq_masked, n.cpu_profile_valid, n.registers_valid, n.hardware_sampled);
    say("backend check_performed=%u result=%d callback_rejected=%u\n",
        n.backend_check_performed, n.backend_result, n.callback_rejected);
    say("gates_clear=%u gates (sdma epdc lcdif_pix lcdif_apb pxp csi ssi1 ssi2 ssi3 usboh3 usdhc1 usdhc2 usdhc3 spdif):", n.gates_clear);
    for (int i = 0; i < PWS_GATE_COUNT; ++i) say(" %u", n.gate[i]);
    say("\ndcp ctrl=%#x stopped=%u reset=%u clock_gated=%u ccm_gated=%u stat=%#x chan=%#x csc=%u sema=%u,%u,%u,%u idle=%u\n",
        n.dcp_ctrl, n.dcp_stopped, n.dcp_reset, n.dcp_clock_gated, n.diagnostic_only, n.dcp_stat_activity,
        n.dcp_channel_enable, n.dcp_csc_enable, n.dcp_sema[0], n.dcp_sema[1], n.dcp_sema[2], n.dcp_sema[3], n.dcp_idle_observed);
    say("usb sampled=%u idle=%u\n", n.usb_sampled, n.usb_idle_observed);
    for (int i = 0; n.usb_sampled && i < 2; ++i)
        say("usb%d id=%#x cap=%#x dciversion=%#x dccparams=%#x cmd=%#x sts=%#x mode=%#x prime=%#x flush=%#x ep=%#x\n", i,
            n.usb[i].id, n.usb[i].cap, n.usb[i].dciversion, n.usb[i].dccparams, n.usb[i].command,
            n.usb[i].status, n.usb[i].mode, n.usb[i].prime, n.usb[i].flush, n.usb[i].endpoints);
    if (!read_text("/sys/module/pw3_first_kernel/parameters/probe_raw", text, sizeof(text)))
        say("raw midr,sctlr,ttbr0_attr,ttbcr,prrr,nmrr,dacr,cpsr,ccgr0..6,actlr,usdhc1_pres_state,usdhc1_read = %s", text);
    /* The module's own verdict: would it let a commit through? */
    int ready = !read_text("/sys/module/pw3_first_kernel/parameters/probe_ready", text, sizeof(text)) && !strcmp(text, "1\n");
    say("RESULT %s\n", rc ? "PROBE_FAILED" : ready ? "READY (the module would allow the jump)" : "REFUSED (the module would not allow the jump)");
    if (jump && !rc && ready) {
        if (write_text("/sys/power/pm_test", "core")) stop("pm_test core for the jump");
        say("JUMP: committing now; the Android kernel starts and this program does not return\n");
        fflush(report); fsync(fileno(report)); sync(); sleep(1); sync();
        rc = ioctl(device_fd, PWS_NATIVE_COMMIT, 0);
        err = errno;
        write_text("/sys/power/pm_test", saved_test);
        say("JUMP RETURNED rc=%d errno=%d (%s): nothing was started\n", rc, err, strerror(err));
        if (!ioctl(device_fd, PWS_NATIVE_REPORT, &n))
            say("after commit: pm_result=%d callback_rejected=%u backend_result=%d suspend_samples=%u\n",
                n.pm_result, n.callback_rejected, n.backend_result, n.suspend_samples);
    }
    cleanup();
    dd_free(&tree);
    fclose(report);
    return 0;
}
