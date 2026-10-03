#include <stdint.h>
#include <stddef.h>
#include <kernel/serial.h>
#include <kernel/apic.h>

#define VGA_MEM ((volatile uint16_t *)0xb8000)
#define VGA_W 80
#define VGA_H 25
#define VGA_ATTR 0x1f
#define TIMER_PROOF_TICKS 500

void gdt_init(void);
void pic_remap(void);
void pic_mask_all(void);
void pic_clear_mask(uint8_t irq);
void idt_init(void);
void timer_init(void);
uint64_t timer_get_ticks(void);

static void vga_clear(void)
{
    for (size_t i = 0; i < VGA_W * VGA_H; i++)
        VGA_MEM[i] = (uint16_t)' ' | ((uint16_t)VGA_ATTR << 8);
}

static void vga_puts(const char *s)
{
    static size_t row = 0;
    static size_t col = 0;

    while (*s) {
        char c = *s++;
        if (c == '\n') {
            col = 0;
            row++;
            continue;
        }
        if (col >= VGA_W) {
            col = 0;
            row++;
        }
        if (row >= VGA_H)
            break;
        VGA_MEM[row * VGA_W + col] =
            (uint16_t)(uint8_t)c | ((uint16_t)VGA_ATTR << 8);
        col++;
    }
}

void kmain(uint64_t magic, uint64_t mb_info)
{
    (void)magic;
    (void)mb_info;

    serial_init();
    vga_clear();

    vga_puts("JackBOT OS 0.5.0\n");
    serial_puts("JackBOT OS 0.5.0\n");

    gdt_init();
    pic_remap();
    idt_init();
    timer_init();
    pic_mask_all();
    pic_clear_mask(0);
    apic_init();
    __asm__ volatile("sti");

    serial_puts("GDT/IDT/PIT initialized\n");

    int timer_reported = 0;
    uint64_t last_dot = 0;
    for (;;) {
        __asm__ volatile("hlt");
        uint64_t t = timer_get_ticks();
        if (t / 100 > last_dot) {
            serial_putc('.');
            last_dot = t / 100;
        }
        if (!timer_reported && t >= TIMER_PROOF_TICKS) {
            serial_puts("\ntimer IRQ alive at tick ");
            serial_puthex(t);
            serial_putc('\n');
            timer_reported = 1;
        }
    }
}
