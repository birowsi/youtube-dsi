"""Fetch public primary-source metadata and reference releases, without uploads."""
import json, re, urllib.request, hashlib, concurrent.futures
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]; OUT=ROOT/'youtube-dsi/investigation'
def fetch(url):
    return urllib.request.urlopen(urllib.request.Request(url,headers={'User-Agent':'DSi-hardware-investigation'}),timeout=40).read()
def issues(repo):
    try:
        data=json.loads(fetch(f'https://api.github.com/repos/{repo}/issues?state=all&per_page=100'))
        (OUT/(repo.replace('/','-')+'-issues.json')).write_text(json.dumps(data,indent=2))
        match=[{'n':i['number'],'title':i['title'],'url':i['html_url'],'body':i.get('body')} for i in data if re.search(r'wifi|sdio|mbk|nwram|scfg|twl|arm7|boot|dsi',i['title']+' '+(i.get('body') or ''),re.I)]
        print(repo,json.dumps(match,ensure_ascii=False)[:14000],flush=True)
    except Exception as e: print(repo,str(e),flush=True)
def release(repo,tag):
    try:
        data=json.loads(fetch(f'https://api.github.com/repos/{repo}/releases/{tag}'))
        (OUT/(repo.replace('/','-')+'-release.json')).write_text(json.dumps(data,indent=2))
        for a in data['assets']:
            if a['name'].endswith(('.nds','.dsi')):
                b=fetch(a['browser_download_url']); p=OUT/(repo.replace('/','-')+'-'+a['name']); p.write_bytes(b)
                print('release',p.name,len(b),hashlib.sha256(b).hexdigest(),flush=True)
    except Exception as e: print(repo,str(e),flush=True)
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    jobs=[pool.submit(issues,r) for r in ['blocksds/sdk','blocksds/libnds','blocksds/dswifi','shinyquagsire23/dsiwifi','Epicpkmn11/dsidl']]
    jobs += [pool.submit(release,r,t) for r,t in [('mtheall/ftpd','tags/v3.2.1'),('Epicpkmn11/dsidl','latest')]]
    for j in jobs: j.result()
for name in ['ftpd','YouTubeDSiWifi','YouTubeDSiAudio']:
    b=(ROOT/f'roms/tools/{name}.nds').read_bytes()
    strings=[m.decode() for m in re.findall(rb'[ -~]{6,}',b) if re.search(rb'sgIP|lwIP|wifi|WMI|SDIO|BMI|gcc|v[1234]\.',m,re.I)]
    (OUT/(name+'-strings.txt')).write_text('\n'.join(strings))
    print(name,'strings:',strings[:35],flush=True)
