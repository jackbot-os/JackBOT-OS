CC := gcc
NASM := nasm

CFLAGS := -ffreestanding -m64 -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
          -mcmodel=large -std=c11 -Wall -Wextra -g -Iinclude
NASMFLAGS := -f elf64
LDFLAGS := -n -T arch/x86_64/linker.ld

BUILD := build
KERN_SRCS := main idt pic timer serial trap apic pmm vmm heap sched userproc crash fb
ARCH_ASM := boot gdt vectors switch
OBJS := $(addprefix $(BUILD)/,$(addsuffix .o,$(KERN_SRCS))) \
        $(addprefix $(BUILD)/,$(addsuffix .o,$(ARCH_ASM)))
KERNEL := iso/boot/kernel
ISO := $(BUILD)/JackBOT-0.5.0.iso
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

$(BUILD)/%.o: kernel/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/userproc.o: $(USER_H) $(CRASH_USER_H)

$(BUILD)/%.o: arch/x86_64/%.asm | $(BUILD)
	$(NASM) $(NASMFLAGS) $< -o $@

$(KERNEL): $(OBJS) arch/x86_64/linker.ld
	ld $(LDFLAGS) -o $@ $(OBJS)

iso: $(ISO)

$(ISO): $(KERNEL) iso/boot/grub/grub.cfg
	grub-mkrescue -o $@ iso

clean:
	rm -f $(BUILD)/*.o $(KERNEL) $(BUILD)/*.iso
