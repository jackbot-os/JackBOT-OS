/* Offline stub for the JackBOT OS Python subset harness. Local VM only.
   Real kernel/py.c compiled unchanged; libc stands in for heap, stdout for serial. */
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

void *kmalloc(size_t n) { return malloc(n ? n : 1); }
void kfree(void *p) { free(p); }

void serial_init(void) {}
void serial_putc(char c) { putchar(c); }
void serial_puts(const char *s) { fputs(s, stdout); }
void serial_puthex(uint64_t v)
{
    char b[17];
    for (int i = 15; i >= 0; i--) {
        uint8_t n = (uint8_t)(v >> (i * 4));
        b[15 - i] = (n < 10) ? (char)('0' + n) : (char)('a' + n - 10);
    }
    fwrite(b, 1, 16, stdout);
}
