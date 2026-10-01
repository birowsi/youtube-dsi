from pathlib import Path
import json,hashlib,subprocess,zipfile
root=Path(__file__).resolve().parents[1];out=root/'investigation/quality'
target=out/'patch-replay';target.mkdir(exist_ok=True)
with zipfile.ZipFile(out/'tjpgd3.zip') as z:
    for name in ('tjpgd.c','tjpgd.h','tjpgdcnf.h'):
        entry=next(p for p in z.namelist() if p.endswith('/'+name) or p==name)
        (target/name).write_bytes(z.read(entry).replace(b'\r\n',b'\n'))
subprocess.run(['patch','-p1','-d',str(target),'-i',str(out/'tjpgd-local.patch')],check=True)
meta=json.loads((out/'jpeg-dependency.json').read_text())
for name,digest in meta['source_sha256'].items():
    assert hashlib.sha256((target/name).read_bytes()).hexdigest()==digest,name
(out/'patch-replay.json').write_text(json.dumps({'patch_replay_bit_exact':True},indent=2))
print('Decoder patch replays bit for bit.')
