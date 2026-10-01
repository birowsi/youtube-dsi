"""Collect additional primary-source working implementations and exact diffs."""
from pathlib import Path
import subprocess, json, urllib.request, concurrent.futures
root=Path(__file__).resolve().parents[1]
out=root/'investigation'; repos=out/'upstream'
def git(repo,*args):
    return subprocess.check_output(['git','-C',str(repos/repo),*args],text=True)
def clone(repo,url):
    path=repos/repo
    if not path.exists(): subprocess.run(['git','clone','--depth=1',url,str(path)],check=True)
    print(repo,git(repo,'log','-1','--format=%H %cs %s').strip(),flush=True)
def fetch(url):
    return urllib.request.urlopen(urllib.request.Request(url,headers={'User-Agent':'DSi-investigation'}),timeout=30).read()
def release():
    try:
        j=json.loads(fetch('https://api.github.com/repos/Epicpkmn11/dsidl/releases/tags/v0.1.1'))
        (out/'dsidl-release-v011.json').write_text(json.dumps(j,indent=2))
        for a in j['assets']:
            if a['name'].endswith('.nds'):
                (out/a['name']).write_bytes(fetch(a['browser_download_url']))
                print('Downloaded DSiDL',a['name'],flush=True)
    except Exception as e: print('DSiDL release',str(e),flush=True)
with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
    jobs=[pool.submit(clone,'classicube','https://github.com/ClassiCube/ClassiCube.git'),
          pool.submit(clone,'nds-shell','https://github.com/trustytrojan/nds-shell.git'),pool.submit(release)]
    for j in jobs: j.result()
for repo,rev in [('dswifi','d58e916dbe5f6e8e8e6ce587a3f5facbf101e7ad'),
                 ('dswifi','52269fb6595e666bdde74f05f92a6de4f9d5e4ea'),
                 ('libnds','7fd8ccbe781ed48a06fe063a2dcbb69035fa97d3')]:
    args=['show',rev] if repo=='dswifi' else ['diff',rev+'..HEAD']
    (out/(repo+'-'+rev[:8]+'.diff')).write_text(git(repo,*args))
