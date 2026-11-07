#ifndef JACKBOT_TCP_H
#define JACKBOT_TCP_H

#include <stdint.h>
#include <stddef.h>

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

struct ip_hdr;

struct tcp_hdr {
    uint16_t sport;
    uint16_t dport;
    uint32_t seq;
    uint32_t ack;
    uint8_t data_off;
    uint8_t flags;
    uint16_t window;
    uint16_t csum;
    uint16_t urg;
} __attribute__((packed));

int tcp_connect(uint32_t ip_hostorder, uint16_t port);
int tcp_connected(int s);
int tcp_send(int s, const void *data, uint16_t len);
int tcp_recv(int s, void *buf, uint16_t cap);
int tcp_peer_closed(int s);
int tcp_failed(int s);
int tcp_close(int s);
void tcp_poll(void);
void tcp_in(struct ip_hdr *ip, const uint8_t *payload, uint16_t len);

#endif
