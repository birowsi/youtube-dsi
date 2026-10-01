"""Offline relay for emulator checks: real server_quality.py code, stubbed YouTube."""
import sys, math, struct, itertools
sys.path.insert(0,'/home/claude/emu/fake'); sys.path.insert(0,'/home/claude/b/server')
import server_quality as sq
import server as legacy
from PIL import Image, ImageDraw, ImageFilter
legacy.search=lambda q:[{'id':'AAAAAAAAAA%d'%i,'title':t} for i,t in enumerate([
 'TheFatRat - Jackpot (Official Video)',
 'お兄ちゃんはおしまい！ ノンクレジットOP「アイシー・ストリート」 | Onimai: I\'m Now Your Sister! Opening',
 '한글 제목 테스트 - 아주 긴 제목이 두 줄로 잘 나뉘는지 확인하는 영상입니다',
 'Short',
 'Nintendo DSi homebrew demo reel 2026',
 'Lo-fi beats to study / relax to (24/7 stream archive)',
])]
base=Image.open('/home/claude/w/NDSi/youtube-dsi/investigation/quality/compare.jpg').convert('RGB').resize((256,192)).filter(ImageFilter.GaussianBlur(0.8))
def fake_live(*a):
    for i in itertools.count():
        if i>=sq.FPS*40: return
        img=base.rotate((i%32)-16) if (i//96)%2 else Image.new('RGB',(256,192),(20+i%200,60,120))
        d=ImageDraw.Draw(img); d.rectangle((10,150,10+(i*4)%230,170),fill=(255,255,255)); d.text((12,12),'frame %d'%i,fill=(255,255,0))
        samples=[]
        for j in range(sq.SAMPLES):
            t=(i*sq.SAMPLES+j)/sq.RATE; v=round(5000*math.sin(2*math.pi*330*t)); samples+= [v,v]
        yield img, struct.pack('<'+'h'*len(samples),*samples)
sq.resolve=lambda vid:(None,None)
sq.live_frames=fake_live
sys.argv=['x','--bind','0.0.0.0','--port','8767']
sq.main()
