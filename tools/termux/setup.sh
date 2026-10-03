#!/data/data/com.termux/files/usr/bin/sh
# Install the YouTube DSi relay on an Android phone (Termux).
# Expects the server files in ~/youtube-dsi/server (see docs/HQ2-GUIDE.md).
set -eu
yes | pkg upgrade -y
# Skip recommended extras (clang, llvm, ...): about 900 MB less.
apt-get install -y --no-install-recommends python python-pip python-pillow ffmpeg nodejs termux-tools
python -m pip install --upgrade yt-dlp yt-dlp-ejs
# audioop left the standard library in Python 3.13.
# audioop-lts is built from source here, so it needs a compiler.
python -c "import audioop" 2>/dev/null || { apt-get install -y clang && python -m pip install audioop-lts; }
mkdir -p ~/.termux/boot
cp ~/youtube-dsi/tools/termux/start.sh ~/.termux/boot/start-youtube-dsi.sh
chmod +x ~/.termux/boot/start-youtube-dsi.sh ~/youtube-dsi/tools/termux/*.sh
echo "Setup done. Start now with: ~/youtube-dsi/tools/termux/start.sh"
