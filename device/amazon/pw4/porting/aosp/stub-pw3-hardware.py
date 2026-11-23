#!/usr/bin/env python3
"""Compile-test only: stage vendor/amazon/pw3-hardware like export_component.py, without kernel modules."""
import shutil, sys
from pathlib import Path
HERE = Path(__file__).resolve().parents[5] / 'tools/kernel-hardware'
out = Path(sys.argv[1])
if out.exists(): shutil.rmtree(out)
out.mkdir(parents=True)
for n in ('init.pw3.usb.modules.rc', 'init.pw3.hardware.rc', 'init.pw3.epdc.rc'):
    t = out/'vendor/etc/init'/n; t.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(HERE/n, t)
shutil.copyfile(HERE/'ueventd.pw3.fragment.rc', out/'vendor/etc/ueventd.pw3.fragment.rc')
for d in ('native', 'policy', 'userspace-patch'):
    for s in sorted((HERE/d).rglob('*')):
        if not s.is_file() or '__pycache__' in s.parts or 'patched-source' in s.parts: continue
        t = out/s.relative_to(HERE); t.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(s, t)
shutil.copyfile(HERE/'COPYING.kernel', out/'COPYING.kernel')
(out/'Android.mk').write_text('LOCAL_PATH := $(call my-dir)\ninclude $(LOCAL_PATH)/native/Android.mk\n')
copies = [p for p in sorted((out/'vendor').rglob('*')) if p.is_file()]
(out/'pw3-usb-component.mk').write_text('PRODUCT_PACKAGES += pw3-usb-check\nPRODUCT_COPY_FILES += \\\n' +
    ' \\\n'.join('    $(PW3_USB_COMPONENT_ROOT)/%s:$(TARGET_COPY_OUT_VENDOR)/%s' % (p.relative_to(out), p.relative_to(out/'vendor')) for p in copies) + '\n')
(out/'.pw3-owned-copy').touch()
print('staged', out)
