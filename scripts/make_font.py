#!/usr/bin/env python3
"""Regenerate the included Unicode BMFont. Requires Pillow and DejaVuSans.ttf.
Usage: python scripts/make_font.py /path/to/DejaVuSans.ttf
The generated atlas is already included; rebuilding the mod needs no Python.
"""
from pathlib import Path
import sys
from PIL import Image, ImageDraw, ImageFont

font = ImageFont.truetype(sys.argv[1], 36)
characters = [chr(c) for c in range(32, 127)]
characters += [chr(c) for c in range(0x410, 0x450)] + ["Ё", "ё"]
columns, cell_w, cell_h = 16, 52, 50
width, height = columns * cell_w, ((len(characters) + columns - 1) // columns) * cell_h
atlas = Image.new("RGBA", (width, height))
draw = ImageDraw.Draw(atlas)
lines = [
    'info face="DejaVu Sans" size=36 bold=0 italic=0 charset="" unicode=1 stretchH=100 smooth=1 aa=1 padding=0,0,0,0 spacing=1,1',
    f"common lineHeight=44 base=34 scaleW={width} scaleH={height} pages=1 packed=0",
    'page id=0 file="bot.png"',
    f"chars count={len(characters)}",
]
for i, character in enumerate(characters):
    left, top, right, bottom = font.getbbox(character)
    x, y = i % columns * cell_w + 2, i // columns * cell_h + 2
    w, h = max(0, right - left), max(0, bottom - top)
    draw.text((x - left, y - top), character, font=font, fill="white")
    lines.append(f"char id={ord(character)} x={x} y={y} width={w} height={h} xoffset={left} yoffset={top} xadvance={round(font.getlength(character))} page=0 chnl=15")
directory = Path(__file__).resolve().parent.parent / "resources"
directory.mkdir(exist_ok=True)
atlas.save(directory / "bot.png")
(directory / "bot.fnt").write_text("\n".join(lines) + "\n", encoding="utf-8")
