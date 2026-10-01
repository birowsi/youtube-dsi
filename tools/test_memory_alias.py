from pathlib import Path
import subprocess,concurrent.futures,struct,json
root=Path(__file__).resolve().parents[2]; out=root/'youtube-dsi/investigation/alias-tests'
runner=root/'youtube-dsi/tools/emutest-build/emutest'
def run(name):
    target=out/name; target.mkdir(parents=True,exist_ok=True)
    with (target/'run.log').open('w') as f:
        subprocess.run([str(runner),str(root/f'roms/tools/YouTubeDSi{name}.nds'),str(target),
                        '360','dsi','none','legacy-mbk'],stdout=f,stderr=subprocess.STDOUT,check=True)
    mem=(target/'memory.bin').read_bytes()
    rom=(root/f'roms/tools/YouTubeDSi{name}.nds').read_bytes()
    offset,size=struct.unpack_from('<I',rom,0x1d0)[0],struct.unpack_from('<I',rom,0x1dc)[0]
    original=rom[offset:offset+size]
    report={'cpu':(target/'cpu.txt').read_text(), 'twl_size':hex(size),
            'first_32_original':original[:32].hex(), 'first_32_at_03000000':mem[:32].hex(),
            'first_32_at_03008000':mem[0x8000:0x8020].hex(),
            'first_32_zero':mem[:32]==bytes(32)}
    (target/'evidence.json').write_text(json.dumps(report,indent=2))
    print(name,json.dumps(report),flush=True)
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
    list(pool.map(run,['Wifi','Audio']))
