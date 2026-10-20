#!/bin/bash
cd /mnt/e/JackBOT-OS/new-chat/JackBOT-OS
make clean
make > build_make.log 2>&1
RC=$?
echo "MAKE_RC=$RC"
echo "--- errors ---"
grep -nE "error:|undefined reference" build_make.log | head -40
echo "--- tail ---"
tail -8 build_make.log
echo "--- objs ---"
ls -1 build/*.o 2>/dev/null | wc -l
ls -l iso/boot/kernel 2>&1
exit $RC
