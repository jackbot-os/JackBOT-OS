.DEFAULT_GOAL := all

CC := gcc
NASM := nasm

CFLAGS := -ffreestanding -m64 -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
          -mcmodel=large -std=c11 -Wall -Wextra -g -Iinclude -Ibuild \
          -Ithird_party/stb -DNET_SELFTEST -MMD -MP
NASMFLAGS := -f elf64
LDFLAGS := -n -T arch/x86_64/linker.ld

BUILD := build
KERN_SRCS := main idt pic timer serial trap apic pmm vmm heap kstring sched userproc crash pci ac97 dev compat nic nic_rtl8139 fb svga cursor ata fat32 part mkfat payload grub_install installer installer_gui protect persist config oobe jbot i18n gfxtext ttf py jca ps2 desktop ui apps app_power app_store app_firewall app_backup app_capture app_clock app_fontmgr app_region app_network app_audio e1000 net tcp http sha256 app_update app_browser gpu3d app_3d app_diskmgr app_about app_calc app_notepad app_monitor app_pyconsole app_jca usb acpi recycle app_usb app_battery app_recycle app_files app_devices selftest splash hal bluetooth voice app_voice app_bluetooth hmac_sha256 tls ahci honeypot
ARCH_ASM := boot gdt vectors switch
OBJS := $(addprefix $(BUILD)/,$(addsuffix .o,$(KERN_SRCS))) \
        $(addprefix $(BUILD)/,$(addsuffix .o,$(ARCH_ASM)))
DEPS := $(OBJS:.o=.d)
-include $(DEPS)
KERNEL := iso/boot/kernel
ISO := $(BUILD)/JackBOT-1.0.2.iso
DISK := $(BUILD)/disk.img
USER_BIN := $(BUILD)/ustest.bin
USER_H := $(BUILD)/ustest.h

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

$(BUILD)/%.o: kernel/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/userproc.o: $(USER_H) $(CRASH_USER_H)

$(BUILD)/%.o: arch/x86_64/%.asm | $(BUILD)
	$(NASM) $(NASMFLAGS) $< -o $@

$(KERNEL): $(OBJS) arch/x86_64/linker.ld
	ld $(LDFLAGS) -o $@ $(OBJS)

iso: $(ISO)

disk: $(DISK)

BOOT_PCM := iso/boot/BOOT.PCM
WARN_PCM := iso/boot/WARN.PCM

$(BOOT_PCM) $(WARN_PCM): assets/sounds/boot.wav assets/sounds/warn.wav tools/wav_to_pcm.py
	mkdir -p iso/boot
	python3 tools/wav_to_pcm.py assets/sounds/boot.wav $(BOOT_PCM) 48000
	python3 tools/wav_to_pcm.py assets/sounds/warn.wav $(WARN_PCM) 48000

$(ISO): $(KERNEL) iso/boot/grub/grub.cfg iso/boot/grub/hd.cfg tools/prep_grub.sh $(BOOT_PCM) $(WARN_PCM)
	bash tools/prep_grub.sh
	grub-mkrescue -o $@ iso

DISK_FONTS := assets/fonts/SANS.ttf assets/fonts/MONO.ttf \
              assets/fonts/MONOB.ttf assets/fonts/CJK.ttf \
              assets/fonts/KR.ttf assets/fonts/AR.ttf \
              assets/fonts/TH.ttf assets/fonts/HI.ttf

$(DISK): assets/logo.png $(DISK_FONTS) | $(BUILD)
	dd if=/dev/zero of=$@ bs=1M count=64
	mkfs.fat -F32 $@ >/dev/null
	mcopy -i $@ assets/logo.png ::LOGO.PNG
	mcopy -i $@ assets/fonts/SANS.ttf  ::SANS.TTF
	mcopy -i $@ assets/fonts/MONO.ttf  ::MONO.TTF
	mcopy -i $@ assets/fonts/MONOB.ttf ::MONOB.TTF
	-mcopy -i $@ assets/fonts/CJK.ttf  ::CJK.TTF
	-mcopy -i $@ assets/fonts/KR.ttf   ::KR.TTF
	-mcopy -i $@ assets/fonts/AR.ttf   ::AR.TTF
	-mcopy -i $@ assets/fonts/TH.ttf   ::TH.TTF
	-mcopy -i $@ assets/fonts/HI.ttf   ::HI.TTF

clean:
	rm -f $(BUILD)/*.o $(BUILD)/*.d $(KERNEL) $(BUILD)/*.iso $(DISK)
