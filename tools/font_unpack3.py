import os, io, tarfile, lzma, sys

TMP = r'E:\JackBOT-OS\new-chat\JackBOT-OS\build_assets'
DST = r'E:\JackBOT-OS\new-chat\JackBOT-OS\assets\fonts'
LOG = []

def deb_members(path):
    data = open(path, 'rb').read()
    assert data[:8] == b'!<arch>\n', 'not ar: %r' % data[:8]
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
    return out

JOBS = [
    ('garuda.deb', 'Garuda.ttf', 'TH.ttf'),
    ('deva.deb', 'Lohit-Devanagari.ttf', 'HI.ttf'),
]

for deb, want, out in JOBS:
    members = deb_members(os.path.join(TMP, deb))
    dkey = [k for k in members if k.startswith('data.tar')][0]
    raw = members[dkey]
    if dkey.endswith('.xz'):
        raw = lzma.decompress(raw)
    tf = tarfile.open(fileobj=io.BytesIO(raw), mode='r:')
    found = False
    for m in tf.getmembers():
        if m.name.endswith('/' + want) or m.name.endswith(want):
            d = tf.extractfile(m).read()
            open(os.path.join(DST, out), 'wb').write(d)
            LOG.append('%s <- %s (%d) %r' % (out, m.name, len(d), d[:4]))
            found = True
            break
    if not found:
        LOG.append('NOT FOUND %s in %s' % (want, deb))

for f in ['AR.ttf', 'TH.ttf', 'HI.ttf']:
    b = open(os.path.join(DST, f), 'rb').read()
    LOG.append('%s size=%d tag=%r' % (f, len(b), b[:4]))

open(r'E:\JackBOT-OS\new-chat\JackBOT-OS\build_fontcheck.txt',
     'w', encoding='utf-8').write('\n'.join(LOG))
