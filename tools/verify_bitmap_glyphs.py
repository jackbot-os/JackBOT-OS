import re, sys

H = r'E:\JackBOT-OS\new-chat\JackBOT-OS\build\font_sans.h'
txt = open(H, encoding='utf-8').read()

def array(name):
    m = re.search(name + r'\s*\[(?:\d+)?\]\s*=\s*\{(.*?)\};', txt, re.S)
    if not m:
        return None
    vals = re.findall(r'0x[0-9a-fA-F]+|\b\d+\b', m.group(1))
    return [int(v, 0) for v in vals]

cps = array('font_sans_cp16')
# g16 is N rows of 32 bytes
g = re.search(r'font_sans_g16\[\d+\]\[32\]\s*=\s*\{(.*?)\};', txt, re.S).group(1)
rows = re.findall(r'\{([^}]*)\}', g, re.S)
glyphs = []
for r in rows:
    b = [int(v, 16) for v in re.findall(r'0x[0-9a-fA-F]+', r)]
    glyphs.append(b)

out = ['cp16 entries: %d, g16 rows: %d' % (len(cps), len(glyphs))]
empty = []
targets = {
    'arabic_forms': lambda c: 0xFE80 <= c <= 0xFEFC,
    'hangul': lambda c: 0xAC00 <= c <= 0xD7AF,
    'thai': lambda c: 0x0E00 <= c <= 0x0E7F,
    'devanagari': lambda c: 0x0900 <= c <= 0x097F,
    'cjk': lambda c: 0x4E00 <= c <= 0x9FFF,
}
counts = {k: 0 for k in targets}
empties = {k: [] for k in targets}
for i, c in enumerate(cps):
    if i >= len(glyphs):
        break
    blank = not any(glyphs[i])
    for k, fn in targets.items():
        if fn(c):
            counts[k] += 1
            if blank:
                empties[k].append(c)
for k in targets:
    n = counts[k]
    e = empties[k]
    out.append('%-14s glyphs=%d blank=%d %s' %
               (k, n, len(e), ('sample ' + ','.join('U+%04X' % x for x in e[:6])) if e else ''))
open(r'E:\JackBOT-OS\new-chat\JackBOT-OS\build_vbox.txt', 'w',
     encoding='utf-8').write('\n'.join(out))
