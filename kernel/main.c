#include <stdint.h>
#include <stddef.h>

#define VGA_MEM ((volatile uint16_t *)0xb8000)
#define VGA_W 80
#define VGA_H 25
#define VGA_ATTR 0x1f

#define SERIAL_PORT 0x3f8

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void serial_init(void)
{
    outb(SERIAL_PORT + 1, 0x00);
    outb(SERIAL_PORT + 3, 0x80);
    outb(SERIAL_PORT + 0, 0x03);
    outb(SERIAL_PORT + 1, 0x00);
    outb(SERIAL_PORT + 3, 0x03);
    outb(SERIAL_PORT + 2, 0xc7);
    outb(SERIAL_PORT + 4, 0x0b);
}

static void serial_puts(const char *s)
{
    while (*s)
        outb(SERIAL_PORT, (uint8_t)*s++);
}

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
    (void)mb_info;

    serial_init();
    vga_clear();

    vga_puts("JackBOT OS 0.5.0\n");
    vga_puts("x86_64 long mode kernel alive\n");
    if (magic == 0x36d76289)
        vga_puts("multiboot2 handoff OK\n");
    else
        vga_puts("warning: unexpected boot magic\n");

    serial_puts("JackBOT OS 0.5.0 - serial console alive\n");
    serial_puts("x86_64 long mode kernel alive\n");

    for (;;)
        __asm__ volatile("hlt");
}
