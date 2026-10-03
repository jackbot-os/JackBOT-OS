CC := gcc
LD := ld
NASM := nasm

CFLAGS := -ffreestanding -m64 -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
          -mcmodel=large -std=c11 -Wall -Wextra -g -Iinclude
NASMFLAGS := -f elf64
LDFLAGS := -n -T arch/x86_64/linker.ld

BUILD := build
OBJS := $(BUILD)/boot.o $(BUILD)/main.o
KERNEL := iso/boot/kernel
ISO := $(BUILD)/JackBOT-0.5.0.iso

.PHONY: all iso clean

all: $(KERNEL)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/boot.o: arch/x86_64/boot.asm | $(BUILD)
	$(NASM) $(NASMFLAGS) $< -o $@

$(BUILD)/main.o: kernel/main.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL): $(OBJS) arch/x86_64/linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

iso: $(ISO)

$(ISO): $(KERNEL) iso/boot/grub/grub.cfg
	grub-mkrescue -o $@ iso

clean:
	rm -f $(BUILD)/*.o $(KERNEL) $(BUILD)/*.iso
