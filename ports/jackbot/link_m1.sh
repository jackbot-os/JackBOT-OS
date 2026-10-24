#!/bin/bash
# M1 link probe: standalone bare-metal ELF using picolibc's hosted crt0 and
# noflash linker script. Quantifies unresolved symbols after libpython +
# built-in objects + picolibc. Does NOT claim CPython runs.
set -u
export PATH=/root/bin:$PATH
TRIPLE=x86_64-unknown-none
SYSROOT=/root/sysroot
LIBDIR=$SYSROOT/usr/lib
PY=/root/Python-3.12.7
CC=$TRIPLE-cc
OBJ=/root/m1obj
mkdir -p "$OBJ" /root/inc/kernel

cat > /root/inc/kernel/serial.h <<'EOF'
#ifndef KERNEL_SERIAL_H
#define KERNEL_SERIAL_H
void serial_putc(char c);
void serial_puts(const char *s);
#endif
EOF

"$CC" -ffreestanding -mno-red-zone -c /root/m1_serial_stub.c -o "$OBJ/serial_stub.o"
"$CC" -ffreestanding -mno-red-zone -I/root/inc -I"$PY/Include" -I"$PY" -I"$PY/Include/internal" -c /root/jackbot_main.c -o "$OBJ/jackbot_main.o"
"$CC" -ffreestanding -mno-red-zone -I"$PY/Include" -I"$PY" -c /root/m1_syscalls.c -o "$OBJ/m1_syscalls.o"
"$CC" -ffreestanding -mno-red-zone -I"$PY/Include" -I"$PY" -c /root/m1_main.c -o "$OBJ/m1_main.o"
echo "SUPPORT_OBJECTS=$?"

OBJS=$(find "$PY" -name '*.o' ! -path '*/py-host/*' ! -path '*/Programs/*' | sort)
echo "PY_OBJECTS=$(echo "$OBJS" | wc -l)"

# ld.lld lacks GNU ld's ALIGN_WITH_INPUT; strip it for the probe.
LDSCRIPT=/root/m1_noflash.ld
sed '/^[[:space:]]*ALIGN_WITH_INPUT[[:space:]]*$/d' "$LIBDIR/picolibc_noflash.ld" > "$LDSCRIPT"

ld.lld -T "$LDSCRIPT" \
  --defsym __flash=0x00100000 --defsym __flash_size=0x10000000 \
  --defsym __ram=0x40000000 --defsym __ram_size=0x40000000 \
  --gc-sections \
  "$LIBDIR/crt0-hosted.o" \
  "$OBJ/m1_main.o" "$OBJ/jackbot_main.o" "$OBJ/m1_syscalls.o" "$OBJ/serial_stub.o" \
  --start-group $OBJS "$LIBDIR/libcrt0-hosted.a" "$LIBDIR/libc.a" "$LIBDIR/libm.a" \
  --end-group \
  -o /root/m1_python.elf 2>/root/link.log
echo "LINK=$?"
ls -la /root/m1_python.elf 2>/dev/null || echo "NO ELF YET"
echo "=== unresolved count ==="
grep -c 'undefined symbol' /root/link.log || true
grep 'undefined symbol' /root/link.log | sed -E 's/.*undefined symbol: //; s/ >>> referenced.*//' | sort -u > /root/link_undef.txt
echo "=== unique unresolved ==="
wc -l /root/link_undef.txt
head -80 /root/link_undef.txt
