# SPDX-License-Identifier: GPL-2.0-only
"""Where the build host keeps the tools and trees the entry is built from.

The release scripts (boot/build_entry.py, boot/build_launcher.py,
installer/build_payload.py, installer/screens/render_screens.py) used to name
the PW3 build VM outright: its host name, /home/builder/... A second model is
built on another host, so the paths are read from a JSON file named by the
environment variable READOM_BUILD_HOST, key by key over the defaults below.
Without the variable nothing changes: the defaults are the PW3 build VM's
values, exactly as they were written into the scripts before.

The variable, not an argument, because build_entry.py runs build_launcher.py
and build_module.py as child processes and every one of them has to agree on
the host; an environment variable reaches all of them unchanged.

Keys (all optional; a host file gives the ones that differ):
  hostname               the host the build runs on, checked (platform.node())
  arm_tools              prefix of the ARM binutils: readelf, strip, nm, objdump
  musl_gcc               the static ARM musl compiler (tools/build-test-libc.sh)
  android_kernel_source  the Android kernel's source tree: its UAPI headers are
                         what the boot programs are compiled against
  android_kernel_build   its O= build directory, for a model whose decompressor
                         footprint is derived from the build (footprint "pw4")
  module_kernel_source   the stock kernel tree the handoff module is built
  module_kernel_output   against, and its build directory; overrides the profile
  entry_work             scratch directory of build_entry.py
  aosp_out               the AOSP out/ directory of the build (make_ext4fs,
                         file_contexts.bin of the product)
"""
import json, os, platform, sys

DEFAULTS = {
    'hostname': 'lima-pw3',
    'arm_tools': '/home/builder/toolchains/aosp-arm-gcc49/bin/arm-linux-androideabi-',
    'musl_gcc': '/home/builder/toolchains/musl-arm/bin/musl-gcc',
    'android_kernel_source': '/home/builder/pw3-build/linux-android-core',   # tools/build-kernel.sh android-core
    'android_kernel_build': None,
    'module_kernel_source': None,    # None: the profile's
    'module_kernel_output': None,
    'entry_work': '/home/builder/pw3-entry',
    'aosp_out': '/home/builder/android-src/out',
}


def load(what='Run as the builder in the build VM (tools/vm.sh shell).'):
    """The host's settings, after checking this is that host and not root."""
    host = dict(DEFAULTS)
    path = os.environ.get('READOM_BUILD_HOST')
    if path:
        with open(path) as f:
            given = json.load(f)
        unknown = sorted(set(given) - set(DEFAULTS))
        if unknown:
            sys.exit('%s: unknown keys %s' % (path, ', '.join(unknown)))
        host.update(given)
    if platform.node() != host['hostname'] or os.geteuid() == 0:
        sys.exit(what if not path else
                 'Run as an ordinary user on %s (READOM_BUILD_HOST=%s).' % (host['hostname'], path))
    return host
