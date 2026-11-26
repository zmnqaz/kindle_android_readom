#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""The device half of the PW4 initramfs, for development jumps. Superseded by the
release entry, which makes it on the device (boot/devicedata, dd_tree_archive;
device/amazon/pw4/porting/release.md); kept as the record of what the jumps ran with.

On the PW3 this half is made on the device by the launcher; for the PW4 port
it is still put together on the computer, from files read off the device
(never committed):
  --device-files DIR  with  epdc_ED060KC4.fw            (stock /lib/firmware/imx/epdc)
                            firmware/bcmdhd/fw_bcmdhd_sr.bin, nvram_wfo.txt  (stock /opt/brcm_4343w/wifi)
                            firmware/bluetooth/BCM4343A1.hcd  (optional: the chosen one of the stock
                              /opt/brcm_4343w/bluetooth/firmware/BCM4343A1_*.hcd, renamed)
and the two configuration files of the storage entry.

Placed as: /lib/firmware/imx/epdc/epdc_ED060KC4.fw (the EPDC driver's request;
pw4-handoff-init deletes it once the driver is up) and /firmware/bcmdhd/*
(bcmdhd's firmware_path/nvram_path, device/amazon/pw4/hardware/init.pw4.hardware.rc;
labeled by init.pw3-main.rc's restorecon of /firmware), and
/firmware/bluetooth/BCM4343A1.hcd (FwPatchFileName of
device/amazon/pw4/bluetooth/bt_vendor.conf; group bluetooth is given by
init.pw4.bluetooth.rc, the archive's entries are all root's).
"""
import argparse
from pathlib import Path

D = 0o40755


def newc(files):
    out = bytearray()
    for ino, (name, mode, data) in enumerate(files + [('TRAILER!!!', 0, b'')], 1):
        nb = name.encode() + b'\0'
        out += ('070701' + ''.join('%08x' % v for v in [ino, mode, 0, 0, 1, 0, len(data), 0, 0, 0, 0, len(nb), 0])).encode() + nb
        out += bytes(-len(out) % 4)
        out += data
        out += bytes(-len(out) % 4)
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--preinit-conf', type=Path, required=True)
    ap.add_argument('--testdata-conf', type=Path, required=True)
    ap.add_argument('--device-files', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    a = ap.parse_args()
    dev = a.device_files
    files = [('pw3-preinit.conf', 0o100644, a.preinit_conf.read_bytes()),
             ('pw3-testdata.conf', 0o100644, a.testdata_conf.read_bytes()),
             ('lib', D, b''), ('lib/firmware', D, b''), ('lib/firmware/imx', D, b''), ('lib/firmware/imx/epdc', D, b''),
             ('lib/firmware/imx/epdc/epdc_ED060KC4.fw', 0o100644, (dev / 'epdc_ED060KC4.fw').read_bytes())]
    wifi = dev / 'firmware/bcmdhd'
    if wifi.is_dir():
        files += [('firmware', D, b''), ('firmware/bcmdhd', 0o40750, b'')]
        files += [('firmware/bcmdhd/' + n, 0o100640, (wifi / n).read_bytes()) for n in ('fw_bcmdhd_sr.bin', 'nvram_wfo.txt')]
    bt = dev / 'firmware/bluetooth/BCM4343A1.hcd'
    if bt.is_file():
        if not any(n == 'firmware' for n, _, _ in files):
            files += [('firmware', D, b'')]
        # Readable by all: the root file system is read-only by the time
        # init.pw4.bluetooth.rc could give it to group bluetooth, and the HAL
        # runs as bluetooth (Jump 45). SELinux still limits who may read it.
        files += [('firmware/bluetooth', 0o40755, b''),
                  ('firmware/bluetooth/BCM4343A1.hcd', 0o100644, bt.read_bytes())]
    a.output.write_bytes(newc(files))
    for name, mode, data in files:
        print('%06o %8d %s' % (mode, len(data), name))


if __name__ == '__main__':
    main()
