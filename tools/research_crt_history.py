from pathlib import Path
import subprocess
out=Path(__file__).resolve().parents[1]/'investigation';repo=out/'upstream/sdk'
def git(*args):return subprocess.check_output(['git','-C',str(repo),*args],text=True)
print(git('log','--oneline','-S0x03000000','--','sys/crts/ds_arm7.ld'),flush=True)
for rev in ['94a2aa5d','1ed23ea5','f4b65f65','3c1e4e23','e117e2d0']:
    diff=git('show',rev,'--','sys/crts/ds_arm7.ld','sys/crts/ds_arm7_crt0.s','sys/crts/ds_arm9_crt0.s')
    (out/('sdk-'+rev+'.diff')).write_text(diff)
    print(diff[:10000],flush=True)
