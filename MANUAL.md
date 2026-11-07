# JackBOT OS v1.0.4 鈥?User Manual

## 1. Booting

1. Attach `JackBOT-1.0.4.iso` to a VirtualBox VM (2 GB RAM, VMSVGA graphics,
   PS/2 mouse, NAT networking) or burn it to a disc / USB stick on real x86_64
   hardware.
2. The GRUB menu offers:
   - **Try JackBOT OS (Live Desktop)** 鈥?boots the full desktop without a disk.
   - **Install JackBOT OS** 鈥?the graphical installer.
3. The installer asks for the **language** (205 languages with native names),
   region/time zone, and the target disk (automatic or custom MBR partitioning,
   FAT32 format, GRUB install).

> Language is chosen **only in the installer**. The system settings panel
> deliberately has no language switch; it changes language only through a
> reinstall.

## 2. First boot (OOBE)

On the first start after installation a wizard sets display language, keyboard
layout and time zone/region; the choices are written to `JBOT.CFG` on disk.

## 3. Desktop

- **F2** 鈥?system settings panel: 5 tabs (Display, Region, Audio, Permissions,
  System). The Region tab lets you pick a real region and toggle satellite
  location permission.
- **Start menu** 鈥?power, store, firewall, backup, capture, calendar, font
  manager, region, network, audio, update, browser (JBrowser), 3D studio, disk
  manager, about, calculator, notepad, monitor, Python console, JCA runner, USB
  devices, battery/power, recycle bin, file manager, device manager, voice,
  Bluetooth, games.
- **Jbot** 鈥?the built-in assistant window (self-contained intent model, no
  external API). It answers greetings, help, time/date, disk free space, system
  information, simple arithmetic and more.
- Wallpaper thumbnails and the settings panel are drawn by the kernel's own GUI
  code (no X11).

## 4. Networking and the browser

- The network stack brings the link up by DHCP. Supported NICs: AMD PCnet
  (Am79C970A/973 鈥?VirtualBox default) and Intel PRO/1000 (82540EM), plus
  RTL-8139.
- Open **JBrowser** from the start menu, type a URL and press Enter.
  - `http://` pages load over plain TCP/HTTP.
  - `https://` pages now work: the browser performs a TLS 1.2 handshake
    (ECDHE/X25519, AES-128-GCM) and decrypts the page.

### HTTPS honesty note

The TLS implementation is real (handshake and record encryption verified
against OpenSSL), **but the server certificate is not validated** 鈥?there is no
CA trust store and no chain/signature verification. A man-in-the-middle cannot
be detected. Do not use this browser for anything sensitive. TLS 1.3 is not
supported.

## 5. Files and programs

- Filesystem: FAT32 on MBR partitions (create/read/write/overwrite/delete,
  subdirectories, long file names).
- Program formats: `.jca` executable, `.sh` script, `.lib` library, `.xjh`
  batch, `.control` remote-control program.
- **Python console**: a built-in subset interpreter (arithmetic, strings,
  lists, dicts, functions, recursion, `for`/`while`/`if`, common built-ins).
  It is not CPython: no imports, classes, exceptions or standard library.
- **JCA runner** executes `.jca` programs through the built-in loader.

## 6. Honeypot (off by default)

The honeypot module is **passive only**: it never opens a port, never sends a
packet and never probes anything. When enabled in the Firewall app it records
unclaimed UDP traffic, TCP SYN probes and ICMP echo requests into an in-memory
table (visible in the firewall app) and writes one serial audit line per new
probe. It is disabled by default and only logs what already reaches the
machine.

## 7. Updating and recovery

- System Update app: fetches a manifest, downloads a package and verifies its
  SHA-256 before accepting it.
- If the desktop does not appear, check the serial console output
  (`virtualbox serial.log`): boot progress, self-test results and any crash
  record are printed there.

## 8. Known limitations

See the "Known limitations" section of the README. In short: no certificate
validation, TLS 1.2 only, Python subset only, several hardware drivers are
detection-only, Bluetooth/voice are skeletons, single-core scheduling.

## 9. Running C programs (.jca native payloads)

A .jca file that begins with the JCAN magic carries a compiled x86-64
image instead of JCA bytecode. Build one on the Debian build VM:

    python tools/jcacc.py hello.c -o HELLO.JCA

(chibicc compiles the C, s/ld produce a flat image at USER_BASE, and
the tool prepends the JCAN header.) Copy HELLO.JCA to a FAT32 disk the
system mounts and open it from the File Explorer: it is started as a ring-3
process and talks to the kernel through the JackBOT syscall ABI (int 0x80,
ax=0 print one character, ax=2 exit). C++ works as well: pass a .cpp/.cc file and jcacc.py uses the host g++
in freestanding mode (no STL, no exceptions, no RTTI, no crt).

