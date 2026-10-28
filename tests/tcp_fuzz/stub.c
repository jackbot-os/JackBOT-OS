/* Offline stub for JackBOT OS tcp.c harness. No real network. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <kernel/net.h>

static uint8_t local_ip[4] = {10, 0, 2, 15};
static uint64_t tick = 0;
int stub_sent = 0;

int net_ready(void) { return 1; }

const uint8_t *net_local_ip(void) { return local_ip; }

uint32_t net_ipv4(const uint8_t p[4])
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

void net_fmt_ip(uint32_t hostorder, uint8_t out[4])
{
    out[0] = (uint8_t)(hostorder >> 24);
    out[1] = (uint8_t)(hostorder >> 16);
    out[2] = (uint8_t)(hostorder >> 8);
    out[3] = (uint8_t)hostorder;
}

uint16_t net_pseudo_checksum(uint32_t src, uint32_t dst, uint8_t proto,
                             const void *data, uint16_t len)
{
    (void)src; (void)dst; (void)proto; (void)data; (void)len;
    return 0;
}

int net_send_ip(uint8_t proto, uint32_t dst_host, const void *data, uint16_t len)
{
    (void)proto; (void)dst_host; (void)data; (void)len;
    stub_sent++;
    return 0;
}

uint64_t timer_get_ticks(void) { return ++tick; }

uint16_t htons16(uint16_t x)
{
    return (uint16_t)((x >> 8) | (x << 8));
}

uint32_t htons32(uint32_t x)
{
    return ((x >> 24) & 0xFF) | ((x >> 8) & 0xFF00) |
           ((x << 8) & 0xFF0000) | ((x << 24) & 0xFF000000);
}

/* serial no-ops (declared in serial.h, unused by tcp.c) */
void serial_init(void) {}
void serial_putc(char c) { (void)c; }
void serial_puts(const char *s) { (void)s; }
void serial_puthex(uint64_t v) { (void)v; }
