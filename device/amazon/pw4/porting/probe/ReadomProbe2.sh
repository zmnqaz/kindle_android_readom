#!/bin/sh
# Name: Readom Probe 2
# Author: readom PW4 port
# Read-only: second round of kernel/hardware facts into /mnt/us/readom-probe2/. Changes nothing on the system.

OUT=/mnt/us/readom-probe2
mkdir -p "$OUT"
LOG="$OUT/probe2.txt"
: > "$LOG"

sec() { echo "" >> "$LOG"; echo "===== $1 =====" >> "$LOG"; }
run() { sec "$*"; sh -c "$*" >> "$LOG" 2>&1; }

eips 1 1 "Readom probe 2 running..." 2>/dev/null

run 'date; id; uname -a'

# Full symbol table: kptr_restrict=0, so these are real addresses
cat /proc/kallsyms > "$OUT/kallsyms.txt" 2>/dev/null

# Kernel memory pages holding the functions and structures the handoff checks.
# lowmem is one to one here (PAGE_OFFSET == PHYS_OFFSET), so a symbol's address is its
# physical address; dump two whole pages per symbol from /dev/mem and slice on the PC.
dump_sym() {
    addr=$(grep -m1 " $1\$" /proc/kallsyms | cut -d' ' -f1)
    [ -n "$addr" ] || { echo "$1: not found" >> "$LOG"; return; }
    page=${addr%???}
    echo "$1: $addr page 0x$page" >> "$LOG"
    dd if=/dev/mem of="$OUT/mem-$1-$addr.bin" bs=4096 skip=$((0x$page)) count=2 2>> "$LOG"
}
sec 'memory dumps'
for s in pm_test_show outer_cache l2c210_flush_all l2c310_disable l2c310_flush_all_erratum l2c210_sync l2c310_resume soft_restart cpu_v7_proc_fin; do
    dump_sym "$s"
done
run 'grep -E " (l2c[0-9a-z_]*|outer_cache|l2x0_[a-z_]*)$" /proc/kallsyms'

# Display
run 'for f in name rotate stride mode modes virtual_size bits_per_pixel pan blank; do echo "$f: $(cat /sys/class/graphics/fb0/$f 2>&1)"; done'
run 'fbset 2>&1; fbset -i 2>&1'
run 'ls -laR /mnt/wfm 2>&1 | head -50'
run 'ls -la /lib/firmware/imx/epdc 2>&1'
run 'for p in /sys/module/mxc_epdc_v2_fb/parameters/*; do echo "$p: $(cat $p 2>&1)"; done'
run 'ls /sys/devices/platform/ 2>&1 | head; ls /sys/class/graphics/fb0/ 2>&1'

# Wi-Fi files and module parameters
run 'ls -laR /opt/brcm_4343w 2>&1 | head -60'
run 'for p in /sys/module/bcmdhd/parameters/*; do echo "$p: $(cat $p 2>&1)"; done'

# Partitions
run 'for d in /sys/block/mmcblk1/mmcblk1*; do echo "== $d"; cat $d/uevent; echo "start=$(cat $d/start 2>/dev/null) size=$(cat $d/size)"; done'
run 'cat /sys/block/mmcblk1/device/name /sys/block/mmcblk1/device/type /sys/block/mmcblk1/device/manfid 2>&1'

# Input
run 'cat /proc/bus/input/devices'
run 'cat /proc/gt9xx_config 2>&1 | head -40'
run 'for f in /sys/class/input/input2/*; do [ -f "$f" ] && echo "$f: $(cat $f 2>&1)"; done'

# Power, front light
run 'cat /sys/class/power_supply/bd71827_bat/uevent /sys/class/power_supply/bd71827_ac/uevent'
run 'cat /sys/class/backlight/bl/max_brightness /sys/class/backlight/bl/brightness /sys/class/backlight/bl/actual_brightness'
run 'cat /sys/power/state; cat /sys/power/pm_test; ls /sys/power'
run 'cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_available_frequencies /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>&1'
run 'for f in soc_id revision machine family; do echo "$f: $(cat /sys/devices/soc0/$f 2>&1)"; done'

# How stock loads its drivers
run 'grep -rnE "insmod|modprobe|g_mass_storage|bcmdhd|gt9xx|mxc_epdc" /etc/upstart /etc/init.d /etc/init 2>/dev/null | head -80'
run 'ls -la /etc/upstart 2>&1 | head -120'
run 'cat /proc/modules; for m in /sys/module/*/parameters; do echo "$m"; done 2>&1 | head -80'

sync
eips 1 2 "Done: /mnt/us/readom-probe2" 2>/dev/null
