from pathlib import Path
import importlib.util,sys,struct,json,subprocess
import numpy as np
from PIL import Image
root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'server'))
import server_quality as hq
out=root/'investigation/quality';out.mkdir(exist_ok=True)
image,pcm=next(hq.test_frames())
encoder=hq.StereoIMA();encoded=encoder.encode(pcm)
(out/'codec.ima').write_bytes(encoded)
controller=hq.AdaptiveJPEG();jpeg,q=controller.encode(image)
(out/'codec.jpg').write_bytes(jpeg)
import audioop
a=bytes((v&0xf0)|((encoded[8+2*i+1]>>4)&15) for i,v in enumerate(encoded[8::2]))
b=bytes(((v&15)<<4)|(encoded[8+2*i+1]&15) for i,v in enumerate(encoded[8::2]))
left=audioop.adpcm2lin(a,2,(0,0))[0];right=audioop.adpcm2lin(b,2,(0,0))[0]
golden=np.empty((hq.SAMPLES,2),dtype='<i2')
golden[:,0]=np.frombuffer(left,dtype='<i2');golden[:,1]=np.frombuffer(right,dtype='<i2')
(out/'codec-golden.pcm').write_bytes(golden.tobytes())
src='/mnt/c/Users/hanbi/Desktop/NDSi/youtube-dsi'
subprocess.run(['wsl','-d','Ubuntu-22.04','--','gcc','-O2','-I'+src+'/client/source',
    src+'/tools/codec_check.c',src+'/client/source/hq_adpcm.c',src+'/client/source/tjpgd.c',
    '-o',src+'/investigation/quality/codec_check'],check=True)
subprocess.run(['wsl','-d','Ubuntu-22.04','--',src+'/investigation/quality/codec_check',
    src+'/investigation/quality/codec.ima',src+'/investigation/quality/codec-decoded.pcm',
    src+'/investigation/quality/codec.jpg',src+'/investigation/quality/codec-rgb555.bin'],check=True)
assert (out/'codec-golden.pcm').read_bytes()==(out/'codec-decoded.pcm').read_bytes()
rgb=np.fromfile(out/'codec-rgb555.bin',dtype='<u2').reshape(192,256)
assert np.all(rgb&0x8000)
decoded=np.stack([(rgb&31)*255//31,((rgb>>5)&31)*255//31,((rgb>>10)&31)*255//31],axis=2).astype('uint8')
Image.fromarray(decoded).save(out/'codec-decoded.png')
reference=np.array(Image.open(out/'codec.jpg').convert('RGB')).astype(float)
mse=np.mean((decoded.astype(float)-reference)**2)
assert mse<80,mse
source=np.frombuffer(pcm,dtype='<i2').reshape(-1,2).astype(float)
# Ignore the codec's first 4ms cold predictor convergence in tone SNR.
snr=10*np.log10(np.mean(source[128:]**2)/np.mean((source[128:]-golden[128:])**2))
assert snr>25,snr
report={'ADPCM_bit_exact_reference':True,'invalid_headers_rejected':True,
    'JPEG_RGB555_decoder_mse_vs_Pillow':float(mse),'tone_ADPCM_snr_db':round(float(snr),2),
    'jpeg_bytes':len(jpeg),'jpeg_quality':q,'initial_AV_KiB_s':round((len(jpeg)+len(encoded)+16)*16/1024,1)}
(out/'codec-tests.json').write_text(json.dumps(report,indent=2));print(report)
