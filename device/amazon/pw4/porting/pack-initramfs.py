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
    if any(e.name in {n for n, _, _ in pack.LOG_NODES} for e in entries):
        sys.exit('the ramdisk already carries a log node')
    entries += pack.log_nodes()
    public = gzip.compress(pack.cpio_bytes(entries), mtime=0)
    pack.verify_log_nodes(pack.read_ramdisk_bytes(public))

    import json
    profile = json.loads(a.profile.read_text())
    cmdline = ' '.join([profile['cmdline_base']] + CMDLINE_ANDROID)
    a.output.mkdir(parents=True, exist_ok=True)
    (a.output / 'public-initramfs.gz').write_bytes(public)
    (a.output / 'cmdline.txt').write_text(cmdline + '\n')
    print('public_initramfs_bytes=%d' % len(public))
    print('init_sha256=%s' % sha(by_name['init'].data))
    print('cmdline_sha256=%s' % sha(cmdline.encode()))
    print('kernel_release=%s' % a.kernel_release)


if __name__ == '__main__':
    main()
