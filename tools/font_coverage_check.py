#!/usr/bin/env python3
import json, sys
from PIL import ImageFont

ROOT = r'E:\JackBOT-OS\new-chat\JackBOT-OS'
FONTS = [
    ('SANS', ROOT + r'\assets\fonts\SANS.ttf'),
    ('MONO', ROOT + r'\assets\fonts\MONO.ttf'),
    ('MONOB', ROOT + r'\assets\fonts\MONOB.ttf'),
    ('CJK', ROOT + r'\assets\fonts\CJK.ttf'),
    ('AR', ROOT + r'\assets\fonts\AR.ttf'),
    ('TH', ROOT + r'\assets\fonts\TH.ttf'),
    ('HI', ROOT + r'\assets\fonts\HI.ttf'),
]

fonts = [(n, ImageFont.truetype(p, 24)) for n, p in FONTS]

def has_glyph(f, ch):
    try:
        m = f.getmask(ch)
        return m.getbbox() is not None
    except Exception:
        return False

data = json.load(open(ROOT + r'\tools\i18n_data.json', encoding='utf-8'))
cps = set()
for lg in data['langs']:
    for k in data['keys']:
        for ch in lg['strings'].get(k, ''):
            cps.add(ord(ch))
    for ch in lg['name']:
        cps.add(ord(ch))

missing = []
for cp in sorted(cps):
    if cp < 128:
        continue
    ch = chr(cp)
    owner = None
    for n, f in fonts:
        if has_glyph(f, ch):
            owner = n
            break
    if not owner:
        missing.append(cp)

lines = []
lines.append('non-ASCII codepoints used: %d' % len([c for c in cps if c >= 128]))
if missing:
    lines.append('MISSING %d codepoints:' % len(missing))
    for cp in missing:
        lines.append('  U+%04X %s' % (cp, chr(cp)))
else:
    lines.append('ALL non-ASCII codepoints covered (SANS/CJK/AR/TH/HI chain).')

# sample rendering per script
samples = {
    'AR U+0627 alef': 0x0627,
    'AR U+0644 lam': 0x0644,
    'TH U+0E01 ko kai': 0x0E01,
    'TH U+0E17 tho phuthao': 0x0E17,
    'HI U+0905 devanagari a': 0x0905,
    'HI U+0939 ha': 0x0939,
    'CJK U+4E2D': 0x4E2D,
}
for label, cp in samples.items():
    ch = chr(cp)
    where = [n for n, f in fonts if has_glyph(f, ch)]
    lines.append('%-26s -> %s' % (label, ','.join(where) or 'NONE'))

open(ROOT + r'\build_fontcheck.txt', 'w', encoding='utf-8').write(
    '\n'.join(lines))
