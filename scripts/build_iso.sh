#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

mkdir -p build
make clean
make iso

echo "=== result ==="
ls -la build/JackBOT-1.0.0.iso
