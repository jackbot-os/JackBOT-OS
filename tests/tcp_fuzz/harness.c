/* Offline malformed-segment harness for the real kernel tcp.c.
 * Local VM only: no socket, no network, no external target. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <kernel/net.h>
#include <kernel/tcp.h>

static uint8_t peer[4] = {10, 0, 2, 20};
static uint32_t peer_ip;
static int failures = 0;

#define CHECK(cond, name) do { \
    if (cond) { printf("PASS: %s\n", name); } \
    else { printf("FAIL: %s\n", name); failures++; } \
} while (0)

/* fill a TCP segment; returns total segment length */
static int build_seg(uint8_t *buf, uint16_t sport, uint16_t dport,
                     uint32_t seq, uint32_t ack, uint8_t doff_words,
                     uint8_t flags, const uint8_t *data, uint16_t dlen)
{
    memset(buf, 0, 1500);
    uint16_t off = (uint16_t)(doff_words * 4);
    buf[0] = (uint8_t)(sport >> 8); buf[1] = (uint8_t)sport;
    buf[2] = (uint8_t)(dport >> 8); buf[3] = (uint8_t)dport;
    buf[4] = (uint8_t)(seq >> 24); buf[5] = (uint8_t)(seq >> 16);
    buf[6] = (uint8_t)(seq >> 8);  buf[7] = (uint8_t)seq;
    buf[8] = (uint8_t)(ack >> 24); buf[9] = (uint8_t)(ack >> 16);
    buf[10] = (uint8_t)(ack >> 8); buf[11] = (uint8_t)ack;
    buf[12] = (uint8_t)(doff_words << 4);
    buf[13] = flags;
    if (data && dlen) memcpy(buf + off, data, dlen);
    return off + dlen;
}

static void feed(struct ip_hdr *ip, const uint8_t *seg, uint16_t len)
{
    tcp_in(ip, seg, len);
}

int main(void)
{
    peer_ip = net_ipv4(peer);

    struct ip_hdr ip;
    memset(&ip, 0, sizeof ip);
    memcpy(ip.src, peer, 4);

    uint8_t seg[1500];

    /* Case 1: connect creates a socket in SYN_SENT */
    int s = tcp_connect(peer_ip, 80);
    CHECK(s >= 0, "connect allocates socket");
    CHECK(!tcp_connected(s), "socket not connected yet (SYN_SENT)");

    /* Case 2: truncated segment (len < 20) must be dropped, no crash */
    int n = build_seg(seg, 80, 4096, 1000, 0, 5, TCP_SYN | TCP_ACK, 0, 0);
    feed(&ip, seg, 10);
    CHECK(!tcp_connected(s), "truncated segment dropped");

    /* Case 3: oversized segment (len > 1500) dropped */
    feed(&ip, seg, 1600);
    CHECK(!tcp_connected(s), "oversized segment dropped");

    /* Case 4: doff beyond segment length dropped */
    n = build_seg(seg, 80, 4096, 1000, 0, 15, TCP_SYN | TCP_ACK, 0, 0);
    feed(&ip, seg, 20);
    CHECK(!tcp_connected(s), "doff > len dropped");

    /* Case 5: zero data offset dropped */
    n = build_seg(seg, 80, 4096, 1000, 0, 0, TCP_SYN | TCP_ACK, 0, 0);
    feed(&ip, seg, 20);
    CHECK(!tcp_connected(s), "zero doff dropped");

    /* Case 6: SYN without ACK must not complete handshake */
    n = build_seg(seg, 80, 4096, 1000, 0, 5, TCP_SYN, 0, 0);
    feed(&ip, seg, n);
    CHECK(!tcp_connected(s), "bare SYN rejected (need SYN|ACK)");

    /* Case 7: legitimate SYN|ACK completes handshake */
    n = build_seg(seg, 80, 4096, 1000, 0, 5, TCP_SYN | TCP_ACK, 0, 0);
    feed(&ip, seg, n);
    CHECK(tcp_connected(s), "SYN|ACK completes handshake");

    /* Case 8: RST after established -> failed */
    n = build_seg(seg, 80, 4096, 1001, 0, 5, TCP_RST, 0, 0);
    feed(&ip, seg, n);
    CHECK(tcp_failed(s), "RST marks socket failed");
    tcp_close(s);

    /* Case 9: new connection; out-of-order data must not be buffered */
    s = tcp_connect(peer_ip, 80);
    n = build_seg(seg, 80, 4097, 2000, 0, 5, TCP_SYN | TCP_ACK, 0, 0);
    feed(&ip, seg, n);
    CHECK(tcp_connected(s), "second connection handshake");
    uint8_t payload[32];
    memset(payload, 'A', sizeof payload);
    /* seq far ahead -> out of order, ignored */
    n = build_seg(seg, 80, 4097, 9999, 0, 5, TCP_PSH | TCP_ACK, payload, 32);
    feed(&ip, seg, n);
    uint8_t out[32];
    int got = tcp_recv(s, out, sizeof out);
    CHECK(got == 0, "out-of-order segment not buffered");

    /* Case 10: in-order data delivered */
    n = build_seg(seg, 80, 4097, 2001, 0, 5, TCP_PSH | TCP_ACK, payload, 32);
    feed(&ip, seg, n);
    got = tcp_recv(s, out, sizeof out);
    CHECK(got == 32, "in-order data delivered");

    /* Case 11: FIN with wrong sequence does not close peer */
    n = build_seg(seg, 80, 4097, 9999, 0, 5, TCP_FIN | TCP_ACK, 0, 0);
    feed(&ip, seg, n);
    CHECK(!tcp_peer_closed(s), "wrong-sequence FIN ignored");

    /* Case 12: FIN with correct sequence closes peer */
    n = build_seg(seg, 80, 4097, 2033, 0, 5, TCP_FIN | TCP_ACK, 0, 0);
    feed(&ip, seg, n);
    CHECK(tcp_peer_closed(s), "correct FIN marks peer closed");
    tcp_close(s);

    printf(failures ? "\n=== %d FAILURE(S) ===\n" : "\n=== ALL PASS ===\n", failures);
    return failures ? 1 : 0;
}
