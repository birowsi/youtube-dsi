"""Analyze actual melonDS GPU, SPU and Ethernet output from the fixed ROM."""
from pathlib import Path
import hashlib,json,struct
import numpy as np
from PIL import Image
root=Path(__file__).resolve().parents[1]
report={'rom_sha256':hashlib.sha256((root/'client/YouTubeDSiCompat.nds').read_bytes()).hexdigest(),
        'real_hardware_verified':False,'melonDS_commit':'906e9ebb27da8c6a715cd7abab4abfe8a8d29427',
        'profile_semantics':'Synthetic loader constraints via public APIs; no emulator core changes.',
        'tests':{}}
# Preserve human evidence independently from regenerated emulator measurements.
# A different ROM hash must be verified again rather than inheriting a PASS.
hardware_path=root/'investigation/real-hardware-verification.json'
if hardware_path.exists():
    hardware=json.loads(hardware_path.read_text(encoding='utf-8'))
    if hardware['rom_sha256']==report['rom_sha256']:
        report['real_hardware_verified']=True
        report['real_hardware_evidence']=hardware
def packets(path):
    raw=path.read_bytes();pos=24
    while pos+16<=len(raw):
        size=struct.unpack_from('<I',raw,pos+8)[0]
        yield raw[pos+16:pos+16+size]
        pos+=16+size
def network(path):
    commands=[];dhcp=[]
    for p in packets(path):
        if len(p)<42 or p[12:14]!=b'\x08\x00':continue
        at=14+(p[14]&15)*4;end=14+struct.unpack_from('>H',p,16)[0]
        if p[23]==6 and len(p)>=at+20:
            payload=p[at+(p[at+12]>>4)*4:end]
            if payload.startswith((b'TEST\n',b'SEARCH ',b'PLAY ')):
                c=payload.decode('ascii').strip()
                if c not in commands:commands.append(c)
        if p[23]==17 and len(p)>=at+8:
            sport,dport=struct.unpack_from('>HH',p,at)
            if {sport,dport}=={67,68}:
                payload=p[at+8:end]
                if len(payload)>=240 and payload[236:240]==b'\x63\x82\x53\x63':
                    j=240
                    while j+2<=len(payload):
                        opt=payload[j]
                        if opt==255:break
                        if opt==0:j+=1;continue
                        n=payload[j+1]
                        if opt==53 and n==1 and payload[j+2] not in dhcp:dhcp.append(payload[j+2])
                        j+=2+n
    return {'tcp_commands':commands,'dhcp_message_types':dhcp,'dhcp_ack_observed':5 in dhcp}
paths=[root/'investigation/compat-tests'/p for p in ['direct','legacy-mbk','legacy-locked']]
paths += [root/'investigation/compat-live']
for path in paths:
    for p in path.glob('*.bgra'):
        Image.frombytes('RGBA',(256,384),p.read_bytes(),'raw','BGRA').convert('RGB').save(p.with_suffix('.png'))
    pcm=np.fromfile(path/'audio.pcm',dtype='<i2').reshape(-1,2)
    seconds=len(pcm)//48000
    blocks=pcm[:seconds*48000].reshape(seconds,48000,2).astype(float)
    rms=np.sqrt(np.mean(blocks**2,axis=(1,2)))
    active=np.flatnonzero(rms>20)
    assert len(active)>5,(path.name,rms)
    data=network(path/'network.pcap')
    assert data['dhcp_ack_observed'],data
    if path.name!='compat-live':
        assert data['tcp_commands']==['TEST'],data
        mid=blocks[active[len(active)//2],:,0]
        data['test_tone_peak_hz']=int(np.argmax(abs(np.fft.rfft(mid*np.hanning(len(mid))))))
        assert abs(data['test_tone_peak_hz']-440)<=1,data
    else:
        assert data['tcp_commands']==['SEARCH deltarune dsi demo','PLAY 2Wi9SJYScKg'],data
        assert len(active)>=30,len(active)
    data.update({'spu_nonzero_seconds':len(active),'spu_active_mean_rms':round(float(rms[active].mean()),2),
                 'cpu':(path/'cpu.txt').read_text()})
    report['tests'][path.name]=data
(root/'investigation/compat-verification.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))
