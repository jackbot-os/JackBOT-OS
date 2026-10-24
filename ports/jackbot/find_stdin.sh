#!/bin/bash
cd /root/sysroot/usr/lib || exit 9
echo "PWD=$(pwd)"
echo "=== strong/weak stdio globals in libc.a ==="
llvm-nm -A libc.a 2>/dev/null | grep -E 'stdin$|stdout$|stderr$' | grep -vE ' U '
echo "=== in libcrt0-hosted.a ==="
llvm-nm -A libcrt0-hosted.a 2>/dev/null | grep -E 'stdin|stdout|stderr'
echo "=== members libcrt0-hosted.a ==="
llvm-ar t libcrt0-hosted.a
echo "=== any member defining __iob / console ==="
llvm-nm -A libc.a 2>/dev/null | grep -E ' T __iob| D __iob|__stdio_console|console_init|__picolibc_stdio' | head
