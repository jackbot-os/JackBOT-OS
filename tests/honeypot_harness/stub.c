/* Offline stub for the honeypot module harness. Local VM only.
   Real kernel/honeypot.c compiled unchanged; libc/stdout stand in for the
   kernel config, timer, serial and net helpers. */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

static uint64_t g_ticks;
static int g_hp_cfg;

uint64_t timer_get_ticks(void) { return g_ticks++; }

int config_honeypot(void) { return g_hp_cfg; }
void config_set_honeypot(int v) { g_hp_cfg = v ? 1 : 0; }
void config_save(void) {}

uint32_t net_ipv4(const uint8_t p[4])
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

uint16_t htons16(uint16_t x)
{
    return (uint16_t)((x >> 8) | (x << 8));
}

void net_fmt_ip(uint32_t host, uint8_t out[4])
{
    out[0] = (uint8_t)(host >> 24);
    out[1] = (uint8_t)(host >> 16);
    out[2] = (uint8_t)(host >> 8);
    out[3] = (uint8_t)host;
}

void serial_putc(char c) { putchar(c); }
void serial_puts(const char *s) { fputs(s, stdout); }
