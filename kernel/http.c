#include <stdint.h>
#include <stddef.h>
#include <kernel/kstring.h>
#include <kernel/net.h>
#include <kernel/tcp.h>
#include <kernel/http.h>
#include <kernel/serial.h>

uint64_t timer_get_ticks(void);

#define RESPCAP 60000
#define DNS_PORT 53200
#define MAX_REDIRECTS 5

struct dns_hdr {
    uint16_t id;
    uint16_t flags;
    uint16_t qd;
    uint16_t an;
    uint16_t ns;
    uint16_t ar;
} __attribute__((packed));

struct http_client {
    int state;
    char host[80];
    char path[140];
    uint16_t port;
    uint16_t dnsid;
    int sock;
    uint8_t resp[RESPCAP];
    uint16_t resp_len;
    int status;
    int hdr_len;
    uint64_t t0;
    int tries;
    int redirects;
};

static struct http_client cl[HCLIENTS];

static struct {
    uint16_t id;
    int busy, done, fail;
    uint32_t ip;
    uint64_t t0;
    int tries;
} dns;

static int dns_build(uint8_t *buf, uint16_t id, const char *host)
{
    struct dns_hdr *h = (void *)buf;
    h->id = htons16(id);
    h->flags = htons16(0x0100);
    h->qd = htons16(1);
    h->an = 0; h->ns = 0; h->ar = 0;

    int i = 12;
    const char *p = host;
    while (*p) {
        const char *dot = strchr(p, '.');
        int l = dot ? (int)(dot - p) : (int)strlen(p);
        buf[i++] = (uint8_t)l;
        memcpy(buf + i, p, l);
        i += l;
        if (!dot)
            break;
        p = dot + 1;
    }
    buf[i++] = 0;
    buf[i++]=0; buf[i++]=1;
    buf[i++]=0; buf[i++]=1;
    return i;
}

static int dns_start(const char *host, uint16_t id)
{
    if (dns.busy)
        return -1;
    static uint8_t q[300];
    int n = dns_build(q, id, host);
    if (net_send_udp(net_ipv4(net_dns()), 53, DNS_PORT, q, (uint16_t)n) < 0)
        return -1;
    dns.id = id;
    dns.busy = 1;
    dns.done = 0;
    dns.fail = 0;
    dns.tries = 0;
    dns.t0 = timer_get_ticks();
    return 0;
}

static int dns_skip(const uint8_t *b, int len, int pos)
{
    while (pos < len) {
        uint8_t l = b[pos];
        if (l == 0)
            return pos + 1;
        if ((l & 0xC0) == 0xC0)
            return pos + 2;
        pos += 1 + l;
    }
    return pos;
}

static void dns_handler(uint32_t src, uint16_t sport,
                        const uint8_t *data, uint16_t len)
{
    (void)src; (void)sport;
    const struct dns_hdr *h = (const void *)data;
    if (ntohs16(h->id) != dns.id)
        return;
    int qd = ntohs16(h->qd);
    int an = ntohs16(h->an);
    int pos = 12;
    for (int i = 0; i < qd; i++) {
        pos = dns_skip(data, len, pos);
        pos += 4;
    }
    int found = 0;
    for (int i = 0; i < an; i++) {
        pos = dns_skip(data, len, pos);
        if (pos + 10 > len)
            break;
        int type = (data[pos] << 8) | data[pos + 1];
        int rdlen = (data[pos + 8] << 8) | data[pos + 9];
        pos += 10;
        if (type == 1 && rdlen == 4 && pos + 4 <= len) {
            dns.ip = net_ipv4(data + pos);
            found = 1;
        }
        pos += rdlen;
    }
    dns.busy = 0;
    if (found)
        dns.done = 1;
    else
        dns.fail = 1;
}

static void dns_tick(void)
{
    if (!dns.busy)
        return;
    uint64_t now = timer_get_ticks();
    if (now - dns.t0 >= 50) {
        dns.tries++;
        if (dns.tries > 6) {
            dns.busy = 0;
            dns.fail = 1;
        }
    }
}

static int literal_ip(const char *host, uint32_t *out)
{
    int parts[4];
    int pi = 0, v = 0, digits = 0;
    for (const char *p = host; ; p++) {
        if (*p >= '0' && *p <= '9') {
            v = v * 10 + (*p - '0');
            digits++;
            if (digits > 3 || v > 255)
                return 0;
        } else if (*p == '.' || *p == 0) {
            if (!digits || pi >= 4)
                return 0;
            parts[pi++] = v;
            v = 0; digits = 0;
            if (*p == 0)
                break;
        } else {
            return 0;
        }
    }
    if (pi != 4)
        return 0;
    *out = ((uint32_t)parts[0]<<24)|((uint32_t)parts[1]<<16)
         | ((uint32_t)parts[2]<<8)|parts[3];
    return 1;
}

static void parse_response(struct http_client *c)
{
    c->status = 0;
    c->hdr_len = 0;
    const uint8_t *r = c->resp;
    const char *sp = strstr((const char *)r, " ");
    if (sp)
        c->status = atoi(sp + 1);
    const char *end = strstr((const char *)r, "\r\n\r\n");
    if (end)
        c->hdr_len = (int)(end - (const char *)r) + 4;
    else
        c->hdr_len = c->resp_len;
}

static int build_request(struct http_client *c, char *req)
{
    int i = 0;
    const char *a;
    a = "GET "; while(*a) req[i++]=*a++;
    a = c->path; while(*a) req[i++]=*a++;
    a = " HTTP/1.1\r\nHost: "; while(*a) req[i++]=*a++;
    a = c->host; while(*a) req[i++]=*a++;
    a = "\r\nUser-Agent: JackBOT-OS/1.0\r\n"
        "Accept: text/html,text/plain\r\nConnection: close\r\n\r\n";
    while(*a) req[i++]=*a++;
    return i;
}

/* Case-insensitive header lookup. Returns value length, 0 if absent. */
static int find_header(const struct http_client *c, const char *name,
                       char *out, int osz)
{
    int nl = (int)strlen(name);
    int i = 0;
    while (i < c->hdr_len) {
        int eol = i;
        while (eol < c->hdr_len && c->resp[eol] != '\n')
            eol++;
        int match = 1;
        for (int k = 0; k < nl; k++) {
            char a = c->resp[i + k];
            char b = name[k];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) { match = 0; break; }
        }
        if (match) {
            int j = i + nl;
            while (j < eol && (c->resp[j] == ' ' || c->resp[j] == '\t' ||
                               c->resp[j] == ':'))
                j++;
            int k = 0;
            while (j < eol && c->resp[j] != '\r' && k < osz - 1)
                out[k++] = (char)c->resp[j++];
            out[k] = 0;
            return k;
        }
        i = eol + 1;
    }
    return 0;
}

static int is_redirect_status(int st)
{
    return st == 301 || st == 302 || st == 303 || st == 307 || st == 308;
}

/* Parse an absolute http:// URL into host/path/port. Returns 0 on https. */
static int parse_abs_url(const char *url, char *host, int hsz,
                         char *path, int psz, uint16_t *port)
{
    const char *p = url;
    if (!strncmp(p, "https://", 8))
        return 0;
    if (!strncmp(p, "http://", 7))
        p += 7;
    int hi = 0;
    while (*p && *p != '/' && *p != ':' && hi < hsz - 1)
        host[hi++] = *p++;
    host[hi] = 0;

    *port = 80;
    if (*p == ':') {
        p++;
        int v = 0;
        while (*p >= '0' && *p <= '9')
            v = v * 10 + (*p++ - '0');
        if (v)
            *port = (uint16_t)v;
    }
    if (*p == '/') {
        int k = 0;
        while (*p && k < psz - 1)
            path[k++] = *p++;
        path[k] = 0;
    } else {
        path[0] = '/';
        path[1] = 0;
    }
    return 1;
}

/* Turn a Location header (absolute, absolute-path or relative) into a new
   request target, using the current host/path as the base. */
static int resolve_location(const char *loc, struct http_client *base,
                            char *host, int hsz, char *path, int psz,
                            uint16_t *port)
{
    if (!strncmp(loc, "https://", 8))
        return 0;
    if (!strncmp(loc, "http://", 7))
        return parse_abs_url(loc, host, hsz, path, psz, port);

    if (loc[0] == '/') {
        /* absolute path on the same host/port */
        strncpy(host, base->host, hsz - 1);
        host[hsz - 1] = 0;
        *port = base->port;
        int k = 0;
        while (loc[k] && k < psz - 1) {
            path[k] = loc[k];
            k++;
        }
        path[k] = 0;
        return 1;
    }

    /* relative path: replace the file component of the current path */
    strncpy(host, base->host, hsz - 1);
    host[hsz - 1] = 0;
    *port = base->port;
    int k = 0;
    const char *slash = strrchr(base->path, '/');
    if (slash)
        k = (int)(slash - base->path) + 1;
    if (k >= psz)
        k = psz - 1;
    memcpy(path, base->path, k);
    int m = 0;
    while (loc[m] && k < psz - 1)
        path[k++] = loc[m++];
    path[k] = 0;
    return 1;
}

static void begin_request(struct http_client *p, const char *host,
                          const char *path, uint16_t port)
{
    int red = p->redirects;
    int sock_keep = p->sock;
    memset(p, 0, sizeof(*p));
    p->redirects = red;
    strncpy(p->host, host, sizeof p->host - 1);
    strncpy(p->path, path, sizeof p->path - 1);
    p->port = port ? port : 80;
    p->state = H_DNS;
    p->sock = -1;
    p->tries = 0;
    if (sock_keep >= 0)
        tcp_close(sock_keep);
    static int registered;
    if (!registered) {
        udp_register(DNS_PORT, dns_handler);
        registered = 1;
    }
}

int http_get(int c, const char *host, const char *path, uint16_t port)
{
    if (c < 0 || c >= HCLIENTS)
        return -1;
    struct http_client *p = &cl[c];
    begin_request(p, host, path, port);
    p->dnsid = (uint16_t)(0x1000 + c * 0x100 +
                          (timer_get_ticks() & 0xFF));
    return 0;
}

int http_get_url(int c, const char *url)
{
    if (c < 0 || c >= HCLIENTS)
        return -1;
    if (!strncmp(url, "https://", 8)) {
        struct http_client *p = &cl[c];
        memset(p, 0, sizeof(*p));
        p->state = H_FAIL;
        serial_puts("[HTTP] https not supported (no TLS)\n");
        return -1;
    }
    char host[80];
    char pathbuf[140];
    uint16_t port = 80;
    if (!parse_abs_url(url, host, sizeof host, pathbuf, sizeof pathbuf,
                       &port))
        return -1;
    int rc = http_get(c, host, pathbuf, port);
    return rc;
}

void http_poll_client(int c)
{
    if (c < 0 || c >= HCLIENTS)
        return;
    struct http_client *p = &cl[c];
    dns_tick();
    uint64_t now = timer_get_ticks();

    switch (p->state) {
    case H_DNS: {
        uint32_t ip;
        if (literal_ip(p->host, &ip)) {
            p->sock = tcp_connect(ip, p->port);
            p->state = H_CONNECT;
            p->t0 = now;
            break;
        }
        if (!net_ready())
            break;
        if (!dns.busy && dns.id == p->dnsid) {
            if (dns.done) {
                p->sock = tcp_connect(dns.ip, p->port);
                p->state = H_CONNECT;
                p->t0 = now;
            } else if (p->tries < 3) {
                p->dnsid++;
                dns_start(p->host, p->dnsid);
                p->tries++;
            } else {
                p->state = H_FAIL;
            }
        } else if (!dns.busy) {
            dns_start(p->host, p->dnsid);
        }
        break;
    }
    case H_CONNECT:
        if (tcp_connected(p->sock)) {
            static char req[600];
            int n = build_request(p, req);
            tcp_send(p->sock, req, (uint16_t)n);
            p->state = H_RECV;
            p->t0 = now;
        } else if (tcp_failed(p->sock) || now - p->t0 > 300) {
            p->state = H_FAIL;
        }
        break;
    case H_RECV: {
        static uint8_t tmp[1500];
        int n = tcp_recv(p->sock, tmp, sizeof tmp);
        if (n > 0 && p->resp_len + n <= RESPCAP) {
            memcpy(p->resp + p->resp_len, tmp, n);
            p->resp_len += (uint16_t)n;
            p->t0 = now;
        }
        if (tcp_peer_closed(p->sock)) {
            parse_response(p);
            /* follow redirects automatically */
            if (is_redirect_status(p->status)) {
                char loc[256];
                if (p->redirects < MAX_REDIRECTS &&
                    find_header(p, "Location:", loc, sizeof loc)) {
                    char nhost[80], npath[140];
                    uint16_t nport = 80;
                    if (!resolve_location(loc, p, nhost, sizeof nhost,
                                          npath, sizeof npath, &nport)) {
                        serial_puts("[HTTP] redirect to https unsupported\n");
                        p->state = H_FAIL;
                    } else {
                        serial_puts("[HTTP] redirect -> ");
                        serial_puts(nhost);
                        serial_puts(npath);
                        serial_putc('\n');
                        begin_request(p, nhost, npath, nport);
                        p->redirects++;
                        p->dnsid = (uint16_t)(0x2000 + c * 0x100 +
                                              (timer_get_ticks() & 0xFF));
                    }
                    break;
                }
                serial_puts("[HTTP] redirect without Location/limit\n");
            }
            p->state = H_DONE;
        } else if (now - p->t0 > 500) {
            p->state = H_FAIL;
        }
        break;
    }
    default:
        break;
    }
}

void http_close_client(int c)
{
    if (c < 0 || c >= HCLIENTS)
        return;
    struct http_client *p = &cl[c];
    if (p->sock >= 0)
        tcp_close(p->sock);
    p->state = H_IDLE;
    p->resp_len = 0;
}

int http_state(int c)
{
    return (c >= 0 && c < HCLIENTS) ? cl[c].state : H_IDLE;
}
int http_ok(int c)
{
    return (c >= 0 && c < HCLIENTS) && cl[c].state == H_DONE &&
           (cl[c].status == 200 || cl[c].status == 0);
}
int http_failed(int c)
{
    return (c >= 0 && c < HCLIENTS) && cl[c].state == H_FAIL;
}
int http_status(int c)
{
    return (c >= 0 && c < HCLIENTS) ? cl[c].status : 0;
}
const uint8_t *http_body(int c)
{
    if (c < 0 || c >= HCLIENTS)
        return 0;
    return cl[c].resp + cl[c].hdr_len;
}
int http_body_len(int c)
{
    if (c < 0 || c >= HCLIENTS)
        return 0;
    return (int)cl[c].resp_len - cl[c].hdr_len;
}
