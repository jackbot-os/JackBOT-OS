#!/bin/bash
OUT=/mnt/e/JackBOT-OS/new-chat/JackBOT-OS/dis_out.txt
cp /mnt/e/JackBOT-OS/new-chat/JackBOT-OS/iso/boot/kernel /tmp/k
{
  echo "=== objdump start ==="
  objdump -d /tmp/k > /tmp/dis.txt 2>&1
  echo "objdump rc=$? lines=$(wc -l < /tmp/dis.txt)"
  echo "=== context around 105582 ==="
  grep -nE '1055[0-9a-f]{2}:' /tmp/dis.txt | head -20
  echo "=== nearest function label before 105582 ==="
  awk '/^[0-9a-f]+ </{lbl=$0} / 105582:/{print lbl; print; exit}' /tmp/dis.txt
} > "$OUT" 2>&1
echo "DIS_DONE" >> "$OUT"
