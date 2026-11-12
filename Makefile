.DEFAULT_GOAL := all

CC := gcc
NASM := nasm

CFLAGS := -ffreestanding -m64 -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
          -mcmodel=large -std=c11 -Wall -Wextra -g -fno-strict-aliasing \
          -Iinclude -Ibuild \
          -Ithird_party/stb -DNET_SELFTEST -MMD -MP
NASMFLAGS := -f elf64
LDFLAGS := -n -T arch/x86_64/linker.ld

BUILD := build
KERN_SRCS := main idt pic timer clock serial trap apic pmm vmm heap kstring sched userproc crash panic pci session ac97 dev compat nic nic_rtl8139 nic_pcnet fb svga gpu gpu_nv gpu_intel gpu_amd cursor ata blk fat32 part mkfat fsprobe payload grub_install installer installer_gui protect persist config oobe jbot i18n gfxtext ttf py jca jcac posix ps2 hwd hwcompat desktop ui apps app_power app_store app_firewall app_backup app_capture app_clock app_fontmgr app_region app_network app_audio app_maps e1000 net tcp http sha256 x25519 aes_gcm app_update app_browser gpu3d app_3d app_diskmgr app_about app_calc app_notepad app_monitor app_pyconsole app_term app_procmgr app_jca usb wlan acpi recycle app_usb app_battery app_recycle app_files app_devices selftest splash hal bluetooth voice app_voice app_bluetooth hmac_sha256 tls ahci honeypot wallpaper_image app_games perm users login
ARCH_ASM := boot gdt vectors switch
OBJS := $(addprefix $(BUILD)/,$(addsuffix .o,$(KERN_SRCS))) \
        $(addprefix $(BUILD)/,$(addsuffix .o,$(ARCH_ASM)))
DEPS := $(OBJS:.o=.d) $(DRV_OBJS:.o=.d)
-include $(DEPS)
KERNEL := iso/boot/kernel
ISO := $(BUILD)/JackBOT-1.0.9.iso
DISK := $(BUILD)/disk.img
USER_BIN := $(BUILD)/ustest.bin
USER_H := $(BUILD)/ustest.h
PTHREAD_USER_BIN := $(BUILD)/uthread.bin
PTHREAD_USER_H := $(BUILD)/uthread.h

.PHONY: all iso clean

all: $(KERNEL)

$(BUILD):
	mkdir -p $(BUILD)

CRASH_USER_BIN := $(BUILD)/ucrash.bin
CRASH_USER_H := $(BUILD)/ucrash.h

$(USER_H): user/ustest.asm | $(BUILD)
	$(NASM) -f bin $< -o $(USER_BIN)
	python3 tools/bin_to_header.py $(USER_BIN) $@ ustest_bin

$(CRASH_USER_H): user/ucrash.asm | $(BUILD)
	$(NASM) -f bin $< -o $(CRASH_USER_BIN)
	python3 tools/bin_to_header.py $(CRASH_USER_BIN) $@ ucrash_bin

$(PTHREAD_USER_H): user/uthread.asm | $(BUILD)
	$(NASM) -f bin $< -o $(PTHREAD_USER_BIN)
	python3 tools/bin_to_header.py $(PTHREAD_USER_BIN) $@ uthread_bin

I18N_JSON := tools/i18n_data.json
I18N_H := $(BUILD)/i18n_data.h
CS_FILE := $(BUILD)/extra_charset.txt
$(I18N_H) $(CS_FILE): $(I18N_JSON) tools/gen_i18n.py | $(BUILD)
	python3 tools/gen_i18n.py $(I18N_JSON) $(I18N_H) $(CS_FILE)

DROID_TTF := assets/fonts/CJK.ttf
DEJAVU_TTF := assets/fonts/SANS.ttf
MONO_TTF := assets/fonts/MONO.ttf
MONOB_TTF := assets/fonts/MONOB.ttf
KR_TTF := assets/fonts/KR.ttf
AR_TTF := assets/fonts/AR.ttf
TH_TTF := assets/fonts/TH.ttf
HI_TTF := assets/fonts/HI.ttf
FONT_FALLBACKS := $(DROID_TTF) $(KR_TTF) $(AR_TTF) $(TH_TTF) $(HI_TTF)
FONT_SANS_H := $(BUILD)/font_sans.h
FONT_MONO_H := $(BUILD)/font_mono.h
FONT_MONOB_H := $(BUILD)/font_mono_bold.h
$(FONT_SANS_H): tools/font_to_header.py $(DEJAVU_TTF) $(FONT_FALLBACKS) $(CS_FILE)
	python3 tools/font_to_header.py $(DEJAVU_TTF) $@ "UI Sans" font_sans $(CS_FILE) $(FONT_FALLBACKS)
$(FONT_MONO_H): tools/font_to_header.py $(MONO_TTF) $(FONT_FALLBACKS) $(CS_FILE)
	python3 tools/font_to_header.py $(MONO_TTF) $@ "Mono" font_mono $(CS_FILE) $(FONT_FALLBACKS)
$(FONT_MONOB_H): tools/font_to_header.py $(MONOB_TTF) $(FONT_FALLBACKS) $(CS_FILE)
	python3 tools/font_to_header.py $(MONOB_TTF) $@ "Mono Bold" font_mono_bold $(CS_FILE) $(FONT_FALLBACKS)
$(BUILD)/gfxtext.o: $(FONT_SANS_H) $(FONT_MONO_H) $(FONT_MONOB_H)
$(BUILD)/i18n.o: $(I18N_H)
$(BUILD)/main.o: $(I18N_H) $(FONT_SANS_H) $(FONT_MONO_H) $(FONT_MONOB_H)
$(BUILD)/desktop.o: $(I18N_H) $(FONT_SANS_H) $(FONT_MONO_H) $(FONT_MONOB_H)

TTF_CFLAGS := $(filter-out -mno-mmx -mno-sse -mno-sse2,$(CFLAGS)) -msse2

$(BUILD)/ttf.o: kernel/ttf.c | $(BUILD)
	$(CC) $(TTF_CFLAGS) -w -c $< -o $@

# Default wallpaper: the East Lake photo is linked into the kernel image as
# a read-only binary blob (symbols _binary_assets_donghu_1600x900_bmp_*).
WALLPAPER_BMP := assets/donghu_1600x900.bmp
WALLPAPER_OBJ := $(BUILD)/donghu_bmp.o
$(WALLPAPER_OBJ): $(WALLPAPER_BMP) | $(BUILD)
	objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
	    --rename-section .data=.rodata,alloc,load,readonly,data,contents \
	    $< $@

$(BUILD)/%.o: kernel/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# Device drivers live under drivers/ so they are readable and greppable on
# their own. Each subdirectory is flat: one .c per driver, one .h per driver.
DRV_SRCS := drivers/usb/xhci.c drivers/compat/jb_bsd_compat.c \
            drivers/net/wlan/rtl8xxxu.c drivers/net/wlan/rt2800usb.c \
            drivers/net/wlan/ath9k_htc.c drivers/net/wlan/mt7601u.c
DRV_OBJS := $(patsubst drivers/%.c,$(BUILD)/drv_%.o,$(DRV_SRCS))

$(BUILD)/drv_%.o: drivers/%.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Rendering and desktop code compiled with optimisation. These are the files
# that run per frame (pixel loops, blending, text, 3D rasteriser), so -O2 buys
# the smoothness the whole UI is judged on. Hardware drivers deliberately stay
# at the default level: their busy-wait polling loops were written assuming
# unoptimised code and must not be reordered.
RENDER_OPT_SRCS := desktop fb cursor ui gfxtext ttf gpu3d app_3d apps \
                   app_files app_browser app_games wallpaper_image splash
$(foreach f,$(RENDER_OPT_SRCS),$(eval $(BUILD)/$(f).o: CFLAGS += -O2))

$(BUILD)/userproc.o: $(USER_H) $(CRASH_USER_H) $(PTHREAD_USER_H)

$(BUILD)/%.o: arch/x86_64/%.asm | $(BUILD)
	$(NASM) $(NASMFLAGS) $< -o $@

$(KERNEL): $(OBJS) $(DRV_OBJS) $(WALLPAPER_OBJ) arch/x86_64/linker.ld
	ld $(LDFLAGS) -o $@ $(OBJS) $(DRV_OBJS) $(WALLPAPER_OBJ)

iso: $(ISO)

disk: $(DISK)

BOOT_PCM := iso/boot/BOOT.PCM
WARN_PCM := iso/boot/WARN.PCM

$(BOOT_PCM) $(WARN_PCM): assets/sounds/boot.wav assets/sounds/warn.wav tools/wav_to_pcm.py
	mkdir -p iso/boot
	python3 tools/wav_to_pcm.py assets/sounds/boot.wav $(BOOT_PCM) 48000
	python3 tools/wav_to_pcm.py assets/sounds/warn.wav $(WARN_PCM) 48000

$(ISO): $(KERNEL) $(DISK) iso/boot/grub/grub.cfg iso/boot/grub/hd.cfg tools/prep_grub.sh $(BOOT_PCM) $(WARN_PCM)
	bash tools/prep_grub.sh
	cp $(DISK) iso/boot/JACKBOT.IMG
	grub-mkrescue -o $@ iso

DISK_FONTS := assets/fonts/SANS.ttf assets/fonts/MONO.ttf \
              assets/fonts/MONOB.ttf assets/fonts/CJK.ttf \
              assets/fonts/KR.ttf assets/fonts/AR.ttf \
              assets/fonts/TH.ttf assets/fonts/HI.ttf \
              $(wildcard assets/fonts/lang/*.ttf)

$(DISK): assets/logo.png $(DISK_FONTS) | $(BUILD)
	dd if=/dev/zero of=$@ bs=1M count=128
	mformat -i $@ -F :: >/dev/null 2>&1
	mcopy -i $@ assets/logo.png ::LOGO.PNG
	mcopy -i $@ assets/fonts/SANS.ttf  ::SANS.TTF
	mcopy -i $@ assets/fonts/MONO.ttf  ::MONO.TTF
	mcopy -i $@ assets/fonts/MONOB.ttf ::MONOB.TTF
	-mcopy -i $@ assets/fonts/CJK.ttf  ::CJK.TTF
	-mcopy -i $@ assets/fonts/KR.ttf   ::KR.TTF
	-mcopy -i $@ assets/fonts/AR.ttf   ::AR.TTF
	-mcopy -i $@ assets/fonts/TH.ttf   ::TH.TTF
	-mcopy -i $@ assets/fonts/HI.ttf   ::HI.TTF
	for d in SYSTEM SYSTEM/ETC SYSTEM/BIN SYSTEM/DRV SYSTEM/LIB SYSTEM/LICENSE; do \
	    mmd -i $@ ::$$d 2>/dev/null || true; done
	@for f in $$(find system -type f | sort); do \
	    rel=$${f#system/}; up=$$(printf 'SYSTEM/%s' "$$rel" | tr 'a-z' 'A-Z'); \
	    mcopy -o -i $@ "$$f" "::$$up" >/dev/null 2>&1 || echo "  pack skip $$f"; \
	done; echo "  system files on disk: $$(find system -type f | wc -l)"
	@n=0; for f in assets/fonts/lang/*.ttf; do b=$$(basename $$f); u=$$(echo $$b | tr 'a-z' 'A-Z'); mcopy -o -i $@ $$f ::$$u >/dev/null 2>&1 && n=$$((n+1)); done; echo "  lang fonts on disk: $$n"

clean:
	rm -f $(BUILD)/*.o $(BUILD)/*.d $(KERNEL) $(BUILD)/*.iso $(DISK)

