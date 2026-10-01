import json, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]; OUT=ROOT/'youtube-dsi/investigation'; REPOS=OUT/'upstream'
def git(repo,*args):
    return subprocess.run(['git','-C',str(repo),*args],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT).stdout
results={}
for repo,paths in {'sdk':['sys/crts','tools/ndstool','libs'],'dswifi':['source/arm7/twl/card.twl.c','source/arm7/twl/sdio.twl.c','source/arm7/twl/ndma.twl.c','source/arm7/wfc.c','source/arm9/access_point.c'],'libnds':['source/arm7','include/nds/arm7'],'devkit-libnds':['source','include']}.items():
    r=REPOS/repo
    results[repo]=git(r,'log','--format=%h %cs %s','--',*paths)
    (OUT/(repo+'-history.txt')).write_text(results[repo])
print('SDK release submodules:\n'+git(REPOS/'sdk','ls-tree','v1.24.0:libs'))
print('DSWiFi recent relevant history:\n'+results['dswifi'][:9500])
print('SDK CRT history:\n'+git(REPOS/'sdk','log','-25','--format=%h %cs %s','--','sys/crts'))
for name in ['twilight-research','nds-bootstrap-research']:
    r=ROOT/'youtube-dsi/tools'/name
    paths=git(r,'ls-tree','-r','--name-only','HEAD').splitlines()
    out=OUT/name
    for p in paths:
        if p.endswith(('.cpp','.c','.h','.s','.S')) and ('bootloader' in p or 'bootstub' in p or 'load_crt' in p):
            content=git(r,'show','HEAD:'+p)
            if any(t in content.lower() for t in ['mbk','scfg','nwram']):
                dest=out/p; dest.parent.mkdir(parents=True,exist_ok=True); dest.write_text(content)
    print(name,'loader evidence files:',len(list(out.rglob('*.*'))))
