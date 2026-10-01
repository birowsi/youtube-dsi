"""Validate ELF ranges, CRT reservation and packed ROM, preserving controls."""
import subprocess,struct,json,hashlib,re
from pathlib import Path
root=Path(__file__).resolve().parents[1]; out=root/'investigation'
bin=Path('/opt/wonderful/toolchain/gcc-arm-none-eabi/bin')
def tool(name,*args):return subprocess.check_output([str(bin/('arm-none-eabi-'+name)),*map(str,args)],text=True)
def syms(path):
    text=tool('nm','-n',path)
    return {m[2]:int(m[0],16) for line in text.splitlines() if len(m:=line.split())==3 and re.fullmatch('[0-9a-fA-F]+',m[0])}
a7=root/'compat-runtime/arm7.elf';a9=root/'client/build/YouTubeDSiCompat.elf';rom=root/'client/YouTubeDSiCompat.nds'
s7,s9=syms(a7),syms(a9)
assert s7['__arm7i_start__']==0x02d00000
assert s7['__twl_end__']<s7['__twl_heap_limit']==0x02d40000
assert s7['__end__']<s7['__sp_usr']<=0x0380fdc0
assert s9['__twl_end__']<0x02d00000
crt9=tool('objdump','-d',a9)
assert '2d00000' in crt9 or '#47185920' in crt9
assert 'movhi' in crt9
for name,path in [('compat-arm7',a7),('compat-arm9',a9)]:
    (out/(name+'-symbols.txt')).write_text(tool('nm','-n',path))
    (out/(name+'-sections.txt')).write_text(tool('readelf','-W','-S','-l',path))
    (out/(name+'-disasm.txt')).write_text(tool('objdump','-d',path))
def header(p):
    b=p.read_bytes();u32=lambda off:struct.unpack_from('<I',b,off)[0]
    cpu=lambda off:dict(zip(['rom','entry','ram','size'],[hex(u32(off+4*i)) for i in range(4)]))
    assert p.suffix!='.nds' or len(b)>0x200
    return {'sha256':hashlib.sha256(b).hexdigest(),'unit':b[0x12],
            'arm9':cpu(0x20),'arm7':cpu(0x30),'arm9i':cpu(0x1c0),'arm7i':cpu(0x1d0),
            'mbk_global':b[0x180:0x194].hex(),'mbk_arm7':[hex(u32(o)) for o in [0x1a0,0x1a4,0x1a8]],
            'mbk9':hex(u32(0x1ac)),'access':hex(u32(0x1b4)),
            'scfg_mask':hex(u32(0x1b8)),'app_flags':hex(b[0x1bf]),
            'device_list_header_reserved':b[0x1e0:0x200].hex()}
report={'arm7_symbols':{k:hex(s7[k]) for k in ['__arm7_start__','__arm7_end__','__arm7i_lma__','__arm7i_start__','__arm7i_end__','__twl_bss_start__','__twl_bss_end__','__twl_heap_limit','__end__','__sp_usr','__sp_irq','__sp_svc']},
        'arm9_symbols':{k:hex(s9[k]) for k in ['__end__','__twl_end__','__sp_usr']},
        'headers':{p.name:header(p) for p in [rom,out/'mtheall-ftpd-ftpd.nds',out/'Epicpkmn11-dsidl-dsidl.dsi']},
        'control_hashes':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (root.parent/'roms/tools').glob('YouTube*.nds')}}
b=rom.read_bytes();off,size=struct.unpack_from('<I',b,0x1d0)[0],struct.unpack_from('<I',b,0x1dc)[0]
# Confirm ARM7i is packed at its declared LMA, with a nonempty image.
assert struct.unpack_from('<I',b,0x1d8)[0]==s7['__arm7i_lma__']
assert size==s7['__arm7i_end__']-s7['__arm7i_start__']>0
raw=out/'compat-arm7i.bin';subprocess.run([str(bin/'arm-none-eabi-objcopy'),'-O','binary','-j','.twl',str(a7),str(raw)],check=True)
assert raw.read_bytes()==b[off:off+size]
(out/'compat-static.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))
