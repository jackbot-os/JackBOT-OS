import json, os, urllib.request, gzip, io

ROOT = r'E:\JackBOT-OS\new-chat\JackBOT-OS'
OUT = os.path.join(ROOT, 'build_assets', 'debian-rootfs.tar.gz')
LOG = []

MIRRORS = [
    'https://docker.m.daocloud.io',
    'https://docker.1ms.run',
    'https://dockerproxy.net',
]
REPO = 'library/debian'
TAG = 'bookworm-slim'


def get(url, headers=None, timeout=60):
    req = urllib.request.Request(url, headers=headers or {})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read()


def try_mirror(base):
    hdrs = {
        'Accept': 'application/vnd.docker.distribution.manifest.list.v2+json,'
                  ' application/vnd.oci.image.index.v1+json,'
                  ' application/vnd.docker.distribution.manifest.v2+json'
    }
    raw = get('%s/v2/%s/manifests/%s' % (base, REPO, TAG), hdrs)
    idx = json.loads(raw)
    digest = None
    if 'manifests' in idx:
        for m in idx['manifests']:
            p = m.get('platform', {})
            if p.get('os') == 'linux' and p.get('architecture') == 'amd64':
                digest = m['digest']
                break
        if not digest:
            raise RuntimeError('no amd64 manifest')
        raw = get('%s/v2/%s/manifests/%s' % (base, REPO, digest), hdrs)
        idx = json.loads(raw)
    layers = idx['layers']
    LOG.append('layers=%d via %s' % (len(layers), base))
    data = b''
    for i, l in enumerate(layers):
        d = l['digest']
        blob = get('%s/v2/%s/blobs/%s' % (base, REPO, d), timeout=180)
        LOG.append('  layer %d %d bytes' % (i, len(blob)))
        data += blob
    open(OUT, 'wb').write(data)
    return True


ok = False
for base in MIRRORS:
    try:
        LOG.append('trying %s' % base)
        if try_mirror(base):
            ok = True
            break
    except Exception as e:
        LOG.append('  failed: %r' % e)

if ok:
    sz = os.path.getsize(OUT)
    LOG.append('rootfs tar.gz=%d bytes' % sz)
    # validate gzip + peek tar names
    with gzip.open(OUT, 'rb') as g:
        head = g.read(2048)
    LOG.append('gzip peek ok, head has ./ : %s' % (b'./' in head or b'bin' in head))
else:
    LOG.append('ALL MIRRORS FAILED')

open(os.path.join(ROOT, 'build_err.txt'), 'w',
     encoding='utf-8').write('\n'.join(LOG))
