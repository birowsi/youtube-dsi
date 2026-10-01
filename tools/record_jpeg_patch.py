"""Record the complete licensed decoder diff and hashes for reproducibility."""
from pathlib import Path
import difflib,hashlib,json,zipfile
root=Path(__file__).resolve().parents[1];out=root/'investigation/quality'
archive=out/'tjpgd3.zip';patch=[];hashes={}
with zipfile.ZipFile(archive) as z:
    for name in ('tjpgd.c','tjpgd.h','tjpgdcnf.h'):
        entry=next(p for p in z.namelist() if p.endswith('/'+name) or p==name)
        old=z.read(entry).decode().replace('\r\n','\n');new=(root/'client/source'/name).read_text()
        (root/'client/source'/name).write_bytes(new.encode())
        patch.extend(difflib.unified_diff(old.splitlines(keepends=True),new.splitlines(keepends=True),
            fromfile='a/'+name,tofile='b/'+name))
        hashes[name]=hashlib.sha256((root/'client/source'/name).read_bytes()).hexdigest()
(out/'tjpgd-local.patch').write_text(''.join(patch))
meta={'source':'https://elm-chan.org/fsw/tjpgd/arc/tjpgd3.zip','version':'R0.03',
    'archive_sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),
    'changes':['RGB565 native-size decode','fast Huffman tables','ARM9 ARM/O3',
               'hot decode loops in ITCM','fused YCbCr/RGB565 output'],
    'source_sha256':hashes}
(out/'jpeg-dependency.json').write_text(json.dumps(meta,indent=2))
print(json.dumps(meta,indent=2))
