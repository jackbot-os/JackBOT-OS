# JackBOT OS v1.0.4 EastLake

A from-scratch x86_64 hobby operating system (C + assembly kernel, custom GUI,
FAT32, own program format) with a graphical installer, a first-boot setup
wizard, a desktop environment and a real networked browser that speaks HTTP
**and HTTPS (TLS 1.2)**.

This repository is the **release / website** repository. It contains the
ready-to-use ISO, the project website and the user manual. Kernel source code
is not published here.

## Download

- **64-bit ISO**: [JackBOT-1.0.4.iso](JackBOT-1.0.4.iso) (34.92 MB)
- SHA-256:
  `984a8cea1dd22fff2bd25113292374d3b596a050d30a459f7b41cd9ab0031593`
- MD5: `cc56a8abaec1d83a851990f74056eb60`
- Project website: [index.html](index.html)
- User manual: [MANUAL.md](MANUAL.md)

## Build environment (reproducible)

- **Debian 13 (Trixie) inside VirtualBox** the only build environment used
  (no WSL, no QEMU, no VMware).
- gcc 14.2.0 (freestanding, C11), NASM 2.16.03, GNU ld, GRUB 2 / grub-mkrescue,
  xorriso.
- Build: `make iso` 鈫?`build/JackBOT-1.0.4.iso`.
- Result of the shipped build: full clean rebuild, `BUILD_RC=0`, one pre-existing
  linker warning remains (`ld: warning: iso/boot/kernel has a LOAD segment with
  RWX permissions`).

## What is new in v1.0.4

- **Real HTTPS in the built-in browser** a self-written TLS 1.2 client
  (ECDHE with X25519, AES-128-GCM record layer, SHA-256 PRF, Finished
  verification) is integrated into the HTTP client and loads encrypted pages.
- **AMD PCnet NIC driver** (PCnet-PCI II / FAST III 79C970A/973/975) 鈥?the
  VirtualBox default adapter now works: DHCP, DNS, TCP, HTTP and HTTPS all run
  over it (verified: DHCP bound, `https://example.com` 584-byte page fetched
  through TLS).
- **Kernel memory-safety fixes** user-process address-space activation race
  fixed (the boot self-test process no longer faults at USER_BASE); heap rejects
  size wrap-around, guards `calloc` overflow, validates header magic on
  `realloc`, poisons headers on free (double-free detection); page-table
  allocation failures no longer dereference NULL; wallpaper-buffer allocation
  failure falls back to a solid background.
- **F2 settings-panel crash fixed** the `wp_color` divide-by-zero
  (`w - 1 == 0` for a 1-pixel-wide thumbnail) is guarded; the settings panel
  opens and renders normally (verified in VirtualBox, 0 crashes, 0 panics).
- **Python subset hardening** 鈥?container allocation, `range` cap, `split`
  token bound and `str.replace` error path hardened; offline harness passes
  52/52 cases plus the built-in self-test.
- **Liquid-glass menus and a faster desktop** — the start menu, its right-hand
  quick buttons (Settings / Jbot / Browser / Files / Back) and the right-click
  context menu are now alpha-blended over a freshly repainted frame, so they
  stay translucent without ghost trails, and opening an app no longer wipes
  the window behind the menu. The mouse IRQ no longer writes a serial line per
  packet and memcpy/memset are word-at-a-time, which removes the pointer
  stutter and the jerky animations.
- **Passive honeypot audit trail** still off by default, still never sends a
  packet; now writes one serial audit line per newly observed probe (unclaimed
  UDP, TCP SYN, ICMP echo).

Existing v1.0.x features retained: graphical installer, first-boot OOBE, 205
interface languages with real TTF fonts, VMSVGA hardware cursor, adaptive
full-screen desktop, ATA LBA48 + MBR + FAT32, JCA programs, micro-Python
console, AC97 boot chime, Jbot voice/AI panel, Bluetooth/LED panel skeletons,
USB/ACPI enumeration.

## Requirements

- x86_64 PC or virtual machine; 2 GB RAM recommended.
- Tested with VirtualBox: graphics VMSVGA, NIC **AMD PCnet-PCI II (default) or
  Intel PRO/1000 82540EM**, audio ICH AC97, PS/2 mouse.

## Quick start (VirtualBox)

1. Create a 64-bit VM with 2 GB RAM and a virtual hard disk.
2. Networking: NAT. The default Am79C973 (PCnet) adapter and the Intel PRO/1000
   MT 82540EM adapter are both supported.
3. Attach `JackBOT-1.0.4.iso` to the optical drive and boot from it.
4. Pick "Try JackBOT OS (Live Desktop)" to explore without installing, or run
   the graphical installer (remove the disc and reboot when it finishes).
5. After installation, complete the first-boot OOBE (language, keyboard, time
   zone) and the desktop appears. Press **F2** for the settings panel.

## Known limitations (honest list)

- **HTTPS certificate validation is NOT implemented.** The TLS 1.2 handshake and
  record encryption are real and verified against OpenSSL, but the server
  certificate chain is only parsed structurally 鈥?it is not validated against a
  trust store, so a man-in-the-middle cannot be detected. TLS 1.3 is not
  supported; servers that refuse TLS 1.2 will fail.
- One TLS session at a time (the browser opens one page at a time).
- The Python console runs a hand-written subset (no imports, classes,
  exceptions or standard library); CPython is not ported.
- Several compatibility-database entries are detection probes only: AHCI/NVMe
  command queues and e1000e are not driven functionally.
- Bluetooth pairing/L2CAP/GATT, on-device speech-to-text and the voice assistant
  are interface skeletons.
- OOBE keyboard layout selection is stored but does not remap every physical key.
- The honeypot log is in memory + serial only (no on-disk log), off by default.
- Single-core scheduling; no SMP, no preemption across cores.
- Remaining toolchain warning: the kernel LOAD segment is RWX.

## Tests run for this release (all local, inside VirtualBox)

- Clean rebuild in Debian 13 VM (`BUILD_RC=0`).
- Boot in VirtualBox: desktop renders, **F2 settings panel opens without any
  kernel fault or panic** (serial: 0 crash lines, 0 panic lines; screenshot
  palette analysis matches the settings-panel colours).
- Network: PCnet driver init OK, DHCP bound, TCP/HTTP fetch OK,
  **TLS 1.2 handshake to `example.com` OK, 584-byte encrypted page received**.
- Offline harnesses: X25519 (RFC 7748 vectors), AES-128-GCM (NIST SP 800-38D
  test cases 1-4, decrypt + tamper detection), Python subset 52/52, FAT32
  functional suite + 200-round fuzz under ASAN (60 000 operations, no crash).
- Honeypot harness: default-off, passive-only, merge/clear/capacity behaviour.

## License

This repository contains only the compiled release, the website and the manual.
All rights reserved by the JackBOT OS project. Source code is not distributed
here.

## Name

Lee




