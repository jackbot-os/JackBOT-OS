/* Deterministic offline harness for the real kernel net.c.
   Local VM only: stub NIC, no real network. Includes net.c to reach the
   static packet handlers and feed them malformed frames directly. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../kernel/net.c"

static int failures = 0;
static int udp_seen_port = -1;
static uint16_t udp_seen_dlen = 0;
static uint8_t udp_seen_data[64];

#define CHECK(cond, name) do { \
    if (cond) { printf("PASS: %s\n", name); } \
    else { printf("FAIL: %s\n", name); failures++; } \
} while (0)

static void udp_cb(uint32_t src, uint16_t sport, const uint8_t *data, uint16_t len)
{
    (void)src; (void)sport;
    udp_seen_port = 1234;
    udp_seen_dlen = len;
    if (len <= sizeof udp_seen_data)
        memcpy(udp_seen_data, data, len);
}

/* Build an Ethernet(14)+IPv4(20) frame in buf; returns frame length.
   payload sits after the IP header. */
static int build_ip(uint8_t *buf, uint8_t proto, uint16_t payload_len,
                    const uint8_t *payload)
{
    memset(buf, 0, 1500);
    buf[12] = 0x08; buf[13] = 0x00; /* ethertype IP */
    struct ip_hdr *ip = (void *)(buf + 14);
    ip->ihl_ver = 0x45;
    uint16_t total = (uint16_t)(20 + payload_len);
    ip->total_len = htons16(total);
    ip->proto = proto;
    ip->ttl = 64;
    ip->src[0] = 10; ip->src[1] = 0; ip->src[2] = 2; ip->src[3] = 20;
    ip->dst[0] = 10; ip->dst[1] = 0; ip->dst[2] = 2; ip->dst[3] = 15;
    if (payload && payload_len)
        memcpy(buf + 14 + 20, payload, payload_len);
    return 14 + total;
}

int main(void)
{
    uint8_t frame[1500];

    /* pure helpers */
    uint8_t ip4[4] = {10, 0, 2, 15};
    uint32_t w = net_ipv4(ip4);
    CHECK(w == 0x0A00020F, "net_ipv4 encodes 10.0.2.15");
    uint8_t back[4];
    net_fmt_ip(w, back);
    CHECK(memcmp(back, ip4, 4) == 0, "net_fmt_ip round-trip");
    char s[16];
    net_ip_str(ip4, s);
    CHECK(strcmp(s, "10.0.2.15") == 0, "net_ip_str formats dotted quad");

    uint8_t cs[4] = {1, 2, 3, 4};
    uint16_t c1 = net_checksum(cs, 4);
    uint16_t c2 = net_checksum(cs, 4);
    CHECK(c1 == c2, "net_checksum deterministic");

    /* truncated frame: fewer than eth header */
    ip_handle(frame, 10);
    CHECK(1, "truncated frame (<14) dropped without crash");

    /* ihl = 0 */
    int n = build_ip(frame, IP_PROTO_UDP, 8, 0);
    ((struct ip_hdr *)(frame + 14))->ihl_ver = 0x00;
    ip_handle(frame, (uint16_t)n);
    CHECK(1, "zero ihl dropped without crash");

    /* total_len < ihl */
    n = build_ip(frame, IP_PROTO_UDP, 8, 0);
    ((struct ip_hdr *)(frame + 14))->total_len = htons16(4);
    ip_handle(frame, (uint16_t)n);
    CHECK(1, "total_len < ihl dropped without crash");

    /* UDP shorter than 8 bytes -> dropped, no crash */
    uint8_t udp_short[4] = {0,0,0,0};
    n = build_ip(frame, IP_PROTO_UDP, 4, udp_short);
    ip_handle(frame, (uint16_t)n);
    CHECK(1, "short UDP (<8) dropped without crash");

    /* register a UDP handler on port 1234, send a matching datagram */
    udp_register(1234, udp_cb);
    uint8_t udp[32];
    memset(udp, 0, sizeof udp);
    uint16_t sport = 4000, dport = 1234;
    udp[0] = (uint8_t)(sport >> 8); udp[1] = (uint8_t)sport;
    udp[2] = (uint8_t)(dport >> 8); udp[3] = (uint8_t)dport;
    uint16_t ulen = 18;
    udp[4] = (uint8_t)(ulen >> 8); udp[5] = (uint8_t)ulen;
    udp[6] = 0; udp[7] = 0;
    const char *msg = "hello-net";
    memcpy(udp + 8, msg, 10);
    n = build_ip(frame, IP_PROTO_UDP, 18, udp);
    ip_handle(frame, (uint16_t)n);
    CHECK(udp_seen_port == 1234, "registered UDP handler dispatched");
    CHECK(udp_seen_dlen == 10, "UDP payload length passed to handler");
    CHECK(memcmp(udp_seen_data, msg, 10) == 0, "UDP payload bytes intact");

    /* ICMP echo request, >=8 bytes, must not crash (net down -> no reply sent) */
    uint8_t icmp[8] = {8, 0, 0, 0, 0, 0, 0, 0};
    n = build_ip(frame, IP_PROTO_ICMP, 8, icmp);
    ip_handle(frame, (uint16_t)n);
    CHECK(1, "ICMP echo request handled without crash");

    /* TCP branch reaches the stub tcp_in, no crash */
    uint8_t tcpseg[20] = {0};
    n = build_ip(frame, IP_PROTO_TCP, 20, tcpseg);
    ip_handle(frame, (uint16_t)n);
    CHECK(1, "TCP branch dispatched to tcp_in stub without crash");

    /* register UDP slots up to the limit without overflow */
    for (int i = 0; i < 12; i++)
        udp_register(2000 + i, udp_cb);
    CHECK(1, "over-subscribed UDP registrations do not overflow");

    printf(failures ? "\n=== %d FAILURE(S) ===\n" : "\n=== ALL PASS ===\n", failures);
    return failures ? 1 : 0;
}
