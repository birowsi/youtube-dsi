@echo off
cd /d "%~dp0"
py -3.11 server\server_quality.py --bind 0.0.0.0 --port 8767
pause
