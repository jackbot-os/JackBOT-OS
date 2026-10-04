CC := gcc
NASM := nasm

CFLAGS := -ffreestanding -m64 -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
          -mcmodel=large -std=c11 -Wall -Wextra -g -Iinclude -Ibuild
NASMFLAGS := -f elf64
LDFLAGS := -n -T arch/x86_64/linker.ld

BUILD := build
KERN_SRCS := main idt pic timer serial trap apic pmm vmm heap sched userproc crash fb ata fat32 installer protect persist i18n gfxtext ps2 desktop
ARCH_ASM := boot gdt vectors switch
OBJS := $(addprefix $(BUILD)/,$(addsuffix .o,$(KERN_SRCS))) \
        $(addprefix $(BUILD)/,$(addsuffix .o,$(ARCH_ASM)))
KERNEL := iso/boot/kernel
ISO := $(BUILD)/JackBOT-0.5.0.iso
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
	xxd -i -n ustest_bin $(USER_BIN) $@

$(CRASH_USER_H): user/ucrash.asm | $(BUILD)
	$(NASM) -f bin $< -o $(CRASH_USER_BIN)
	xxd -i -n ucrash_bin $(CRASH_USER_BIN) $@

I18N_JSON := tools/i18n_data.json
I18N_H := $(BUILD)/i18n_data.h
CS_FILE := $(BUILD)/extra_charset.txt
$(I18N_H) $(CS_FILE): $(I18N_JSON) tools/gen_i18n.py | $(BUILD)
	python3 tools/gen_i18n.py $(I18N_JSON) $(I18N_H) $(CS_FILE)

DROID_TTF := /usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf
MONO_TTF := /usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf
MONOB_TTF := /usr/share/fonts/truetype/liberation/LiberationMono-Bold.ttf
FONT_SANS_H := $(BUILD)/font_sans.h
FONT_MONO_H := $(BUILD)/font_mono.h
FONT_MONOB_H := $(BUILD)/font_mono_bold.h
$(FONT_SANS_H): tools/font_to_header.py $(DROID_TTF) $(CS_FILE)
	python3 tools/font_to_header.py $(DROID_TTF) $@ "UI Sans" font_sans $(CS_FILE)
$(FONT_MONO_H): tools/font_to_header.py $(MONO_TTF) $(DROID_TTF) $(CS_FILE)
	python3 tools/font_to_header.py $(MONO_TTF) $@ "Mono" font_mono $(CS_FILE) $(DROID_TTF)
$(FONT_MONOB_H): tools/font_to_header.py $(MONOB_TTF) $(DROID_TTF) $(CS_FILE)
	python3 tools/font_to_header.py $(MONOB_TTF) $@ "Mono Bold" font_mono_bold $(CS_FILE) $(DROID_TTF)
$(BUILD)/gfxtext.o: $(FONT_SANS_H) $(FONT_MONO_H) $(FONT_MONOB_H)
$(BUILD)/i18n.o: $(I18N_H)
$(BUILD)/main.o: $(I18N_H) $(FONT_SANS_H) $(FONT_MONO_H) $(FONT_MONOB_H)
$(BUILD)/desktop.o: $(I18N_H) $(FONT_SANS_H) $(FONT_MONO_H) $(FONT_MONOB_H)

$(BUILD)/%.o: kernel/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/userproc.o: $(USER_H) $(CRASH_USER_H)

$(BUILD)/%.o: arch/x86_64/%.asm | $(BUILD)
	$(NASM) $(NASMFLAGS) $< -o $@

$(KERNEL): $(OBJS) arch/x86_64/linker.ld
	ld $(LDFLAGS) -o $@ $(OBJS)

iso: $(ISO) $(DISK)

$(ISO): $(KERNEL) iso/boot/grub/grub.cfg
	grub-mkrescue -o $@ iso

$(DISK): | $(BUILD)
	dd if=/dev/zero of=$@ bs=1M count=64
	mkfs.fat -F32 $@ >/dev/null

clean:
	rm -f $(BUILD)/*.o $(KERNEL) $(BUILD)/*.iso $(DISK)
