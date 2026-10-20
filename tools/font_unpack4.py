import os, io, tarfile, lzma
from fontTools.ttLib import TTFont

TMP = r'E:\JackBOT-OS\new-chat\JackBOT-OS\build_assets'
DST = r'E:\JackBOT-OS\new-chat\JackBOT-OS\assets\fonts'
LOG = []

def deb_data(path):
    data = open(path, 'rb').read()
    assert data[:8] == b'!<arch>\n', 'not ar'
    i = 8
    out = {}
    while i + 60 <= len(data):
        hdr = data[i:i + 60]
        i += 60
        name = hdr[0:16].decode('ascii').strip().rstrip('/')
        size = int(hdr[48:58].decode('ascii').strip())
        body = data[i:i + size]
        i += size + (size & 1)
        out[name] = body
    dkey = [k for k in out if k.startswith('data.tar')][0]
    raw = out[dkey]
    if dkey.endswith('.xz'):
        raw = lzma.decompress(raw)
    return tarfile.open(fileobj=io.BytesIO(raw), mode='r:')

def extract(deb, predicates, outname):
    tf = deb_data(os.path.join(TMP, deb))
    names = tf.getnames()
    LOG.append('-- %s members:' % deb)
    for n in names:
        if n.endswith(('.ttf', '.otf')):
            LOG.append('   ' + n)
    for m in tf.getmembers():
        for pred in predicates:
            if pred(m.name):
                d = tf.extractfile(m).read()
                open(os.path.join(DST, outname), 'wb').write(d)
                LOG.append('wrote %s <- %s (%d)' % (outname, m.name, len(d)))
                return True
    return False

# Korean: prefer NanumGothic.ttf
extract('nanum.deb',
        [lambda n: n.endswith('/NanumGothic.ttf')],
        'KR.ttf')

# Arabic: Scheherazade New regular TTF (full presentation forms)
extract('sche.deb',
        [lambda n: n.lower().endswith('scheherazadenew-regular.ttf'),
         lambda n: n.lower().endswith('scheherazade-regular.ttf'),
         lambda n: n.lower().endswith('scheherazadenew.ttf')],
        'AR.ttf')

# verify
for f in ['AR.ttf', 'KR.ttf']:
    p = os.path.join(DST, f)
    t = TTFont(p, lazy=True)
    cmap = t.getBestCmap()
    ol = 'glyf' if 'glyf' in t else 'CFF'
    LOG.append('VERIFY %s outline=%s cmap=%d' % (f, ol, len(cmap)))

t = TTFont(os.path.join(DST, 'AR.ttf'), lazy=True)
cmap = t.getBestCmap()
pres_missing = [c for c in range(0xFE80, 0xFEFF) if c not in cmap]
LOG.append('AR presentation forms missing: %d %s' %
           (len(pres_missing), ['U+%04X' % c for c in pres_missing]))

t = TTFont(os.path.join(DST, 'KR.ttf'), lazy=True)
cmap = t.getBestCmap()
for cp in [0xAC00, 0xD55C, 0xAD6D]:
    LOG.append('KR U+%04X present=%s' % (cp, cp in cmap))

open(r'E:\JackBOT-OS\new-chat\JackBOT-OS\build_fontcheck.txt',
     'w', encoding='utf-8').write('\n'.join(LOG))
