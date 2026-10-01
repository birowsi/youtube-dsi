from pathlib import Path
import urllib.request,json,concurrent.futures,re
out=Path(__file__).resolve().parents[1]/'investigation'
def page(t):
    repo,num=t
    try:
        url=f'https://codeberg.org/api/v1/repos/blocksds/{repo}/issues?state=all&type=all&limit=100&page={num}'
        raw=urllib.request.urlopen(urllib.request.Request(url,headers={'User-Agent':'DSi-investigation'}),timeout=30).read()
        j=json.loads(raw);(out/f'codeberg-{repo}-issues-{num}.json').write_bytes(raw)
        matches=[{'n':i['number'],'title':i['title'],'body':i.get('body'),'url':i.get('html_url')} for i in j
                 if re.search(r'ndsi|DSi|SDIO|NWRAM|MBK|SCFG|ARM7i|white screen|heap|arm7.*boot|wifi.*(fail|hang|crash)',i['title']+' '+(i.get('body') or ''),re.I)]
        print(repo,num,json.dumps(matches,ensure_ascii=False)[:14000],flush=True)
    except Exception as e:print(t,str(e),flush=True)
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    list(pool.map(page,[('sdk',2),('sdk',3),('sdk',4),('libnds',2)]))
