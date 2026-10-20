import zipfile, os

APPX = r'E:\JackBOT-OS\new-chat\JackBOT-OS\build_assets\debian.appx'
OUT = r'E:\JackBOT-OS\new-chat\JackBOT-OS\build_assets'
LOG = r'E:\JackBOT-OS\new-chat\JackBOT-OS\build_vbox.txt'

lines = []
bundle = zipfile.ZipFile(APPX)
inner_name = 'DistroLauncher-Appx_1.12.2.0_x64.appx'
inner_bytes = bundle.read(inner_name)
inner_path = os.path.join(OUT, 'debian_x64.appx')
open(inner_path, 'wb').write(inner_bytes)
lines.append('wrote inner %s (%d)' % (inner_path, len(inner_bytes)))

z = zipfile.ZipFile(inner_path)
for n in z.namelist():
    lines.append('  INNER: %s (%d)' % (n, z.getinfo(n).file_size))

target = None
for n in z.namelist():
    ln = n.lower()
    if ln.endswith('install.tar.gz') or ln.endswith('install.tar'):
        target = n
        break
if target is None:
    cands = [n for n in z.namelist() if n.lower().endswith(('.tar', '.gz', '.tar.gz'))]
    if cands:
        target = max(cands, key=lambda n: z.getinfo(n).file_size)

if target:
    data = z.read(target)
    outp = os.path.join(OUT, os.path.basename(target))
    open(outp, 'wb').write(data)
    lines.append('EXTRACTED %s -> %s (%d bytes)' % (target, outp, len(data)))
else:
    lines.append('NO ROOTFS TAR IN INNER APPX')

open(LOG, 'w', encoding='utf-8').write('\n'.join(lines))
print('\n'.join(lines))
