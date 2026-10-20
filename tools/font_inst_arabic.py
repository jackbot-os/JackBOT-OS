from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont

p = r'E:\JackBOT-OS\new-chat\JackBOT-OS\assets\fonts\AR.ttf'
t = TTFont(p)
instantiateVariableFont(t, {'wght': 400}, inplace=True)
t.save(p)

t2 = TTFont(p, lazy=True)
cmap = t2.getBestCmap()
lines = []
lines.append('after instancing: fvar=%s glyf=%s cmap=%d' %
             ('fvar' in t2, 'glyf' in t2, len(cmap)))
miss = [c for c in range(0xFE80, 0xFEFD) if c not in cmap]
lines.append('shaper forms missing: %d' % len(miss))
for cp in [0x0627, 0x0644, 0x0639, 0xFE81, 0xFE82, 0xFEDF, 0xFEE0]:
    lines.append('U+%04X %s' % (cp, cp in cmap))
open(r'E:\JackBOT-OS\new-chat\JackBOT-OS\build_fontcheck.txt',
     'w', encoding='utf-8').write('\n'.join(lines))
