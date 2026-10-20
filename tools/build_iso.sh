#!/bin/bash
cd /mnt/e/JackBOT-OS/new-chat/JackBOT-OS
which mkfs.fat >/dev/null 2>&1 || apt-get install -y --no-install-recommends dosfstools
make iso > build_iso.log 2>&1
RC=$?
echo "ISO_RC=$RC"
echo "--- errors ---"
grep -nE "error|No such|not found|Error" build_iso.log | head -30
echo "--- tail ---"
tail -12 build_iso.log
echo "--- artifacts ---"
ls -l build/JackBOT-0.5.0.iso build/disk.img 2>&1
exit $RC
