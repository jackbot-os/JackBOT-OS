/* Offline harness for kernel/honeypot.c. Local VM only, no real network. */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <kernel/honeypot.h>
#include <kernel/net.h>
#include <kernel/tcp.h>

static int fails;
static int checks;

#define CHECK(cond, msg) do { \
    checks++; \
    if (cond) { printf("PASS: %s\n", msg); } \
    else { fails++; printf("FAIL: %s\n", msg); } \
} while (0)

static void make_tcp(uint8_t *seg, uint16_t sport, uint16_t dport, uint8_t flags)
{
    seg[0] = (uint8_t)(sport >> 8); seg[1] = (uint8_t)sport;
    seg[2] = (uint8_t)(dport >> 8); seg[3] = (uint8_t)dport;
    for (int i = 4; i < 12; i++) seg[i] = 0;
    seg[12] = 0x50;
    seg[13] = flags;
    for (int i = 14; i < 20; i++) seg[i] = 0;
}

static struct ip_hdr mk_ip(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    struct ip_hdr ip;
    memset(&ip, 0, sizeof(ip));
    ip.src[0] = a; ip.src[1] = b; ip.src[2] = c; ip.src[3] = d;
    return ip;
}

int main(void)
{
    honeypot_init();
    CHECK(honeypot_enabled() == 0, "honeypot starts disabled");

    honeypot_observe_udp(0x0A00020F, 5353, 8888, 0);
    CHECK(honeypot_entry_count() == 0, "disabled: unclaimed UDP not recorded");

    honeypot_set(1);
    CHECK(honeypot_enabled() == 1, "honeypot can be enabled");

    honeypot_observe_udp(0x0A00020F, 5353, 8888, 0);
    honeypot_observe_udp(0x0A00020F, 5353, 8888, 0);
    CHECK(honeypot_entry_count() == 1, "same UDP source+port merged");
    CHECK(honeypot_entries()[0].hits == 2, "merged entry hit count = 2");

    honeypot_observe_udp(0x0A00020F, 5353, 9999, 0);
    CHECK(honeypot_entry_count() == 2, "different UDP port recorded separately");

    honeypot_observe_udp(0x0A00020F, 5353, 8888, 1);
    CHECK(honeypot_entry_count() == 2, "claimed UDP not recorded");

    struct ip_hdr ip1 = mk_ip(10, 0, 2, 20);
    uint8_t syn[20], ack[20];
    make_tcp(syn, 4444, 22, TCP_SYN);
    make_tcp(ack, 4444, 22, TCP_ACK);
    honeypot_observe_tcp(&ip1, ack, 20);
    CHECK(honeypot_entry_count() == 2, "plain ACK ignored");
    honeypot_observe_tcp(&ip1, syn, 20);
    CHECK(honeypot_entry_count() == 3, "SYN probe recorded");

    honeypot_observe_icmp(0x0A00020E, 0);
    CHECK(honeypot_entry_count() == 3, "ICMP echo reply ignored");
    honeypot_observe_icmp(0x0A00020E, 8);
    CHECK(honeypot_entry_count() == 4, "ICMP echo request recorded");

    for (int i = 0; i < 40; i++)
        honeypot_observe_udp(0x0A000200 + (uint32_t)i, 1000, 2000, 0);
    CHECK(honeypot_entry_count() == HONEYPOT_MAX_ENTRIES,
          "entry table capped without overflow");

    honeypot_clear();
    CHECK(honeypot_entry_count() == 0, "clear empties the table");

    honeypot_self_test();

    printf("== checks=%d fails=%d ==\n", checks, fails);
    return fails ? 1 : 0;
}
