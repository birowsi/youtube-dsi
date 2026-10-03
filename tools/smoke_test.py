"""Exercise the actual bridge wire protocol, synthetic AV and live YouTube."""
import argparse
import json
import math
import socket
import struct
import sys
from pathlib import Path
from PIL import Image

parser = argparse.ArgumentParser()
parser.add_argument("--host", default="192.168.0.4")
parser.add_argument("--live", action="store_true")
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]


def connect(command):
    sock = socket.create_connection((args.host, 8765), 15)
    sock.settimeout(90)
    sock.sendall(command.encode("utf-8") + b"\n")
    return sock, sock.makefile("rb")


def check_stream(command, frames):
    sock, stream = connect(command)
    try:
        line = stream.readline()
        assert line == b"OK STREAM\n", line
        header = struct.unpack("<4s6I", stream.read(28))
        assert header == (b"YDS1", 128, 96, 8, 16000, 2000, 1), header
        rms, last = [], b""
        for _ in range(frames):
            raw = stream.read(12288 + 4000)
            assert len(raw) == 16288, len(raw)
            last = raw[:12288]
            pcm = struct.unpack("<2000h", raw[12288:])
            rms.append(math.sqrt(sum(x * x for x in pcm) / len(pcm)))
        assert max(rms) > 10, "No audio"
        if command == "TEST":
            assert stream.read(1) == b"", "Wrong synthetic duration"
        colors = bytes(component for p in last for component in
                       (((p >> 5) & 7) * 255 // 7, ((p >> 2) & 7) * 255 // 7, (p & 3) * 255 // 3))
        image = Image.frombytes("RGB", (128, 96), colors).resize((256, 192), Image.Resampling.NEAREST)
        name = "youtube-preview.png" if command.startswith("PLAY") else "test-preview.png"
        image.save(root / "docs/legacy" / name)
        print(command, "frames:", frames, "audio RMS:", round(sum(rms)/len(rms), 1), "preview:", name)
        return {"command": command, "frames": frames, "audio_rms": round(sum(rms)/len(rms), 1)}
    finally:
        stream.close()
        sock.close()


sock, stream = connect("HELLO")
assert stream.readline() == b"OK YouTubeDSi 1\n"
stream.close(); sock.close()
checks = [check_stream("TEST", 96)]
if args.live:
    sock, stream = connect("SEARCH DELTARUNE DSi Demo")
    line = stream.readline().decode().strip()
    assert line.startswith("OK "), line
    count = int(line.split()[1])
    assert count > 0
    entries = [stream.readline().decode().strip().split("\t", 1) for _ in range(count)]
    stream.close(); sock.close()
    print("Live search:", entries[:3])
    checks.append({"search_results": count, "first_result": entries[0]})
    checks.append(check_stream("PLAY 2Wi9SJYScKg", 48))
(root / "docs/legacy/verification.json").write_text(json.dumps(checks, ensure_ascii=False, indent=2), encoding="utf-8")
print("PASS: wire protocol and AV checks")
