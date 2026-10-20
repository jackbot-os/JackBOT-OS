#!/bin/bash
set -e
export DEBIAN_FRONTEND=noninteractive

echo "== configure apt mirrors (Debian 11 bullseye) =="
cp /etc/apt/sources.list /etc/apt/sources.list.orig 2>/dev/null || true

# bullseye is archived: only the base repo still carries every needed
# package; the security/updates pools were pruned on mirrors (404s).
echo 'Acquire::Check-Valid-Until "false";' > /etc/apt/apt.conf.d/99noexpiry
cat > /etc/apt/sources.list <<'EOF'
deb http://mirrors.aliyun.com/debian bullseye main
EOF

if ! apt-get update; then
  echo "== aliyun failed, falling back to archive.debian.org =="
  cat > /etc/apt/sources.list <<'EOF'
deb http://archive.debian.org/debian bullseye main
EOF
  apt-get update
fi

echo "== install build toolchain =="
apt-get install -y --no-install-recommends \
  gcc make nasm xorriso mtools bash \
  grub-pc-bin grub-common grub-efi-amd64-bin \
  xxd python3 python3-pil ca-certificates wget

echo "== versions =="
gcc --version | head -1
nasm --version | head -1
make --version | head -1
xorriso --version 2>/dev/null | head -1
grub-mkrescue --version 2>/dev/null | head -1
python3 --version
python3 -c "import PIL; print('PIL', PIL.__version__)"
ls -d /usr/lib/grub/i386-pc
echo "== SETUP_WSL_DONE =="
