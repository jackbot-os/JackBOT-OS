#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

make iso >/dev/null

cp /usr/share/OVMF/OVMF_VARS_4M.fd build/OVMF_VARS_4M.fd

LOG="build/boot_uefi.log"
rm -f "$LOG"

timeout -s KILL 30 qemu-system-x86_64 \
    -machine q35 \
    -m 512 \
    -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
    -drive if=pflash,format=raw,file=build/OVMF_VARS_4M.fd \
    -cdrom build/JackBOT-0.5.0.iso \
    -display none \
    -serial file:"$LOG" || true

echo "=== serial log ==="
cat "$LOG"

if grep -q "serial console alive" "$LOG"; then
    echo "UEFI BOOT TEST: PASS"
else
    echo "UEFI BOOT TEST: FAIL"
    exit 1
fi
