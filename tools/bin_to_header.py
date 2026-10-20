#!/usr/bin/env python3
import sys, os

IN = sys.argv[1]
OUT = sys.argv[2]
SYM = sys.argv[3]

data = open(IN, 'rb').read()
guard = 'BIN_%s_H' % SYM.upper()

with open(OUT, 'w', encoding='utf-8') as f:
    f.write('#ifndef %s\n#define %s\n' % (guard, guard))
    f.write('static const unsigned char %s[] = {\n' % SYM)
    for i in range(0, len(data), 12):
        chunk = data[i:i+12]
        f.write('  ' + ','.join('0x%02x' % b for b in chunk) + ',\n')
    f.write('};\n')
    f.write('static const unsigned int %s_len = %d;\n' % (SYM, len(data)))
    f.write('#endif\n')
print('%s: %d bytes -> %s' % (SYM, len(data), OUT))
