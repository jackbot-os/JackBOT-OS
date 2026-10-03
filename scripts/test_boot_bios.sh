#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

make iso >/dev/null

LOG="build/boot_bios.log"
rm -f "$LOG"

timeout -s KILL 20 qemu-system-x86_64 \
    -machine pc \
    -m 512 \
    -cdrom build/JackBOT-0.5.0.iso \
    -display none \
    -serial file:"$LOG" || true

echo "=== serial log ==="
cat "$LOG"

if grep -q "timer IRQ alive" "$LOG"; then
    echo "BIOS BOOT TEST: PASS"
else
    echo "BIOS BOOT TEST: FAIL"
    exit 1
fi
