#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""The PW4 public initramfs, as boot/build_entry.py makes the PW3 one -- for
development jumps only. boot/build_entry.py --profile boot/profiles/pw4-5.18.1.1.1.json
now makes the release one (device/amazon/pw4/porting/release.md), without the switches below.

From this build's ramdisk: the storage table for image files (fstab.pw3 is
fstab.pw3-testfile), the storage entry renamed to pw3-testfile-init.real with
the PW4 handoff restore (boot/handoff-init/pw4-handoff-init.c) in its place,
the display and touch drivers in /pw4-handoff, and the two log nodes. Nothing from the
device goes in here: the panel firmware and the configuration are the device
archive the probe appends on the Kindle.

Prints the values pw3-preinit.conf needs (init_sha256, cmdline_sha256).
"""
import argparse, gzip, hashlib, importlib.util, stat, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
CMDLINE_ANDROID = ['rdinit=/pw3-testfile-init', 'androidboot.hardware=pw3', 'androidboot.init_rc=/init.pw3-main.rc']


def sha(b):
    return hashlib.sha256(b).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--ramdisk', type=Path, required=True)
    ap.add_argument('--handoff-init', type=Path, required=True)
    ap.add_argument('--epdc-module', type=Path, required=True)
    ap.add_argument('--touch-module', type=Path, required=True)
    ap.add_argument('--kernel-release', required=True)
    ap.add_argument('--profile', type=Path, default=ROOT / 'boot/profiles/pw4-5.18.1.1.1.json')
    ap.add_argument('--output', type=Path, required=True, help='directory for public-initramfs.gz and cmdline.txt')
    ap.add_argument('--cmdline-extra', action='append', default=[],
                    help='a kernel parameter for a diagnostic jump (e.g. pw4_busfreq_off), appended')
    ap.add_argument('--boot-prop', action='append', default=[],
                    help='NAME=VALUE set on init for a diagnostic jump (e.g. service.adb.tcp.port=5555); '
                         'init reads /system/etc/prop.default, not the ramdisk /default.prop')
    ap.add_argument('--tcp-shell', type=int, metavar='PORT',
                    help='diagnostic: a root shell on this TCP port once booted (toybox nc under su); '
                         'Readom adbd is USB only (PW3_USB_ONLY_ADBD)')
    ap.add_argument('--adb-keys', type=Path,
                    help='a host adb public key file installed as /adb_keys (diagnostic, local only)')
    a = ap.parse_args()
    spec = importlib.util.spec_from_file_location('pack', ROOT / 'tools/preinit-pack/pack.py')
    pack = importlib.util.module_from_spec(spec); sys.modules['pack'] = pack; spec.loader.exec_module(pack)

    epdc = a.epdc_module.read_bytes()
    if b'vermagic=%s ' % a.kernel_release.encode() not in epdc:
        sys.exit('the EPDC module is not of kernel ' + a.kernel_release)
    touch = a.touch_module.read_bytes()
    if b'vermagic=%s ' % a.kernel_release.encode() not in touch:
        sys.exit('the touch module is not of kernel ' + a.kernel_release)
    init = a.handoff_init.read_bytes()
    pack.static_arm_elf(init, 'handoff restore')

    entries = pack.read_ramdisk(a.ramdisk)
    by_name = {e.name: e for e in entries}
    if any(n.split('/')[0] in ('pw4-handoff', 'lib') or n == 'pw3-testfile-init.real' for n in by_name):
        sys.exit('the ramdisk already carries handoff or device files')
    by_name['fstab.pw3'].data = by_name['fstab.pw3-testfile'].data
    storage = by_name['pw3-testfile-init']
    pack.static_arm_elf(storage.data, 'storage entry')
    if pack.CURRENT_TESTFILE_CONTRACT not in storage.data:
        sys.exit('the storage entry predates the current command line contract')
    storage.name = 'pw3-testfile-init.real'
    entries += [pack.Entry('pw3-testfile-init', storage.mode, storage.uid, storage.gid, init),
                pack.Entry('pw4-handoff', stat.S_IFDIR | 0o755),
                pack.Entry('pw4-handoff/mxc_epdc_v2_fb.ko', stat.S_IFREG | 0o600, 0, 0, epdc),
                pack.Entry('pw4-handoff/gt9xx-ts.ko', stat.S_IFREG | 0o600, 0, 0, touch)]
    if a.boot_prop:
        rc = by_name['init.pw3.usb.rc']
        lines = ['', '# Diagnostic jump: set by pack-initramfs.py --boot-prop', 'on init']
        lines += ['    setprop %s %s' % tuple(p.split('=', 1)) for p in a.boot_prop]
        rc.data = rc.data.rstrip(b'\n') + b'\n' + '\n'.join(lines).encode() + b'\n'
    if a.tcp_shell:
        # Diagnostic only: Readom's adbd has no TCP transport, so a plain root
        # shell on TCP. init may enter the shell domain (shell_exec), and shell
        # may run su, whose userdebug domain is permissive. The state goes to
        # the kernel log.
        rc = by_name['init.pw3.usb.rc']
        text = [
            '', '# Diagnostic jump: set by pack-initramfs.py --tcp-shell',
            'on property:sys.boot_completed=1',
            '    start pw4tcpsh',
            '    write /dev/kmsg "pw4-diag: boot usb=${sys.usb.config}"',
            'on property:init.svc.pw4tcpsh=*',
            '    write /dev/kmsg "pw4-diag: pw4tcpsh=${init.svc.pw4tcpsh}"',
            'on property:sys.usb.state=*',
            '    write /dev/kmsg "pw4-diag: usb state=${sys.usb.state} adbd=${init.svc.adbd}"',
            'service pw4tcpsh /system/bin/sh -c "exec /system/xbin/su 0 /system/bin/toybox nc -p %d -L /system/bin/sh"' % a.tcp_shell,
            '    disabled',
            '    user shell',
            '    group shell inet',
            '    seclabel u:r:shell:s0',
            '']
        rc.data = rc.data.rstrip(b'\n') + '\n'.join(text).encode()
    if a.adb_keys:
        if 'adb_keys' in by_name:
            sys.exit('the ramdisk already carries /adb_keys')
        entries.append(pack.Entry('adb_keys', stat.S_IFREG | 0o644, 0, 0, a.adb_keys.read_bytes()))
    if any(e.name in {n for n, _, _ in pack.LOG_NODES} for e in entries):
        sys.exit('the ramdisk already carries a log node')
    entries += pack.log_nodes()
    public = gzip.compress(pack.cpio_bytes(entries), mtime=0)
    pack.verify_log_nodes(pack.read_ramdisk_bytes(public))

    import json
    profile = json.loads(a.profile.read_text())
    cmdline = ' '.join([profile['cmdline_base']] + CMDLINE_ANDROID + a.cmdline_extra)
    a.output.mkdir(parents=True, exist_ok=True)
    (a.output / 'public-initramfs.gz').write_bytes(public)
    (a.output / 'cmdline.txt').write_text(cmdline + '\n')
    print('public_initramfs_bytes=%d' % len(public))
    print('init_sha256=%s' % sha(by_name['init'].data))
    print('cmdline_sha256=%s' % sha(cmdline.encode()))
    print('kernel_release=%s' % a.kernel_release)


if __name__ == '__main__':
    main()
