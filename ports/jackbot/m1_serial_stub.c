#include <stdint.h>

static volatile uint8_t *const COM1 = (volatile uint8_t *)0x3F8u;

static void com_wait(void)
{
    while ((COM1[5] & 0x20u) == 0u) { }
}

void serial_putc(char c)
{
    if (c == '\n') {
        com_wait();
        COM1[0] = (uint8_t)'\r';
    }
    com_wait();
    COM1[0] = (uint8_t)c;
}

void serial_puts(const char *s)
{
    while (*s)
        serial_putc(*s++);
}
