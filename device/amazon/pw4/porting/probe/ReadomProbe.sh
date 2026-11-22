#!/bin/sh
# Name: Readom Probe
# Author: readom PW4 port
# Read-only: collects kernel/hardware facts into /mnt/us/readom-probe/. Changes nothing on the system.

OUT=/mnt/us/readom-probe
mkdir -p "$OUT"
LOG="$OUT/probe.txt"
: > "$LOG"

sec() { echo "" >> "$LOG"; echo "===== $1 =====" >> "$LOG"; }
run() { sec "$*"; sh -c "$*" >> "$LOG" 2>&1; }

eips 1 1 "Readom probe running..." 2>/dev/null

run 'date; id'
run 'cat /etc/version.txt /etc/prettyversion.txt 2>/dev/null; cat /mnt/us/system/version.txt'
run 'uname -a'
run 'cat /proc/version'
run 'cat /proc/cmdline'
run 'cat /proc/cpuinfo'
run 'cat /proc/meminfo'
run 'cat /proc/iomem'
run 'cat /proc/partitions'
run 'mount'
run 'df -k'
run 'cat /proc/modules'
run 'ls -la /lib/modules /lib/modules/* 2>&1 | head -200'
run 'cat /proc/sys/kernel/modules_disabled /proc/sys/kernel/kptr_restrict /proc/sys/kernel/dmesg_restrict'
run 'ls /proc/config.gz'
[ -r /proc/config.gz ] && zcat /proc/config.gz > "$OUT/config.txt" 2>/dev/null
run 'grep -E "MODULE_SIG|MODULES|VMSPLIT|PAGE_OFFSET|SMP|NR_CPUS|HOTPLUG_CPU|CACHE_L2X0|ARM_LPAE|OF=|MACH_|SOC_IMX|MXC_EPDC|FB_MXC|REGULATOR_|MFD_|BATTERY_|BRCM|ATH|WLAN|TOUCHSCREEN" '"$OUT"'/config.txt'
run 'grep -E " (pm_test_show|l2x0_[a-z_]+|module_sig_check|mod_verify_sig|machine_shutdown|soft_restart|cpu_v7_proc_fin|cpu_die|__cpu_disable|disable_nonboot_cpus|imx_.*cpu.*|mxc_epdc.*probe)$" /proc/kallsyms | head -100'
run 'cat /proc/device-tree/model; echo; cat /proc/device-tree/compatible | tr "\0" " "'
[ -d /proc/device-tree ] && (cd /proc/device-tree && find . > "$OUT/dt-tree.txt" 2>/dev/null)
[ -e /sys/firmware/fdt ] && cat /sys/firmware/fdt > "$OUT/fdt.dtb" 2>/dev/null
run 'for d in /sys/bus/i2c/devices/*; do echo "$d $(cat $d/name 2>/dev/null)"; done'
run 'for d in /sys/bus/platform/drivers/*; do echo "$d"; done'
run 'ls -l /sys/class/power_supply /sys/class/backlight /sys/class/regulator /sys/class/input /sys/class/net 2>&1'
run 'for d in /sys/class/input/input*; do echo "$d $(cat $d/name)"; done'
run 'cat /sys/class/graphics/fb0/name /sys/class/graphics/fb0/modes /sys/class/graphics/fb0/virtual_size /sys/class/graphics/fb0/bits_per_pixel'
run 'ls -la /lib/firmware /lib/firmware/* 2>&1 | head -100'
run 'cat /proc/interrupts'
run 'cat /proc/cpu/alignment 2>/dev/null; ls /sys/devices/system/cpu; cat /sys/devices/system/cpu/online'
run 'ls -la /dev/mmcblk* /dev/block 2>&1'
run 'cat /proc/mtd 2>/dev/null; ls /dev/mtd* 2>&1'
dmesg > "$OUT/dmesg.txt" 2>&1

sync
eips 1 2 "Done: /mnt/us/readom-probe" 2>/dev/null
