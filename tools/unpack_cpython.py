import os, tarfile, shutil

ROOT = r'E:\JackBOT-OS\new-chat\JackBOT-OS'
TGZ = os.path.join(ROOT, 'build_assets', 'Python-3.12.7.tgz')
TP = os.path.join(ROOT, 'third_party')
DEST = os.path.join(TP, 'cpython')
LOG = []

b = open(TGZ, 'rb').read(2)
LOG.append('gzip magic=%r' % b)

if os.path.exists(DEST):
    shutil.rmtree(DEST)
    LOG.append('removed old third_party/cpython')

with tarfile.open(TGZ, 'r:gz') as tf:
    members = tf.getmembers()
    LOG.append('tar members=%d' % len(members))
    top = members[0].name.split('/')[0]
    LOG.append('top dir=%s' % top)
    tf.extractall(TP, filter='data')

extracted = os.path.join(TP, top)
os.rename(extracted, DEST)

# sanity: key upstream files present, untouched
for f in ['README.rst', 'LICENSE', 'configure', 'Makefile.pre.in',
          'Python/ceval.c', 'Include/Python.h', 'Lib/_pyinstaller']:
    p = os.path.join(DEST, f)
    LOG.append('%-22s %s' % (f, 'OK' if os.path.exists(p) else 'MISSING'))

nfiles = sum(len(fs) for _, _, fs in os.walk(DEST))
LOG.append('total files under third_party/cpython: %d' % nfiles)
open(os.path.join(ROOT, 'build_fontcheck.txt'), 'w',
     encoding='utf-8').write('\n'.join(LOG))
