#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""The PW4 Android kernel as boot/build_entry.py takes it (--kernel DIR), from
the kernel's O= build directory: what tools/build-kernel.sh android-core
exports for the PW3.

  zImage           the kernel
  kernel.release   its release string (the modules' vermagic)
  kernel.config    its .config
  modules.tar.gz   every module of the build, debug info stripped as
                   stage-pw4-hardware.py strips the ones it stages into the
                   system image, flat (modules/NAME.ko)

build_entry.py then requires that the modules in the system image are these
byte for byte, and loads the EPDC and touch drivers from here into the public
initramfs. So the system image has to be built after staging from the same
kernel build: stage-pw4-hardware.py, then the AOSP build, then this.

The archive is made reproducibly (sorted, owner 0, mtime 0, gzip mtime 0), so
two exports of one build are the same bytes.

Usage: export-kernel.py --kernel-build DIR --strip ARM-STRIP --output DIR
"""
import argparse, gzip, hashlib, io, json, shutil, subprocess, sys, tarfile, tempfile
from pathlib import Path


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--kernel-build', type=Path, required=True, help='the O= directory of the PW4 Android kernel')
    ap.add_argument('--strip', required=True, help='the ARM strip of the toolchain')
    ap.add_argument('--output', type=Path, required=True)
    a = ap.parse_args()
    build = a.kernel_build
    release = (build / 'include/config/kernel.release').read_text().strip()
    zimage = build / 'arch/arm/boot/zImage'
    if not zimage.is_file():
        sys.exit('no zImage in ' + str(build))
    modules = sorted(p for p in build.rglob('*.ko') if '.tmp' not in p.name)
    names = [p.name for p in modules]
    if len(set(names)) != len(names):
        sys.exit('two modules of one name in the build: ' + ', '.join(sorted({n for n in names if names.count(n) > 1})))
    a.output.mkdir(parents=True, exist_ok=False)
    shutil.copyfile(zimage, a.output / 'zImage')
    shutil.copyfile(build / '.config', a.output / 'kernel.config')
    (a.output / 'kernel.release').write_text(release + '\n')
    raw = io.BytesIO()
    with tempfile.TemporaryDirectory() as temp, tarfile.open(fileobj=raw, mode='w', format=tarfile.USTAR_FORMAT) as tar:
        for path in modules:
            data = path.read_bytes()
            if b'vermagic=%s ' % release.encode() not in data:
                sys.exit('%s is not of kernel %s' % (path, release))
            stripped = Path(temp) / path.name
            subprocess.run([a.strip, '--strip-debug', '-o', str(stripped), str(path)], check=True)
            info = tarfile.TarInfo('modules/' + path.name)
            info.size, info.mode, info.mtime = stripped.stat().st_size, 0o644, 0
            with stripped.open('rb') as f:
                tar.addfile(info, f)
    (a.output / 'modules.tar.gz').write_bytes(gzip.compress(raw.getvalue(), mtime=0))
    result = {'schema': 1, 'kernel_release': release, 'zImage_sha256': sha(a.output / 'zImage'),
              'config_sha256': sha(a.output / 'kernel.config'), 'modules': len(modules),
              'modules_tar_sha256': sha(a.output / 'modules.tar.gz')}
    (a.output / 'build-result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=1))


if __name__ == '__main__':
    main()
