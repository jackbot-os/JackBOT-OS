import os
from PIL import Image, ImageDraw, ImageFont

H = 16
cands = {
    "DroidFallback": "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
    "DejaVuSans": "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "LiberationSans": "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "LiberationMono": "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
}

for name, path in cands.items():
    if not os.path.exists(path):
        print("===", name, "MISSING:", path)
        continue
    f = ImageFont.truetype(path, 13)
    print("===", name)
    for ch in "We":
        img = Image.new("L", (8, H), 0)
        ImageDraw.Draw(img).text((1, 0), ch, font=f, fill=255)
        px = img.load()
        print("  char", repr(ch))
        for ry in range(H):
            print("   " + "".join("#" if px[rx, ry] > 127 else "."
                                  for rx in range(8)))
