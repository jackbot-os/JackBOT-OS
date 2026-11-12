# -*- coding: utf-8 -*-
"""Create the on-disk system tree that ships on the boot volume (FAT32, 8.3 names)."""
import os

ROOT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "system")

FILES = {
"README.TXT": """JackBOT OS system tree
======================
This directory ships on the boot volume after the installer finishes.
It holds the configuration files, driver descriptors and scripts that the
system reads at boot. Everything here is plain text on purpose: you can open,
diff and repair any file from the File Manager or the Terminal.
""",

"ETC/SYSTEM.INI": """[system]
product=JackBOT OS
version=1.0.9
codename=EastLake
arch=x86_64
boot=multiboot2
timezone=Asia/Shanghai
language=en

[limits]
max_threads=64
kernel_stack_kb=256
user_heap_default_mb=64

[policy]
allow_user_processes=1
panic_on_fault=1
recover_user_fault=1
""",

"ETC/BOOT.INI": """[boot]
default_entry=1
timeout_seconds=3
serial_debug=1
serial_port=0x3F8
serial_baud=115200

[entries]
1=JackBOT OS 1.0.9 (EastLake)
2=JackBOT OS 1.0.9 (safe mode, no drivers)
""",

"ETC/DISPLAY.INI": """[display]
preferred=1920x1080
fallback=1024x768
min=800x600
depth=32
double_buffer=1
dirty_rect=1

[modes]
800x600
1024x768
1280x720
1366x768
1600x900
1920x1080
2560x1440
3840x2160

[scaling]
ui_scale_min=0.75
ui_scale_max=2.0
""",

"ETC/DRIVERS.INI": """[drivers]
autoload=1
probe_order=pci,usb,ps2

[video]
vbe
svga
vmware_svga
intel_gpu
amd_gpu
nvidia_gpu

[storage]
ata
ahci

[input]
ps2_keyboard
ps2_mouse
usb_hid

[net]
e1000
rtl8139
pcnet

[audio]
ac97
hda
""",

"ETC/NET.INI": """[net]
mode=dhcp
dns1=223.5.5.5
dns2=1.1.1.1
mtu=1500
http_timeout_ms=8000

[http]
user_agent=JackBOT-OS/1.0.9
max_redirects=3
tls=enabled
""",

"ETC/HOSTS.CFG": """127.0.0.1 localhost
::1 localhost
""",

"ETC/SECURITY.INI": """[security]
firewall_default=on
honeypot=off
password_hash=pbkdf2-sha256
password_salt_len=16
session_lock_on_idle=0

[permissions]
require_consent_for=microphone,voice,camera
""",

"ETC/LOCALE.INI": """[locale]
language=en
date_format=YYYY-MM-DD
time_format=24h
first_day=1
""",

"ETC/USERS.INI": """[users]
count=1

[user1]
name=owner
role=admin
home=\\USERS\\OWNER
""",

"ETC/THEME.INI": """[theme]
name=liquid-glass
base=dark
accent=#5A6EF0
accent2=#B6E300
corner_radius=12
density=comfortable
cursor=software

[wallpaper]
default=\\WALLPAPER.BMP
mode=fit
""",

"ETC/FONTS.INI": """[fonts]
sans=\\SANS.TTF
sans_bold=\\MONOB.TTF
mono=\\MONO.TTF
cjk=\\CJK.TTF
arabic=\\AR.TTF
korean=\\KR.TTF
thai=\\TH.TTF
hindi=\\HI.TTF

[render]
hinting=1
fallback=1
""",

"ETC/KEYMAP.INI": """[keymap]
layout=us
compose=0
repeat_delay_ms=250
repeat_rate_ms=30
""",

"BIN/SYSINFO.JBS": """# sysinfo - print product, memory and display facts
echo JackBOT OS 1.0.9 (EastLake)
echo arch   : x86_64
mem
display
""",

"BIN/CLEAN.JBS": """# clean - clear temporary files from the boot volume
echo cleaning \\TEMP
del \\TEMP\\*
echo cleaning \\RECYCLED
del \\RECYCLED\\*
""",

"BIN/LOGDUMP.JBS": """# logdump - copy the last boot log out of the volume
copy \\BOOT.LOG \\LOGS\\LAST.LOG
echo copied
""",

"BIN/BACKUP.JBS": """# backup - copy the system configuration to a backup folder
mkdir \\BACKUP
copy \\SYSTEM\\ETC\\*.INI \\BACKUP\\
echo configuration backed up
""",

"BIN/HELLO.JBS": """echo Hello from JackBOT OS
""",

"DRV/PS2.KD": """name=ps2
bus=isa
irq=1
ports=0x60,0x64
provides=keyboard,mouse
""",

"DRV/ATA.KD": """name=ata
bus=pci
class=0x0101
provides=block
pio=1
""",

"DRV/AHCI.KD": """name=ahci
bus=pci
class=0x0106
provides=block
dma=1
""",

"DRV/VBE.KD": """name=vbe
bus=bios
provides=framebuffer
modes=vesa,bochs,vmware
""",

"DRV/E1000.KD": """name=e1000
bus=pci
vendor=0x8086
devices=0x100E,0x100F,0x10D3
provides=net
""",

"DRV/R8139.KD": """name=rtl8139
bus=pci
vendor=0x10EC
devices=0x8139
provides=net
""",

"DRV/USBAUD.KD": """name=usb_hid
bus=usb
class=0x03
provides=keyboard,mouse
""",

"LIB/VERSION.TXT": """1.0.9
EastLake
2026-11
""",

"LIB/PROTOCOL.TXT": """Kernel to app interface, current call set:
  ui_fit / ui_window / ui_rect / ui_border   window drawing
  fb_*                                        framebuffer access
  thread_enum / thread_count_user             process enumeration
  pmm_total_bytes / pmm_free_bytes            memory facts
  timer_get_ticks                             uptime source
""",

"LICENSE/LICENSE.TXT": """JackBOT OS License (draft, not legal advice)
=============================================
Copyright (c) 2026 the JackBOT OS author. All rights reserved.

1. You may run this software on any machine you own.
2. You may read the source for learning and for reporting bugs.
3. You may not redistribute the source or binaries, in whole or in part,
   without written permission.
4. You may not ship it inside another product, paid or free.
5. No warranty. The author is not liable for data loss or hardware damage.

This is NOT the GNU GPL. The GPL requires that everyone who receives the
software also receives the source and may redistribute it; that is the
opposite of what this licence allows, so the GPL name must not be used on
this project.
""",
}


def main():
    for rel, body in FILES.items():
        path = os.path.join(ROOT, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(body)
    print("files:", len(FILES))


if __name__ == "__main__":
    main()
