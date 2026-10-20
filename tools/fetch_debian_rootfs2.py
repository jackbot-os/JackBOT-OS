import json, os, re, urllib.request, urllib.parse, gzip

ROOT = r'E:\JackBOT-OS\new-chat\JackBOT-OS'
OUT = os.path.join(ROOT, 'build_assets', 'debian-rootfs.tar.gz')
LOG = []

MIRRORS = [
    'https://docker.m.daocloud.io',
    'https://docker.1ms.run',
    'https://hub.rat.dev',
]
REPO = 'library/debian'
TAG = 'bookworm-slim'


def request(url, headers=None, timeout=120):
    req = urllib.request.Request(url, headers=headers or {})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, r.read(), dict(r.headers)
    except urllib.error.HTTPError as e:
        return e.code, e.read(), dict(e.headers)


def get_token(auth_header, repo):
    m = re.search(r'realm="([^"]+)"', auth_header)
    realm = m.group(1)
    svc = re.search(r'service="([^"]+)"', auth_header)
    params = {}
    if svc:
        params['service'] = svc.group(1)
    params['scope'] = 'repository:%s:pull' % repo
    u = realm + '?' + urllib.parse.urlencode(params)
    st, body, _ = request(u)
    if st != 200:
        raise RuntimeError('token http %d' % st)
    return json.loads(body)['token']


def mirror_pull(base):
    hdrs = {
        'Accept': 'application/vnd.docker.distribution.manifest.list.v2+json,'
                  ' application/vnd.oci.image.index.v1+json,'
                  ' application/vnd.docker.distribution.manifest.v2+json'
    }
    url = '%s/v2/%s/manifests/%s' % (base, REPO, TAG)
    st, body, h = request(url, hdrs)
    if st == 401:
        tok = get_token(h.get('Www-Authenticate', ''), REPO)
        hdrs['Authorization'] = 'Bearer ' + tok
        st, body, h = request(url, hdrs)
    if st != 200:
        raise RuntimeError('manifest http %d %r' % (st, body[:120]))
    idx = json.loads(body)
    digest = None
    if 'manifests' in idx:
        for mm in idx['manifests']:
            p = mm.get('platform', {})
            if p.get('os') == 'linux' and p.get('architecture') == 'amd64':
                digest = mm['digest']
        if not digest:
            raise RuntimeError('no amd64')
        st, body, h = request('%s/v2/%s/manifests/%s' % (base, REPO, digest),
                              hdrs)
        if st == 401:
            hdrs['Authorization'] = 'Bearer ' + get_token(
                h.get('Www-Authenticate', ''), REPO)
            st, body, h = request(
                '%s/v2/%s/manifests/%s' % (base, REPO, digest), hdrs)
        if st != 200:
            raise RuntimeError('submanifest http %d' % st)
        idx = json.loads(body)
    data = b''
    for l in idx['layers']:
        burl = '%s/v2/%s/blobs/%s' % (base, REPO, l['digest'])
        st, blob, h = request(burl, timeout=300)
        if st == 401:
            hdrs['Authorization'] = 'Bearer ' + get_token(
                h.get('Www-Authenticate', ''), REPO)
            st, blob, h = request(burl, headers=hdrs, timeout=300)
        if st != 200:
            raise RuntimeError('blob http %d' % st)
        LOG.append('  layer %d bytes' % len(blob))
        data += blob
    open(OUT, 'wb').write(data)
    return True


for base in MIRRORS:
    try:
        LOG.append('trying %s' % base)
        if mirror_pull(base):
            LOG.append('OK via %s size=%d' % (base, os.path.getsize(OUT)))
            break
    except Exception as e:
        LOG.append('  failed: %r' % e)
else:
    LOG.append('ALL MIRRORS FAILED')

open(os.path.join(ROOT, 'build_err.txt'), 'w',
     encoding='utf-8').write('\n'.join(LOG))
