from pathlib import Path
import urllib.request,json,concurrent.futures,re
out=Path(__file__).resolve().parents[1]/'investigation'
def task(repo):
    try:
        url=f'https://codeberg.org/api/v1/repos/blocksds/{repo}/issues?state=all&type=all&limit=100'
        raw=urllib.request.urlopen(urllib.request.Request(url,headers={'User-Agent':'DSi-source-investigation'}),timeout=30).read()
        j=json.loads(raw);(out/('codeberg-'+repo+'-issues.json')).write_bytes(raw)
        print(repo,json.dumps([{'number':i['number'],'title':i['title'],'body':i.get('body'),'url':i.get('html_url')} for i in j
                              if re.search('wifi|sdio|nwram|mbk|scfg|arm7|boot|twl',i['title']+' '+(i.get('body') or ''),re.I)],ensure_ascii=False)[:15000],flush=True)
    except Exception as e:print(repo,str(e),flush=True)
with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
    list(pool.map(task,['sdk','libnds','dswifi']))
