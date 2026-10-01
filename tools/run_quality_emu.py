from pathlib import Path
import json,subprocess,sys
root=Path(__file__).resolve().parents[1]
subprocess.run([sys.executable,str(root/'tools/verify_quality_static.py')],check=True)
stats=json.loads((root/'investigation/quality/static.json').read_text())['stats_address']
name=sys.argv[1] if len(sys.argv)>1 else 'normal'
schedule='auto-live' if name=='live' else 'auto-quality'
frames='6000' if name=='live' else '2400'
out=root/'investigation/quality'/name;out.mkdir(exist_ok=True)
(out/'static.json').write_text((root/'investigation/quality/static.json').read_text())
with (out/'run.log').open('w') as f:
    subprocess.run([str(root/'tools/emutest-build/emutest'),str(root/'client/YouTubeDSiHQ.nds'),
        str(out),frames,'dsi',schedule,'legacy-locked',stats[2:]],stdout=f,stderr=subprocess.STDOUT,check=True)
print(name,'HQ emulator run complete')
