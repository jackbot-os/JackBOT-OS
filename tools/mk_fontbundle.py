#!/usr/bin/env python3
"""把 assets/fonts/lang/*.ttf 打成一个内核可索引的字体包。

格式（小端）:
  magic  'LGF1'  (4 bytes)
  count  u32
  表项 count 个:  name 12 bytes (ASCII, 大写, 不含 .TTF) | off u32 | size u32
  之后是各字体数据，off 相对文件头。
"""
import os, struct, sys

SRC = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "assets", "fonts", "lang")
OUT = sys.argv[1] if len(sys.argv) > 1 else "iso/boot/LGFONT.BIN"

fonts = []
for f in sorted(os.listdir(SRC)):
    if f.lower().endswith(".ttf"):
        p = os.path.join(SRC, f)
        fonts.append((os.path.splitext(f)[0].upper()[:12], p, os.path.getsize(p)))

hdr = 8 + len(fonts) * 20
data_off = hdr
entries = []
blobs = []
for name, path, size in fonts:
    entries.append((name, data_off + sum(len(b) for b in blobs), size))
    blobs.append(open(path, "rb").read())

with open(OUT, "wb") as fh:
    fh.write(b"LGF1")
    fh.write(struct.pack("<I", len(fonts)))
    for name, off, size in entries:
        nb = name.encode("ascii", "replace").ljust(12, b"\0")[:12]
        fh.write(nb + struct.pack("<II", off, size))
    for b in blobs:
        fh.write(b)

print("LGFONT bundle:", len(fonts), "fonts,", os.path.getsize(OUT), "bytes ->", OUT)
