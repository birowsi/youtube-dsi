"""Opt-in YDS2 JPEG/stereo ADPCM relay. The proven YDS1 server is preserved.

YDS2 header: <4s8I magic,width,height,fps,rate,samples,channels,JPEG=1,IMA=1.
Packets: <4I sequence,jpeg_length,audio_length,jpeg_quality + JPEG + ADPCM.
ADPCM header <hBBhBB contains the two initial predictor/index states. Each
following byte contains the left high nibble and right low nibble for a frame.
Feedback BUF milliseconds state (0=startup,1=playing,2=rebuffer,3=paused).

YDS3 (PLAY3/TEST3, 2026-10-02) keeps the YDS2 packet layout. A packet whose
jpeg_length is 0 repeats the previous picture. The relay keeps the picture
quality steady and lowers the picture rate when a scene does not fit the
measured Wi-Fi rate, instead of squeezing every frame into a small JPEG.
Feedback: BUF milliseconds state decode_ms late_frames_per_second [hidden].
hidden=1 means the DSi lid is closed: only audio is needed, so no pictures are sent.
SEARCH3 adds a 1-bit Galmuri9 title bitmap per result after the text lines.
SEARCH4 (2026-10-03) returns up to 24 results; each text line is
"id<TAB>length_seconds<TAB>title" (length 0 unknown, -1 live), and the title
bitmaps are wrapped narrower so the DSi can draw the length to their right.
SEARCH5 (2026-10-04) is SEARCH4 plus the channel name: "OK n w h cw ch", lines
"id<TAB>length<TAB>channel<TAB>title", and after each title bitmap a one-line
cw x ch channel bitmap.

PLAY4 <id> <start_seconds> is PLAY3 from a position, for seeking. It answers
"OK STREAM4 <duration_seconds> <start_seconds>" before the YDS3 header. The
resolved media URLs are cached, so a seek only restarts ffmpeg.

Discovery: UDP "YTDSI?<TAB>nonce" on the relay port is answered with
"YTDSI!<TAB>port<TAB>nonce", so the DSi finds the PC after its address changes.
"""
from __future__ import annotations
import argparse
import collections
from pathlib import Path
import audioop
import io
import ipaddress
import os
import itertools
import logging
import math
import select
import socket
import socketserver
import struct
import subprocess
import threading
import time
from http.server import ThreadingHTTPServer
from PIL import Image, ImageDraw, ImageFont
import server as legacy

WIDTH, HEIGHT, FPS, RATE = 256, 192, 16, 32000
SAMPLES = RATE // FPS
HEADER = struct.pack('<4s8I', b'YDS2', WIDTH, HEIGHT, FPS, RATE, SAMPLES, 2, 1, 1)
HEADER3 = struct.pack('<4s8I', b'YDS3', WIDTH, HEIGHT, FPS, RATE, SAMPLES, 2, 1, 1)
TITLE_W, TITLE_H = 232, 22
# SEARCH4: more results per search; titles wrap at 188 px, leaving room for the length.
SEARCH4_RESULTS, SEARCH4_WRAP = 24, 188
CHANNEL_W, CHANNEL_H = 232, 12
FONT_PATH = Path(__file__).resolve().parent / 'fonts' / 'Galmuri9.ttf'
TEST_STALL_MS = 0
# Set by a new PLAY request so the stream still holding PLAY_LOCK ends at once.
REPLACE = threading.Event()
ACTIVE = None   # socket of the stream holding PLAY_LOCK
# Debug: YTDSI_DUMP=<dir> records every sent stream (.yds) and the DSi feedback (.tsv).
DUMP_DIR = os.environ.get('YTDSI_DUMP')


class StereoIMA:
    def __init__(self):
        self.states = [(0, 0), (0, 0)]

    def encode(self, pcm):
        if len(pcm) != SAMPLES * 4:
            raise ValueError('Incomplete stereo PCM block')
        header = struct.pack('<hBBhBB', *self.states[0], 0, *self.states[1], 0)
        left = audioop.tomono(pcm, 2, 1, 0)
        right = audioop.tomono(pcm, 2, 0, 1)
        a, self.states[0] = audioop.lin2adpcm(left, 2, self.states[0])
        b, self.states[1] = audioop.lin2adpcm(right, 2, self.states[1])
        packed = bytearray(SAMPLES)
        for i, (x, y) in enumerate(zip(a, b)):
            packed[2*i] = (x & 0xf0) | (y >> 4)
            packed[2*i+1] = ((x & 15) << 4) | (y & 15)
        return header + packed


class AdaptiveJPEG:
    def __init__(self):
        self.budget = 6000
        self.last_adjust = 0
        self.feedback = (3000, 0, 0)
        self.lock = threading.Lock()

    def update(self, ms, mode, decode_ms=0, *unused):
        with self.lock:
            self.feedback = (ms, mode, decode_ms)

    def encode(self, image):
        now = time.monotonic()
        with self.lock:
            ms, mode, decode_ms = self.feedback
        if now - self.last_adjust >= 2:
            if mode == 2 or (mode == 1 and (ms < 1500 or decode_ms > 60)):
                self.budget = max(1500, int(self.budget * .78))
            elif mode == 1 and ms > 5300 and decode_ms <= 58:
                self.budget = min(12000, int(self.budget * 1.08))
            self.last_adjust = now
        def save(q):
            target = io.BytesIO()
            # Baseline 4:2:0 halves IDCT work versus 4:4:4 on this ARM9.
            image.save(target, 'JPEG', quality=q, subsampling=2, optimize=False,
                       progressive=False)
            return target.getvalue()
        best, best_q = save(1), 1
        lo, hi = 2, 92
        while lo <= hi:
            q = (lo + hi) // 2
            data = save(q)
            if len(data) <= self.budget:
                best, best_q = data, q
                lo = q + 1
            else:
                hi = q - 1
        if len(best) > 32768:
            raise ValueError('JPEG packet exceeds client limit')
        return best, best_q


def save_jpeg(image, q):
    target = io.BytesIO()
    image.save(target, 'JPEG', quality=q, subsampling=2, optimize=False, progressive=False)
    return target.getvalue()


class SmartRate:
    """Steady-quality rate control for YDS3.

    Tokens refill at the video byte rate the DSi link has sustained. A frame
    is sent at the current steady quality when enough tokens exist; otherwise
    the previous picture is repeated (0-byte packet). At least one picture in
    every MAX_GAP+1 frames is sent, then at the best quality that fits. The
    steady quality moves a few steps every two seconds, so it does not jump
    from frame to frame with scene complexity.
    """
    # Above q80 the JPEG grows ~20% for little visible gain on the 256x192 RGB555
    # screen; spare bytes go to more pictures per second instead.
    QMIN, QMAX, MAX_GAP = 50, 80, 3
    RATE_MIN, RATE_MAX = 40000.0, 220000.0

    def __init__(self):
        self.rate = 90000.0          # video bytes per second (audio is extra); ~DSi NTR Wi-Fi
        self.tokens = self.rate * 0.3
        self.q = 64
        self.gap = 0
        self.first = True
        self.window = collections.deque(maxlen=FPS * 2)
        self.window_bytes = collections.deque(maxlen=FPS * 2)
        self.last_adjust = time.monotonic()
        self.feedback = (0, 0, 0, 0, 0)
        self.lock = threading.Lock()
        self.budget = 0              # status compatibility

    def update(self, ms, mode, decode_ms=0, late=0, hidden=0):
        with self.lock:
            self.feedback = (ms, mode, decode_ms, late, hidden)
            self.feedback_at = time.monotonic()

    def hidden(self):
        with self.lock:
            return bool(self.feedback[4])

    def adjust(self):
        now = time.monotonic()
        if now - self.last_adjust < 2:
            return
        self.last_adjust = now
        with self.lock:
            ms, mode, decode_ms, late, hidden = self.feedback
        if hidden:
            return                       # no pictures: nothing to learn about Wi-Fi or decoding
        if mode == 2 or (mode == 1 and ms < 2000):
            self.rate *= 0.85
        elif (mode == 1 and ms >= 4500 and late <= 1 and
              sum(self.window_bytes) >= self.rate * len(self.window_bytes) / FPS * 0.8):
            # Raise only while the rate is actually the limit; a simple scene
            # that uses few bytes says nothing about extra Wi-Fi capacity.
            self.rate *= 1.06
        self.rate = min(self.RATE_MAX, max(self.RATE_MIN, self.rate))
        sent = sum(self.window) / max(1, len(self.window))
        if mode == 1 and late >= 3:
            if self.q <= self.QMIN:
                self.rate *= 0.9         # fewer pictures = less decoding
            self.q -= 4                  # DSi decode cannot keep up
        elif sent < 0.55:
            self.q -= 3                  # too many repeated pictures
        elif sent > 0.95 and late == 0 and self.tokens > self.rate * 0.3:
            self.q += 2
        self.q = min(self.QMAX, max(self.QMIN, self.q))

    def encode(self, image):
        self.adjust()
        self.tokens = min(self.tokens + self.rate / FPS, self.rate * 0.6)
        if not self.first and self.hidden():
            self.gap = 0
            return b'', self.q
        data, q = save_jpeg(image, self.q), self.q
        if self.first or len(data) <= self.tokens:
            pass
        elif self.gap >= self.MAX_GAP:
            lo, hi, best = self.QMIN, self.q - 1, None
            while lo <= hi:
                mid = (lo + hi) // 2
                candidate = save_jpeg(image, mid)
                if len(candidate) <= self.tokens:
                    best, q = candidate, mid
                    lo = mid + 1
                else:
                    hi = mid - 1
            if best is None:
                q = self.QMIN
                best = save_jpeg(image, q)
            data = best
        else:
            self.gap += 1
            self.window.append(0)
            self.window_bytes.append(0)
            return b'', self.q
        while len(data) > 32768 and q > 10:
            q -= 10
            data = save_jpeg(image, q)
        if len(data) > 32768:
            raise ValueError('JPEG packet exceeds client limit')
        self.first = False
        self.gap = 0
        self.tokens -= len(data)
        self.window.append(1)
        self.window_bytes.append(len(data))
        return data, q


def read_feedback(sock, controller, stop):
    pending = b''
    while not stop.is_set():
        try:
            if not select.select([sock], [], [], .2)[0]:
                continue
            part = sock.recv(256)
            if not part:
                return
            pending += part
            if len(pending) > 1024:
                return
            while b'\n' in pending:
                line, pending = pending.split(b'\n', 1)
                words = line.split()
                if words[:1] == [b'DBG']:
                    controller.debug = line.decode('ascii', 'replace')[4:]
                    continue
                if len(words) in (3,4,5,6) and words[0] == b'BUF':
                    ms, mode = int(words[1]), int(words[2])
                    if 0 <= ms <= 10000 and 0 <= mode <= 3:
                        extra = [int(w) for w in words[3:]]
                        controller.update(ms, mode, *extra)
        except (OSError, ValueError):
            return


def send_all(sock, data, stall=180):
    """sendall() that gives up when a new request replaces this stream."""
    view, last = memoryview(data), time.monotonic()
    while view:
        if REPLACE.is_set():
            raise ConnectionAbortedError('Stream replaced by a new request')
        try:
            sent = sock.send(view)
        except socket.timeout:
            if time.monotonic() - last > stall:
                raise
            continue
        view, last = view[sent:], time.monotonic()


class Pacer:
    """Keep the DSi buffer under its 6 s capacity so its receiver never stops reading.

    With the buffer full the DSi stops calling recv(), its TCP window closes and
    packets queue up inside its network stack. On real Wi-Fi (retransmissions,
    out-of-order segments) that is where playback crashed after a minute or two;
    the emulator's lossless network never reached that state. The DSi reports its
    buffer once a second; between reports the level is estimated from what was
    sent since and how long it has been playing.
    """
    AHEAD = 4.5          # seconds of audio buffered on the DSi

    def __init__(self):
        self.seen = None
        self.base_count = 0

    def wait(self, encoder, count):
        while not REPLACE.is_set():
            with encoder.lock:
                ms, mode = encoder.feedback[0], encoder.feedback[1]
                stamp = getattr(encoder, 'feedback_at', None)
            if stamp is None or mode not in (1, 3):
                return             # starting up or rebuffering: send freely
            if stamp != self.seen:
                self.seen, self.base_count = stamp, count
            buffered = ms / 1000 + (count - self.base_count) / FPS
            if mode == 1:
                buffered -= time.monotonic() - stamp
            if buffered < self.AHEAD:
                return
            time.sleep(0.03)


def send_stream(sock, frames, v3=False, ok=b'OK STREAM\n'):
    encoder, audio = (SmartRate() if v3 else AdaptiveJPEG()), StereoIMA()
    stop = threading.Event()
    feedback = threading.Thread(target=read_feedback, args=(sock, encoder, stop), daemon=True)
    first = next(frames, None)
    if first is None:
        raise RuntimeError('No complete video/audio packet produced')
    feedback.start()
    # Short timeouts let send_all() notice a takeover while the DSi is not reading.
    sock.settimeout(1)
    send_all(sock, ok + (HEADER3 if v3 else HEADER))
    count, total, pictures = 0, 0, 0
    pacer = Pacer()
    dump = trace = None
    if DUMP_DIR:
        name = os.path.join(DUMP_DIR, time.strftime('%Y%m%d-%H%M%S'))
        dump, trace = open(name + '.yds', 'wb'), open(name + '.tsv', 'w')
        dump.write(HEADER3 if v3 else HEADER)
        trace.write('time\tpacket\tfeedback\trate\tq\tjpeg_bytes\tdsi_debug\n')
    try:
        for image, pcm in itertools.chain([first], frames):
            if REPLACE.is_set():
                legacy.LOG.info('Stream replaced by a new request')
                return
            if v3:
                pacer.wait(encoder, count)
            jpeg, q = encoder.encode(image)
            block = audio.encode(pcm)
            packet = struct.pack('<4I', count, len(jpeg), len(block), q) + jpeg + block
            if dump:
                dump.write(packet)
                trace.write(f'{time.time():.2f}\t{count}\t{getattr(encoder, "feedback", "")}\t'
                            f'{round(getattr(encoder, "rate", 0))}\t{q}\t{len(jpeg)}\t'
                            f'{getattr(encoder, "debug", "")}\n')
                trace.flush()
            send_all(sock, packet)
            count += 1
            total += len(packet)
            pictures += bool(jpeg)
            if count % FPS == 0:
                extra = dict(video_rate_kib_s=round(encoder.rate/1024, 1), steady_quality=encoder.q,
                             picture_fps=round(pictures*FPS/count, 1)) if v3 else {}
                legacy.state(state='streaming', quality=q, jpeg_budget=encoder.budget,
                             streamed_seconds=count/FPS, average_kib_s=round(total/(count/FPS)/1024, 1), **extra)
        if v3:
            legacy.LOG.info('HQ3 stream: %d frames, %d pictures, %.1f KiB/s, rate=%.1f KiB/s, q=%d',
                            count, pictures, total / max(count/FPS, 1) / 1024, encoder.rate/1024, encoder.q)
        else:
            legacy.LOG.info('HQ stream: %d frames, %.1f KiB/s, budget=%d', count,
                            total / max(count/FPS, 1) / 1024, encoder.budget)
        # End the AV direction but keep feedback open until the DSi drains its
        # buffered tail. This avoids writing into an already closed connection.
        sock.shutdown(socket.SHUT_WR)
        deadline = time.monotonic() + 180
        while feedback.is_alive() and time.monotonic() < deadline and not REPLACE.wait(.2):
            pass
    finally:
        stop.set()
        feedback.join(timeout=1)
        for file in (dump, trace):
            if file:
                file.close()


def test_frames(seconds=24):
    for index in range(seconds * FPS):
        if TEST_STALL_MS and index == 8 * FPS:
            time.sleep(TEST_STALL_MS / 1000)
        image = Image.new('RGB', (WIDTH, HEIGHT))
        draw = ImageDraw.Draw(image)
        for x in range(WIDTH):
            draw.line((x, 0, x, 143), fill=(x, (index*5) % 256, 255-x))
        for y in range(144, HEIGHT, 4):
            for x in range(0, WIDTH, 4):
                draw.rectangle((x, y, x+3, y+3), fill='white' if (x+y)//4 % 2 else 'black')
        x = (index*3) % 224
        draw.rectangle((x, 45, x+31, 76), fill=(255, 255, 255), outline=(0, 0, 0), width=2)
        draw.text((8, 8), '256x192 / 16fps / stereo', fill=(255, 255, 255))
        samples = []
        for j in range(SAMPLES):
            t = (index*SAMPLES+j)/RATE
            samples.extend((round(6000*math.sin(2*math.pi*440*t)),
                            round(6000*math.sin(2*math.pi*660*t))))
        yield image, struct.pack('<'+'h'*len(samples), *samples)


def resolve(video_id):
    if not legacy.re.fullmatch(r'[A-Za-z0-9_-]{11}', video_id):
        raise ValueError('Invalid YouTube ID')
    legacy.state(state='resolving', last_video=video_id, error='')
    with legacy.yt_dlp.YoutubeDL(legacy.ydl_options(
            # The DSi shows 256x144 (16:9), so 360p is plenty and costs the relay less
            # than 480p. Plain HTTPS files first: HLS segments failed to open on the
            # phone relay and seek poorly.
            format='bestvideo[height<=360][vcodec^=avc1][protocol=https]+bestaudio[ext=m4a][protocol=https]/'
                   'bestvideo[height<=360][vcodec^=avc1]+bestaudio[ext=m4a]/'
                   'bestvideo[height<=360]+bestaudio/best[height<=360]/best')) as ydl:
        data = ydl.extract_info('https://www.youtube.com/watch?v='+video_id, download=False)
    streams = data.get('requested_formats') or [data]
    video = next((f for f in streams if f.get('vcodec') != 'none'), None)
    audio = next((f for f in streams if f.get('acodec') != 'none'), None)
    if not video or not audio:
        raise ValueError('Video/audio not available')
    legacy.LOG.info('HQ source video=%s (%sp), audio=%s', video.get('format_id'),
                    video.get('height'), audio.get('format_id'))
    video['_duration'] = int(data.get('duration') or 0)
    return video, audio


_resolved = {}


def resolve_cached(video_id):
    """Media URLs stay valid for hours; reuse them so a seek starts quickly."""
    hit = _resolved.get(video_id)
    if hit and time.monotonic() - hit[0] < 3600:
        return hit[1]
    result = resolve(video_id)
    _resolved.clear()
    _resolved[video_id] = (time.monotonic(), result)
    return result


def input_options(stream, start=0):
    # Retry an interrupted public-media connection from its byte position.
    # -ss before -i seeks the input (HTTP range request), not by decoding from 0.
    # -short_seek_size 1: always seek with a new range request. YouTube sends at about
    # playback speed, so reading ahead to a point 30 s in took 15 s instead of 1 s.
    return ['-reconnect','1','-reconnect_streamed','1',
            '-reconnect_on_network_error','1','-reconnect_delay_max','2',
            '-short_seek_size','1'] + \
        (['-ss', str(start)] if start else []) + legacy.ffmpeg_input(stream)


def drain_errors(pipe, tag, tails):
    for line in iter(pipe.readline,b''):
        text=legacy.re.sub(r'https?://\S+','[media URL]',line.decode('utf-8','replace').strip())
        if text:
            tails[tag]=text[-400:]
            legacy.LOG.warning('%s: %s',tag,text)


def live_frames(video, audio, start=0):
    base = [legacy.FFMPEG, '-hide_banner', '-nostdin', '-loglevel', 'error']
    vf = f'fps={FPS},scale={WIDTH}:{HEIGHT}:flags=lanczos:force_original_aspect_ratio=decrease,pad={WIDTH}:{HEIGHT}:(ow-iw)/2:(oh-ih)/2'
    commands = [base+input_options(video, start)+['-an','-vf',vf,'-pix_fmt','rgb24','-f','rawvideo','pipe:1'],
                base+input_options(audio, start)+['-vn','-ac','2','-ar',str(RATE),
                    '-af',f'aresample={RATE}:filter_size=64','-f','s16le','pipe:1']]
    processes, tails = [], {}
    try:
        for tag, command in zip(('HQ video', 'HQ audio'), commands):
            p = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                 creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            processes.append(p)
            threading.Thread(target=drain_errors, args=(p.stderr, tag, tails), daemon=True).start()
        while True:
            rgb = legacy.read_exact(processes[0].stdout, WIDTH*HEIGHT*3)
            pcm = legacy.read_exact(processes[1].stdout, SAMPLES*4)
            if len(rgb) != WIDTH*HEIGHT*3:
                if not rgb and processes[0].wait()==0:
                    return
                raise RuntimeError('Video ended or failed: '+'; '.join(tails.values()))
            if len(pcm)!=SAMPLES*4 and processes[1].wait()!=0:
                raise RuntimeError('Audio ended or failed: '+'; '.join(tails.values()))
            yield Image.frombytes('RGB', (WIDTH, HEIGHT), rgb), pcm.ljust(SAMPLES*4, b'\0')
    finally:
        for p in processes:
            if p.poll() is None:
                p.terminate()
            try:
                p.wait(timeout=3)
            except subprocess.TimeoutExpired:
                p.kill(); p.wait()
            p.stdout.close(); p.stderr.close()


_title_font = None


def title_font():
    global _title_font
    if _title_font is None:
        _title_font = ImageFont.truetype(str(FONT_PATH), 10)
    return _title_font


def title_bitmap(title, wrap=TITLE_W):
    """Wrap a Unicode title into two Galmuri9 lines of `wrap` pixels; return a
    TITLE_W x TITLE_H 1-bit MSB-first bitmap."""
    font = title_font()
    text = ' '.join(title.split())
    lines, rest = [], text
    for row in range(2):
        if not rest:
            break
        if font.getlength(rest) <= wrap or row == 1:
            line = rest
            if font.getlength(line) > wrap:
                while line and font.getlength(line + '...') > wrap:
                    line = line[:-1]
                line = line.rstrip() + '...'
            lines.append(line)
            rest = ''
            break
        cut = len(rest)
        while cut > 1 and font.getlength(rest[:cut]) > wrap:
            cut -= 1
        space = rest.rfind(' ', 0, cut + 1)
        if space > cut // 2:
            cut = space
        lines.append(rest[:cut].rstrip())
        rest = rest[cut:].lstrip()
    image = Image.new('L', (TITLE_W, TITLE_H))
    draw = ImageDraw.Draw(image)
    for row, line in enumerate(lines):
        draw.text((0, row * 12 - 1), line, font=font, fill=255)
    return image.point(lambda p: 255 if p >= 90 else 0, mode='1').tobytes()


def channel_bitmap(name):
    """One Galmuri9 line, cut with '...' to CHANNEL_W; 1-bit MSB-first bitmap."""
    font, text = title_font(), ' '.join(name.split())
    if font.getlength(text) > CHANNEL_W:
        while text and font.getlength(text + '...') > CHANNEL_W:
            text = text[:-1]
        text = text.rstrip() + '...'
    image = Image.new('L', (CHANNEL_W, CHANNEL_H))
    ImageDraw.Draw(image).text((0, -1), text, font=font, fill=255)
    return image.point(lambda p: 255 if p >= 90 else 0, mode='1').tobytes()


class Handler(legacy.Handler):
    def handle(self):
        global ACTIVE
        locked = False
        frames = None
        try:
            raw = self.rfile.readline(1025)
            if len(raw) > 1024 or not raw.endswith(b'\n'):
                raise ValueError('Invalid request')
            cmd, _, arg = raw.decode('utf-8').strip().partition(' ')
            legacy.LOG.info('%s: %s %s', self.client_address[0], cmd, arg)
            self.request.settimeout(180)
            if cmd == 'HELLO':
                self.request.sendall(b'OK YouTubeDSi 2\n')
            elif cmd == 'SEARCH':
                results = legacy.search(arg)
                lines = [f'OK {len(results)}\n']
                for item in results:
                    title = legacy.re.sub(r'[\t\r\n]', ' ', item['title']).encode('ascii','replace').decode()[:100]
                    lines.append(item['id']+'\t'+title+'\n')
                self.request.sendall(''.join(lines).encode('ascii'))
            elif cmd == 'SEARCH3':
                results = legacy.search(arg)
                lines = [f'OK {len(results)} {TITLE_W} {TITLE_H}\n']
                bitmaps = []
                for item in results:
                    title = legacy.re.sub(r'[\t\r\n]', ' ', item['title'])
                    lines.append(item['id']+'\t'+title.encode('ascii','replace').decode()[:100]+'\n')
                    bitmaps.append(title_bitmap(title))
                self.request.sendall(''.join(lines).encode('ascii') + b''.join(bitmaps))
            elif cmd == 'SEARCH5':
                results = legacy.search(arg, count=SEARCH4_RESULTS)
                lines = [f'OK {len(results)} {TITLE_W} {TITLE_H} {CHANNEL_W} {CHANNEL_H}\n']
                bitmaps = []
                for item in results:
                    title = legacy.re.sub(r'[\t\r\n]', ' ', item['title'])
                    channel = legacy.re.sub(r'[\t\r\n]', ' ', item['channel'])
                    lines.append(f"{item['id']}\t{item['duration']}\t"
                                 + channel.encode('ascii','replace').decode()[:60] + '\t'
                                 + title.encode('ascii','replace').decode()[:100] + '\n')
                    bitmaps.append(title_bitmap(title, SEARCH4_WRAP) + channel_bitmap(channel))
                self.request.sendall(''.join(lines).encode('ascii') + b''.join(bitmaps))
            elif cmd == 'SEARCH4':
                results = legacy.search(arg, count=SEARCH4_RESULTS)
                lines = [f'OK {len(results)} {TITLE_W} {TITLE_H}\n']
                bitmaps = []
                for item in results:
                    title = legacy.re.sub(r'[\t\r\n]', ' ', item['title'])
                    lines.append(f"{item['id']}\t{item['duration']}\t"
                                 + title.encode('ascii','replace').decode()[:100] + '\n')
                    bitmaps.append(title_bitmap(title, SEARCH4_WRAP))
                self.request.sendall(''.join(lines).encode('ascii') + b''.join(bitmaps))
            elif cmd in ('PLAY4','PLAY3','TEST3','PLAY2','TEST2','PLAY','TEST'):
                locked = legacy.PLAY_LOCK.acquire(blocking=False)
                if not locked:
                    # One DSi plays at a time; a new request takes over from the old stream,
                    # which may still be closing its ffmpeg processes after the DSi left.
                    REPLACE.set()
                    try:
                        # Unblock a sendall() to a DSi that left without closing the connection.
                        ACTIVE.shutdown(socket.SHUT_RDWR)
                    except (AttributeError, OSError):
                        pass
                    locked = legacy.PLAY_LOCK.acquire(timeout=15)
                    if not locked:
                        raise ValueError('The previous video is still closing. Try again.')
                REPLACE.clear()
                ACTIVE = self.request
                if cmd == 'TEST3':
                    frames = test_frames(); send_stream(self.request, frames, v3=True)
                elif cmd == 'PLAY4':
                    video_id, _, start = arg.partition(' ')
                    for attempt in range(2):
                        video, audio = resolve_cached(video_id)
                        duration = video['_duration']
                        start = max(0, min(int(start or 0), max(0, duration - 2)))
                        frames = live_frames(video, audio, start)
                        try:
                            first = next(frames)
                            break
                        except (RuntimeError, StopIteration) as exc:
                            frames.close()
                            if attempt or '403' not in str(exc):
                                raise
                            # The media URL was refused; resolve the video again once.
                            _resolved.clear()
                    # `frames` stays the generator so the cleanup below can close ffmpeg.
                    send_stream(self.request, itertools.chain([first], frames), v3=True,
                                ok=f'OK STREAM4 {duration} {start}\n'.encode())
                elif cmd == 'PLAY3':
                    frames = live_frames(*resolve(arg)); send_stream(self.request, frames, v3=True)
                elif cmd == 'TEST2':
                    frames = test_frames(); send_stream(self.request, frames)
                elif cmd == 'PLAY2':
                    frames = live_frames(*resolve(arg)); send_stream(self.request, frames)
                elif cmd == 'TEST':
                    legacy.test_stream(self.request)
                else:
                    legacy.transcode(self.request, *legacy.streams(arg))
            else:
                raise ValueError('Unknown command')
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            legacy.LOG.info('DSi disconnected')
        except Exception as exc:
            legacy.LOG.exception('HQ request failed')
            legacy.state(state='error', error=str(exc))
            try:
                self.request.sendall(('ERR '+str(exc).replace('\n',' ')+'\n').encode('ascii','replace'))
            except OSError:
                pass
        finally:
            if frames is not None:
                # Stopping ffmpeg can take seconds; do it in the background so a seek's
                # new stream (waiting for PLAY_LOCK) starts at once.
                threading.Thread(target=frames.close, daemon=True).start()
            if locked:
                legacy.PLAY_LOCK.release()
            if legacy.STATUS['state'] != 'error':
                legacy.state(state='ready')


def discovery(sock, port):
    """Answer DSi broadcast probes on the relay port (UDP)."""
    while True:
        data, addr = sock.recvfrom(256)
        try:
            kind, nonce = data.decode('ascii').strip().split('\t')
            address = ipaddress.ip_address(addr[0])
        except (UnicodeError, ValueError):
            continue
        if kind == 'YTDSI?' and legacy.re.fullmatch(r'[0-9a-f]{1,19}', nonce) and (
                address.is_private or address.is_loopback):
            sock.sendto(f'YTDSI!\t{port}\t{nonce}\n'.encode(), addr)


def main():
    global TEST_STALL_MS
    parser = argparse.ArgumentParser()
    parser.add_argument('--bind', default='0.0.0.0')
    parser.add_argument('--port', type=int, default=8767)
    parser.add_argument('--ffmpeg', default='C:/ffmpeg/bin/ffmpeg.exe')
    parser.add_argument('--test-stall-ms', type=int, default=0)
    args = parser.parse_args()
    legacy.FFMPEG = args.ffmpeg
    TEST_STALL_MS = args.test_stall_ms
    logging.basicConfig(level=logging.INFO, format='%(asctime)s %(levelname)s %(message)s')
    status = ThreadingHTTPServer(('127.0.0.1', args.port+1), legacy.StatusHandler)
    threading.Thread(target=status.serve_forever, daemon=True).start()
    try:
        probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        probe.bind((args.bind, args.port))
        threading.Thread(target=discovery, args=(probe, args.port), daemon=True).start()
        legacy.LOG.info('DSi auto-discovery on UDP %s:%d', args.bind, args.port)
    except OSError as exc:
        legacy.LOG.warning('Auto-discovery unavailable: %s', exc)
    legacy.LOG.info('HQ relay TCP %s:%d, status port%d', args.bind, args.port, args.port+1)
    with legacy.Server((args.bind, args.port), Handler) as server:
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass
    status.shutdown()


if __name__ == '__main__':
    main()
