from pathlib import Path
import hashlib
import os
import urllib.request
from PIL import Image, ImageDraw, ImageFont

root = Path(__file__).resolve().parent
font_path = Path(os.environ.get('LOCALAPPDATA', Path.home()/'.cache'))/'Android/water-lens-toolchain/fonts/Roboto.ttf'
if not font_path.exists():
    font_path.parent.mkdir(parents=True, exist_ok=True)
    url = 'https://raw.githubusercontent.com/google/fonts/6183fc0d26361f6ddfd6f6b7a736e1467c6d8a43/ofl/roboto/Roboto%5Bwdth,wght%5D.ttf'
    data = urllib.request.urlopen(url).read()
    if hashlib.sha256(data).hexdigest() != 'd7598e12c5dbef095ff8272cfc55da0250bd07fbdecbac8a530b9b277872a134':
        raise RuntimeError('Unexpected font checksum')
    font_path.write_bytes(data)
font = ImageFont.truetype(str(font_path), 28)
font.set_variation_by_name('Regular')
atlas = Image.new('L', (512, 264))
draw = ImageDraw.Draw(atlas)
advances = []
for code in range(32, 128):
    cell = code - 32
    draw.text(((cell % 16) * 32 + 2, (cell // 16) * 44 + 2), chr(code), font=font, fill=255)
    advances.append(f'{font.getlength(chr(code)):.4f}f')
(root/'assets/ui-font.r8').write_bytes(atlas.tobytes())
(root/'src/font_metrics.h').write_text('#pragma once\nstatic constexpr float glyphAdvance[96]{' + ','.join(advances) + '};\n')
print('Generated UI glyph atlas and matching advance metrics.')
