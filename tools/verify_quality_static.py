from pathlib import Path
import subprocess,struct,json,hashlib
root=Path(__file__).resolve().parents[1];out=root/'investigation/quality'
prefix='/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-'
elf=root/'client/build/YouTubeDSiHQ.elf';rom=root/'client/YouTubeDSiHQ.nds'
def command(name,*args):return subprocess.check_output([prefix+name,*map(str,args)],text=True)
symbols={}
for line in command('nm','-n',elf).splitlines():
    fields=line.split()
    if len(fields)==3:
        try:symbols[fields[2]]=int(fields[0],16)
        except ValueError:pass
assert symbols['__end__']<0x02380000
assert symbols['__itcm_size']<32*1024
assert symbols['__arm9i_start__']==0x02400000
assert 0x02400000<=symbols['__twl_bss_start__']<symbols['__twl_end__']<0x02d00000
baseline=(root/'releases/YouTubeDSiCompat-20261001/client/YouTubeDSiCompat.nds').read_bytes()
data=rom.read_bytes()
for address in [0x30,0x1d0]:
    assert data[address+4:address+16]==baseline[address+4:address+16], 'ARM7 entry/load/size differs'
    def image(b):
        offset,_,_,size=struct.unpack_from('<4I',b,address)
        return b[offset:offset+size]
    assert image(data)==image(baseline),'ARM7 runtime differs from working release'
assert data[0x12]==baseline[0x12]==2
assert data[0x180:0x1c0]==baseline[0x180:0x1c0], 'DSi MBK/access/SCFG fields differ'
report={'sha256':hashlib.sha256(data).hexdigest(),'arm7_identical_to_hardware_working_release':True,
    'ARM9_normal_end':hex(symbols['__end__']),
    'ARM9i_start':hex(symbols['__arm9i_start__']),
    'ARM9_TWL_BSS_end':hex(symbols['__twl_end__']),
    'ARM9_ITCM_used_bytes':symbols['__itcm_size'],
    'ARM9_heap_limit':'0x02d00000','ARM7_reserved_end':'0x02d40000',
    'stats_address':hex(symbols['hq_stats'])}
(out/'static.json').write_text(json.dumps(report,indent=2))
(out/'arm9-sections.txt').write_text(command('readelf','-W','-S','-l',elf))
(out/'arm9-symbols.txt').write_text(command('nm','-n',elf))
print(json.dumps(report,indent=2))
