#include <kernel/serial.h>

#define PORT 0x3f8

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

void serial_init(void)
{
    outb(PORT + 1, 0x00);
    outb(PORT + 3, 0x80);
    outb(PORT + 0, 0x03);
    outb(PORT + 1, 0x00);
    outb(PORT + 3, 0x03);
    outb(PORT + 2, 0xc7);
    outb(PORT + 4, 0x0b);
}

void serial_putc(char c)
{
    outb(PORT, (uint8_t)c);
}

void serial_puts(const char *s)
{
    while (*s)
        serial_putc(*s++);
}

void serial_puthex(uint64_t v)
{
    static const char digits[] = "0123456789abcdef";
    serial_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t nibble = (v >> i) & 0xf;
        serial_putc(digits[nibble]);
    }
}
