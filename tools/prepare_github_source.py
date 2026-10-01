"""Create an explicit, reviewable source tree for public GitHub publication.

Never traverse the SD/NAND workspace or copy logs, vendor binaries, cookies,
upstream repositories, emulator firmware, or existing private backup ZIPs.
"""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / 'publish/YouTubeDSi'
DEST.mkdir(parents=True, exist_ok=True)

def copy(relative):
    src = ROOT / relative
    dst = DEST / relative
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(src, dst)

files = ['client/Makefile', 'server/server.py', 'server/server_quality.py',
         'tools/build_quality.sh', 'tools/build_compat.sh',
         'tools/prepare_compat_runtime.py', 'tools/prepare_quality_runtime.py',
         'tools/check_jpeg_patch.py', 'tools/fetch_tjpgd.py', 'tools/codec_check.c',
         'tools/analyze_quality.py', 'tools/analyze_compat.py',
         'tools/run_quality_emu.py', 'investigation/HARDWARE_FINDINGS.md',
         'investigation/real-hardware-verification.json',
         'investigation/compat-static.json', 'investigation/compat-verification.json',
         'investigation/ftpd-module-layout.json',
         'investigation/quality/QUALITY_FINDINGS.md',
         'investigation/quality/verification.json',
         'investigation/quality/static.json',
         'investigation/quality/codec-tests.json',
         'investigation/quality/jpeg-optimization-tests.json',
         'investigation/quality/jpeg-dependency.json',
         'investigation/quality/tjpgd-local.patch',
         'investigation/quality/tjpgd3.zip',
         'investigation/quality/patch-replay.json',
         'investigation/bunjalloo/실행환경-분석.md',
         'investigation/bunjalloo/binary-evidence.json',
         'investigation/bunjalloo/arm7-crt-disassembly.txt']
files += [str(p.relative_to(ROOT)) for p in (ROOT / 'client/source').glob('*') if p.suffix in ('.c', '.h')]
files += [str(p.relative_to(ROOT)) for p in (ROOT / 'runtime-upstream').rglob('*') if p.is_file()]
files += [str(p.relative_to(ROOT)) for p in (ROOT / 'tools/emutest').glob('*') if p.is_file()]
for name in ('normal', 'stall', 'live'):
    for filename in ('static.json', 'quality.tsv', 'cpu.txt'):
        p = ROOT / 'investigation/quality' / name / filename
        if p.exists(): files.append(str(p.relative_to(ROOT)))
for name in ('Wifi', 'Audio'):
    for filename in ('evidence.json', 'cpu.txt'):
        p = ROOT / 'investigation/alias-tests' / name / filename
        if p.exists(): files.append(str(p.relative_to(ROOT)))
for name in ('dsi-direct', 'dsi-legacy-mbk', 'ds-direct'):
    files.append(f'investigation/bunjalloo/{name}/evidence.json')
for relative in files:
    copy(relative)

# A synthetic test frame only: no live YouTube frames or network captures.
src = ROOT / 'investigation/quality/normal/quality-1200.png'
dst = DEST / 'docs/hq-emulator.png'
dst.parent.mkdir(exist_ok=True)
shutil.copyfile(src, dst)

licenses = DEST / 'LICENSES'
licenses.mkdir(exist_ok=True)
for source, target in [
    ('tools/dswifi-src/COPYING', 'DSWiFi-MIT.txt'),
    ('tools/dswifi-src/COPYING.lwip', 'lwIP-BSD.txt'),
    ('tools/dswifi-src/COPYING.mbedtls', 'MbedTLS-Apache-2.0.txt'),
    ('tools/maxmod-src/COPYING', 'Maxmod-ISC.txt'),
]:
    shutil.copyfile(ROOT / source, licenses / target)
for source, target in [('licenses/LICENSE', 'libnds-Zlib.txt'),
                       ('licenses/LICENSE.FatFs', 'FatFs.txt')]:
    data = subprocess.check_output(['git', '-C', str(ROOT / 'tools/libnds-src'),
                                    'show', 'HEAD:' + source])
    (licenses / target).write_bytes(data)
for identifier in ('MPL-2.0', 'GPL-3.0-or-later', 'FSFAP'):
    url = f'https://raw.githubusercontent.com/spdx/license-list-data/main/text/{identifier}.txt'
    data = urllib.request.urlopen(url, timeout=30).read()
    assert len(data) > 100
    (licenses / (identifier + '.txt')).write_bytes(data)

# Preserve binary identity while making the PC relay CLI usable elsewhere.
# These are host argument defaults only, not protocol/codec changes.
for name in ('server.py', 'server_quality.py'):
    p = DEST / 'server' / name
    text = p.read_text()
    text = text.replace("default='192.168.0.4'", "default='0.0.0.0'")
    text = text.replace('default="192.168.0.4"', 'default="0.0.0.0"')
    text = text.replace("default='C:/ffmpeg/bin/ffmpeg.exe'", "default='ffmpeg'")
    text = text.replace('default="C:/ffmpeg/bin/ffmpeg.exe"', 'default="ffmpeg"')
    p.write_text(text, encoding='utf-8')

# Scrub workstation paths from copied prose/telemetry only. Frozen SDK input
# bytes and client source remain identical for the measured ROM rebuild.
for p in (DEST / 'investigation').rglob('*'):
    if p.is_file() and p.suffix in ('.md', '.json', '.txt', '.tsv'):
        text = p.read_text(encoding='utf-8')
        for path in ('/mnt/c/Users/hanbi/Desktop/NDSi',
                     'C:/Users/hanbi/Desktop/NDSi',
                     'C:\\Users\\hanbi\\Desktop\\NDSi'):
            text = text.replace(path, '<workspace>')
        p.write_text(text, encoding='utf-8')

base = (ROOT / 'client/YouTubeDSiCompat.nds').read_bytes()
import struct
cpu = lambda off: struct.unpack_from('<4I', base, off)
reference = {'rom_sha256': hashlib.sha256(base).hexdigest(),
             'unit_code': base[0x12], 'twl_header': base[0x180:0x1c0].hex(),
             'arm7': {}, 'arm7i': {}}
for name, off in (('arm7', 0x30), ('arm7i', 0x1d0)):
    start, entry, ram, size = cpu(off)
    reference[name] = {'entry': entry, 'ram': ram, 'size': size,
                      'image_sha256': hashlib.sha256(base[start:start+size]).hexdigest()}
(DEST / 'investigation/compat-runtime-reference.json').write_text(json.dumps(reference, indent=2) + '\n')
(DEST / 'VERSION').write_text('0.1.0\n')
print('Prepared public source tree:', DEST, 'copied files:', len(files))
