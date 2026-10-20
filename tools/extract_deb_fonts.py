import os, io, tarfile

TMP = r'E:\JackBOT-OS\new-chat\JackBOT-OS\build_assets'
DST = r'E:\JackBOT-OS\new-chat\JackBOT-OS\assets\fonts'
LOG = []

def unpack_deb(path):
    data = open(path, 'rb').read()
    assert data[:8] == b'!<arch>', 'not ar'
    i = 8
    while i < len(data):
        hdr = data[i:i + 60]
        i += 60
        if len(hdr) < 60:
            break
        name = hdr[0:16].decode('ascii').strip()
        size = int(hdr[48:58].decode('ascii').strip())
        body = data[i:i + size]
        i += size + (size & 1)
        if name.startswith('data.tar'):
            return body
    return None

JOBS = [
    ('garuda.deb', 'Garuda.ttf', 'TH.ttf'),
    ('deva.deb', 'Lohit-Devanagari.ttf', 'HI.ttf'),
]

for deb, want, out in JOBS:
    body = unpack_deb(os.path.join(TMP, deb))
    tf = tarfile.open(fileobj=io.BytesIO(body), mode='r:*')
    for m in tf.getmembers():
        if m.name.endswith(want):
            f = tf.extractfile(m)
            data = f.read()
            open(os.path.join(DST, out), 'wb').write(data)
            LOG.append('%s <- %s (%d bytes) tag=%r' %
                       (out, m.name, len(data), data[:4]))
            break
    else:
        LOG.append('%s NOT FOUND in %s' % (want, deb))

for f in ['AR.ttf', 'TH.ttf', 'HI.ttf']:
    p = os.path.join(DST, f)
    b = open(p, 'rb').read()
    LOG.append('%s size=%d tag=%r' % (f, len(b), b[:4]))

open(os.path.join(DST, '..', '..', 'build_fontcheck.txt'),
     'w', encoding='utf-8').write('\n'.join(LOG))
