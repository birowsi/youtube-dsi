"""Decode the actual MOD7 relocation tables in the official ftpd release.

The CRT function at file offset0x284 reads triples [VMA, initialized_end,
BSS_end], copies successive LMA bytes with memcpy, then clears the BSS tail.
The module header gives separate normal and TWL table bounds. This establishes
runtime destinations without claiming to possess the original release ELF.
"""
from pathlib import Path
import hashlib,json,struct

root=Path(__file__).resolve().parents[1]
out=root/'investigation'
rom=(out/'mtheall-ftpd-ftpd.nds').read_bytes()
assert hashlib.sha256(rom).hexdigest()=='63cf06c4e13772544630f60c91921c9fd616ad507b55174cb057fc3125fa8423'
def image(off):
    start,_,_,size=struct.unpack_from('<4I',rom,off)
    return rom[start:start+size]
a,t=image(0x30),image(0x1d0)
assert a[4:8]==b'MOD7'
m=struct.unpack_from('<6I',a,8)
def table(data,start,end,base):
    assert base<=start<=end<=base+len(data)
    assert (end-start)%12==0
    return [[hex(v) for v in struct.unpack_from('<3I',data,i)]
            for i in range(start-base,end-base,12)]
report={'rom_sha256':hashlib.sha256(rom).hexdigest(),
        'module_words':[hex(v) for v in m],
        'triple_semantics':['VMA','initialized_end','BSS_end'],
        'normal_sections':table(a,m[1],m[2],0x02380000),
        'twl_sections':table(t,m[4],m[5],m[3])}
assert report['twl_sections']==[['0x37c0000','0x37c757c','0x37c9ca8']]
(out/'ftpd-module-layout.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))
