#!/usr/bin/env python3
import sys
from PIL import Image, ImageDraw, ImageFont

FONT_PATH = sys.argv[1]
OUT = sys.argv[2]
W, H = 8, 16
SIZE = 14

fnt = ImageFont.truetype(FONT_PATH, SIZE)
ascent, descent = fnt.getmetrics()

glyphs = []
for code in range(128):
    img = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(img)
    ch = chr(code)
    if code >= 32 and code != 127:
        adv = d.textlength(ch, font=fnt)
        x = int(round((W - adv) / 2))
        y = (H - (ascent + descent)) // 2
        d.text((x, y), ch, font=fnt, fill=255)
    px = img.load()
    rows = []
    for ry in range(H):
        b = 0
        for rx in range(W):
            if px[rx, ry] > 127:
                b |= 0x80 >> rx
        rows.append(b)
    glyphs.append(rows)

with open(OUT, "w") as f:
    f.write("#ifndef FONT_GLYPH_H\n#define FONT_GLYPH_H\n")
    f.write("#define FONT_W 8\n#define FONT_H 16\n")
    f.write("static const unsigned char font_glyphs[128][16] = {\n")
    for rows in glyphs:
        f.write("  {" + ",".join("0x%02x" % b for b in rows) + "},\n")
    f.write("};\n#endif\n")
