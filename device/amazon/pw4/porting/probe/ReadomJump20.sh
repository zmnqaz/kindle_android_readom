#!/bin/sh
# Name: Readom Jump 20
# Author: readom PW4 port
# FOURTEENTH HANDOFF on the PW4: Jump 13 plus the PW4 USB gadget (configfs, adb), bcmdhd Wi-Fi and a USB register watcher in the kernel log.
# It mounts mmcblk1p2 read only for the panel waveform, loads the display
# driver, shows the start picture, then runs Readom's preinit, which maps
# pw4-android/system.img and data.img from the user FAT and starts Android.
# Nothing writes any stock partition. The Kindle restarts by itself when
# Android stops; the log is read back over SSH from /proc/printkbuf.

SRC=/mnt/us/readom-jump20
WORK=/tmp/readom-jump20
OUT=$SRC/jump20.txt
LOG=$SRC/jump20-shell.txt
: > "$LOG"

eips 1 1 "Readom jump 14: starting Android kernel test" 2>/dev/null
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

eips 1 2 "Jumping. Kindle restarts by itself in ~30 s" 2>/dev/null
"$WORK/pw4-probe" "$WORK" "$OUT" jump >> "$LOG" 2>&1

# Only reached when the module refused or the jump returned.
echo "pw4-probe exit $? (no jump happened)" >> "$LOG"
cat /sys/power/pm_test >> "$LOG" 2>&1
dmesg | tail -200 | sed -E 's/([0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}/<mac>/g' > "$SRC/dmesg-after.txt" 2>&1
rm -rf "$WORK"
lipc-set-prop com.lab126.cmd wirelessEnable 1 >> "$LOG" 2>&1
sync
eips 1 3 "No jump: see readom-jump20/jump20.txt" 2>/dev/null
