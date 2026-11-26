#!/bin/sh
# Name: Readom Jump 55
# Author: readom PW4 port
# Handoff into Android on the PW4. Up to three attempts; says on screen when a USB cable stops it.
# It mounts mmcblk1p2 read only for the panel waveform, loads the display
# driver, shows the start picture, then runs Readom's preinit, which maps
# pw4-android/system.img and data.img from the user FAT and starts Android.
# Nothing writes any stock partition. The Kindle restarts by itself when
# Android stops; the log is read back over SSH from /proc/printkbuf.

SRC=/mnt/us/readom-jump55
WORK=/tmp/readom-jump55
OUT=$SRC/jump55.txt
LOG=$SRC/jump55-shell.txt
: > "$LOG"

eips 1 1 "Readom Jump 55: preparing Android" 2>/dev/null
{
    date; uname -a; cat /proc/uptime
    echo "== before"; cat /sys/power/pm_test
    grep -E "MemFree|MemAvailable" /proc/meminfo
} >> "$LOG" 2>&1

# Wi-Fi off first: one bus master less. Its SDIO host (usdhc1) has to be idle
# when the jump is committed, or the module's syscore callback refuses (busy).
wifi_off() {
    lipc-set-prop com.lab126.cmd wirelessEnable 0 >> "$LOG" 2>&1
    sleep ${1:-3}
}
wifi_off 3

rm -rf "$WORK"; mkdir -p "$WORK"
for f in pw4-probe pw3_first_kernel.ko zImage public-initramfs.gz cmdline.txt device-archive.cpio; do
    cp "$SRC/$f" "$WORK/$f" || { echo "copy $f failed" >> "$LOG"; exit 1; }
done
chmod 755 "$WORK/pw4-probe"
sync
echo 1 > /proc/sys/vm/drop_caches

# Up to three attempts. The probe only returns when no jump happened; why is
# in $OUT (kept per attempt as $OUT.N):
#   RESULT REFUSED with usboh3 on   -> a USB cable is in: stop and say so;
#   RESULT REFUSED otherwise        -> a clock still on (Wi-Fi going down,
#                                      a screen update): wait and retry;
#   JUMP RETURNED ... errno=16      -> the callback refused at commit: retry;
#   RESULT PROBE_FAILED, pm_result=-16 before the callback (the library
#                                      indexer delaying the freezer): retry.
why="unknown"
attempt=1
while [ $attempt -le 3 ]; do
    echo "== attempt $attempt $(cat /proc/uptime)" >> "$LOG"
    "$WORK/pw4-probe" "$WORK" "$OUT" jump >> "$LOG" 2>&1
    status=$?
    cp "$OUT" "$OUT.$attempt" 2>/dev/null
    gates=$(sed -n 's/^gates_clear=.*spdif): //p' "$OUT")
    set -- $gates
    usb=${10:-0}
    if grep -q '^RESULT REFUSED' "$OUT"; then
        if [ "$usb" != "0" ]; then why="usb"; break; fi
        why="refused (clocks: $gates)"
    elif grep -q '^JUMP RETURNED .*errno=16' "$OUT"; then
        why="busy at commit"
    elif grep -q '^RESULT PROBE_FAILED$' "$OUT" &&
         grep -q 'pm_called=1 pm_result=-16 suspend_samples=0 resume_samples=0$' "$OUT"; then
        why="suspend busy"
    else
        why="status $status"; break
    fi
    grep -q '^cleanup module_unloaded=1 pm_test_restored=1 ' "$OUT" || break
    echo "attempt $attempt: $why; retrying" >> "$LOG"
    eips 1 2 "Attempt $attempt did not jump; retrying" 2>/dev/null
    wifi_off 5
    attempt=$((attempt + 1))
done

# Only reached when no jump happened.
echo "pw4-probe exit $status (no jump happened: $why)" >> "$LOG"
cat /sys/power/pm_test >> "$LOG" 2>&1
dmesg | tail -200 | sed -E 's/([0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}/<mac>/g' > "$SRC/dmesg-after.txt" 2>&1
rm -rf "$WORK"
lipc-set-prop com.lab126.cmd wirelessEnable 1 >> "$LOG" 2>&1
sync
if [ "$why" = "usb" ]; then
    eips 1 3 "No jump: unplug the USB cable, then tap again" 2>/dev/null
else
    eips 1 3 "No jump ($why): restart the Kindle, then tap again" 2>/dev/null
fi
