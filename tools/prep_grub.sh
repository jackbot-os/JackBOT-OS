#!/bin/bash
set -e

GRUB_I386=/usr/lib/grub/i386-pc
GRUB_FONT=/usr/share/grub/unicode.pf2
PCDIR=iso/boot/grub/i386-pc
FONTDIR=iso/boot/grub/fonts
TTFDIR=iso/boot/fonts

mkdir -p "$PCDIR" "$FONTDIR" "$TTFDIR"

cp "$GRUB_I386/boot.img" "$PCDIR/boot.img"

# 复刻 grub-install 标准硬盘布局：core.img 不内嵌 normal（normal 从磁盘加载），
# 只内嵌磁盘识别模块。prefix 用自动 BIOS 盘号 (,msdos1)。
grub-mkimage --directory "$GRUB_I386" \
    --prefix '(,msdos1)/boot/grub' \
    --output "$PCDIR/core.img" \
    --format i386-pc fat part_msdos biosdisk

# 生成 GRUB 模块 blob：normal + multiboot2 完整依赖闭包（真实文件名，供 LFN 写盘）
python3 - "$GRUB_I386" iso/boot/GRUBMODS.BIN <<'PY'
import os, struct, sys
I386 = sys.argv[1]
OUT = sys.argv[2]
deps = {}
for line in open(os.path.join(I386, 'moddep.lst')):
    line = line.strip()
    if ':' in line:
        m, ds = line.split(':', 1)
        deps[m.strip()] = ds.split()

def closure(roots):
    need = set(roots)
    st = list(roots)
    while st:
        m = st.pop()
        for d in deps.get(m, []):
            if d not in need:
                need.add(d)
                st.append(d)
    return sorted(need)

mods = closure(['normal', 'multiboot2'])
out = bytearray(b'GRUBMODS1')
out += struct.pack('<I', len(mods))
total = 0
for m in mods:
    fn = m + '.mod'
    data = open(os.path.join(I386, fn), 'rb').read()
    nb = fn.encode()
    out += struct.pack('<I', len(nb)) + nb
    out += struct.pack('<I', len(data)) + data
    total += len(data)
open(OUT, 'wb').write(out)
print('GRUBMODS modules=%d blob=%d totalmod=%d' % (len(mods), len(out), total))
PY

cp "$GRUB_FONT" "$FONTDIR/unicode.pf2"

cp assets/fonts/SANS.ttf  "$TTFDIR/SANS.TTF"
cp assets/fonts/MONO.ttf  "$TTFDIR/MONO.TTF"
cp assets/fonts/MONOB.ttf "$TTFDIR/MONOB.TTF"
cp assets/fonts/CJK.ttf   "$TTFDIR/CJK.TTF"
cp assets/fonts/KR.ttf    "$TTFDIR/KR.TTF"
cp assets/fonts/AR.ttf    "$TTFDIR/AR.TTF"
cp assets/fonts/TH.ttf    "$TTFDIR/TH.TTF"
cp assets/fonts/HI.ttf    "$TTFDIR/HI.TTF"

echo "GRUB payload prepared:"
ls -l "$PCDIR/boot.img" "$PCDIR/core.img" iso/boot/GRUBMODS.BIN "$FONTDIR/unicode.pf2"
echo PREP_GRUB_DONE
