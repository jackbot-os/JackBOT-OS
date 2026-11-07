/* Offline stub for the real kernel net.c harness. No real NIC, no network. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <kernel/net.h>

static uint8_t mac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
static uint64_t tick = 0;
int stub_sent = 0;

int nic_init_all(void) { return 0; }
int nic_active(void) { return 1; }
int nic_send(const void *d, uint16_t n) { (void)d; (void)n; stub_sent++; return 0; }
int nic_recv(void *b, uint16_t c) { (void)b; (void)c; return 0; }
void nic_get_mac(uint8_t out[6]) { memcpy(out, mac, 6); }
const char *nic_active_name(void) { return "stub-nic"; }

void tcp_in(struct ip_hdr *ip, const uint8_t *payload, uint16_t len)
{ (void)ip; (void)payload; (void)len; }

void honeypot_observe_udp(uint32_t s, uint16_t sp, uint16_t dp, int claimed)
{ (void)s; (void)sp; (void)dp; (void)claimed; }
void honeypot_observe_tcp(const struct ip_hdr *ip, const uint8_t *p, uint16_t len)
{ (void)ip; (void)p; (void)len; }
void honeypot_observe_icmp(uint32_t s, uint8_t type) { (void)s; (void)type; }

uint64_t timer_get_ticks(void) { return ++tick; }

void serial_init(void) {}
void serial_putc(char c) { (void)c; }
void serial_puts(const char *s) { (void)s; }
void serial_puthex(uint64_t v) { (void)v; }
