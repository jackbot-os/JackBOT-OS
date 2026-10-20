#!/usr/bin/env python3
import sys
from PIL import Image, ImageDraw, ImageFont

PRIMARY = sys.argv[1]
OUT = sys.argv[2]
FONTNAME = sys.argv[3]
SYMBOL = sys.argv[4]
CSFILE = sys.argv[5]
FALLBACKS = sys.argv[6:]

H = 16
SIZE8 = 13
SIZE16 = 15

p8 = ImageFont.truetype(PRIMARY, SIZE8)
p16 = ImageFont.truetype(PRIMARY, SIZE16)
fb8 = [ImageFont.truetype(f, SIZE8) for f in FALLBACKS]
fb16 = [ImageFont.truetype(f, SIZE16) for f in FALLBACKS]

extra = []
with open(CSFILE, encoding="utf-8") as f:
    for line in f:
        line = line.strip()
        if line:
            extra.append(int(line))
wide = sorted(c for c in extra if c >= 256)

WELCOME_LATIN = sorted({ord(c) for c in "Welcome"})
wide16 = sorted(set(wide) | set(WELCOME_LATIN))

def place_x(ch, w, fnt):
    if w == 8:
        return 1
    try:
        b = fnt.getbbox(ch)
        nw = b[2] - b[0]
        return (w - nw) // 2 - b[0]
    except Exception:
        return 0

def draw_glyph(ch, w, fnt_main, fnt_fbs):
    nbytes = (w + 7) // 8

    def render(fnt):
        img = Image.new("L", (w, H), 0)
        ImageDraw.Draw(img).text((place_x(ch, w, fnt), 0), ch,
                                 font=fnt, fill=255)
        return img

    def bits(img):
        px = img.load()
        rows = []
        for ry in range(H):
            bs = [0] * nbytes
            for rx in range(w):
                if px[rx, ry] > 127:
                    bs[rx // 8] |= 0x80 >> (rx % 8)
            rows.extend(bs)
        return rows

    rows = bits(render(fnt_main))
    if not any(rows):
        for fnt in fnt_fbs:
            rows = bits(render(fnt))
            if any(rows):
                break
    return rows

g8 = []
for cp in range(256):
    if cp < 32 or cp == 127:
        g8.append([0] * 16)
    else:
        g8.append(draw_glyph(chr(cp), 8, p8, fb8))

g16 = [draw_glyph(chr(cp), 16, p16, fb16) for cp in wide16]

def emit_rows(rows, per):
    parts = []
    for i in range(0, len(rows), per):
        parts.append("    " + ",".join("0x%02x" % b for b in rows[i:i+per]))
    return ",\n".join(parts)

with open(OUT, "w", encoding="utf-8") as f:
    f.write("#ifndef FONT_%s_H\n#define FONT_%s_H\n" % (SYMBOL, SYMBOL))
    f.write("#include <stdint.h>\n#include <kernel/gfxtext.h>\n")
    f.write("static const uint8_t %s_g8[256][16] = {\n" % SYMBOL)
    for rows in g8:
        f.write("  {\n" + emit_rows(rows, 16) + "\n  },\n")
    f.write("};\n")
    f.write("static const uint32_t %s_cp16[%d] = {\n" % (SYMBOL, len(wide16)))
    line = "  " + ",".join("0x%x" % c for c in wide16)
    f.write(line + "\n};\n")
    f.write("static const uint8_t %s_g16[%d][32] = {\n" % (SYMBOL, len(wide16)))
    for rows in g16:
        f.write("  {\n" + emit_rows(rows, 16) + "\n  },\n")
    f.write("};\n")
    f.write("static const struct font_set %s = {\n" % SYMBOL)
    f.write('  "%s", %s_g8, %s_cp16, %d, %s_g16\n};\n'
            % (FONTNAME, SYMBOL, SYMBOL, len(wide16), SYMBOL))
    f.write("#endif\n")
