from pathlib import Path
import subprocess,concurrent.futures
root=Path(__file__).resolve().parents[1]
runner=root/'tools/emutest-build/emutest'
rom=root/'client/YouTubeDSiCompat.nds'
def run(profile):
    out=root/'investigation/compat-tests'/profile
    out.mkdir(parents=True,exist_ok=True)
    with (out/'run.log').open('w') as f:
        p=subprocess.run([str(runner),str(rom),str(out),'1200','dsi','auto-test',profile],stdout=f,stderr=subprocess.STDOUT)
    print(profile,p.returncode,(out/'cpu.txt').read_text(),flush=True)
# The relay permits one stream at a time. Keep AV checks sequential.
for profile in ['direct','legacy-mbk','legacy-locked']:
    run(profile)
