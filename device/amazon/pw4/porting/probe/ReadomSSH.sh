#!/bin/sh
# Name: Readom SSH
# Author: readom PW4 port
# Starts an SSH server (dropbear, key login only, port 2222) on Wi-Fi so the
# port work can be driven from the computer. Nothing is installed: the binary
# runs from /tmp, the firewall rule and the no-screensaver setting last until
# the next restart. Tap again after every restart.

SRC=/mnt/us/readom-ssh
RUN=/tmp/readom-ssh
LOG=$SRC/ssh.txt
: > "$LOG"
eips 1 1 "Readom SSH: starting Wi-Fi and SSH...          " 2>/dev/null

lipc-set-prop com.lab126.cmd wirelessEnable 1 >> "$LOG" 2>&1
lipc-set-prop com.lab126.powerd preventScreenSaver 1 >> "$LOG" 2>&1

rm -rf "$RUN"; mkdir -p "$RUN"; chmod 700 "$RUN"
cp "$SRC/dropbearmulti" "$RUN/" && chmod 755 "$RUN/dropbearmulti"
# dropbear reads root's ~/.ssh/authorized_keys. On the Kindle root's home is
# normally in /tmp; if it is not writable, a /tmp directory is bound over it
# until the next restart.
H=$(awk -F: '$1=="root"{print $6}' /etc/passwd); [ -n "$H" ] || H=/tmp/root
if ! mkdir -p "$H/.ssh" 2>/dev/null || ! touch "$H/.ssh/.w" 2>/dev/null; then
    mkdir -p "$RUN/home/.ssh"; mount -o bind "$RUN/home" "$H" >> "$LOG" 2>&1; mkdir -p "$H/.ssh"
fi
rm -f "$H/.ssh/.w"
cp "$SRC/authorized_keys" "$H/.ssh/authorized_keys"
chmod 755 "$H"; chmod 700 "$H/.ssh"; chmod 600 "$H/.ssh/authorized_keys"
echo "root home $H" >> "$LOG"
# One host key for this Kindle, made on first use and kept in the user store.
if [ ! -s "$SRC/host_ed25519" ]; then
    "$RUN/dropbearmulti" dropbearkey -t ed25519 -f "$RUN/host_ed25519" >> "$LOG" 2>&1
    cp "$RUN/host_ed25519" "$SRC/host_ed25519"
fi
cp "$SRC/host_ed25519" "$RUN/host_ed25519"; chmod 600 "$RUN/host_ed25519"

# Up to a minute for an address.
IP=""
for i in $(seq 1 30); do
    IP=$(ifconfig wlan0 2>/dev/null | sed -n 's/.*inet addr:\([0-9.]*\).*/\1/p')
    [ -n "$IP" ] && break
    sleep 2
done

iptables -C INPUT -p tcp --dport 2222 -j ACCEPT 2>/dev/null || iptables -I INPUT -p tcp --dport 2222 -j ACCEPT >> "$LOG" 2>&1
[ -f "$RUN/pid" ] && kill "$(cat "$RUN/pid")" 2>/dev/null
"$RUN/dropbearmulti" dropbear -r "$RUN/host_ed25519" -p 2222 -s -g -P "$RUN/pid" >> "$LOG" 2>&1
sleep 1
{ date; echo "ip=$IP"; cat "$RUN/pid" 2>/dev/null; "$RUN/dropbearmulti" dropbearkey -y -f "$RUN/host_ed25519" 2>/dev/null | grep -E "^ssh-|Fingerprint"; } >> "$LOG" 2>&1
echo "$IP" > "$SRC/ip.txt"
sync
if [ -n "$IP" ] && [ -s "$RUN/pid" ]; then
    eips 1 1 "Readom SSH ready: $IP port 2222          " 2>/dev/null
else
    eips 1 1 "Readom SSH failed: see readom-ssh/ssh.txt   " 2>/dev/null
fi
