#ifndef JACKBOT_NET_H
#define JACKBOT_NET_H

#include <stdint.h>
#include <stddef.h>

#define ETH_P_ARP 0x0806
#define ETH_P_IP  0x0800

#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP  6
#define IP_PROTO_UDP  17

struct eth_hdr {
    uint8_t dst[6];
    uint8_t src[6];
    uint16_t ethertype;
} __attribute__((packed));

struct arp_pkt {
    uint16_t htype;
    uint16_t ptype;
    uint8_t hlen;
    uint8_t plen;
    uint16_t op;
    uint8_t sha[6];
    uint8_t spa[4];
    uint8_t tha[6];
    uint8_t tpa[4];
} __attribute__((packed));

struct ip_hdr {
    uint8_t ihl_ver;
    uint8_t tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t frag;
    uint8_t ttl;
    uint8_t proto;
    uint16_t csum;
    uint8_t src[4];
    uint8_t dst[4];
} __attribute__((packed));

struct icmp_hdr {
    uint8_t type;
    uint8_t code;
    uint16_t csum;
    uint32_t rest;
} __attribute__((packed));

struct udp_hdr {
    uint16_t sport;
    uint16_t dport;
    uint16_t len;
    uint16_t csum;
} __attribute__((packed));

void net_init(void);
void net_poll(void);
int net_ready(void);

int net_send_ip(uint8_t proto, uint32_t dst_host,
                const void *data, uint16_t len);
int net_send_udp(uint32_t dst_host, uint16_t dport, uint16_t sport,
                 const void *data, uint16_t len);

typedef void (*udp_handler)(uint32_t src_ip, uint16_t sport,
                            const uint8_t *data, uint16_t len);
void udp_register(uint16_t local_port, udp_handler h);

uint16_t net_checksum(const void *data, size_t len);
uint16_t net_pseudo_checksum(uint32_t src, uint32_t dst, uint8_t proto,
                             const void *data, uint16_t len);

const uint8_t *net_local_ip(void);
const uint8_t *net_gateway(void);
const uint8_t *net_dns(void);
const uint8_t *net_netmask(void);
const uint8_t *net_mac(void);

uint32_t net_ipv4(const uint8_t p[4]);
void net_fmt_ip(uint32_t hostorder, uint8_t out[4]);
void net_ip_str(const uint8_t ip[4], char out[16]);
int net_same_subnet(const uint8_t a[4], const uint8_t b[4]);

uint16_t htons16(uint16_t x);
uint32_t htons32(uint32_t x);
#define ntohs16 htons16
#define ntohs32 htons32

#endif
