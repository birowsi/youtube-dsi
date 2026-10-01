from pathlib import Path
import urllib.request,json,subprocess,hashlib,struct,concurrent.futures
out=Path(__file__).resolve().parents[1]/'investigation'
def fetch(url):
    return urllib.request.urlopen(urllib.request.Request(url,headers={'User-Agent':'DSi-investigation'}),timeout=45).read()
def dl(repo,tag):
    j=json.loads(fetch(f'https://api.github.com/repos/{repo}/releases/tags/{tag}'))
    (out/(repo.replace('/','-')+'-'+tag+'.json')).write_text(json.dumps(j,indent=2))
    for a in j['assets']:
        if a['name'].endswith(('.nds','.dsi')):
            p=out/(repo.replace('/','-')+'-'+a['name']); p.write_bytes(fetch(a['browser_download_url']))
            print(p.name,len(p.read_bytes()),hashlib.sha256(p.read_bytes()).hexdigest(),flush=True)
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
    fs=[pool.submit(dl,'Epicpkmn11/dsidl','v0.1.1'),pool.submit(dl,'ClassiCube/ClassiCube','1.3.8')]
    for f in fs:
        try:f.result()
        except Exception as e:print(str(e),flush=True)
repo=out/'upstream/ftpd'
for path in ['CMakeLists.txt','source/nds/platform.cpp']:
    p=out/('ftpd-v321-'+Path(path).name)
    p.write_text(subprocess.check_output(['git','-C',str(repo),'show','v3.2.1:'+path],text=True))
print('Exact ftpd tag sources saved',flush=True)
