import io
p = r'E:\JackBOT-OS\new-chat\JackBOT-OS\build\extra_charset.txt'
cps = [int(x) for x in open(p, encoding='utf-8').read().split()]
s = set(cps)
out = []
out.append('total codepoints in charset: %d' % len(cps))

ar_base = [c for c in s if 0x0600 <= c <= 0x06FF]
ar_pres = [c for c in s if 0xFE70 <= c <= 0xFEFF]
out.append('arabic base (0600-06FF): %d' % len(ar_base))
out.append('arabic presentation B (FE70-FEFF): %d' % len(ar_pres))
out.append('FE80-FEFC present: %s' % any(0xFE80 <= c <= 0xFEFC for c in s))

# sample arabic translation chars
for c in [0x0627, 0x0644, 0x0639, 0xFE91, 0xFEE0, 0xFEDF, 0xFE82]:
    out.append('U+%04X in charset: %s' % (c, c in s))

# korean / thai / hindi blocks
out.append('hangul syllables (AC00-D7AF): %d' %
           sum(1 for c in s if 0xAC00 <= c <= 0xD7AF))
out.append('thai (0E00-0E7F): %d' % sum(1 for c in s if 0x0E00 <= c <= 0x0E7F))
out.append('devanagari (0900-097F): %d' % sum(1 for c in s if 0x0900 <= c <= 0x097F))
out.append('CJK (4E00-9FFF): %d' % sum(1 for c in s if 0x4E00 <= c <= 0x9FFF))

open(r'E:\JackBOT-OS\new-chat\JackBOT-OS\build_vbox.txt', 'w',
     encoding='utf-8').write('\n'.join(out))
