#!/bin/sh
# Name: Readom Log 7
# Author: readom PW4 port
# Read-only: saves the previous boot's kernel log (/proc/printkbuf, the
# Lab126 buffer at 0x9be00000) to /mnt/us/readom-jump7/printkbuf.txt.
# Run it right after the Kindle restarted from "Readom Jump 7".
OUT=/mnt/us/readom-jump7
mkdir -p "$OUT"
eips 1 1 "Readom log 7: saving previous kernel log" 2>/dev/null
cat /proc/printkbuf > "$OUT/printkbuf.raw" 2>/dev/null
tr '\000' ' ' < "$OUT/printkbuf.raw" | sed -E 's/([0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}/<mac>/g' > "$OUT/printkbuf.txt"
rm -f "$OUT/printkbuf.raw"
{ date; cat /proc/uptime; uname -a; dmesg | grep -iE "printk log|log_goal" ; } > "$OUT/log7-info.txt" 2>&1
sync
eips 1 2 "Done: /mnt/us/readom-jump7/printkbuf.txt" 2>/dev/null
