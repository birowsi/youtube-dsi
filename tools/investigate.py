"""Reproducible, read-only static evidence collection for hardware fixes."""
import hashlib, json, os, struct, subprocess, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'youtube-dsi/investigation'
OUT.mkdir(exist_ok=True)
SDK=Path('/opt/wonderful/thirdparty/blocksds/core')
BIN=Path('/opt/wonderful/toolchain/gcc-arm-none-eabi/bin')
def run(args):
    p=subprocess.run([str(a) for a in args],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    return p.stdout
def inventory():
    data={}
    for name,p in [('sdk',ROOT/'reference/blocksds-reference')]+[(n,ROOT/'youtube-dsi/tools'/n) for n in ['dswifi-src','libnds-src','maxmod-src','melonDS-src','nds-bootstrap-research','twilight-research']]:
        data[name]={'head':run(['git','-C',p,'log','-1','--format=%H %cs %s']).strip(), 'remote':run(['git','-C',p,'remote','-v']).strip(),'shallow':run(['git','-C',p,'rev-parse','--is-shallow-repository']).strip(), 'status':run(['git','-C',p,'status','--short']).strip()}
    data['installed_version']=(SDK/'version.txt').read_text()
    data['compiler']=run([BIN/'arm-none-eabi-gcc','--version'])
    data['packages']=run(['/opt/wonderful/bin/wf-pacman','-Q'])
    (OUT/'inventory.json').write_text(json.dumps(data,indent=2))
    print(json.dumps(data,indent=2))
def disasm():
    elfs={'connect2':ROOT/'youtube-dsi/hardware-runtime/arm7_probe.elf','connect3':ROOT/'youtube-dsi/hardware-runtime/arm7_YouTubeDSiConnect3.elf'}
    elfs.update({n:SDK/f'sys/arm7/main_core/arm7_{n}.elf' for n in ['dswifi','maxmod','dswifi_maxmod','minimal']})
    for name,elf in elfs.items():
        if not elf.exists(): continue
        for suffix,args in [('symbols',['nm','-n']),('sections',['readelf','-W','-S','-l']),('disasm',['objdump','-d'])]:
            cmd=BIN/('arm-none-eabi-'+args[0]); content=run([cmd,*args[1:],elf]); (OUT/f'{name}-{suffix}.txt').write_text(content)
        print(name,hashlib.sha256(elf.read_bytes()).hexdigest())
    print('Disassembly and memory sections saved to',OUT)
def headers():
    data={}
    for p in sorted((ROOT/'roms/tools').glob('*.nds')):
        if not (p.name.startswith('YouTube') or p.name=='ftpd.nds'): continue
        b=p.read_bytes(); u32=lambda off:struct.unpack_from('<I',b,off)[0]
        cpu=lambda off:dict(zip(['rom','entry','ram','size'],[hex(u32(off+i*4)) for i in range(4)]))
        data[p.name]={'sha256':hashlib.sha256(b).hexdigest(),'unit':b[0x12], 'arm9':cpu(0x20),'arm7':cpu(0x30),'arm9i':cpu(0x1c0),'arm7i':cpu(0x1d0), 'mbk_global':b[0x180:0x194].hex(),'mbk_arm9':[hex(u32(i)) for i in [0x194,0x198,0x19c]],'mbk_arm7':[hex(u32(i)) for i in [0x1a0,0x1a4,0x1a8]],'mbk9':hex(u32(0x1ac)), 'access':hex(u32(0x1b4)), 'scfg_mask':hex(u32(0x1b8)), 'app_flags':hex(b[0x1bf]),'title':b[:12].decode('ascii',errors='replace')}
        for cpu_name,off in [('arm7',0x30),('arm7i',0x1d0)]:
            (OUT/f'{p.stem}-{cpu_name}.bin').write_bytes(b[u32(off):u32(off)+u32(off+12)])
    (OUT/'headers.json').write_text(json.dumps(data,indent=2))
    print(json.dumps(data,indent=2))
if __name__=='__main__':
    {'inventory':inventory,'disasm':disasm,'headers':headers}[sys.argv[1]]()
