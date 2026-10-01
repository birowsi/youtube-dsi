from pathlib import Path
import subprocess,sys
root=Path(__file__).resolve().parents[1]
subprocess.run([sys.executable,str(root/'tools/verify_compat_static.py')],check=True)
subprocess.run([sys.executable,str(root/'tools/test_compat_profiles.py')],check=True)
out=root/'investigation/compat-live';out.mkdir(exist_ok=True)
with (out/'run.log').open('w') as f:
    subprocess.run([str(root/'tools/emutest-build/emutest'),str(root/'client/YouTubeDSiCompat.nds'),
                    str(out),'5400','dsi','auto-live','legacy-locked'],stdout=f,stderr=subprocess.STDOUT,check=True)
print('Final candidate runtime and live YouTube regression finished',flush=True)
