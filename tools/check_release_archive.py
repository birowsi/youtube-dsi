"""Read-only verification of every file named by a release manifest."""
from pathlib import Path
import hashlib,json,sys,zipfile
archive=Path(sys.argv[1])
with zipfile.ZipFile(archive) as z:
    manifest=json.loads(z.read('manifest.json'))
    for name,sha in manifest['files'].items():
        assert hashlib.sha256(z.read(name)).hexdigest()==sha,name
    assert hashlib.sha256(z.read('client/YouTubeDSiHQ.nds')).hexdigest()==manifest['rom_sha256']
print(json.dumps({'verified_files':len(manifest['files']),
    'rom_sha256':manifest['rom_sha256'],'real_hardware_verified':manifest['real_hardware_verified']},indent=2))
