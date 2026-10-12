#!/usr/bin/env python3
"""Build the Cyrillic BMFont shipped with the mod, using Pillow + DejaVu Sans."""
from pathlib import Path
import argparse
import math
from PIL import Image, ImageDraw, ImageFont

parser = argparse.ArgumentParser()
parser.add_argument('--font', default='/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf')
parser.add_argument('--out', type=Path, default=Path(__file__).resolve().parents[1] / 'resources')
args = parser.parse_args()
args.out.mkdir(parents=True, exist_ok=True)
font = ImageFont.truetype(args.font, 32)
ascent, descent = font.getmetrics()
chars = sorted(set(range(32, 127)) | set(range(160, 256)) |
               set(range(0x400, 0x500)) | set(range(0x2010, 0x2027)))
width = 1024
padding = 2
rows = []
x = y = row_height = 0
for code in chars:
    char = chr(code)
    left, top, right, bottom = font.getbbox(char)
    glyph_width = max(1, right - left)
    glyph_height = max(1, bottom - top)
    if x + glyph_width + 2 * padding > width:
        x = 0
        y += row_height
        row_height = 0
    rows.append((code, x + padding, y + padding, glyph_width, glyph_height,
                 left, top, round(font.getlength(char))))
    x += glyph_width + 2 * padding
    row_height = max(row_height, glyph_height + 2 * padding)
height = 2 ** math.ceil(math.log2(max(1, y + row_height)))
atlas = Image.new('RGBA', (width, height), (0, 0, 0, 0))
draw = ImageDraw.Draw(atlas)
for code, gx, gy, gw, gh, left, top, advance in rows:
    draw.text((gx - left, gy - top), chr(code), font=font, fill=(255, 255, 255, 255))
lines = [
    'info face="CommentFont" size=32 bold=0 italic=0 charset="" unicode=1 stretchH=100 smooth=1 aa=1 padding=0,0,0,0 spacing=0,0',
    f'common lineHeight={ascent + descent} base={ascent} scaleW={width} scaleH={height} pages=1 packed=0',
    'page id=0 file="comment-font.png"',
    f'chars count={len(rows)}',
]
for code, gx, gy, gw, gh, left, top, advance in rows:
    lines.append(f'char id={code} x={gx} y={gy} width={gw} height={gh} '
                 f'xoffset={left} yoffset={top} xadvance={advance} page=0 chnl=15')
lines.append('kernings count=0')
atlas.save(args.out / 'comment-font.png', optimize=True)
(args.out / 'comment-font.fnt').write_text('\n'.join(lines) + '\n', encoding='utf-8')
print(f'Generated {len(rows)} glyphs, {width}x{height} atlas')
