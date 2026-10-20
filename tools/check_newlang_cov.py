#!/usr/bin/env python3
# Report codepoints in the i18n charset that have no glyph in the bundled
# SANS + fallback font chain (same render-vs-blank test as font_to_header.py).
import json, io
from PIL import Image, ImageDraw, ImageFont

ROOT = "assets/fonts/"
PRIMARY = ROOT + "SANS.ttf"
FALLBACKS = [ROOT + "CJK.ttf", ROOT + "KR.ttf", ROOT + "AR.ttf",
             ROOT + "TH.ttf", ROOT + "HI.ttf"]
SIZE = 15
H = 16

fonts = [ImageFont.truetype(PRIMARY, SIZE)] + [ImageFont.truetype(f, SIZE)
                                               for f in FALLBACKS]

def has_glyph(ch, fnt):
    img = Image.new("L", (16, H), 0)
    try:
        ImageDraw.Draw(img).text((1, 0), ch, font=fnt, fill=255)
    except Exception:
        return False
    return any(px > 127 for px in img.getdata())

def covered(ch):
    return any(has_glyph(ch, f) for f in fonts)

with io.open("tools/i18n_data.json", encoding="utf-8") as f:
    data = json.load(f)

missing = {}
for lg in data["langs"]:
    text = lg["name"]
    for v in lg["strings"].values():
        text += v
    bad = sorted({c for c in text if ord(c) >= 128 and not covered(c)})
    if bad:
        missing[lg["code"]] = bad

total_missing = 0
for code, bad in missing.items():
    total_missing += len(bad)
    print("%s (%d missing): %s" % (
        code, len(bad),
        " ".join("U+%04X'%s'" % (ord(c), c) for c in bad)))

print("----")
print("languages with missing glyphs: %d / %d" %
      (len(missing), len(data["langs"])))
print("total uncovered non-ASCII characters: %d" % total_missing)
