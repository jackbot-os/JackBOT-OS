#include <stdint.h>
#include <stddef.h>
#include <kernel/kstring.h>
#include <kernel/net.h>
#include <kernel/tcp.h>
#include <kernel/serial.h>

uint64_t timer_get_ticks(void);

#define NSOCKS 4
#define RXCAP 8192
#define RXMASK 8191
#define TXCAP 1200

enum {
    TS_CLOSED = 0,
    TS_SYN_SENT,
    TS_ESTABLISHED,
    TS_FIN_WAIT,
    TS_CLOSE_WAIT
};

struct tcp_sock {
    int used;
    int state;
    uint8_t rip[4];
    uint16_t lport, rport;
    uint32_t iss;
    uint32_t snd_nxt;
    uint32_t rcv_nxt;
    uint8_t rx[RXCAP];
    uint16_t rx_head, rx_tail;
    uint8_t txbuf[TXCAP];
    uint16_t txbuf_len;
    uint32_t txbuf_seq;
    int tx_pending;
    uint64_t last_xmit;
    int retries;
    int peer_closed;
    int failed;
};

static struct tcp_sock socks[NSOCKS];
static uint16_t next_port = 4096;

static int rx_space(struct tcp_sock *s)
{
    return (s->rx_head - s->rx_tail - 1) & RXMASK;
}

static void rx_push(struct tcp_sock *s, const uint8_t *data, uint16_t len)
{
    int space = rx_space(s);
    if ((int)len > space)
        len = (uint16_t)space;
    for (int i = 0; i < (int)len; i++) {
        s->rx[s->rx_tail] = data[i];
        s->rx_tail = (s->rx_tail + 1) & RXMASK;
    }
}

static int send_seg(struct tcp_sock *s, uint8_t flags,
                    const uint8_t *opt, uint8_t optlen,
                    const uint8_t *data, uint16_t dlen, uint32_t use_seq)
{
    static uint8_t seg[1500];
    struct tcp_hdr *t = (void *)seg;
    uint8_t words = (uint8_t)(5 + optlen / 4);

    t->sport = htons16(s->lport);
    t->dport = htons16(s->rport);
    t->seq = htons32(use_seq);
    t->ack = htons32(s->rcv_nxt);
    t->data_off = (uint8_t)(words << 4);
    t->flags = flags;
    t->window = htons16(64240);
    t->csum = 0;
    t->urg = 0;
    if (optlen)
        memcpy(seg + 20, opt, optlen);
    if (dlen)
        memcpy(seg + 20 + optlen, data, dlen);

    uint16_t seglen = (uint16_t)(20 + optlen + dlen);
    uint32_t src = net_ipv4(net_local_ip());
    uint32_t dst = net_ipv4(s->rip);
    t->csum = net_pseudo_checksum(src, dst, IP_PROTO_TCP, seg, seglen);
    return net_send_ip(IP_PROTO_TCP, net_ipv4(s->rip), seg, seglen);
}

static void send_ack(struct tcp_sock *s)
{
    send_seg(s, TCP_ACK, 0, 0, 0, 0, s->snd_nxt);
}

int tcp_connect(uint32_t ip, uint16_t port)
{
    if (!net_ready())
        return -1;
    int idx = -1;
    for (int i = 0; i < NSOCKS; i++)
        if (!socks[i].used) { idx = i; break; }
    if (idx < 0)
        return -1;

    struct tcp_sock *s = &socks[idx];
    memset(s, 0, sizeof(*s));
    s->used = 1;
    s->state = TS_SYN_SENT;
    net_fmt_ip(ip, s->rip);
    s->rport = port;
    s->lport = next_port++;
    s->iss = (uint32_t)(timer_get_ticks() * 2654435761u) + 1024;
    s->snd_nxt = s->iss;
    s->rcv_nxt = 0;

    static const uint8_t mss[4] = {0x02, 0x04, 0x05, 0xB4};
    send_seg(s, TCP_SYN, mss, 4, 0, 0, s->iss);
    s->snd_nxt = s->iss + 1;
    s->last_xmit = timer_get_ticks();
    s->retries = 0;
    return idx;
}

static struct tcp_sock *find_sock(struct ip_hdr *ip, struct tcp_hdr *t)
{
    uint16_t lport = ntohs16(t->dport);
    uint16_t rport = ntohs16(t->sport);
    for (int i = 0; i < NSOCKS; i++) {
        struct tcp_sock *s = &socks[i];
        if (s->used && s->lport == lport && s->rport == rport &&
            !memcmp(s->rip, ip->src, 4))
            return s;
    }
    return 0;
}

void tcp_in(struct ip_hdr *ip, const uint8_t *payload, uint16_t len)
{
    if (len < 20)
        return;
    struct tcp_hdr *t = (struct tcp_hdr *)payload;
    uint8_t doff = (uint8_t)((t->data_off >> 4) * 4);
    if (doff < 20 || doff > len)
        return;

    struct tcp_sock *s = find_sock(ip, t);
    if (!s)
        return;

    uint32_t seq = ntohs32(t->seq);
    uint32_t ack = ntohs32(t->ack);
    uint8_t flags = t->flags;
    uint8_t *data = (uint8_t *)payload + doff;
    uint16_t dlen = (uint16_t)(len - doff);

    if (flags & TCP_RST) {
        s->failed = 1;
        s->state = TS_CLOSED;
        return;
    }

    if (s->state == TS_SYN_SENT && (flags & TCP_SYN)) {
        s->rcv_nxt = seq + 1;
        s->snd_nxt = s->iss + 1;
        s->state = TS_ESTABLISHED;
        send_ack(s);
        if (dlen) {
            rx_push(s, data, dlen);
            s->rcv_nxt += dlen;
        }
        return;
    }

    if (s->state != TS_ESTABLISHED && s->state != TS_FIN_WAIT)
        return;

    if (dlen > 0) {
        if (seq == s->rcv_nxt) {
            rx_push(s, data, dlen);
            s->rcv_nxt += dlen;
            send_ack(s);
        } else {
            send_ack(s);
        }
    }

    if (flags & TCP_FIN) {
        if (seq + dlen == s->rcv_nxt) {
            s->rcv_nxt++;
            s->peer_closed = 1;
            if (s->state == TS_ESTABLISHED)
                s->state = TS_CLOSE_WAIT;
            send_ack(s);
        }
    }

    if (flags & TCP_ACK) {
        if (s->tx_pending &&
            (int32_t)(ack - (s->txbuf_seq + s->txbuf_len)) >= 0) {
            s->tx_pending = 0;
            s->txbuf_len = 0;
        }
        if ((int32_t)(ack - s->snd_nxt) > 0)
            s->snd_nxt = ack;
    }
}

int tcp_send(int s, const void *data, uint16_t len)
{
    if (s < 0 || s >= NSOCKS)
        return -1;
    struct tcp_sock *p = &socks[s];
    if (!p->used || p->state != TS_ESTABLISHED)
        return -1;
    if (len > TXCAP)
        len = TXCAP;
    memcpy(p->txbuf, data, len);
    p->txbuf_seq = p->snd_nxt;
    send_seg(p, TCP_PSH | TCP_ACK, 0, 0, data, len, p->snd_nxt);
    p->snd_nxt += len;
    p->tx_pending = 1;
    p->txbuf_len = len;
    p->last_xmit = timer_get_ticks();
    p->retries = 0;
    return (int)len;
}

int tcp_recv(int s, void *buf, uint16_t cap)
{
    if (s < 0 || s >= NSOCKS)
        return -1;
    struct tcp_sock *p = &socks[s];
    if (!p->used)
        return -1;
    int n = 0;
    while (p->rx_head != p->rx_tail && n < cap) {
        ((uint8_t *)buf)[n++] = p->rx[p->rx_head];
        p->rx_head = (p->rx_head + 1) & RXMASK;
    }
    return n;
}

int tcp_close(int s)
{
    if (s < 0 || s >= NSOCKS)
        return -1;
    struct tcp_sock *p = &socks[s];
    if (!p->used)
        return -1;
    if (p->state == TS_ESTABLISHED || p->state == TS_CLOSE_WAIT ||
        p->state == TS_FIN_WAIT) {
        send_seg(p, TCP_FIN | TCP_ACK, 0, 0, 0, 0, p->snd_nxt);
        p->snd_nxt++;
    }
    p->used = 0;
    p->state = TS_CLOSED;
    return 0;
}

void tcp_poll(void)
{
    uint64_t now = timer_get_ticks();
    static const uint8_t mss[4] = {0x02, 0x04, 0x05, 0xB4};
    for (int i = 0; i < NSOCKS; i++) {
        struct tcp_sock *s = &socks[i];
        if (!s->used)
            continue;
        if (s->state == TS_SYN_SENT) {
            if (now - s->last_xmit >= 20) {
                send_seg(s, TCP_SYN, mss, 4, 0, 0, s->iss);
                s->last_xmit = now;
                s->retries++;
                if (s->retries > 12) {
                    s->failed = 1;
                    s->state = TS_CLOSED;
                }
            }
        } else if (s->state == TS_ESTABLISHED && s->tx_pending) {
            if (now - s->last_xmit >= 50) {
                send_seg(s, TCP_PSH | TCP_ACK, 0, 0, s->txbuf,
                         s->txbuf_len, s->txbuf_seq);
                s->last_xmit = now;
                s->retries++;
                if (s->retries > 10)
                    s->failed = 1;
            }
        }
    }
}

int tcp_connected(int s)
{
    if (s < 0 || s >= NSOCKS)
        return 0;
    return socks[s].state == TS_ESTABLISHED ||
           socks[s].state == TS_CLOSE_WAIT;
}

int tcp_peer_closed(int s)
{
    if (s < 0 || s >= NSOCKS)
        return 0;
    return socks[s].peer_closed;
}

int tcp_failed(int s)
{
    if (s < 0 || s >= NSOCKS)
        return 1;
    return socks[s].failed;
}
