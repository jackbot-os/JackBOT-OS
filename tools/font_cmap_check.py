#!/usr/bin/env python3
import json
from fontTools.ttLib import TTFont

ROOT = r'E:\JackBOT-OS\new-chat\JackBOT-OS'
PATHS = {
    'SANS': ROOT + r'\assets\fonts\SANS.ttf',
    'CJK': ROOT + r'\assets\fonts\CJK.ttf',
    'KR': ROOT + r'\assets\fonts\KR.ttf',
    'AR': ROOT + r'\assets\fonts\AR.ttf',
    'TH': ROOT + r'\assets\fonts\TH.ttf',
    'HI': ROOT + r'\assets\fonts\HI.ttf',
}

cmaps = {}
outline = {}
fvar = {}
for n, p in PATHS.items():
    t = TTFont(p, lazy=True)
    cmaps[n] = t.getBestCmap()
    outline[n] = 'glyf' if 'glyf' in t else ('CFF' if 'CFF ' in t else '?')
    fvar[n] = 'fvar' if 'fvar' in t else 'static'

def chain_owner(cp):
    for n in ['SANS', 'CJK', 'KR', 'AR', 'TH', 'HI']:
        if cp in cmaps[n]:
            return n
    return None

data = json.load(open(ROOT + r'\tools\i18n_data.json', encoding='utf-8'))
cps = set()
for lg in data['langs']:
    for k in data['keys']:
        for ch in lg['strings'].get(k, ''):
            cps.add(ord(ch))
    for ch in lg['name']:
        cps.add(ord(ch))

missing = []
owners = {}
for cp in sorted(cps):
    if cp < 128:
        continue
    o = chain_owner(cp)
    if o is None:
        missing.append(cp)
    else:
        owners[o] = owners.get(o, 0) + 1

lines = []
for n in PATHS:
    lines.append('%-4s outline=%-4s %s cmap=%d' %
                 (n, outline[n], fvar[n], len(cmaps[n])))
lines.append('non-ASCII codepoints: %d' % len([c for c in cps if c >= 128]))
lines.append('served by: ' + ', '.join('%s=%d' % (k, v)
                                       for k, v in sorted(owners.items())))
if missing:
    lines.append('MISSING %d:' % len(missing))
    for cp in missing:
        lines.append('  U+%04X %s' % (cp, chr(cp)))
else:
    lines.append('ALL non-ASCII codepoints covered by the fallback chain.')

# The shaper emits FE80..FEFC (FEFD/FEFE no longer emitted: U+0622
# is written separately). Every codepoint the shaper can emit must exist.
pres = list(range(0xFE80, 0xFEFD))
pres_missing = [c for c in pres if c not in cmaps['AR']]
lines.append('Arabic shaper output forms missing: %d %s' %
             (len(pres_missing),
              ['U+%04X' % c for c in pres_missing[:20]]))
# separated U+0622 path: lam ini/med + alef madda iso/fin
for cp in [0xFE81, 0xFE82, 0xFEDF, 0xFEE0]:
    lines.append('AR U+%04X present=%s' % (cp, cp in cmaps['AR']))

open(ROOT + r'\build_fontcheck.txt', 'w', encoding='utf-8').write(
    '\n'.join(lines))
