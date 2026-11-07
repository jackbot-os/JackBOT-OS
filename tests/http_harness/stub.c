/* Offline stub for the real kernel http.c harness. No TCP socket, no DNS, no net. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <kernel/net.h>
#include <kernel/tcp.h>

static uint8_t dns_ip[4] = {10, 0, 2, 3};
static uint64_t tick = 0;

int net_ready(void) { return 0; }
const uint8_t *net_dns(void) { return dns_ip; }
int net_send_udp(uint32_t d, uint16_t dp, uint16_t sp, const void *d2, uint16_t l)
{ (void)d; (void)dp; (void)sp; (void)d2; (void)l; return 0; }

uint32_t net_ipv4(const uint8_t p[4])
{ return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }

uint16_t htons16(uint16_t x) { return (uint16_t)((x >> 8) | (x << 8)); }
uint32_t htons32(uint32_t x)
{ return ((x & 0xFF) << 24) | ((x & 0xFF00) << 8) |
         ((x >> 8) & 0xFF00) | (x >> 24); }

int tcp_connect(uint32_t ip, uint16_t port) { (void)ip; (void)port; return 0; }
int tcp_connected(int s) { (void)s; return 0; }
int tcp_send(int s, const void *d, uint16_t l) { (void)s; (void)d; (void)l; return 0; }
int tcp_recv(int s, void *b, uint16_t c) { (void)s; (void)b; (void)c; return -1; }
int tcp_peer_closed(int s) { (void)s; return 0; }
int tcp_failed(int s) { (void)s; return 0; }
int tcp_close(int s) { (void)s; return 0; }
void tcp_poll(void) {}
void tcp_in(struct ip_hdr *ip, const uint8_t *p, uint16_t l) { (void)ip; (void)p; (void)l; }

void udp_register(uint16_t port, void (*h)(uint32_t,uint16_t,const uint8_t*,uint16_t))
{ (void)port; (void)h; }

int tls_engine_ready(void) { return 0; }

uint64_t timer_get_ticks(void) { return ++tick; }

void serial_init(void) {}
void serial_putc(char c) { (void)c; }
void serial_puts(const char *s) { (void)s; }
void serial_puthex(uint64_t v) { (void)v; }
