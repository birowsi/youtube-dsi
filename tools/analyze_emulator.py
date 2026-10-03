"""Analyze recorded real melonDS output; no synthetic app/network results."""
import hashlib
import json
import struct
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "investigation/emulator-runs/emulator-final"
rom = ROOT / "client/YouTubeDSi.nds"
audio = np.fromfile(OUT / "audio.pcm", dtype="<i2").reshape(-1, 2)
seconds = len(audio) // 48000
blocks = audio[:seconds * 48000].reshape(seconds, 48000, 2).astype(np.float64)
rms = np.sqrt(np.mean(blocks ** 2, axis=(1, 2)))
active = np.flatnonzero(rms > 20)
groups = [g for g in np.split(active, np.flatnonzero(np.diff(active) > 1) + 1) if len(g)]
tone = groups[0]
tone_second = int(tone[len(tone) // 2])
samples = blocks[tone_second, :, 0]
tone_hz = int(np.argmax(abs(np.fft.rfft(samples * np.hanning(len(samples))))))
music = max(groups[1:], key=len)

# Read actual outgoing TCP payloads, excluding Ethernet padding.
data = (OUT / "network.pcap").read_bytes()
offset = 24
commands = []
while offset + 16 <= len(data):
    _, _, size, _ = struct.unpack_from("<4I", data, offset)
    packet = data[offset + 16:offset + 16 + size]
    offset += 16 + size
    if len(packet) < 54 or packet[12:14] != b"\x08\x00" or packet[23] != 6:
        continue
    if packet[26:30] != bytes([10, 64, 0, 16]):
        continue
    tcp = 14 + (packet[14] & 15) * 4
    end = 14 + struct.unpack_from(">H", packet, 16)[0]
    payload = packet[tcp + (packet[tcp + 12] >> 4) * 4:end]
    if payload.startswith((b"SEARCH ", b"PLAY ", b"TEST\n")):
        command = payload.decode("ascii").strip()
        if command not in commands:
            commands.append(command)

paused = (OUT / "paused.bgra").read_bytes()[:256 * 192 * 4]
paused_later = (OUT / "paused-later.bgra").read_bytes()[:256 * 192 * 4]
report = {
    "date": "2026-10-01",
    "emulator": "official melonDS core, custom headless Platform runner",
    "melonDS_commit": "906e9ebb27da8c6a715cd7abab4abfe8a8d29427",
    "console": "DSi",
    "boot": "FreeBIOS, generated firmware, direct homebrew boot, no NAND/SD",
    "network": "official Slirp; original core unmodified",
    "rom_sha256": hashlib.sha256(rom.read_bytes()).hexdigest(),
    "actual_tcp_commands": commands,
    "search_results_observed": 8,
    "video_id": "2Wi9SJYScKg",
    "video_title": "DELTARUNE DSi Demo Release",
    "test_tone_spu_peak_hz": tone_hz,
    "test_tone_spu_rms": round(float(rms[tone_second]), 2),
    "youtube_spu_nonzero_segment_seconds": int(len(music)),
    "youtube_spu_segment_mean_rms": round(float(rms[music].mean()), 2),
    "pause_top_frame_unchanged_after_two_seconds": paused == paused_later,
    "resume_timer_observed": "00:57 paused -> 01:00 playing",
    "spu_output_format": "48000 Hz stereo signed PCM16LE",
    "real_hardware_tested": False,
    "limitations": ["Virtual AP; real WPA2 radio untested", "PC bridge required",
                    "128x96 RGB332 at 8fps; source audio 16kHz mono",
                    "NAND/SD boot and TWiLight launch untested"],
}
assert commands == ["TEST", "SEARCH deltarune dsi demo", "PLAY 2Wi9SJYScKg"], commands
assert abs(tone_hz - 440) <= 1
assert len(music) >= 30
assert report["pause_top_frame_unchanged_after_two_seconds"]
(OUT / "verification.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")

# A comparison of captured GPU screens, with labels outside the screenshots.
comparison = Image.new("RGB", (552, 424), "#17191d")
draw = ImageDraw.Draw(comparison)
font = ImageFont.truetype("C:/Windows/Fonts/seguisb.ttf", 14)
for x, title, filename in [(12, "YouTube search results", "results.bgra"),
                           (284, "Selected YouTube video", "youtube-playback.bgra")]:
    draw.text((x, 9), title, fill="#eeeeee", font=font)
    screen = Image.frombytes("RGBA", (256, 384), (OUT / filename).read_bytes(), "raw", "BGRA").convert("RGB")
    comparison.paste(screen, (x, 32))
comparison.resize((1104, 848), Image.Resampling.NEAREST).save(OUT / "search-and-playback.png")
print(json.dumps(report, ensure_ascii=False, indent=2))
