"""Fetch the original small, permissively licensed ChaN JPEG decoder."""
from pathlib import Path
import hashlib,io,json,urllib.request,zipfile,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
url='https://elm-chan.org/fsw/tjpgd/arc/tjpgd3.zip'
data=urllib.request.urlopen(url,timeout=30).read()
assert hashlib.sha256(data).hexdigest()=='052fe3efbc9a8be29f31597ad009c5b51a4f6905878eb28569e0ab3d46d0c013', 'Upstream archive changed'
out=root/'investigation/quality';out.mkdir(exist_ok=True)
(out/'tjpgd3.zip').write_bytes(data)
target=root/'client/source';target.mkdir(exist_ok=True)
with zipfile.ZipFile(io.BytesIO(data)) as z:
    names=z.namelist()
    for name in ['tjpgd.c','tjpgd.h','tjpgdcnf.h']:
        entry=next(p for p in names if p.endswith('/'+name) or p==name)
        (target/name).write_bytes(z.read(entry).replace(b'\r\n',b'\n'))
subprocess.run(['patch','-p1','-d',str(target),'-i',str(out/'tjpgd-local.patch')],check=True)
meta=json.loads((out/'jpeg-dependency.json').read_text())
for name,digest in meta['source_sha256'].items():
    assert hashlib.sha256((target/name).read_bytes()).hexdigest()==digest,(name,'Source differs')
print('TJpgDec fetched; original copyright and license retained.')
