#!/data/data/com.termux/files/usr/bin/sh
cd ~/youtube-dsi/server || exit 1
[ -f relay.pid ] && kill "$(cat relay.pid)" 2>/dev/null && echo "Relay stopped"
rm -f relay.pid
termux-wake-unlock
