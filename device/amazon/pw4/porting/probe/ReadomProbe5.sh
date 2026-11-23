#!/bin/sh
# Name: Readom Probe 5
# Author: readom PW4 port
# First run of the PW4 handoff module, PROBE ONLY: the module is built without
# the jump. It checks the Android kernel, device tree and initramfs, runs one
# pm_test=core suspend/resume cycle, reports, unloads, and puts pm_test back.
# Writes only /mnt/us/readom-probe5/; touches no partition.

SRC=/mnt/us/readom-probe5
WORK=/tmp/readom-probe5
OUT=$SRC/probe5.txt
LOG=$SRC/probe5-shell.txt
: > "$LOG"

eips 1 1 "Readom probe 5 running (do not touch)" 2>/dev/null
{
    date; uname -a; cat /proc/uptime
    echo "== before"; cat /sys/power/pm_test; lsmod 2>/dev/null | grep -q bcmdhd && echo "wifi driver loaded" || echo "wifi driver not loaded"; grep -E "MemFree|MemAvailable" /proc/meminfo
    lsmod 2>/dev/null | head -20
} >> "$LOG" 2>&1

rm -rf "$WORK"; mkdir -p "$WORK"
for f in pw4-probe pw3_first_kernel.ko zImage public-initramfs.gz cmdline.txt; do
    cp "$SRC/$f" "$WORK/$f" || { echo "copy $f failed" >> "$LOG"; exit 1; }
done
chmod 755 "$WORK/pw4-probe"
sync
echo 1 > /proc/sys/vm/drop_caches
dmesg | tail -5 > "$SRC/dmesg-before.txt" 2>&1

"$WORK/pw4-probe" "$WORK" "$OUT" >> "$LOG" 2>&1
echo "pw4-probe exit $?" >> "$LOG"

{
    echo "== after"; cat /sys/power/pm_test; lsmod 2>/dev/null | grep pw3_first_kernel || echo "module not loaded"
    cat /proc/uptime
} >> "$LOG" 2>&1
dmesg | tail -200 | sed -E 's/([0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}/<mac>/g' > "$SRC/dmesg-after.txt" 2>&1
rm -rf "$WORK"
sync
eips 1 2 "Done: /mnt/us/readom-probe5" 2>/dev/null
