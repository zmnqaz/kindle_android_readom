#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""The PW4 handoff end to end in QEMU (mcimx6ul-evk): a first Linux built like
the PW4 stock kernel (4.1.15, device tree, 2G/2G, SMP_ON_UP, RAM at
0x80000000) loads the module (profile 4), makes the device tree from its own
/sys/firmware/fdt as the device would, is refused what breaks the build's
rules, runs two PROBEs and commits; the second Linux is a QEMU build of the
Android kernel (3G/1G, gzip), whose init checks that it got the build's
command line and both halves of the initramfs.

What it does not show: the PW4's own clocks, DMA masters, PL310 or Falcon
(QEMU's i.MX6UL has none of them); see device/amazon/pw4/porting/handoff.md.

Usage: run_qemu_pw4.py --stage1-source DIR --stage1-build DIR --stage2-source DIR
                       --stage2-build DIR --tools DIR --qemu-image IMAGE --output DIR
The ARM user-space tools and QEMU run in IMAGE (docker): arm-linux-gnueabihf-gcc,
cpio, gzip, qemu-system-arm.
"""
import argparse, gzip, json, os, subprocess, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
BOOT = HERE.parent.parent
PROFILE = BOOT / 'profiles/qemu-pw4.json'


def run(cmd, **kw):
    return subprocess.run(cmd, check=True, **kw)


def newc(files):
    """An uncompressed newc archive of {path: bytes} (regular files, mode 0644/0755)."""
    out = bytearray()
    ino = 1
    def entry(name, mode, data):
        nonlocal ino
        name_b = name.encode() + b'\0'
        hdr = '070701' + ''.join('%08x' % v for v in [ino, mode, 0, 0, 1, 0, len(data), 0, 0, 0, 0, len(name_b), 0])
        out.extend(hdr.encode()); out.extend(name_b)
        out.extend(bytes(-len(out) % 4)); out.extend(data); out.extend(bytes(-len(out) % 4))
        ino += 1
    for path, (mode, data) in files.items():
        entry(path, mode, data)
    entry('TRAILER!!!', 0, b'')
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    for name in ['stage1-source', 'stage1-build', 'stage2-source', 'stage2-build', 'tools', 'output']:
        ap.add_argument('--' + name, type=Path, required=True)
    ap.add_argument('--qemu-image', required=True)
    ap.add_argument('--timeout', type=int, default=300)
    a = ap.parse_args()
    out = a.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    docker = ['docker', 'run', '--rm', '-u', '%d:%d' % (os.getuid(), os.getgid()),
              '-v', '%s:%s' % (out, out), '-v', '%s:%s' % (BOOT, BOOT), '-w', str(out), a.qemu_image]
    profile = json.loads(PROFILE.read_text())
    cmdline = profile['cmdline_base']
    (out / 'cmdline.txt').write_text(cmdline + '\n')

    # Both inits, static, from the same source.
    common = ['-static', '-O2', '-Wall', '-Werror', '-I', str(BOOT / 'handoff'), str(HERE / 'test-init-pw4.c'),
              str(BOOT / 'handoff/sha256.c'), str(BOOT / 'handoff/pw4_fdt.c'), str(BOOT / 'handoff/pw3_plan.c')]
    run(docker + ['arm-linux-gnueabihf-gcc', '-o', str(out / 'init1')] + common)
    run(docker + ['arm-linux-gnueabihf-gcc', '-DSECONDARY', '-o', str(out / 'init2')] + common)

    # The second Linux's public initramfs (gzip) and the device's archive (newc).
    public = gzip.compress(newc({'init': (0o100755, (out / 'init2').read_bytes()),
                                 'proc': (0o040755, b''), 'sys': (0o040755, b''),
                                 'public-marker': (0o100644, b'public\n')}), mtime=0)
    (out / 'public-initramfs.gz').write_bytes(public)
    (out / 'device-archive.cpio').write_bytes(newc({'device-marker': (0o100644, b'device\n')}))

    # The decompressor's footprint, from the second kernel's own build files.
    tools_prefix = str(a.tools / 'arm-linux-androideabi-')
    run([sys.executable, str(BOOT / 'handoff/derive_kernel_footprint.py'), '--profile', 'pw4',
         '--source', str(a.stage2_source), '--build', str(a.stage2_build), '--tools-prefix', tools_prefix,
         '--output', str(out / 'footprint')], stdout=subprocess.DEVNULL)
    zimage = a.stage2_build / 'arch/arm/boot/zImage'
    env = dict(os.environ, READOM_ARM_TOOLS=str(a.tools) + '/', PATH=str(a.tools) + ':' + os.environ['PATH'])
    run([sys.executable, str(BOOT / 'build_module.py'), str(PROFILE), '--kernel', str(zimage),
         '--public-initramfs', str(out / 'public-initramfs.gz'), '--cmdline', str(out / 'cmdline.txt'),
         '--footprint', str(out / 'footprint/footprint.json'), '--kernel-source', str(a.stage1_source),
         '--kernel-output', str(a.stage1_build), '--output', str(out / 'module')], env=env, stdout=subprocess.DEVNULL)

    # The first Linux's initramfs.
    first = newc({'init': (0o100755, (out / 'init1').read_bytes()),
                  'proc': (0o040755, b''), 'sys': (0o040755, b''), 'dev': (0o040755, b''),
                  'pw3_first_kernel.ko': (0o100644, (out / 'module/pw3_first_kernel.ko').read_bytes()),
                  'target-zImage': (0o100644, zimage.read_bytes()),
                  'target-public-initramfs': (0o100644, public),
                  'target-device-archive': (0o100644, (out / 'device-archive.cpio').read_bytes()),
                  'target-cmdline': (0o100644, (cmdline + '\n').encode())})
    (out / 'stage1-initramfs.gz').write_bytes(gzip.compress(first, mtime=0))
    for f in ['arch/arm/boot/zImage', 'arch/arm/boot/dts/imx6ul-14x14-evk.dtb']:
        (out / ('stage1-' + Path(f).name)).write_bytes((a.stage1_build / f).read_bytes())

    log = out / 'qemu.log'
    name = 'pw4-handoff-%d' % os.getpid()
    with log.open('wb') as f:
        try:
            subprocess.run(['docker', 'run', '--rm', '--name', name, '-v', '%s:%s' % (out, out), a.qemu_image,
                            'qemu-system-arm', '-M', 'mcimx6ul-evk', '-m', '512M', '-nographic', '-no-reboot',
                            '-kernel', str(out / 'stage1-zImage'), '-dtb', str(out / 'stage1-imx6ul-14x14-evk.dtb'),
                            '-initrd', str(out / 'stage1-initramfs.gz'),
                            '-append', 'console=ttymxc0,115200 rdinit=/init panic=-1'],
                           stdout=f, stderr=subprocess.STDOUT, timeout=a.timeout)
        except subprocess.TimeoutExpired:
            pass
        finally:
            subprocess.run(['docker', 'kill', name], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    text = log.read_text(errors='replace')
    want = ['PW4_FIRST_KERNEL_READY', 'PW4_SECOND_KERNEL_PASS']
    for line in text.splitlines():
        if line.startswith(('PW4_', 'FAIL line=')):
            print(line)
    missing = [w for w in want if w not in text]
    if missing:
        sys.exit('missing %s; see %s' % (', '.join(missing), log))
    print('PW4 handoff in QEMU: first kernel ready, second kernel passed')


if __name__ == '__main__':
    main()
