#!/usr/bin/env bash
# CPython M1 build for JackBOT on x86_64-unknown-none with picolibc.
#
# Run inside the Debian build VM (clang/meson/ninja on PATH).
#
# Status:
#   Parts 1-3 are VERIFIED (picolibc cross libc builds, sysroot installs,
#   a libc-using object compiles for x86_64-unknown-none).
#   Part 4 (libpython3.12.a) is WIP. Per PORTING.md the M1 milestone is NOT
#   complete until a kernel test task runs Py_Initialize and serial prints 2.
#
# Host paths inside the VM mirror what was used during the verified run.
set -uo pipefail

TRIPLE=x86_64-unknown-none
PICOLIBC_TAR=${PICOLIBC_TAR:-$HERE/picolibc.tar.gz}
HERE=${JB_M1_HOME:-$HOME/jackbot-m1}
mkdir -p "$HERE"
SRC=$HERE/picolibc-main
SYSROOT=$HERE/sysroot
CROSS=$HERE/x86_64-jackbot-cross.txt

# --- 1. toolchain (Debian 13) -------------------------------------------
# apt-get install -y --no-install-recommends clang llvm meson ninja-build

# --- 2. picolibc (bare-metal C library) ----------------------------------
test -d "$SRC" || tar xzf "$PICOLIBC_TAR" -C "$HERE"
rm -rf "$SRC/build-x86_64"
meson setup "$SRC/build-x86_64" "$SRC" \
  --cross-file="$CROSS" \
  -Dtests=false -Dmultilib=false -Dsemihost=false \
  -Dsingle-thread=true -Dio-long-long=true \
  -Dnewlib-global-errno=true -Dthread-local-storage=false
ninja -C "$SRC/build-x86_64"
meson configure "$SRC/build-x86_64" -Dprefix=/usr
rm -rf "$SYSROOT"
meson install -C "$SRC/build-x86_64" --destdir "$SYSROOT"
echo "picolibc installed: $(find "$SYSROOT" -name libc.a)"

# --- 3. smoke test: compile a libc-using object (linking is M2) ----------
clang --target=$TRIPLE -ffreestanding -nostdlib -mno-red-zone \
  --sysroot="$SYSROOT" -c "$HERE/m1_smoke.c" -o "$HERE/m1_smoke.o"
file "$HERE/m1_smoke.o"
echo "SMOKE OK"

# --- 4. CPython 3.12 -> libpython3.12.a (WIP) ----------------------------
# Autotools cross build for a hosted C runtime on bare metal. configure's
# link/run probes need either a linkable crt+syscalls+linker-script or the
# matching ac_cv overrides in config.site; this is iterated from the real
# configure errors, not guessed.
PY=$HERE/Python-3.12.7
if [ -d "$PY" ]; then
  cd "$PY"
  make distclean >/dev/null 2>&1 || true
  CONFIG_SITE="$HERE/config.site" ./configure \
    --host=$TRIPLE --build=x86_64-pc-linux-gnu \
    --disable-shared --without-threads --without-ensurepip \
    --with-build-python=python3 \
    CC="clang --target=$TRIPLE --sysroot=$SYSROOT"
  echo "configure exit: $?"
fi
