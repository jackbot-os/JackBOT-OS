#include <stdint.h>
#include <stddef.h>
#include <kernel/kstring.h>
#include <kernel/net.h>
#include <kernel/e1000.h>
#include <kernel/nic.h>
#include <kernel/serial.h>

uint64_t timer_get_ticks(void);
void tcp_in(struct ip_hdr *ip, const uint8_t *payload, uint16_t len);

#define ARP_SLOTS 16
#define UDP_SLOTS 8
#define FRAME_CAP 2048

#define ICMP_ECHOREQ 8

struct arp_entry {
    uint8_t ip[4];
    uint8_t mac[6];
    int used;
};

struct udp_slot {
    uint16_t port;
    udp_handler h;
    int used;
};

struct dhcp_pkt {
    uint8_t op;
    uint8_t htype;
    uint8_t hlen;
    uint8_t hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t chaddr[16];
    uint8_t sname[64];
    uint8_t file[128];
    uint32_t magic;
    uint8_t options[312];
} __attribute__((packed));

enum { D_DISCOVER, D_REQUEST, D_DONE, D_FAIL };

static struct {
    uint8_t mac[6];
    uint8_t ip[4];
    uint8_t mask[4];
    uint8_t gw[4];
    uint8_t dns[4];
    int up;
} S;

static struct arp_entry arp_tbl[ARP_SLOTS];
static struct udp_slot udp_tbl[UDP_SLOTS];
static uint16_t ip_id = 1;

static int dhcp_state;
static uint32_t xid;
static uint64_t dhcp_t0;
static int dhcp_tries;
static uint8_t offered[4], serverid[4];

uint16_t htons16(uint16_t x)
{
    return (uint16_t)((x >> 8) | (x << 8));
}

uint32_t htons32(uint32_t x)
{
    return ((x & 0xFF) << 24) | ((x & 0xFF00) << 8)
         | ((x >> 8) & 0xFF00) | (x >> 24);
}

uint32_t net_ipv4(const uint8_t p[4])
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8) | p[3];
}

void net_fmt_ip(uint32_t host, uint8_t out[4])
{
    out[0] = (uint8_t)(host >> 24);
    out[1] = (uint8_t)(host >> 16);
    out[2] = (uint8_t)(host >> 8);
    out[3] = (uint8_t)host;
}

void net_ip_str(const uint8_t ip[4], char out[16])
{
    int n = 0;
    for (int i = 0; i < 4; i++) {
        int v = ip[i];
        if (v >= 100) { out[n++] = (char)('0' + v / 100); v %= 100; }
        if (v >= 10)  { out[n++] = (char)('0' + v / 10); v %= 10; }
        out[n++] = (char)('0' + v);
        if (i < 3)
            out[n++] = '.';
    }
    out[n] = 0;
}

int net_same_subnet(const uint8_t a[4], const uint8_t b[4])
{
    uint32_t m = net_ipv4(S.mask);
    return (net_ipv4(a) & m) == (net_ipv4(b) & m);
}

uint16_t net_checksum(const void *data, size_t len)
{
    const uint16_t *p = data;
    uint32_t sum = 0;
    while (len > 1) {
        sum += *p++;
        len -= 2;
    }
    if (len)
        sum += *(const uint8_t *)p;
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

uint16_t net_pseudo_checksum(uint32_t src, uint32_t dst, uint8_t proto,
                             const void *data, uint16_t len)
{
    static uint8_t buf[2000];
    uint8_t sb[4], db[4];
    net_fmt_ip(src, sb);
    net_fmt_ip(dst, db);
    memcpy(buf, sb, 4);
    memcpy(buf + 4, db, 4);
    buf[8] = 0;
    buf[9] = proto;
    buf[10] = (uint8_t)(len >> 8);
    buf[11] = (uint8_t)(len & 0xFF);
    memcpy(buf + 12, data, len);
    return net_checksum(buf, (size_t)(12 + len));
}

static void arp_add(const uint8_t ip[4], const uint8_t mac[6])
{
    for (int i = 0; i < ARP_SLOTS; i++) {
        if (arp_tbl[i].used && !memcmp(arp_tbl[i].ip, ip, 4)) {
            memcpy(arp_tbl[i].mac, mac, 6);
            return;
        }
    }
    for (int i = 0; i < ARP_SLOTS; i++) {
        if (!arp_tbl[i].used) {
            arp_tbl[i].used = 1;
            memcpy(arp_tbl[i].ip, ip, 4);
            memcpy(arp_tbl[i].mac, mac, 6);
            return;
        }
    }
}

static int arp_lookup(const uint8_t ip[4], uint8_t out[6])
{
    for (int i = 0; i < ARP_SLOTS; i++)
        if (arp_tbl[i].used && !memcmp(arp_tbl[i].ip, ip, 4)) {
            memcpy(out, arp_tbl[i].mac, 6);
            return 1;
        }
    return 0;
}

static void arp_request(const uint8_t target[4])
{
    static uint8_t frame[64];
    struct eth_hdr *e = (void *)frame;
    struct arp_pkt *a = (void *)(frame + sizeof(struct eth_hdr));
    uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    memcpy(e->dst, bcast, 6);
    memcpy(e->src, S.mac, 6);
    e->ethertype = htons16(ETH_P_ARP);

    a->htype = htons16(1);
    a->ptype = htons16(ETH_P_IP);
    a->hlen = 6;
    a->plen = 4;
    a->op = htons16(1);
    memcpy(a->sha, S.mac, 6);
    memcpy(a->spa, S.ip, 4);
    memset(a->tha, 0, 6);
    memcpy(a->tpa, target, 4);
    nic_send(frame, sizeof(struct eth_hdr) + sizeof(struct arp_pkt));
}

static void arp_handle(const uint8_t *frame)
{
    const struct arp_pkt *a = (const void *)(frame + sizeof(struct eth_hdr));
    if (ntohs16(a->htype) != 1 || ntohs16(a->ptype) != ETH_P_IP)
        return;
    arp_add(a->spa, a->sha);
    if (ntohs16(a->op) == 1 && (!S.up || net_same_subnet(a->spa, S.ip))) {
        static uint8_t reply[64];
        struct eth_hdr *e = (void *)reply;
        struct arp_pkt *r = (void *)(reply + sizeof(struct eth_hdr));
        memcpy(e->dst, a->sha, 6);
        memcpy(e->src, S.mac, 6);
        e->ethertype = htons16(ETH_P_ARP);
        *r = *a;
        r->op = htons16(2);
        memcpy(r->tha, a->sha, 6);
        memcpy(r->tpa, a->spa, 4);
        memcpy(r->sha, S.mac, 6);
        memcpy(r->spa, S.ip, 4);
        nic_send(reply, sizeof(struct eth_hdr) + sizeof(struct arp_pkt));
    }
}

int net_send_ip(uint8_t proto, uint32_t dst_host,
                const void *data, uint16_t len)
{
    if (!S.up)
        return -1;
    uint8_t dst[4];
    net_fmt_ip(dst_host, dst);
    const uint8_t *next = net_same_subnet(dst, S.ip) ? dst : S.gw;
    uint8_t dmac[6];
    if (!arp_lookup(next, dmac)) {
        arp_request(next);
        return -1;
    }
    static uint8_t pkt[1600];
    struct eth_hdr *e = (void *)pkt;
    struct ip_hdr *ip = (void *)(pkt + sizeof(struct eth_hdr));
    uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    int isb = dst[0]==0xFF && dst[1]==0xFF && dst[2]==0xFF && dst[3]==0xFF;
    memcpy(e->dst, isb ? bcast : dmac, 6);
    memcpy(e->src, S.mac, 6);
    e->ethertype = htons16(ETH_P_IP);

    ip->ihl_ver = 0x45;
    ip->tos = 0;
    ip->total_len = htons16((uint16_t)(20 + len));
    ip->id = htons16(ip_id++);
    ip->frag = 0;
    ip->ttl = 64;
    ip->proto = proto;
    ip->csum = 0;
    memcpy(ip->src, S.ip, 4);
    memcpy(ip->dst, dst, 4);
    ip->csum = net_checksum(ip, 20);
    memcpy(pkt + 14 + 20, data, len);
    return nic_send(pkt, (uint16_t)(14 + 20 + len));
}

int net_send_udp(uint32_t dst_host, uint16_t dport, uint16_t sport,
                 const void *data, uint16_t len)
{
    static uint8_t seg[1500];
    struct udp_hdr *u = (void *)seg;
    u->sport = htons16(sport);
    u->dport = htons16(dport);
    u->len = htons16((uint16_t)(8 + len));
    u->csum = 0;
    memcpy(seg + 8, data, len);
    return net_send_ip(IP_PROTO_UDP, dst_host, seg, (uint16_t)(8 + len));
}

void udp_register(uint16_t port, udp_handler h)
{
    for (int i = 0; i < UDP_SLOTS; i++) {
        if (!udp_tbl[i].used) {
            udp_tbl[i].used = 1;
            udp_tbl[i].port = port;
            udp_tbl[i].h = h;
            return;
        }
    }
}

static void icmp_handle(struct ip_hdr *ip, uint8_t *payload, uint16_t len)
{
    struct icmp_hdr *ic = (void *)payload;
    if (ic->type == ICMP_ECHOREQ && len >= 8) {
        ic->type = 0;
        ic->code = 0;
        ic->csum = 0;
        ic->csum = net_checksum(ic, len);
        net_send_ip(IP_PROTO_ICMP, net_ipv4(ip->src), ic, len);
    }
}

static void udp_handle(struct ip_hdr *ip, const uint8_t *payload, uint16_t len)
{
    if (len < 8)
        return;
    const struct udp_hdr *u = (const void *)payload;
    uint16_t dport = ntohs16(u->dport);
    uint16_t sport = ntohs16(u->sport);
    uint32_t src = net_ipv4(ip->src);
    const uint8_t *data = payload + 8;
    uint16_t dlen = (uint16_t)(len - 8);
    for (int i = 0; i < UDP_SLOTS; i++)
        if (udp_tbl[i].used && udp_tbl[i].port == dport && udp_tbl[i].h)
            udp_tbl[i].h(src, sport, data, dlen);
}

static void ip_handle(const uint8_t *frame, uint16_t flen)
{
    const struct ip_hdr *ip = (const void *)(frame + 14);
    uint8_t ihl = (ip->ihl_ver & 0xF) * 4;
    if (ihl < 20)
        return;
    uint16_t total = ntohs16(ip->total_len);
    if (total > flen - 14)
        total = flen - 14;
    if (total < ihl)
        return;
    if (S.up && memcmp(ip->dst, S.ip, 4) &&
        !(ip->dst[0]==0xFF&&ip->dst[3]==0xFF))
        return;
    const uint8_t *payload = frame + 14 + ihl;
    uint16_t plen = (uint16_t)(total - ihl);
    switch (ip->proto) {
    case IP_PROTO_ICMP:
        icmp_handle((struct ip_hdr *)ip, (uint8_t *)payload, plen);
        break;
    case IP_PROTO_UDP:
        udp_handle((struct ip_hdr *)ip, payload, plen);
        break;
    case IP_PROTO_TCP:
        tcp_in((struct ip_hdr *)ip, payload, plen);
        break;
    }
}

static uint8_t *dhcp_opt(struct dhcp_pkt *p, uint8_t code)
{
    uint8_t *o = p->options;
    int i = 0;
    while (i < (int)sizeof(p->options)) {
        uint8_t c = o[i];
        if (c == 255)
            break;
        if (c == 0) { i++; continue; }
        uint8_t l = o[i + 1];
        if (c == code)
            return &o[i + 2];
        i += 2 + l;
    }
    return 0;
}

static void apply_static_fallback(void)
{
    net_fmt_ip(0x0A00020F, S.ip);
    net_fmt_ip(0xFFFFFF00, S.mask);
    net_fmt_ip(0x0A000202, S.gw);
    net_fmt_ip(0x0A000203, S.dns);
    S.up = 1;
    dhcp_state = D_FAIL;
    serial_puts("[DHCP] fallback static 10.0.2.15\n");
}

static void dhcp_handler(uint32_t src, uint16_t sport,
                         const uint8_t *data, uint16_t len)
{
    (void)src; (void)sport;
    if (len < (int)sizeof(struct dhcp_pkt) - 312)
        return;
    const struct dhcp_pkt *p = (const void *)data;
    if (p->xid != htons32(xid))
        return;
    uint8_t *t = dhcp_opt((struct dhcp_pkt *)p, 53);
    if (!t)
        return;
    uint8_t type = *t;

    if (type == 2 && dhcp_state == D_DISCOVER) {
        memcpy(offered, &p->yiaddr, 4);
        uint8_t *sid = dhcp_opt((struct dhcp_pkt *)p, 54);
        if (sid)
            memcpy(serverid, sid, 4);
        dhcp_state = D_REQUEST;
        dhcp_t0 = timer_get_ticks();
        dhcp_tries = 0;
    } else if (type == 5 && dhcp_state == D_REQUEST) {
        memcpy(S.ip, offered, 4);
        uint8_t *nm = dhcp_opt((struct dhcp_pkt *)p, 1);
        uint8_t *gw = dhcp_opt((struct dhcp_pkt *)p, 3);
        uint8_t *dns = dhcp_opt((struct dhcp_pkt *)p, 6);
        if (nm) memcpy(S.mask, nm, 4); else net_fmt_ip(0xFFFFFF00,S.mask);
        if (gw) memcpy(S.gw, gw, 4); else net_fmt_ip(0x0A000202,S.gw);
        if (dns) memcpy(S.dns, dns, 4); else net_fmt_ip(0x0A000203,S.dns);
        S.up = 1;
        dhcp_state = D_DONE;
        serial_puts("[DHCP] bound\n");
    }
}

static int raw_dhcp_send(int type)
{
    static uint8_t frame[1600];
    memset(frame, 0, sizeof(frame));
    struct eth_hdr *e = (void *)frame;
    struct ip_hdr *ip = (void *)(frame + 14);
    struct udp_hdr *u = (void *)(frame + 34);
    struct dhcp_pkt *d = (void *)(frame + 42);

    uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    memcpy(e->dst, bcast, 6);
    memcpy(e->src, S.mac, 6);
    e->ethertype = htons16(ETH_P_IP);

    d->op = 1; d->htype = 1; d->hlen = 6;
    d->xid = htons32(xid);
    d->flags = htons16(0x8000);
    memcpy(d->chaddr, S.mac, 6);
    d->magic = htons32(0x63825363);

    uint8_t *o = d->options;
    int i = 0;
    o[i++]=53; o[i++]=1; o[i++]= (uint8_t)type;
    o[i++]=55; o[i++]=3; o[i++]=1; o[i++]=3; o[i++]=6;
    if (type == 3) {
        o[i++]=50; o[i++]=4; memcpy(o+i,offered,4); i+=4;
        o[i++]=54; o[i++]=4; memcpy(o+i,serverid,4); i+=4;
    }
    o[i++]=255;

    uint16_t dhcp_len = (uint16_t)(240 + i);
    uint16_t seg_len = (uint16_t)(8 + dhcp_len);
    uint16_t ip_len = (uint16_t)(20 + seg_len);

    ip->ihl_ver = 0x45;
    ip->total_len = htons16(ip_len);
    ip->id = htons16(ip_id++);
    ip->ttl = 64;
    ip->proto = IP_PROTO_UDP;
    net_fmt_ip(0, ip->src);
    net_fmt_ip(0xFFFFFFFF, ip->dst);
    ip->csum = net_checksum(ip, 20);

    u->sport = htons16(68);
    u->dport = htons16(67);
    u->len = htons16(seg_len);
    u->csum = net_pseudo_checksum(0, 0xFFFFFFFF, IP_PROTO_UDP, u, seg_len);

    return nic_send(frame, (uint16_t)(14 + ip_len));
}

static void dhcp_tick(void)
{
    if (dhcp_state == D_DONE || dhcp_state == D_FAIL)
        return;
    uint64_t now = timer_get_ticks();
    if (now - dhcp_t0 >= 100) {
        dhcp_tries++;
        int type = (dhcp_state == D_DISCOVER) ? 1 : 3;
        raw_dhcp_send(type);
        dhcp_t0 = now;
        if (dhcp_tries >= 6) {
            if (dhcp_state == D_REQUEST)
                apply_static_fallback();
            else
                dhcp_state = D_REQUEST;
        }
    }
}

void net_init(void)
{
    memset(&S, 0, sizeof(S));
    if (nic_init_all() != 0)
        return;
    nic_get_mac(S.mac);
    net_fmt_ip(0, S.ip);
    net_fmt_ip(0, S.mask);
    udp_register(68, dhcp_handler);

    xid = 0x4A424F54;
    dhcp_state = D_DISCOVER;
    dhcp_tries = 0;
    dhcp_t0 = timer_get_ticks();
    raw_dhcp_send(1);
}

void net_poll(void)
{
    static uint8_t rbuf[FRAME_CAP];
    int n;
    int guard = 0;
    while ((n = nic_recv(rbuf, sizeof rbuf)) > 0 && guard++ < 16) {
        if (n < (int)sizeof(struct eth_hdr))
            continue;
        const struct eth_hdr *e = (const void *)rbuf;
        uint16_t et = ntohs16(e->ethertype);
        if (et == ETH_P_ARP)
            arp_handle(rbuf);
        else if (et == ETH_P_IP)
            ip_handle(rbuf, (uint16_t)n);
    }
    dhcp_tick();
}

int net_ready(void) { return S.up; }
const uint8_t *net_local_ip(void) { return S.ip; }
const uint8_t *net_gateway(void) { return S.gw; }
const uint8_t *net_dns(void) { return S.dns; }
const uint8_t *net_netmask(void) { return S.mask; }
const uint8_t *net_mac(void) { return S.mac; }
