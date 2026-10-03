#include <stdint.h>

#define IDT_ENTRIES 256
#define KERNEL_CS 0x08
#define GATE_TRAP 0x8f
#define GATE_INT  0x8e

struct idt_entry {
    uint16_t off_lo;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  flags;
    uint16_t off_mid;
    uint32_t off_hi;
    uint32_t zero;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

extern uint64_t isr_stub_table[49];

static struct idt_entry idt[IDT_ENTRIES];

static void idt_set(uint8_t n, uint64_t handler, uint8_t flags)
{
    idt[n].off_lo = handler & 0xffff;
    idt[n].selector = KERNEL_CS;
    idt[n].ist = 0;
    idt[n].flags = flags;
    idt[n].off_mid = (handler >> 16) & 0xffff;
    idt[n].off_hi = (handler >> 32) & 0xffffffff;
    idt[n].zero = 0;
}

void idt_init(void)
{
    for (int i = 0; i < 48; i++)
        idt_set((uint8_t)i, isr_stub_table[i], GATE_INT);
    idt_set(255, isr_stub_table[48], GATE_INT);

    struct idt_ptr ptr = {
        .limit = sizeof(idt) - 1,
        .base = (uint64_t)&idt,
    };
    __asm__ volatile("lidt %0" : : "m"(ptr));
}
