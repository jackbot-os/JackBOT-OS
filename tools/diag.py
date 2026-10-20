import os
d = r'E:\JackBOT-OS\new-chat\JackBOT-OS\build_assets'
lines = []
for f in sorted(os.listdir(d)):
    p = os.path.join(d, f)
    b = open(p, 'rb').read(32)
    lines.append('%s size=%d head=%r' % (f, os.path.getsize(p), b))
open(r'E:\JackBOT-OS\new-chat\JackBOT-OS\build_err.txt',
     'w', encoding='utf-8').write('\n'.join(lines))
