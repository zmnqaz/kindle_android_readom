#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Build the Android entry for the Kindle library from public build outputs, in the
build VM: the boot programs, the public initramfs, the handoff module
(build_module.py) and the launcher (build_launcher.py).

What only a device has (EPDC waveform, Wi-Fi calibration, ATAGs, the place of
its user store on the eMMC, its data image's UUID) is not an input: the
launcher makes it on the device at each start (devicedata/). The result can be
given to anyone with the same model and stock firmware.

There is one kind of entry: the one that installs a release. The test entry
and its scaffolding -- an init rc of its own, the test helper that held a
logcat capture open for the life of the device and sampled it on a timer, and
the switches for diagnostics, the framework start gate and staying awake --
went on 2026-10-29. A device nobody is watching should not carry what only a
watched one can use.

Usage: build_entry.py [--profile FILE] --export DIR --kernel DIR --system-path PATH --data PATH:BYTES
                      --installer DIR --screens DIR --release-url URL [--release-url URL] --ca-bundle FILE
                      --zstd-archive FILE
                      --name FILE.sh --title TITLE --output DIR [--dev-unpinned]
  --profile      the model and stock firmware (boot/profiles/); the PW3 one when not given.
                 Its "entry" says what goes into the initramfs and after the stock command
                 line, its "stock_files" what the helper checks on the stock system
  --export       export-aosp-images.py output (ramdisk.img, system.raw.img)
  --kernel       build-kernel.sh android-core output (zImage, modules.tar.gz, kernel.release);
                 every kernel module in the system image must be this kernel's, byte for byte
  --system-path  the system image on the user store, e.g. pw3-android/system.img
  --data         the data image on the user store and its size
  --installer …  the system image the entry installs first (build_launcher.py); all
                 five are required, because an entry that installs nothing is not
                 one this project makes. build_launcher.py can still build the
                 launcher without them, which is what one host test does
                 (boot/handoff/tests/run_client_test.py); that is not an entry
  --dev-unpinned for a profile whose stock_files are not read yet: a development entry
                 that prints what it finds instead of refusing (build_launcher.py)

The build host's tools and trees come from boot/build_host.py: the PW3 build VM
unless READOM_BUILD_HOST names a host file. A profile whose decompressor
footprint is derived from the build ("footprint_profile": "pw4") also needs the
host's android_kernel_build, and its handoff module is built against the host's
module_kernel_source and module_kernel_output.
"""
import argparse, gzip, hashlib, importlib.util, json, os, re, shutil, stat, subprocess, sys, tarfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
PORT = HERE.parent
sys.path.insert(0, str(HERE))
import build_host   # noqa: E402

PROFILE = HERE / 'profiles/pw3-5.16.2.1.1.json'
HOST = None   # build_host.load(), in main()
# The profile's "entry" (boot/profiles/): cmdline_android is what goes after
# the stock command line. The PW3 profile's, and why:
#
# After the stock command line (profile "cmdline_base"): the storage entry,
# Android's init with this device's rc, the system and data images as files on
# the user store. Nothing follows them: the test switches went with the test
# entry on 2026-10-29, which is why the entry contract is V10 and the key count
# PID 1 checks is three.
# loglevel=1 (after the stock "quiet", so it wins): only emergencies go to the
# serial console. The stock line keeps console=ttymxc0,115200, which nothing
# reads on a Kindle in use, and this 3.0 kernel writes the console with
# interrupts off: each error-level line held the CPU about 12 ms (Test V48, 20
# lines of 120 characters: 0.27 s against 0.03 s below the console level).
# Init's errors, the boot report and failing rc writes all came through there;
# the kernel log itself keeps every line.
# panic=5: without it, a panic spends two minutes destroying the evidence of
# what caused it.
#
# panic_timeout is 0 unless set, and 3.0's panic() then spins in mdelay() with
# interrupts enabled rather than stopping. The V81 capture of 2026-10-23 shows
# both halves of what that costs: `PC is at __delay+0x0/0xc` with
# `LR is at panic+0x194/0x1a0` is the spin, and `imx2_wdt_isr` in a stack above
# `show_regs` is the i.MX2 watchdog reaching the end of its own count and
# resetting the board -- which is the restart the reader saw about two minutes
# after the sleep screen. So a panic here does not hang forever; it leaves a
# dead screen for two minutes.
#
# The worse half is what happens during those two minutes. With interrupts on,
# the timer that caused the fault keeps firing, so the oops repeats, and the
# kernel ring is 32 KB: that capture holds three dumps and the text of the
# first oops is already gone. Rebooting on the first one keeps the only dump
# that names the cause.
#
# The evidence survives the restart: log_buf is written back to physical memory
# as each line is stored (patches/android-core, kernel/printk.c), the next
# kernel saves that memory as /proc/printkbuf, and the launcher files it at the
# next start -- which is how this panic was read at all. And a restart cannot
# loop, because it boots the stock Kindle: Android only starts when the reader
# taps the library entry.
#
# The PW4 profile has the same keys but panic=5, which its cmdline_base already
# carries (with softlockup_panic and hung_task_panic: the reasons are in that
# profile). It has no serial console at all, so loglevel=1 is not about the
# console's cost there; it is there so that the two models' Android kernels
# log the same way.
#
# "device_names" are the top-level names the device archive owns: never in a
# public initramfs, because they are made on the device (devicedata/) or are a
# person's.


def sha(data):
    return hashlib.sha256(data).hexdigest()


def run(command, **kwargs):
    subprocess.run(command, check=True, **kwargs)


def static_arm(path):
    elf = subprocess.check_output([HOST['arm_tools'] + 'readelf', '-h', '-lW', str(path)], text=True)
    if 'INTERP' in elf or 'Machine:                           ARM' not in elf:
        sys.exit('not a static ARM executable: ' + str(path))


def boot_programs(work, entry, release):
    """The restore program (PID 1 after the jump), against this kernel's UAPI headers.

    Compiled here from boot/handoff-init/, for every model: the PW4 one used to
    be a binary copied from one development jump to the next (pack-jump.sh)."""
    headers = work / 'uapi'
    (work / 'headers-build').mkdir()
    run(['make', '-s', '-C', str(HOST['android_kernel_source']), 'O=' + str(work / 'headers-build'), 'ARCH=arm',
         'headers_install', 'INSTALL_HDR_PATH=' + str(headers)], stdout=subprocess.DEVNULL)
    flags = [HOST['musl_gcc'], '-std=gnu11', '-static', '-Os', '-Wall', '-Wextra', '-Werror', '-isystem', str(headers / 'include'),
             '-I', str(HERE / 'handoff-init')]
    if entry['init_kernel_release']:
        # The program loads the drivers only under the kernel they were built
        # for, and compares uname with this string: a release built from a
        # tree with another local version would otherwise boot without a
        # screen or touch, every time, with nothing in the build to say so.
        if not re.fullmatch(r'[A-Za-z0-9._+-]{1,64}', release):
            sys.exit('kernel release unusable as a string: %r' % release)
        flags.append('-DKERNEL_RELEASE="%s"' % release)
    out = {}
    name = entry['handoff_init']
    if not re.fullmatch(r'[a-z0-9-]+', name):
        sys.exit('invalid handoff_init in the profile: %r' % name)
    run(flags + [str(HERE / 'handoff-init' / (name + '.c')), '-o', str(work / (name + '.unstripped'))])
    run([HOST['arm_tools'] + 'strip', '-o', str(work / name), str(work / (name + '.unstripped'))])
    static_arm(work / name)
    out[name] = (work / name).read_bytes()
    if entry['init_kernel_release'] and (release.encode() + b'\0') not in out[name]:
        sys.exit('%s does not carry the kernel release %s' % (name, release))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--profile', type=Path, default=PROFILE)
    ap.add_argument('--dev-unpinned', action='store_true')
    for name in ['export', 'kernel', 'output']:
        ap.add_argument('--' + name, type=Path, required=True)
    for name in ['system-path', 'data', 'name', 'title']:
        ap.add_argument('--' + name, required=True)
    for name in ['installer', 'screens', 'ca-bundle', 'zstd-archive']:
        ap.add_argument('--' + name, type=Path, required=True)
    ap.add_argument('--release-url', action='append', default=[], required=True,
                    help='the directory the payload sits in; repeat it for a second origin, in the order the installer tries them')
    a = ap.parse_args()
    global HOST
    HOST = build_host.load('Run as the builder in the build VM (tools/vm.sh shell).')
    # One directory of the user store holds both images, and the entry reads its
    # name from the configuration under the rule of preinit/TestFileFormat.h. The
    # device refuses to start when the two paths are not that one directory
    # ("system and data test directories differ"), so refuse it here instead.
    if not re.fullmatch(r'[a-z0-9][a-z0-9-]{0,47}/system\.img', a.system_path):
        sys.exit('invalid --system-path')
    if a.system_path.rsplit('/', 1)[0] != a.data.rsplit('/', 1)[0]:
        sys.exit('--system-path and --data must be in one directory of the user store')
    profile = json.loads(a.profile.read_text())
    entry = profile['entry']
    # The size of the data image is the model's (PW3 1.75 GiB, PW4 2 GiB).
    if not re.fullmatch(r'.+:[0-9]+', a.data) or int(a.data.rsplit(':', 1)[1]) != entry['data_bytes']:
        sys.exit('--data must be PATH:%d for this model' % entry['data_bytes'])
    if profile['module_profile'] not in (1, 3):
        sys.exit('an entry is built for a Kindle\'s stock kernel (module profile 1 or 3), not profile %r'
                 % profile['module_profile'])
    derived = profile['footprint_profile'] == 'pw4'
    if derived and not HOST['android_kernel_build']:
        sys.exit('this profile\'s decompressor footprint is derived from the kernel build: '
                 'android_kernel_build in the host file (READOM_BUILD_HOST)')
    if not (HOST['module_kernel_source'] or profile.get('module_kernel_source')):
        sys.exit('no stock kernel tree for the handoff module: module_kernel_source in the profile or the host file')
    spec = importlib.util.spec_from_file_location('pack', PORT / 'tools/preinit-pack/pack.py')
    pack = importlib.util.module_from_spec(spec); sys.modules['pack'] = pack; spec.loader.exec_module(pack)

    export = json.loads((a.export / 'build-result.json').read_text())
    if not export['build_completed'] or export['private_firmware_injected'] is not False:
        sys.exit('not a complete public export: ' + str(a.export))
    system = export['files']['system.raw.img']
    kernel = (a.kernel / 'zImage').read_bytes()
    release = (a.kernel / 'kernel.release').read_text().strip()
    with tarfile.open(a.kernel / 'modules.tar.gz') as archive:
        modules = {Path(m.name).name: archive.extractfile(m).read() for m in archive.getmembers() if m.name.endswith('.ko')}
    # The drivers PID 1 loads before Android (the EPDC, and on PW4 the touch
    # screen): this kernel's, or they do not load.
    handoff_modules = {}
    for name in entry['handoff_modules']:
        if name not in modules:
            sys.exit('the kernel has no %s' % name)
        if b'vermagic=%s ' % release.encode() not in modules[name]:
            sys.exit('%s is not of this kernel' % name)
        handoff_modules[name] = modules[name]
    epdc = handoff_modules[entry['handoff_modules'][0]]

    out = a.output
    out.mkdir(parents=True, exist_ok=False)
    work = Path(HOST['entry_work']) / out.name
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    # What ties the system to the kernel is its modules (/vendor/lib/modules): the
    # same files, or the kernel is not the system's.
    run(['debugfs', '-R', 'rdump /vendor/lib/modules %s' % work, str(a.export / 'system.raw.img')], stderr=subprocess.DEVNULL)
    system_modules = sorted((work / 'modules').glob('*.ko'))
    if not system_modules or any(modules.get(m.name) != m.read_bytes() for m in system_modules):
        sys.exit('the system image carries kernel modules other than --kernel\'s')
    programs = boot_programs(work, entry, release)

    # The public initramfs: this build's ramdisk with the boot programs, the
    # EPDC module, and the storage entry's file-based mount table.
    entries = pack.read_ramdisk(a.export / 'ramdisk.img')
    by_name = {e.name: e for e in entries}
    if any(n.split('/')[0] in entry['device_names'] or n.split('/')[0] == entry['handoff_dir'] for n in by_name):
        sys.exit('the export ramdisk already carries device or handoff files')
    init_sha = sha(by_name['init'].data)
    # Storage on image files of the user store (storage=testfile): /data is the data image's mapping.
    by_name['fstab.pw3'].data = by_name['fstab.pw3-testfile'].data
    storage = by_name['pw3-testfile-init']
    pack.static_arm_elf(storage.data, 'storage entry')
    # The command line below is written from this tree, and the entry that has
    # to accept it comes out of the export. An export made before the three-key
    # command line carries an entry that requires five keys: it would pack, and
    # it would not boot. The contract string is what tells them apart.
    if pack.CURRENT_TESTFILE_CONTRACT not in storage.data:
        raise SystemExit('the export\'s storage entry does not carry '
                         + pack.CURRENT_TESTFILE_CONTRACT.decode()
                         + '; it predates the three-key command line and the entry '
                           'it would build cannot boot. Build the export again.')
    storage.name = 'pw3-testfile-init.real'    # run by the restore program
    entries += [pack.Entry('pw3-testfile-init', storage.mode, storage.uid, storage.gid, programs[entry['handoff_init']]),
                pack.Entry(entry['handoff_dir'], stat.S_IFDIR | 0o755)]
    entries += [pack.Entry(entry['handoff_dir'] + '/' + name, stat.S_IFREG | 0o600, 0, 0, data)
                for name, data in handoff_modules.items()]
    taken = {e.name for e in entries}
    if any(name in taken for name, _, _ in pack.LOG_NODES):
        sys.exit('this initramfs already carries a log node; refusing a duplicate cpio member')
    entries += pack.log_nodes()
    entries.sort(key=lambda e: (e.name.count('/'), e.name))
    public = gzip.compress(pack.cpio_bytes(entries), mtime=0)
    # Read the archive back rather than trust what went in: the log nodes are
    # the one thing a storage entry needs before it can mount anything, so what
    # is checked is the bytes a kernel will unpack. read_ramdisk_bytes() is the
    # same reader, and it is the one that rejects a wrong member type or device.
    pack.verify_log_nodes(pack.read_ramdisk_bytes(public))
    (out / 'public-initramfs.cpio.gz').write_bytes(public)

    cmdline = ' '.join([profile['cmdline_base']] + entry['cmdline_android'])
    (out / 'cmdline.txt').write_text(cmdline + '\n')
    # The entry refuses a system image outside these bounds and then waits for
    # ever (preinit/TestFileFormat.h, pw3_test_size), so the build says so here
    # rather than letting the device be the one to find out.
    if not 256 << 20 <= system['bytes'] <= 1024 << 20 or system['bytes'] % 4096:
        sys.exit('the system image is %d bytes; the entry takes 256 MiB to 1 GiB, '
                 'a whole number of 4 KiB blocks' % system['bytes'])
    # pw3-preinit.conf without the lines of the device (devicedata.c adds fat_offset, fat_sizelimit, block=).
    conf = ''.join('%s=%s\n' % kv for kv in [
        ('schema', 1), ('system_path', a.system_path), ('system_size', system['bytes']), ('system_sha256', system['sha256']),
        ('init_sha256', init_sha), ('cmdline_sha256', sha(cmdline.encode())), ('kernel_release', release),
        ('adb_key_sha256', 'none')])   # adbd asks on the device and keeps the answer in /data/misc/adb
    (out / 'pw3-preinit.conf').write_text(conf)
    shutil.copyfile(a.kernel / 'zImage', out / 'zImage')

    # The handoff module: built against the stock kernel's tree, with the
    # decompressor footprint of this kernel. For PW3 build_module.py derives
    # the footprint itself, from the build VM's kernel tree; for a footprint
    # that is derived from a named build (PW4), it is derived here, from the
    # host's build of this same zImage, and checked to be of it.
    module_args, child_env = [], dict(os.environ)
    if derived:
        if sha((Path(HOST['android_kernel_build']) / 'arch/arm/boot/zImage').read_bytes()) != sha(kernel):
            sys.exit('android_kernel_build of the host file did not build --kernel\'s zImage')
        run([sys.executable, str(HERE / 'handoff/derive_kernel_footprint.py'), '--profile', profile['footprint_profile'],
             '--source', str(HOST['android_kernel_source']), '--build', str(HOST['android_kernel_build']),
             '--tools-prefix', HOST['arm_tools'], '--output', str(out / 'footprint')], stdout=subprocess.DEVNULL)
        module_args += ['--footprint', str(out / 'footprint/footprint.json')]
    for key in ('module_kernel_source', 'module_kernel_output'):
        if HOST[key]:
            module_args += ['--' + key[len('module_'):].replace('_', '-'), str(HOST[key])]
    if os.environ.get('READOM_BUILD_HOST'):
        # build_module.py's own switch for a host other than the build VM: the
        # directory of the AOSP compilers, whose kernel prefix it adds itself.
        child_env['READOM_ARM_TOOLS'] = str(Path(HOST['arm_tools']).parent) + '/'
        # And a fixed place to compile it in, as the VM's /home/builder/pw3-boot-module.
        child_env['READOM_MODULE_WORK'] = str(Path(HOST['entry_work']) / 'boot-module')
    run([sys.executable, str(HERE / 'build_module.py'), str(a.profile), '--kernel', str(out / 'zImage'),
         '--public-initramfs', str(out / 'public-initramfs.cpio.gz'), '--cmdline', str(out / 'cmdline.txt'),
         '--output', str(out / 'module'), *module_args], env=child_env)
    run([sys.executable, str(HERE / 'handoff/audit_cache_boundary.py'), '--module', str(out / 'module/pw3_first_kernel.ko'),
         '--profile', entry['cache_audit'], '--tools-prefix', HOST['arm_tools'],
         '--output', str(out / 'module/cache-audit')], stdout=subprocess.DEVNULL)
    audit = json.loads((out / 'module/cache-audit/result.json').read_text())
    if not audit['passed']:
        sys.exit('cache audit failed: ' + json.dumps(audit['checks']))
    run([sys.executable, str(HERE / 'build_launcher.py'), '--profile', str(a.profile),
         *(['--dev-unpinned'] if a.dev_unpinned else []),
         '--module', str(out / 'module'), '--kernel', str(out / 'zImage'),
         '--public-initramfs', str(out / 'public-initramfs.cpio.gz'), '--cmdline', str(out / 'cmdline.txt'),
         '--preinit-conf', str(out / 'pw3-preinit.conf'), '--data', a.data, '--name', a.name, '--title', a.title,
         '--output', str(out / 'launcher'),
         '--installer', str(a.installer), '--screens', str(a.screens),
         *[arg for u in a.release_url for arg in ('--release-url', u)],
         '--system-image', str(a.export / 'system.raw.img'), '--ca-bundle', str(a.ca_bundle),
         '--zstd-archive', str(a.zstd_archive)])

    launcher = json.loads((out / 'launcher/build-result.json').read_text())
    result = {'schema': 3, 'entry': 'release' if launcher['stock_pins_complete'] else 'development (--dev-unpinned)',
              'launcher': 'launcher/' + a.name, 'profile': a.profile.name,
              'export_receipt_sha256': sha((a.export / 'build-result.json').read_bytes()),
              'system': {'path': a.system_path, **system}, 'data': a.data, 'kernel_release': release, 'kernel_sha256': sha(kernel),
              'public_initramfs': {'bytes': len(public), 'sha256': sha(public), 'entries': len(entries)},
              'epdc_module_sha256': sha(epdc), 'programs_sha256': {n: sha(d) for n, d in programs.items()},
              'handoff_modules_sha256': {n: sha(d) for n, d in handoff_modules.items()},
              'system_modules_checked': len(system_modules),
              'cmdline': cmdline, 'module_cache_audit_checks': len(audit['checks']),
              'installer': launcher['installer'], 'contains_device_data': False}
    (out / 'build-result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=1))


if __name__ == '__main__':
    main()
