import concurrent.futures, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
DEST=ROOT/'youtube-dsi/investigation/upstream'
DEST.mkdir(exist_ok=True)
repos={'ftpd':'https://github.com/mtheall/ftpd.git', 'ftpd-dsi':'https://github.com/shinyquagsire23/ftpd.git','dsiwifi':'https://github.com/shinyquagsire23/dsiwifi.git', 'dsidl':'https://github.com/Epicpkmn11/DSiDL.git', 'sdk':'https://github.com/blocksds/sdk.git','dswifi':'https://github.com/blocksds/dswifi.git','libnds':'https://github.com/blocksds/libnds.git','devkit-libnds':'https://github.com/devkitPro/libnds.git','devkit-dswifi':'https://github.com/devkitPro/dswifi.git'}
def fetch(item):
    name,url=item; dest=DEST/name
    args=['git','clone','--quiet',url,str(dest)] if not dest.exists() else ['git','-C',str(dest),'fetch','--quiet','origin']
    p=subprocess.run(args,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    log=subprocess.run(['git','-C',str(dest),'log','-1','--format=%H %cs %s'],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (DEST/(name+'.fetch.txt')).write_text(p.stdout+log.stdout)
    print(name,p.returncode,log.stdout.strip(),flush=True)
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    list(pool.map(fetch,repos.items()))
