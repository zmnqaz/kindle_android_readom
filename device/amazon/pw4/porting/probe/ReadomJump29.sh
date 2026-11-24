#!/bin/sh
# Name: Readom Jump 29
# Author: readom PW4 port
# FOURTEENTH HANDOFF on the PW4: Jump 13 plus the PW4 USB gadget (configfs, adb), bcmdhd Wi-Fi and a USB register watcher in the kernel log.
# It mounts mmcblk1p2 read only for the panel waveform, loads the display
# driver, shows the start picture, then runs Readom's preinit, which maps
# pw4-android/system.img and data.img from the user FAT and starts Android.
# Nothing writes any stock partition. The Kindle restarts by itself when
# Android stops; the log is read back over SSH from /proc/printkbuf.

SRC=/mnt/us/readom-jump29
WORK=/tmp/readom-jump29
OUT=$SRC/jump29.txt
LOG=$SRC/jump29-shell.txt
: > "$LOG"

eips 1 1 "Readom Jump 29: preparing Android" 2>/dev/null
{
    date; uname -a; cat /proc/uptime
    echo "== before"; cat /sys/power/pm_test
    grep -E "MemFree|MemAvailable" /proc/meminfo
} >> "$LOG" 2>&1

# Wi-Fi off first: one bus master less.
lipc-set-prop com.lab126.cmd wirelessEnable 0 >> "$LOG" 2>&1
sleep 3

rm -rf "$WORK"; mkdir -p "$WORK"
for f in pw4-probe pw3_first_kernel.ko zImage public-initramfs.gz cmdline.txt device-archive.cpio; do
    cp "$SRC/$f" "$WORK/$f" || { echo "copy $f failed" >> "$LOG"; exit 1; }
done
chmod 755 "$WORK/pw4-probe"
sync
echo 1 > /proc/sys/vm/drop_caches

"$WORK/pw4-probe" "$WORK" "$OUT" jump >> "$LOG" 2>&1
status=$?

# A stock userspace task can delay the freezer (e.g. the library indexer
# waiting on FUSE). Retry once only if suspend never reached the callback
# and the probe restored pm_test and unloaded its module.
if [ "$status" -eq 0 ] &&
    grep -q '^RESULT PROBE_FAILED$' "$OUT" &&
    grep -q 'pm_called=1 pm_result=-16 suspend_samples=0 resume_samples=0$' "$OUT" &&
    grep -q '^cleanup module_unloaded=1 pm_test_restored=1 ' "$OUT"; then
    echo "Suspend was busy before the callback; retrying once after 5 seconds." >> "$LOG"
    sleep 5
    "$WORK/pw4-probe" "$WORK" "$OUT" jump >> "$LOG" 2>&1
    status=$?
fi

# Only reached when the module refused or the jump returned.
echo "pw4-probe exit $status (no jump happened)" >> "$LOG"
cat /sys/power/pm_test >> "$LOG" 2>&1
dmesg | tail -200 | sed -E 's/([0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}/<mac>/g' > "$SRC/dmesg-after.txt" 2>&1
rm -rf "$WORK"
lipc-set-prop com.lab126.cmd wirelessEnable 1 >> "$LOG" 2>&1
sync
eips 1 3 "No jump: see readom-jump29/jump29.txt" 2>/dev/null
