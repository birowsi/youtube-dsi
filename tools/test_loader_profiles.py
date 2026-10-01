import concurrent.futures,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]; OUT=ROOT/'youtube-dsi/investigation/loader-tests'
RUN=ROOT/'youtube-dsi/tools/emutest-build/emutest'
tests=[('boot-legacy','roms/tools/YouTubeDSiBoot.nds','legacy-mbk'),('audio-legacy','roms/tools/YouTubeDSiAudio.nds','legacy-mbk'),('wifi-legacy','roms/tools/YouTubeDSiWifi.nds','legacy-mbk'),('wifi-direct','roms/tools/YouTubeDSiWifi.nds','direct'),('connect-native-blocked','roms/tools/YouTubeDSiConnect.nds','native-blocked')]
def test(t):
    name,rom,profile=t; out=OUT/name; out.mkdir(parents=True,exist_ok=True)
    with (out/'run.log').open('w') as f:
        p=subprocess.run([str(RUN),str(ROOT/rom),str(out),'720','dsi','auto-connect',profile],stdout=f,stderr=subprocess.STDOUT)
    print(name,p.returncode,(out/'cpu.txt').read_text().strip(),flush=True)
with concurrent.futures.ThreadPoolExecutor(max_workers=5) as pool:
    list(pool.map(test,tests))
