"""Local YouTube search/transcoding bridge for the YouTubeDSi homebrew.

Wire protocol v1: one command per TCP connection. SEARCH <utf8 query> returns
OK <count> followed by tab separated id/title lines. PLAY <id> and TEST return
OK STREAM, a <4s6I header, then fixed RGB332 + signed PCM16LE packets.
The stream is produced from remote URLs in memory; no video file is downloaded.
"""
from __future__ import annotations

import argparse
import json
import logging
import math
import re
import shutil
import socket
import socketserver
import struct
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent / "vendor"))
import yt_dlp

WIDTH, HEIGHT, FPS, RATE = 128, 96, 8, 16000
SAMPLES = RATE // FPS
VIDEO_BYTES, AUDIO_BYTES = WIDTH * HEIGHT, SAMPLES * 2
HEADER = struct.pack("<4s6I", b"YDS1", WIDTH, HEIGHT, FPS, RATE, SAMPLES, 1)
LOG = logging.getLogger("youtube-dsi")
PLAY_LOCK = threading.Lock()
STATUS_LOCK = threading.Lock()
STATUS = {"state": "ready", "last_query": "", "results": [], "last_video": "", "error": ""}
FFMPEG = "ffmpeg"


def state(**values):
    with STATUS_LOCK:
        STATUS.update(values)


def ydl_options(**extra):
    options = {
        "quiet": True, "no_warnings": False, "noplaylist": True,
        "socket_timeout": 20, "retries": 1, "extractor_retries": 1,
        "js_runtimes": {"node": {}},
    }
    options.update(extra)
    return options


def search(query):
    if not query.strip() or len(query) > 200:
        raise ValueError("Search must contain 1-200 characters")
    state(state="searching", last_query=query, error="")
    with yt_dlp.YoutubeDL(ydl_options(extract_flat="in_playlist", playlistend=8)) as ydl:
        data = ydl.extract_info("ytsearch8:" + query, download=False)
    results = [{"id": entry["id"], "title": entry.get("title", entry["id"])}
               for entry in data.get("entries", []) if entry and re.fullmatch(r"[\w-]{11}", entry.get("id", ""))]
    state(state="ready", results=results)
    return results


def streams(video_id):
    if not re.fullmatch(r"[A-Za-z0-9_-]{11}", video_id):
        raise ValueError("Invalid YouTube video ID")
    state(state="resolving", last_video=video_id, error="")
    with yt_dlp.YoutubeDL(ydl_options(
            format="worst[height>=144][vcodec!=none][acodec!=none]/worstvideo[height>=144]+worstaudio/worst")) as ydl:
        data = ydl.extract_info("https://www.youtube.com/watch?v=" + video_id, download=False)
    formats = data.get("requested_formats") or [data]
    video = next((f for f in formats if f.get("vcodec") != "none"), None)
    audio = next((f for f in formats if f.get("acodec") != "none"), None)
    if not video or not audio:
        raise ValueError("This video has no usable video/audio stream")
    return video, audio


def ffmpeg_input(stream):
    # Only yt-dlp's resolved HTTP media URLs are passed to ffmpeg, never commands.
    headers = stream.get("http_headers", {})
    header_text = "".join(f"{key}: {value}\r\n" for key, value in headers.items()
                          if key.lower() in ("user-agent", "referer", "origin"))
    return ["-rw_timeout", "20000000"] + (["-headers", header_text] if header_text else []) + ["-i", stream["url"]]


def read_exact(pipe, size):
    data = bytearray()
    while len(data) < size:
        part = pipe.read(size - len(data))
        if not part:
            break
        data.extend(part)
    return bytes(data)


def error_drain(pipe, tag, tails):
    # Drain diagnostics so FFmpeg can never deadlock on a full stderr pipe.
    for line in iter(pipe.readline, b""):
        text = line.decode("utf-8", "replace").strip()
        if text:
            tails[tag] = text[-400:]
            LOG.warning("%s: %s", tag, text)


def transcode(sock, video, audio, limit=None):
    base = [FFMPEG, "-hide_banner", "-nostdin", "-loglevel", "error"]
    vf = f"fps={FPS},scale={WIDTH}:{HEIGHT}:force_original_aspect_ratio=decrease,pad={WIDTH}:{HEIGHT}:(ow-iw)/2:(oh-ih)/2,format=rgb8"
    duration = ["-t", str(limit)] if limit else []
    commands = [
        base + ffmpeg_input(video) + duration + ["-an", "-vf", vf, "-pix_fmt", "rgb8", "-f", "rawvideo", "pipe:1"],
        base + ffmpeg_input(audio) + duration + ["-vn", "-ac", "1", "-ar", str(RATE), "-f", "s16le", "pipe:1"],
    ]
    processes = []
    tails = {}
    try:
        for tag, command in zip(("video", "audio"), commands):
            process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            processes.append(process)
            threading.Thread(target=error_drain, args=(process.stderr, tag, tails), daemon=True).start()
        # Read the first complete AV packet before announcing a successful stream.
        frame = read_exact(processes[0].stdout, VIDEO_BYTES)
        pcm = read_exact(processes[1].stdout, AUDIO_BYTES)
        if len(frame) != VIDEO_BYTES or not pcm:
            raise RuntimeError("FFmpeg could not start playback: " + "; ".join(tails.values()))
        sock.sendall(b"OK STREAM\n" + HEADER)
        state(state="streaming")
        count = 0
        while len(frame) == VIDEO_BYTES:
            sock.sendall(frame + pcm.ljust(AUDIO_BYTES, b"\0"))
            count += 1
            frame = read_exact(processes[0].stdout, VIDEO_BYTES)
            pcm = read_exact(processes[1].stdout, AUDIO_BYTES)
        LOG.info("Stream completed: %d frames (%.1f seconds)", count, count / FPS)
    finally:
        for process in processes:
            if process.poll() is None:
                process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            process.stdout.close()
            process.stderr.close()


def test_stream(sock, seconds=12):
    sock.sendall(b"OK STREAM\n" + HEADER)
    for index in range(seconds * FPS):
        # FFmpeg rgb8 stores RRRGGGBB, matching the DS palette.
        frame = bytes(((x // 16) << 5) | ((y // 12) << 2) | (index % 4)
                      for y in range(HEIGHT) for x in range(WIDTH))
        pcm = struct.pack("<" + "h" * SAMPLES, *(
            round(4000 * math.sin(2 * math.pi * 440 * (index * SAMPLES + j) / RATE))
            for j in range(SAMPLES)))
        sock.sendall(frame + pcm)


class Handler(socketserver.StreamRequestHandler):
    def handle(self):
        self.request.settimeout(180)
        lock_acquired = False
        try:
            raw = self.rfile.readline(1025)
            if len(raw) > 1024 or not raw.endswith(b"\n"):
                raise ValueError("Invalid request")
            command, _, argument = raw.decode("utf-8").strip().partition(" ")
            LOG.info("%s: %s %s", self.client_address[0], command, argument)
            if command == "HELLO":
                self.request.sendall(b"OK YouTubeDSi 1\n")
            elif command == "SEARCH":
                results = search(argument)
                lines = [f"OK {len(results)}\n"]
                for item in results:
                    # Console font is ASCII in v1; full titles remain in status.json.
                    title = re.sub(r"[\t\r\n]", " ", item["title"]).encode("ascii", "replace").decode()[:100]
                    lines.append(item["id"] + "\t" + title + "\n")
                self.request.sendall("".join(lines).encode("ascii"))
            elif command in ("PLAY", "TEST"):
                lock_acquired = PLAY_LOCK.acquire(blocking=False)
                if not lock_acquired:
                    raise ValueError("Another client is playing. Stop it first.")
                if command == "TEST":
                    test_stream(self.request)
                else:
                    video, audio = streams(argument)
                    transcode(self.request, video, audio)
            else:
                raise ValueError("Unknown command")
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            LOG.info("DSi disconnected")
        except Exception as exc:
            LOG.exception("Request failed")
            message = re.sub(r"[\r\n\t]", " ", str(exc))[-400:]
            state(state="error", error=message)
            try:
                self.request.sendall(("ERR " + message + "\n").encode("ascii", "replace"))
            except OSError:
                pass
        finally:
            if lock_acquired:
                PLAY_LOCK.release()
                with STATUS_LOCK:
                    if STATUS["state"] != "error":
                        STATUS["state"] = "ready"


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


class StatusHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/status.json":
            with STATUS_LOCK:
                data = json.dumps(STATUS, ensure_ascii=False).encode("utf-8")
            content_type = "application/json; charset=utf-8"
        else:
            data = ("<!doctype html><meta charset=utf-8><title>YouTube DSi</title>"
                    "<h1>YouTube DSi bridge</h1><p>DSi TCP port: 8765</p>"
                    "<p>Open <a href=/status.json>status.json</a> for results and errors.</p>"
                    "<p>Use TEST in the homebrew first, then search and select a video.</p>").encode()
            content_type = "text/html; charset=utf-8"
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, *args):
        pass


def main():
    global FFMPEG
    parser = argparse.ArgumentParser()
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--ffmpeg", default=shutil.which("ffmpeg") or "C:/ffmpeg/bin/ffmpeg.exe")
    args = parser.parse_args()
    FFMPEG = args.ffmpeg
    if not Path(FFMPEG).is_file() and not shutil.which(FFMPEG):
        parser.error("ffmpeg was not found")
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    status_server = ThreadingHTTPServer(("127.0.0.1", args.port + 1), StatusHandler)
    threading.Thread(target=status_server.serve_forever, daemon=True).start()
    LOG.info("DSi bridge: TCP %s:%d; PC status: http://127.0.0.1:%d", args.bind, args.port, args.port + 1)
    with Server((args.bind, args.port), Handler) as server:
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass
    status_server.shutdown()


if __name__ == "__main__":
    main()
