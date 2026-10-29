#include <stdint.h>
#include <stddef.h>
#include <kernel/honeypot.h>
#include <kernel/tcp.h>
#include <kernel/config.h>
#include <kernel/kstring.h>
#include <kernel/serial.h>

uint64_t timer_get_ticks(void);

static int g_on;
static struct honeypot_entry g_table[HONEYPOT_MAX_ENTRIES];
static int g_count;

static uint32_t hp_tick(void)
{
    return (uint32_t)timer_get_ticks();
}

void honeypot_init(void)
{
    g_on = config_honeypot() ? 1 : 0;
    g_count = 0;
    for (int i = 0; i < HONEYPOT_MAX_ENTRIES; i++) {
        g_table[i].used = 0;
        g_table[i].hits = 0;
    }
}

int honeypot_enabled(void) { return g_on; }

void honeypot_set(int on)
{
    g_on = on ? 1 : 0;
    config_set_honeypot(g_on);
    config_save();
}

void honeypot_clear(void)
{
    g_count = 0;
    for (int i = 0; i < HONEYPOT_MAX_ENTRIES; i++) {
        g_table[i].used = 0;
        g_table[i].hits = 0;
    }
}

static struct honeypot_entry *find(uint32_t src, uint16_t dport,
                                   uint8_t proto, uint8_t action)
{
    for (int i = 0; i < g_count; i++) {
        struct honeypot_entry *e = &g_table[i];
        if (e->used && e->src_ip == src && e->dport == dport &&
            e->proto == proto && e->action == action)
            return e;
    }
    return 0;
}

static void record(uint32_t src, uint16_t sport, uint16_t dport,
                   uint8_t proto, uint8_t action)
{
    if (!g_on)
        return;
    struct honeypot_entry *e = find(src, dport, proto, action);
    uint32_t now = hp_tick();
    if (e) {
        e->hits++;
        e->sport = sport;
        e->last_tick = now;
        return;
    }
    if (g_count >= HONEYPOT_MAX_ENTRIES)
        return;
    e = &g_table[g_count++];
    e->used = 1;
    e->src_ip = src;
    e->sport = sport;
    e->dport = dport;
    e->proto = proto;
    e->action = action;
    e->hits = 1;
    e->first_tick = now;
    e->last_tick = now;
}

void honeypot_observe_udp(uint32_t src, uint16_t sport, uint16_t dport,
                          int claimed)
{
    if (claimed)
        return;
    record(src, sport, dport, 'U', HP_ACT_UNCLAIMED_UDP);
}

void honeypot_observe_tcp(const struct ip_hdr *ip, const uint8_t *payload,
                          uint16_t len)
{
    if (!g_on)
        return;
    if (len < 20)
        return;
    const struct tcp_hdr *t = (const void *)payload;
    uint8_t flags = t->flags;
    if (!(flags & TCP_SYN) || (flags & TCP_ACK))
        return;
    uint16_t sport = ntohs16(t->sport);
    uint16_t dport = ntohs16(t->dport);
    record(net_ipv4(ip->src), sport, dport, 'T', HP_ACT_TCP_SYN);
}

void honeypot_observe_icmp(uint32_t src, uint8_t type)
{
    if (type != 8)
        return;
    record(src, 0, 0, 'I', HP_ACT_ICMP_ECHO);
}

int honeypot_entry_count(void) { return g_count; }

const struct honeypot_entry *honeypot_entries(void) { return g_table; }

void honeypot_self_test(void)
{
    int saved = g_on;
    honeypot_clear();
    g_on = 1;
    record(0x0A00020F, 5353, 8888, 'U', HP_ACT_UNCLAIMED_UDP);
    record(0x0A00020F, 5353, 8888, 'U', HP_ACT_UNCLAIMED_UDP);
    record(0x0A00020E, 12345, 22, 'T', HP_ACT_TCP_SYN);

    int ok = (g_count == 2);
    if (ok)
        ok = (g_table[0].hits == 2) && g_table[0].used;
    g_on = saved;
    honeypot_clear();
    serial_puts(ok ? "[HP] test PASS\n" : "[HP] test FAIL\n");
}
