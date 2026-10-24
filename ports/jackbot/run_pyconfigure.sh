#!/usr/bin/env bash
# Cross-configure CPython 3.12 against picolibc for JackBOT (M1, stage 1).
# Runs inside the Debian build VM after build_m1.sh has made /root/sysroot.
set -uo pipefail

mkdir -p /root/bin
cp /root/x86_64-unknown-none-cc /root/bin/x86_64-unknown-none-cc
chmod +x /root/bin/x86_64-unknown-none-cc
export PATH=/root/bin:$PATH
export CONFIG_SITE=/root/config.site

cd /root/Python-3.12.7 || exit 2
make distclean >/dev/null 2>&1 || true

# --host is declared linux-gnu so CPython's MACHDEP table accepts the cross
# build (it has no bare-metal entry); the CC wrapper still emits
# x86_64-unknown-none code against picolibc. config.site answers the probes
# that require running a target binary.
#
# CPython 3.12 removed --without-threads, so pthread primitives will be
# required later; a bare-metal pthread shim is a separate M1 work item.
./configure \
  --host=x86_64-unknown-linux-gnu \
  --build=x86_64-pc-linux-gnu \
  --disable-shared \
  --without-ensurepip \
  --with-build-python=/root/py-host/python \
  CC=x86_64-unknown-none-cc \
  AR=llvm-ar \
  RANLIB=llvm-ranlib \
  READELF=llvm-readelf \
  CFLAGS="-ffreestanding -mno-red-zone -fno-stack-protector"

echo "CFG=$?"
