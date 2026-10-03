"""Freeze HQ source, relay dependencies, evidence and ROM without replacing controls."""
from pathlib import Path
import hashlib,json,shutil,zipfile
root=Path(__file__).resolve().parents[1]
report=json.loads((root/'investigation/quality/verification.json').read_text())
rom=root/'client/YouTubeDSiHQ.nds'
digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
assert digest(rom)==report['rom_sha256']
assert set(report['tests'])=={'normal','stall','live'}
assert all(t['rom_sha256']==report['rom_sha256'] for t in report['tests'].values())
assert report['real_hardware_verified'] is False
expected='b230264e8ce51541bd45c85e29f9dfabd3be2de204eeeeaf715eef55921bf84c'
for protected in (root/'client/YouTubeDSiCompat.nds',root/'sd-root/YouTubeDSiCompat.nds',
                  root.parent/'roms/tools/YouTubeDSiCompat.nds'):
    assert digest(protected)==expected,protected
controls=json.loads((root/'investigation/compat-static.json').read_text())['control_hashes']
for name,sha in controls.items():
    p=root.parent/'roms/tools'/name
    if p.exists():assert digest(p)==sha,p

def preserve_copy(source,target):
    target.parent.mkdir(parents=True,exist_ok=True)
    if target.exists():assert target.read_bytes()==source.read_bytes(),str(target)+' already differs'
    else:shutil.copyfile(source,target)

for target in (root.parent/'roms/tools/YouTubeDSiHQ.nds',root/'sd-root/YouTubeDSiHQ.nds'):
    preserve_copy(rom,target)
release=root/'releases/YouTubeDSiHQ-20261001'
files=[rom,root/'client/Makefile',root/'youtube-dsi.ini',
       root/'start-quality-server.cmd',root/'docs/TROUBLESHOOTING-LOG.md',root/'docs/legacy/HQ-GUIDE.md',root/'docs/legacy/USAGE.txt']
files += list((root/'client/source').glob('*.[ch]'))
files += [root/'server/server.py',root/'server/server_quality.py',root/'server/requirements-quality.txt']
files += [p for p in (root/'server/vendor').rglob('*') if p.is_file() and '__pycache__' not in p.parts]
files += [p for p in (root/'runtime-upstream').rglob('*') if p.is_file()]
files += [p for p in (root/'compat-runtime').glob('*') if p.suffix in ('.s','.ld','.specs','.c','.patch','.elf','.map')]
files += [root/'client/build/YouTubeDSiHQ.elf']
files += [p for p in (root/'tools').glob('*') if p.is_file() and p.suffix in ('.py','.sh','.ps1')
          and not p.name.startswith(('update_','probe_'))]
files += [root/'tools/codec_check.c']
files += [p for p in (root/'tools/emutest').glob('*') if p.is_file()]
files += [root/'investigation/HARDWARE_FINDINGS.md',root/'investigation/real-hardware-verification.json',
          root/'investigation/compat-static.json',root/'investigation/compat-verification.json']
quality=root/'investigation/quality'
files += [p for p in quality.glob('*') if p.is_file() and p.suffix in ('.json','.md','.patch','.zip','.txt')]
for name in ('normal','stall','live'):
    files += [p for p in (quality/name).glob('*') if p.is_file() and p.suffix in ('.json','.tsv','.txt','.log','.pcm','.pcap','.png')]
manifest={'release':'YouTubeDSiHQ-20261001','rom_sha256':report['rom_sha256'],
    'real_hardware_verified':False,'preserved_compat_sha256':expected,
    'build_environment':'BlocksDS1.24.0 / Wonderful GCC16.2.0 / pinned runtime-upstream inputs',
    'relay_environment':'Windows x64 Python3.11.9 / Pillow12.2.0 / vendored yt-dlp2026.08.19',
    'relay_port':8767,'files':{}}
for source in sorted(set(files)):
    relative=source.relative_to(root)
    preserve_copy(source,release/relative)
    manifest['files'][relative.as_posix()]=digest(source)
(release/'manifest.json').write_text(json.dumps(manifest,indent=2,ensure_ascii=False),encoding='utf-8')
archive=release.with_suffix('.zip')
assert not archive.exists(),'Archive already exists; choose a new release instead of overwriting'
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for p in sorted(release.rglob('*')):
        if p.is_file():z.write(p,p.relative_to(release))
print(json.dumps({'archive':str(archive),'size_bytes':archive.stat().st_size,
    'archive_sha256':digest(archive),'rom_sha256':report['rom_sha256'],
    'preserved_files':len(manifest['files'])},indent=2))
