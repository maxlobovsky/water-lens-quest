"""Render a static UI reference from the exported native quads and glyph atlas.

This is an illustration of the app geometry, not a GPU or live tracking test.
Run test-controls.ps1 -Preview first to refresh both mesh exports.
"""
import json
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent
S = 2
image = Image.new('RGB', (1200*S, 920*S), '#102c30')
atlas = Image.frombytes('L', (512, 264), (ROOT/'assets/ui-font.r8').read_bytes())

def label(text, xy, size=22, color='#d9eff0'):
    font = ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf', size*S)
    ImageDraw.Draw(image).text((xy[0]*S, xy[1]*S), text, fill=color, font=font)

def mesh(name, left, top, scale):
    data = json.loads((ROOT/'build'/name).read_text(encoding='utf-8-sig'))['vertices']
    quads = [[data[j:j+12] for j in range(i, i+72, 12)] for i in range(0, len(data), 72)]
    min_x = min(v[0] for q in quads for v in q)
    max_y = max(v[1] for q in quads for v in q)
    for q in quads:
        x0, x1 = min(v[0] for v in q), max(v[0] for v in q)
        y0, y1 = min(v[1] for v in q), max(v[1] for v in q)
        px, py = round((left+(x0-min_x)*scale)*S), round((top+(max_y-y1)*scale)*S)
        w, h = max(1, round((x1-x0)*scale*S)), max(1, round((y1-y0)*scale*S))
        rgba = tuple(round(v*255) for v in q[0][4:8])
        if q[0][8] > .5:
            u0,u1 = min(v[2] for v in q), max(v[2] for v in q)
            v0,v1 = min(v[3] for v in q), max(v[3] for v in q)
            mask = atlas.crop((round(u0*512),round(v0*264),round(u1*512),round(v1*264))).resize((w,h), Image.Resampling.BILINEAR)
        else:
            mask = Image.new('L', (w,h))
            ImageDraw.Draw(mask).rounded_rectangle((0,0,w-1,h-1),radius=q[0][11]*scale*S,fill=rgba[3])
        image.paste(Image.new('RGB',(w,h),rgba[:3]), (px,py),mask)

label('Water Lens', (35,25), 40)
label('Starts in the working Camera mode at 100% view.', (35,82), 22, '#a9c5bf')
label('MAIN CONTROL PANEL', (35,137), 16, '#a9c5bf')
mesh('panel-preview.json',35,175,680)
label('STARTUP / PANEL HIDDEN', (745,137), 16, '#a9c5bf')
ImageDraw.Draw(image).rounded_rectangle((745*S,185*S,1165*S,465*S),radius=22*S,fill='#183c40',outline='#426267',width=S)
label('Your view stays clear', (815,275), 22, '#a9c5bf')
mesh('reopen-preview.json',1028,408,700)
label('Point at Controls, then pinch.', (745,501), 21)
label('The tab follows your screen.', (745,540), 21)
label('Camera mode is the default.', (745,621), 23)
label('Surface test is experimental.', (745,661), 21)
label('Water OFF restores', (745,731), 21)
label('normal system passthrough.', (745,762), 21)
label('Native UI geometry preview — not headset footage', (35,878), 18, '#a9c5bf')
image.resize((1200,920), Image.Resampling.LANCZOS).save(ROOT/'preview.jpg',quality=95)
print(ROOT/'preview.jpg')
