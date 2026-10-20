#!/bin/bash
OUT=/mnt/e/JackBOT-OS/new-chat/JackBOT-OS/diag_out.txt
cp /mnt/e/JackBOT-OS/new-chat/JackBOT-OS/iso/boot/kernel /tmp/k
{
  echo "=== addr2line 0x105582 ==="
  addr2line -e /tmp/k -f -i 0x105582
  echo "=== enclosing/next function ==="
  nm -n /tmp/k | grep -E ' [tT] ' > /tmp/t.txt
  python3 - <<'PY'
target=0x105582
prev=None; nxt=None
for line in open('/tmp/t.txt'):
    p=line.split()
    if len(p)<3: continue
    try: a=int(p[0],16)
    except: continue
    if a<=target: prev=line.strip()
    elif nxt is None:
        nxt=line.strip(); break
print('enclosing:', prev)
print('next     :', nxt)
PY
  echo "=== svga/fb/pci map symbols ==="
  nm /tmp/k | grep -iE 'svga_|fb_init|map_mmio|vmm_map|pci_enum|mem_selftest|payload_collect'
} > "$OUT" 2>&1
echo "DIAG_DONE" >> "$OUT"
