#!/bin/sh
# Collect a read-only USB/PMIC baseline from the currently running PW4 kernel.
# This does not access MMIO or PMIC registers and does not change device state.

OUT=/mnt/us/readom-usb-now
mkdir -p "$OUT" || exit 1
LOG="$OUT/report.txt"
TMP="$LOG.tmp"

{
	date
	echo '=== kernel ==='
	uname -a
	cat /proc/version 2>/dev/null
	cat /proc/cmdline 2>/dev/null
	echo '=== relevant dmesg ==='
	dmesg 2>/dev/null | grep -E 'pw4-trace|usb|ci_hdrc|bd71827|max14656|busfreq|charger' | tail -300
	echo '=== interrupts ==='
	cat /proc/interrupts 2>/dev/null
	echo '=== USB platform runtime PM ==='
	for d in /sys/bus/platform/devices/*ci_hdrc*; do
		[ -d "$d" ] || continue
		echo "$d"
		for f in power/runtime_status power/control power/wakeup; do
			if [ -r "$d/$f" ]; then
				printf '%s: ' "$f"
				cat "$d/$f"
			fi
		done
	done
	echo '=== UDC state ==='
	for f in /sys/class/udc/*/state; do
		[ -r "$f" ] || continue
		printf '%s: ' "$f"
		cat "$f"
	done
	echo '=== power supply state ==='
	for f in /sys/class/power_supply/*/online /sys/class/power_supply/*/status; do
		[ -r "$f" ] || continue
		printf '%s: ' "$f"
		cat "$f"
	done
} > "$TMP" 2>&1

mv "$TMP" "$LOG"
sync
echo "Saved $LOG"
