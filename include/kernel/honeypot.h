#ifndef JACKBOT_HONEYPOT_H
#define JACKBOT_HONEYPOT_H
#include <stdint.h>
#include <kernel/net.h>

#define HONEYPOT_MAX_ENTRIES 32

/*
 * Local passive honeypot (observation-only).
 *
 * Default OFF. When enabled it only records inbound packets that no local
 * service claimed (unclaimed UDP, TCP SYN probes to closed ports, ICMP echo
 * requests). It never opens a listening socket, never answers a packet, never
 * initiates a connection and never sends anything off-box. The network stack
 * on this system is client-only; this module cannot emulate a real service
 * banner or bind a server port.
 */

enum {
    HP_ACT_UNCLAIMED_UDP = 0,
    HP_ACT_TCP_SYN       = 1,
    HP_ACT_ICMP_ECHO     = 2
};

struct honeypot_entry {
    uint32_t src_ip;     /* host order IPv4 */
    uint16_t sport;
    uint16_t dport;
    uint8_t  proto;      /* 'U' / 'T' / 'I' */
    uint8_t  action;
    uint32_t hits;
    uint32_t first_tick;
    uint32_t last_tick;
    int      used;
};

void honeypot_init(void);
int  honeypot_enabled(void);
void honeypot_set(int on);

void honeypot_observe_udp(uint32_t src, uint16_t sport, uint16_t dport, int claimed);
void honeypot_observe_tcp(const struct ip_hdr *ip, const uint8_t *payload, uint16_t len);
void honeypot_observe_icmp(uint32_t src, uint8_t type);

int  honeypot_entry_count(void);
const struct honeypot_entry *honeypot_entries(void);
void honeypot_clear(void);
void honeypot_self_test(void);

#endif
