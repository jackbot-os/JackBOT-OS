# JackBOT OS v1.0.1 EastLake

A hobby x86_64 operating system with a graphical installer, first-boot setup
wizard, a desktop environment, and a real networked browser — built from
scratch and tested in VirtualBox.

This repository is the **release / website** repository. It contains only the
ready-to-use ISO and the project website; source code is not published here.

## Download

- **64-bit ISO**: [JackBOT-1.0.1.iso](JackBOT-1.0.1.iso) (~22 MB)
- Project website: [index.html](index.html)

## Features in this release

- **Expanded PCI compatibility database**: more Intel/Realtek Ethernet,
  Intel/AMD/ASMedia AHCI and HD Audio device IDs are recognized at boot
  (detection/probe level; AHCI/NVMe/e1000e functional drivers are next-stage work)
- **Live Desktop mode**: boot the full desktop without installing
- **Real networked browser**: DHCP/TCP/HTTP over the e1000 NIC, real HTTP 200
  pages, relative-URL navigation and automatic 302-redirect following
  (plain HTTP only; HTTPS/TLS is not implemented yet)
- Intel AC97 driver with a boot chime
- Jbot Voice and Bluetooth/LED control panels (interface skeletons; on-device
  speech-to-text and the full L2CAP/GATT pairing stack are next-stage work)
- Native graphical installer (language, welcome, region/time zone, disk
  partitioning, progress, completion)
- First-boot OOBE wizard: language, keyboard layout, time zone
- 205 interface languages with native names and real TTF fonts
- Refreshed full-screen adaptive desktop, taskbar, wallpaper, settings
- Five new system apps: USB Devices, Battery & Power, Recycle Bin, File
  Explorer, Device Manager
- Real hardware enumeration: ACPI tables, USB host controllers/root hub,
  live PCI device tree
- VMSVGA graphics with a GPU-composited hardware cursor
- BIOS + UEFI hybrid bootable image
- MBR partitioner (up to 4 primary partitions), FAT32, ATA LBA48

## Requirements

- x86_64 PC (or a virtual machine)
- 2 GB RAM recommended
- Tested with VirtualBox (graphics: VMSVGA, NIC: Intel PRO/1000 82540EM,
  audio: ICH AC97, mouse: PS/2)

## Quick start (VirtualBox)

1. Create a 64-bit "Other Linux" VM with 2 GB RAM and a virtual hard disk.
2. Set networking to NAT with the Intel PRO/1000/82540EM adapter type to use
   the browser.
3. Attach `JackBOT-1.0.1.iso` to the optical drive and boot from it.
4. Pick "Try JackBOT OS 1.0.1 (Live Desktop)" to explore without installing, or
   run the graphical installer, remove the disc and reboot.
5. After an install, complete the first-boot setup and you reach the desktop.

## License

This repository contains only the compiled release and website. All rights
reserved by the JackBOT OS project. Source code is not distributed here.
## Name
Lee
