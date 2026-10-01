"""Preserve SDK v1.24 runtime inputs so future builds need no research clone."""
from pathlib import Path
import subprocess,hashlib,json
root=Path(__file__).resolve().parents[1];out=root/'runtime-upstream'
files=['sys/crts/ds_arm7.ld','sys/crts/ds_arm9.ld',
       'sys/crts/ds_arm7_crt0.s','sys/crts/ds_arm9_crt0.s',
       'sys/arm7/main_core/source/main.c']
manifest={'sdk_tag':'v1.24.0','files':{}}
for name in files:
    data=subprocess.check_output(['git','-C',str(root/'investigation/upstream/sdk'),'show','v1.24.0:'+name])
    target=out/name;target.parent.mkdir(parents=True,exist_ok=True)
    if target.exists():assert target.read_bytes()==data,name
    else:target.write_bytes(data)
    manifest['files'][name]=hashlib.sha256(data).hexdigest()
(out/'manifest.json').write_text(json.dumps(manifest,indent=2))
print('Pinned SDK runtime inputs preserved:',out)
