"""Check the real ROM's GPU/SPU/network/telemetry; never imply hardware PASS."""
from pathlib import Path
import hashlib,json,struct,sys
import numpy as np
from PIL import Image
root=Path(__file__).resolve().parents[1]
base=root/'investigation/quality'

def network(path):
    raw=path.read_bytes();pos=24;commands=[];dhcp=[]
    while pos+16<=len(raw):
        size=struct.unpack_from('<I',raw,pos+8)[0]
        p=raw[pos+16:pos+16+size];pos+=16+size
        if len(p)<42 or p[12:14]!=b'\x08\x00':continue
        at=14+(p[14]&15)*4;end=14+struct.unpack_from('>H',p,16)[0]
        if p[23]==6 and len(p)>=at+20:
            payload=p[at+(p[at+12]>>4)*4:end]
            if payload.startswith((b'TEST2\n',b'SEARCH ',b'PLAY2 ')):
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

def analyze(name):
    path=base/name
    meta=json.loads((path/'static.json').read_text())
    for p in path.glob('*.bgra'):
        Image.frombytes('RGBA',(256,384),p.read_bytes(),'raw','BGRA').convert('RGB').save(p.with_suffix('.png'))
    ui_frame=path/('live-3600.png' if name=='live' else 'quality-720.png')
    ui=np.asarray(Image.open(ui_frame))[192:,:,:3]
    data_visible=int(np.count_nonzero(np.max(ui,axis=2)>40))
    assert data_visible>250,(name,'bottom UI/VRAM is blank',data_visible)
    rows=np.loadtxt(path/'quality.tsv',dtype=np.int64)
    active=rows[rows[:,1]==0x48513231]
    assert len(active)>5,name
    last=active[-1,1:]
    data=network(path/'network.pcap');assert data['dhcp_ack_observed'],data
    fields=['magic','received_frames','consumed_samples','decoded_frames','buffer_ms',
            'audio_starvations','rebuffer_count','last_decode_ms','max_decode_ms',
            'skipped_frames','received_bytes','jpeg_quality','state','network_kib_s',
            'decode_errors','shown_index']
    data.update(dict(zip(fields,map(int,last))))
    data['bottom_UI_visible_pixels']=data_visible
    data.pop('magic')
    assert data['decode_errors']==0,data
    assert data['audio_starvations']==0,data
    pcm=np.fromfile(path/'audio.pcm',dtype='<i2').reshape(-1,2)
    blocks=pcm[:len(pcm)//48000*48000].reshape(-1,48000,2).astype(float)
    rms=np.sqrt(np.mean(blocks**2,axis=(1,2)))
    audible=np.flatnonzero(rms>20)
    assert len(audible)>10,(name,rms)
    data['spu_audible_seconds']=len(audible)
    data['spu_mean_active_rms']=round(float(rms[audible].mean()),2)
    if name!='live':
        assert data['tcp_commands']==['TEST2'],data
        assert data['received_frames']==384 and data['consumed_samples']==768000,data
        mid=blocks[audible[len(audible)//2]]
        peaks=[int(np.argmax(abs(np.fft.rfft(mid[:,ch]*np.hanning(len(mid)))))) for ch in (0,1)]
        assert abs(peaks[0]-440)<=2 and abs(peaks[1]-660)<=2,peaks
        data['stereo_tone_peaks_hz']=peaks
        if name=='stall':
            assert data['rebuffer_count']>=1,data
            assert data['decoded_frames']>=383 and data['skipped_frames']<=1,data
        else:
            assert data['decoded_frames']==384 and data['skipped_frames']==0,data
            assert data['rebuffer_count']==0,data
        assert np.any(active[:,13]==3),'pause not observed'
    else:
        assert data['tcp_commands'][0]=='SEARCH deltarune dsi demo',data
        assert data['tcp_commands'][1].startswith('PLAY2 '),data
        assert len(audible)>=30,len(audible)
    data['rom_sha256']=meta['sha256']
    return data

names=sys.argv[1:] or ['normal','stall','live']
report={'real_hardware_verified':False,
        'rom_sha256':hashlib.sha256((root/'client/YouTubeDSiHQ.nds').read_bytes()).hexdigest(),
        'melonDS_commit':'906e9ebb27da8c6a715cd7abab4abfe8a8d29427',
        'profile':'legacy-locked via public APIs; emulator core unchanged',
        'tests':{name:analyze(name) for name in names},
        'codec':json.loads((base/'codec-tests.json').read_text())}
assert all(t['rom_sha256']==report['rom_sha256'] for t in report['tests'].values())
(base/'verification.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))
