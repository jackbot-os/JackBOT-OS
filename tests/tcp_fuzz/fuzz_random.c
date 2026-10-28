/* Random malformed-segment fuzz under ASAN for the real kernel tcp.c.
 * Local VM only; exercises length/doff/flags/seq combinations, no network. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <kernel/net.h>
#include <kernel/tcp.h>

static uint8_t peer[4] = {10, 0, 2, 20};
static uint32_t peer_ip;

static uint32_t rng = 0x12345678u;
static uint32_t rnd(void)
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
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

    uint8_t seg[1700];
    int rounds = 0;
    for (int trial = 0; trial < 5000; trial++) {
        int s = tcp_connect(peer_ip, 80);
        if (s < 0) {
            for (int i = 0; i < 4; i++) tcp_close(i);
            s = tcp_connect(peer_ip, 80);
            if (s < 0) continue;
        }
        uint16_t dport = (uint16_t)(4096 + s);
        for (int k = 0; k < 8; k++) {
            uint16_t len = (uint16_t)(rnd() % 1600);
            uint8_t doff_words = (uint8_t)(rnd() % 16);
            uint8_t flags = (uint8_t)(rnd() & 0x1f);
            uint32_t seq = rnd();
            uint32_t ack = rnd();
            memset(seg, (int)(rnd() & 0xff), sizeof seg);
            seg[0] = 80 >> 8; seg[1] = 80;
            seg[2] = dport >> 8; seg[3] = dport;
            seg[4] = seq >> 24; seg[5] = seq >> 16;
            seg[6] = seq >> 8; seg[7] = seq;
            seg[8] = ack >> 24; seg[9] = ack >> 16;
            seg[10] = ack >> 8; seg[11] = ack;
            seg[12] = (uint8_t)(doff_words << 4);
            seg[13] = flags;
            feed(&ip, seg, len);
            rounds++;
        }
        tcp_close(s);
    }
    printf("FUZZ OK: %d segments processed, no crash under ASAN\n", rounds);
    return 0;
}
