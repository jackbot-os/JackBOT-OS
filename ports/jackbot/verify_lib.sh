#!/bin/bash
cd /root/Python-3.12.7
echo "=== archive members ==="
llvm-ar t libpython3.12.a | wc -l
echo "=== pylifecycle.o ELF header ==="
llvm-readelf -h Python/pylifecycle.o | grep -E 'Class|Machine|Type'
echo "=== posixmodule.o ELF header ==="
llvm-readelf -h Modules/posixmodule.o | grep -E 'Class|Machine|Type'
echo "=== unique undefined symbols ==="
llvm-nm libpython3.12.a 2>/dev/null | grep ' U ' | awk '{print $2}' | sort -u > /root/undef.txt
wc -l /root/undef.txt
echo "=== first 60 undefined ==="
head -60 /root/undef.txt
