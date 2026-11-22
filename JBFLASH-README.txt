JackBOT OS 2.0 - ARM64 flash package
=========================================

Files
-----
JackBOT-OS-2.0-arm64.img   raw image (payload starts at its first byte)
boot.img                     Android boot image v0 container, payload in the
                             kernel field, page size 2048, entry 0x40080000
SHA256SUMS.txt               digests of the two images above

What this package really is
---------------------------
This is the AArch64 bring-up image that boots on the QEMU virt machine:
PL011 console, GIC-400, generic timer, MMU, ramfb framebuffer with the
JackBOT desktop, and the AT/telephony stack driven by the local modem
simulator over UART1.

    qemu-system-aarch64 -M virt -cpu cortex-a53 -m 256 ^
        -kernel JackBOT-OS-2.0-arm64.img -device ramfb

What it is not
--------------
1. It is NOT a Mi Pad 4 (clover) firmware. clover is a Snapdragon 660 board:
   its UART, GIC, timer and display live at different addresses, and its
   peripherals need drivers that this image does not contain. Flashing it to
   clover would not boot.
2. It is NOT signed with a Xiaomi key. An unlocked bootloader is required
   before any custom boot payload can be written; with the lock on, fastboot
   rejects the partition write. No script in this package tries to get around
   that check.
3. The telephone and SMS paths run against the local AT simulator. They are
   not proven against a real baseband.

Flashing (only on hardware whose bootloader is already unlocked)
--------------------------------------------------------------
    fastboot devices
    fastboot getvar unlocked        # must report "yes"; stop if it does not
    fastboot flash boot boot.img
    fastboot reboot

Build tooling notice
--------------------
The aarch64-none-elf GCC/binutils toolchain and QEMU are GPL-licensed tools
used only at build and test time. They are not linked into, bundled with, or
redistributed as part of the JackBOT image. Everything in the images above is
JackBOT code under the HJMA Proprietary Software License v1.0.
