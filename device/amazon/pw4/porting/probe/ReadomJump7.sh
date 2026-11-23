#!/bin/sh
# Name: Readom Jump 7
# Author: readom PW4 port
# SEVENTH HANDOFF on the PW4: 3G/1G, Falcon off, eMMC by sdhci-esdhc-imx.
# Android kernel, device tree and initramfs, runs one PROBE and, only if the
# module says READY, jumps into the Android kernel. That kernel runs a small
# observer that writes to the kernel log and reboots by itself after ~10 s.
# Nothing mounts or writes any partition. After the reboot, tap
# "Readom Log 7" to save the Android kernel's log.

SRC=/mnt/us/readom-jump7
WORK=/tmp/readom-jump7
OUT=$SRC/jump7.txt
LOG=$SRC/jump7-shell.txt
: > "$LOG"

eips 1 1 "Readom jump 7: starting Android kernel test" 2>/dev/null
{
    date; uname -a; cat /proc/uptime
    echo "== before"; cat /sys/power/pm_test
    grep -E "MemFree|MemAvailable" /proc/meminfo
} >> "$LOG" 2>&1

# Wi-Fi off first: one bus master less.
lipc-set-prop com.lab126.cmd wirelessEnable 0 >> "$LOG" 2>&1
sleep 3

rm -rf "$WORK"; mkdir -p "$WORK"
for f in pw4-probe pw3_first_kernel.ko zImage public-initramfs.gz cmdline.txt; do
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
eips 1 3 "No jump: see readom-jump7/jump7.txt" 2>/dev/null
