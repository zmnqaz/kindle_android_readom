#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""The PW4 public initramfs, as boot/build_entry.py makes the PW3 one.

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
    ap.add_argument('--tcp-adbd', type=int, metavar='PORT',
                    help='diagnostic: a second adbd (root) on this TCP port, started on boot and kept '
                         'running whatever the framework does with the USB adbd')
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
    if a.tcp_adbd:
        # Diagnostic only: a root adbd on TCP that the framework does not
        # stop, the host key in /data/misc/adb, a plain shell on TCP port+445
        # as a fallback, and the state of both written to the kernel log.
        rc = by_name['init.pw3.usb.rc']
        nc = a.tcp_adbd + 445
        text = [
            '', '# Diagnostic jump: set by pack-initramfs.py --tcp-adbd',
            'on init',
            '    setprop service.adb.tcp.port %d' % a.tcp_adbd,
            '    setprop service.adb.root 1',
            'on post-fs-data',
            '    mkdir /data/misc/adb 02750 system shell',
            '    copy /adb_keys /data/misc/adb/adb_keys',
            '    chown system shell /data/misc/adb/adb_keys',
            '    chmod 0640 /data/misc/adb/adb_keys',
            '    restorecon_recursive /data/misc/adb',
            'on property:sys.boot_completed=1',
            '    start pw4tcpadbd',
            '    start pw4ncsh',
            '    write /dev/kmsg "pw4-diag: boot tcp=${service.adb.tcp.port} root=${service.adb.root} usb=${sys.usb.config}"',
            'on property:init.svc.pw4tcpadbd=*',
            '    write /dev/kmsg "pw4-diag: pw4tcpadbd=${init.svc.pw4tcpadbd} adbd=${init.svc.adbd}"',
            'on property:init.svc.pw4ncsh=*',
            '    write /dev/kmsg "pw4-diag: pw4ncsh=${init.svc.pw4ncsh}"',
            'on property:sys.usb.state=*',
            '    write /dev/kmsg "pw4-diag: usb state=${sys.usb.state} config=${sys.usb.config} adbd=${init.svc.adbd}"',
            'service pw4tcpadbd /system/bin/adbd --root_seclabel=u:r:su:s0',
            '    disabled',
            '    seclabel u:r:adbd:s0',
            'service pw4ncsh /system/bin/toybox nc -p %d -L /system/bin/sh' % nc,
            '    disabled',
            '    user root',
            '    seclabel u:r:adbd:s0',
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
