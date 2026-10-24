#!/bin/bash
cd /root/sysroot/usr/lib
for a in libc.a libcrt0.a libcrt0-hosted.a libcrt0-minimal.a libcrt0-none.a; do
  echo "==== $a ===="
  llvm-nm "$a" 2>/dev/null | grep -E ' T _start$| T main$|[ABCDGRST] (stdin|stdout|stderr)$|(__heap_start|__heap_end)' | head -20
done
echo "==== stdin/stdout in libc.a (context) ===="
llvm-nm libc.a 2>/dev/null | grep -E ' stdin$| stdout$| stderr$|__heap_start|__heap_end' | head -20
