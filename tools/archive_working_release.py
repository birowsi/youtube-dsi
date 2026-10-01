"""Freeze the real-hardware working release before performance changes."""
from pathlib import Path
import hashlib,json,shutil,zipfile
root=Path(__file__).resolve().parents[1]
out=root/'releases/YouTubeDSiCompat-20261001'
out.mkdir(parents=True,exist_ok=True)
files=['client/YouTubeDSiCompat.nds','client/source/main.c','client/Makefile',
       'client/build/YouTubeDSiCompat.elf','compat-runtime/arm7.elf',
       'compat-runtime/arm7.map','compat-runtime/arm7.ld','compat-runtime/crt7.s',
       'compat-runtime/crt9.s','compat-runtime/main7.c','compat-runtime/runtime.patch',
       'tools/prepare_compat_runtime.py','tools/build_compat.sh','server/server.py',
       'youtube-dsi.ini','investigation/HARDWARE_FINDINGS.md',
       'investigation/compat-static.json','investigation/compat-verification.json',
       'investigation/real-hardware-verification.json']
manifest={'date':'2026-10-01','hardware_report':'잘 되네. Then minor video/audio stutter reported.',
          'SDK':'v1.24.0','dswifi':'02f193979c945b41386f5b5050f7cc426a999484',
          'libnds':'7fd8ccbe781ed48a06fe063a2dcbb69035fa97d3',
          'maxmod':'a797317e5bb4eceebe5f41b85b620d68ef534b79','files':{}}
for name in files:
    source=root/name;target=out/name
    digest=hashlib.sha256(source.read_bytes()).hexdigest()
    if target.exists():
        assert hashlib.sha256(target.read_bytes()).hexdigest()==digest, 'Existing snapshot differs: '+name
    else:
        target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
    manifest['files'][name]=digest
assert manifest['files']['client/YouTubeDSiCompat.nds']=='b230264e8ce51541bd45c85e29f9dfabd3be2de204eeeeaf715eef55921bf84c'
(out/'manifest.json').write_text(json.dumps(manifest,indent=2,ensure_ascii=False),encoding='utf-8')
archive=out.with_suffix('.zip')
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
    for path in out.rglob('*'):
        if path.is_file():z.write(path,path.relative_to(out))
print('Working release archived:',archive)
