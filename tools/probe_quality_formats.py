"""Print safe format metadata and bounded HTTP probes, never signed URLs."""
from pathlib import Path
import sys,json,urllib.request
root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'server'))
import server as s
with s.yt_dlp.YoutubeDL(s.ydl_options(format='best')) as y:
    data=y.extract_info('https://www.youtube.com/watch?v=2Wi9SJYScKg',download=False,process=False)
formats=[]
for f in data['formats']:
    if (f.get('height') or 0)>480:continue
    d={k:f.get(k) for k in ('format_id','protocol','ext','height','vcodec','acodec','abr','format_note')}
    if f.get('format_id') in ('18','91','92','93','94','95','140','251','244','397'):
        try:
            req=urllib.request.Request(f['url'],headers={**f.get('http_headers',{}),'Range':'bytes=0-1023'})
            with urllib.request.urlopen(req,timeout=10) as response:
                d['probe_status']=response.status;d['probe_bytes']=len(response.read(1024))
        except Exception as e:d['probe_error']=type(e).__name__+' '+str(getattr(e,'code',''))
    formats.append(d)
(root/'investigation/quality/source-formats.json').write_text(json.dumps(formats,indent=2))
print(json.dumps(formats,indent=2))
