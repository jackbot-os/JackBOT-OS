#!/usr/bin/env bash
cd "$(dirname "$0")/.."
pkill -f 'qemu-system-x86_64.*JackBOT' 2>/dev/null
qemu-system-x86_64 -machine pc -m 512 -cdrom build/JackBOT-1.0.0.iso \
  -display none -s -S -serial file:build/gdb_boot_serial.log &
QPID=$!
sleep 2
gdb -q -nx iso/boot/kernel \
  -ex 'set pagination off' \
  -ex 'target remote localhost:1234' \
  -ex 'hbreak *0x110f80' \
  -ex 'continue' \
  -ex 'printf "== reached garbage 0x110f80 ==\n"' \
  -ex 'info registers rip rsp rflags' \
  -ex 'printf "== stack dump ==\n"' \
  -ex 'x/24gx $rsp' \
  -ex 'kill' \
  -ex 'quit' </dev/null 2>&1 | grep -vE 'Reading symbols|New remote|Remote debugging|Single stepping'
kill $QPID 2>/dev/null
