# JackBOT OS v0.5.0 "EastLake"

A hobby x86_64 operating system with a graphical installer, first-boot setup
wizard, and a desktop environment — built from scratch and tested in VirtualBox.

This repository is the **release / website** repository. It contains only the
ready-to-use ISO and the project website; source code is not published here.

## Download

- **64-bit ISO**: [JackBOT-0.5.0.iso](JackBOT-0.5.0.iso) (~22.4 MB)
- Project website: [index.html](index.html)

## Features in this release

- Native graphical installer (language, welcome, region/time zone, disk
  partitioning, progress, completion)
- First-boot OOBE wizard: language, keyboard layout, time zone
- 205 interface languages with native names and real TTF fonts
- Full-screen adaptive desktop, taskbar, wallpaper, settings
- VMSVGA graphics with a GPU-composited hardware cursor
- BIOS + UEFI hybrid bootable image
- MBR partitioner (up to 4 primary partitions), FAT32, ATA LBA48

## Requirements

- x86_64 PC (or a virtual machine)
- 2 GB RAM recommended
- Tested with VirtualBox (graphics: VMSVGA, mouse: PS/2)

## Quick start (VirtualBox)

1. Create a 64-bit "Other Linux" VM with 2 GB RAM and a virtual hard disk.
2. Attach `JackBOT-0.5.0.iso` to the optical drive and boot from it.
3. Follow the graphical installer, then remove the disc and reboot.
4. Complete the first-boot setup and you will reach the desktop.

## License

This repository contains only the compiled release and website. All rights
reserved by the JackBOT OS project. Source code is not distributed here.

