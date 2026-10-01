"""Compare the fused output against unmodified upstream RGB565 bit for bit."""
from pathlib import Path
import io,json,re,subprocess,zipfile
import numpy as np
from PIL import Image
root=Path(__file__).resolve().parents[1];out=root/'investigation/quality'
original=out/'codec-upstream';original.mkdir(exist_ok=True)
with zipfile.ZipFile(out/'tjpgd3.zip') as z:
    for name in ('tjpgd.c','tjpgd.h','tjpgdcnf.h'):
        entry=next(p for p in z.namelist() if p.endswith('/'+name) or p==name)
        (original/name).write_bytes(z.read(entry))
config=(original/'tjpgdcnf.h').read_text()
for key,value in [('JD_FORMAT',1),('JD_USE_SCALE',0),('JD_FASTDECODE',2)]:
    config=re.sub(r'(#define\s+'+key+r'\s+)\d+',lambda m:m[1]+str(value),config)
(original/'tjpgdcnf.h').write_text(config)
src='/mnt/c/Users/hanbi/Desktop/NDSi/youtube-dsi'
subprocess.run(['wsl','-d','Ubuntu-22.04','--','gcc','-O2','-I'+src+'/investigation/quality/codec-upstream',
    '-I'+src+'/client/source',src+'/tools/codec_check.c',src+'/client/source/hq_adpcm.c',
    src+'/investigation/quality/codec-upstream/tjpgd.c','-o',src+'/investigation/quality/codec_original'],check=True)
rng=np.random.default_rng(32)
y,x=np.indices((192,256))
patterns=[Image.fromarray(rng.integers(0,256,(192,256,3),dtype='uint8')),
    Image.fromarray(np.stack([x,(y*255//191),((x+y)%256)],axis=2).astype('uint8')),
    Image.new('RGB',(256,192),(250,20,180)),Image.fromarray(((x//4+y//4)%2*255).astype('uint8'))]
cases=0
for image in patterns:
    for quality in (1,50,92):
        for subsampling in (0,2):
            image.save(out/'compare.jpg','JPEG',quality=quality,subsampling=subsampling)
            for binary,dest in [('codec_check','fused.bin'),('codec_original','original.bin')]:
                subprocess.run(['wsl','-d','Ubuntu-22.04','--',src+'/investigation/quality/'+binary,
                    src+'/investigation/quality/codec.ima',src+'/investigation/quality/compare.pcm',
                    src+'/investigation/quality/compare.jpg',src+'/investigation/quality/'+dest],
                    check=True,stdout=subprocess.DEVNULL)
            assert (out/'fused.bin').read_bytes()==(out/'original.bin').read_bytes(),(quality,subsampling,image.mode)
            cases+=1
report={'bit_exact_vs_unmodified_TJpgDec_RGB565':True,'cases':cases,
    'patterns':['random RGB','gradient','solid saturated RGB','grayscale checkerboard'],
    'qualities':[1,50,92],'subsampling':[0,2]}
(out/'jpeg-optimization-tests.json').write_text(json.dumps(report,indent=2));print(report)
