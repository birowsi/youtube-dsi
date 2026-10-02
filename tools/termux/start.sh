#!/data/data/com.termux/files/usr/bin/sh
# Start the quality relay (HQ/HQ2, TCP+UDP 8767) in the background.
# Also installed as ~/.termux/boot/start-youtube-dsi.sh, so Termux:Boot runs it at power-on.
termux-wake-lock
cd ~/youtube-dsi/server || exit 1
if [ -f relay.pid ] && kill -0 "$(cat relay.pid)" 2>/dev/null; then
    echo "Relay already running (pid $(cat relay.pid))"
    exit 0
fi
nohup python server_quality.py --bind 0.0.0.0 --port 8767 --ffmpeg ffmpeg >> relay.log 2>&1 &
echo $! > relay.pid
echo "Relay started (pid $!), log: ~/youtube-dsi/server/relay.log"
