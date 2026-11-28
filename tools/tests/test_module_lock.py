#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""What the PW4 module pins of the stock kernel (boot/build_module.py fdt_lock).

5.18.1 stopped with E101 because the module compared the stock kernel's build
string with 5.18.1.1.1's, although the two kernels are the same where the
module touches it. With the profile's any_firmware the string is not compared
(TARGET_ANY_FIRMWARE, boot/handoff/stage.c); the release and the stock
function hashes still are, and without any_firmware everything is as before."""
import copy
import importlib.util
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('build_module', ROOT / 'boot/build_module.py')
mod = importlib.util.module_from_spec(spec)
sys.argv = [sys.argv[0]]
spec.loader.exec_module(mod)

profile = json.loads((ROOT / 'boot/profiles/pw4-5.18.1.1.1.json').read_text())
failures = []


def check(cond, what):
    if not cond:
        failures.append(what)


assert profile.get('any_firmware') is True, 'the release profile is any_firmware'
lock = mod.fdt_lock(profile)
check('#define TARGET_ANY_FIRMWARE 1' in lock, 'any_firmware profile defines TARGET_ANY_FIRMWARE')
for kept in ('TARGET_UNAME_RELEASE', 'target_pm_show_sha256', 'target_outer_sha256', 'TARGET_COMPATIBLE'):
    check(kept in lock, 'any_firmware still pins ' + kept)

pinned = copy.deepcopy(profile)
pinned['any_firmware'] = False
check('TARGET_ANY_FIRMWARE' not in mod.fdt_lock(pinned), 'a pinned profile does not define it')
del pinned['any_firmware']
check('TARGET_ANY_FIRMWARE' not in mod.fdt_lock(pinned), 'nor one without the key')

stage = (ROOT / 'boot/handoff/stage.c').read_text()
i = stage.index('#elif PWS_PROFILE == 3')
block = stage[i:stage.index('#elif PWS_PROFILE == 4', i)]
check('#ifndef TARGET_ANY_FIRMWARE' in block and 'TARGET_UNAME_VERSION' in block,
      'stage.c compares the build string only without TARGET_ANY_FIRMWARE')
check('TARGET_UNAME_RELEASE' in block.split('#ifndef')[0], 'stage.c always compares the release')

if failures:
    print('\n'.join('FAIL ' + f for f in failures))
    sys.exit(1)
print('module-lock: ok')
