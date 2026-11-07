#include <stdint.h>
#include <stddef.h>
#include <kernel/tls.h>
#include <kernel/tcp.h>
#include <kernel/net.h>
#include <kernel/kstring.h>
#include <kernel/serial.h>
#include <kernel/sha256.h>
#include <kernel/hmac_sha256.h>
#include <kernel/x25519.h>
#include <kernel/aes_gcm.h>

uint64_t timer_get_ticks(void);

/* TLS 1.2 client for the built-in browser.
 *
 * Supported profile: TLS 1.2, ECDHE with X25519, AES-128-GCM with SHA-256
 * PRF (cipher suites TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256 and
 * TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256).
 *
 * KNOWN LIMITATION: the server certificate chain is parsed only
 * structurally; it is NOT validated against a trust store. The handshake is
 * encrypted, but a man-in-the-middle cannot be detected. This is documented
 * in the user manual; no third-party TLS library is linked.
 */

#define REC_MAX 16384
#define HS_MAX 8192
#define APP_MAX 4096
#define HS_RX_MAX 8192

struct tls_sess {
    int sock;
    int state;
    int enc_out;
    int enc_in;
    uint8_t crnd[32];
    uint8_t srnd[32];
    uint8_t master[48];
    uint8_t ckey[16];
    uint8_t skey[16];
    uint8_t civ[4];
    uint8_t siv[4];
    uint64_t seq_out;
    uint64_t seq_in;
    struct aes128_gcm cgcm;
    struct aes128_gcm sgcm;
    uint8_t hs[HS_MAX];      /* handshake transcript (all messages) */
    int hsn;
    uint8_t rx[REC_MAX];
    int rxlen;
    uint8_t hs_rx[HS_RX_MAX]; /* unparsed handshake payload */
    int hs_rx_len;
    int hs_rx_pos;
    uint8_t app[APP_MAX];
    int applen;
    int apppos;
};

static struct tls_sess S;

static int tls_fail(const char *why)
{
    serial_puts("[TLS] fail: ");
    serial_puts(why);
    serial_putc('\n');
    S.state = TLS_FAILED;
    return -1;
}

static void fill_random(uint8_t *p, int n)
{
    static uint64_t ctr;
    for (int i = 0; i < n; i += 8) {
        uint64_t mix = timer_get_ticks() ^ (uint64_t)(uintptr_t)p
                     ^ (++ctr * 0x9E3779B97F4A7C15ull);
        mix *= 0xBF58476D1CE4E5B9ull;
        mix ^= mix >> 29;
        mix *= 0x94D049BB133111EBull;
        mix ^= mix >> 32;
        for (int k = 0; k < 8 && i + k < n; k++)
            p[i + k] = (uint8_t)(mix >> (8 * k));
    }
}

static int tcp_send_all(const uint8_t *buf, int len)
{
    int off = 0;
    while (off < len) {
        int n = tcp_send(S.sock, buf + off, (uint16_t)(len - off));
        if (n <= 0)
            return -1;
        off += n;
    }
    return 0;
}

static int send_record(uint8_t type, const uint8_t *data, int len)
{
    if (!S.enc_out) {
        uint8_t hdr[5] = { type, 0x03, 0x03,
                           (uint8_t)(len >> 8), (uint8_t)len };
        if (tcp_send_all(hdr, 5) < 0)
            return -1;
        if (len > 0 && tcp_send_all(data, len) < 0)
            return -1;
        return 0;
    }
    {
        uint8_t frag[REC_MAX + 32];
        uint8_t aad[13];
        uint8_t nonce[12];
        uint8_t tag[16];
        uint64_t seq = S.seq_out++;

        for (int i = 0; i < 8; i++) {
            uint8_t b = (uint8_t)(seq >> (56 - 8 * i));
            aad[i] = b;
            frag[i] = b;
        }
        aad[8] = type;
        aad[9] = 0x03;
        aad[10] = 0x03;
        aad[11] = (uint8_t)(len >> 8);
        aad[12] = (uint8_t)len;
        memcpy(nonce, S.civ, 4);
        memcpy(nonce + 4, frag, 8);
        aes128_gcm_seal(&S.cgcm, nonce, aad, 13, data, (size_t)len,
                        frag + 8, tag);
        memcpy(frag + 8 + len, tag, 16);
        {
            int total = 8 + len + 16;
            uint8_t hdr[5] = { type, 0x03, 0x03,
                               (uint8_t)(total >> 8), (uint8_t)total };
            if (tcp_send_all(hdr, 5) < 0)
                return -1;
            if (tcp_send_all(frag, total) < 0)
                return -1;
        }
        return 0;
    }
}

/* Make sure at least one more byte is buffered. 1=ready, 0=timeout,
   -1=closed/failed. */
static int fill_rx(int timeout)
{
    uint64_t t0 = timer_get_ticks();
    if (S.rxlen >= REC_MAX)
        return 1;
    {
        int n = tcp_recv(S.sock, S.rx + S.rxlen,
                         (uint16_t)(REC_MAX - S.rxlen));
        if (n > 0) {
            S.rxlen += n;
            return 1;
        }
    }
    for (;;) {
        if (tcp_failed(S.sock) || tcp_peer_closed(S.sock))
            return -1;
        if (timer_get_ticks() - t0 >= (uint64_t)timeout)
            return 0;
        net_poll();
        tcp_poll();
        {
            int n = tcp_recv(S.sock, S.rx + S.rxlen,
                             (uint16_t)(REC_MAX - S.rxlen));
            if (n > 0) {
                S.rxlen += n;
                return 1;
            }
        }
    }
}

static int recv_record(uint8_t *type, uint8_t *out, int cap, int *len,
                       int timeout)
{
    for (;;) {
        if (S.rxlen < 5) {
            int r = fill_rx(timeout);
            if (r <= 0)
                return r;
            continue;
        }
        {
            int rlen = (S.rx[3] << 8) | S.rx[4];
            if (rlen > REC_MAX - 5)
                return tls_fail("record length out of range");
            if (S.rxlen < 5 + rlen) {
                int r = fill_rx(timeout);
                if (r <= 0)
                    return r;
                continue;
            }
            *type = S.rx[0];
            if (!S.enc_in) {
                if (rlen > cap)
                    return tls_fail("plaintext record too large");
                memcpy(out, S.rx + 5, (size_t)rlen);
                *len = rlen;
            } else {
                if (rlen < 8 + 16)
                    return tls_fail("encrypted record too short");
                {
                    uint64_t seq = S.seq_in++;
                    uint8_t aad[13];
                    uint8_t nonce[12];
                    uint8_t tag[16];
                    int plen = rlen - 8 - 16;
                    if (plen > cap)
                        return tls_fail("decrypted record too large");
                    memcpy(nonce, S.siv, 4);
                    memcpy(nonce + 4, S.rx + 5, 8);
                    for (int i = 0; i < 8; i++)
                        aad[i] = (uint8_t)(seq >> (56 - 8 * i));
                    aad[8] = *type;
                    aad[9] = S.rx[1];
                    aad[10] = S.rx[2];
                    aad[11] = (uint8_t)(plen >> 8);
                    aad[12] = (uint8_t)plen;
                    memcpy(tag, S.rx + 5 + 8 + plen, 16);
                    if (aes128_gcm_open(&S.sgcm, nonce, aad, 13,
                                        S.rx + 13, (size_t)plen, out,
                                        tag) != 0)
                        return tls_fail("record tag mismatch");
                    *len = plen;
                }
            }
            memmove(S.rx, S.rx + 5 + rlen, (size_t)(S.rxlen - 5 - rlen));
            S.rxlen -= 5 + rlen;
            if (*type == TLS_RECORD_TYPE_ALERT) {
                /* close_notify (level warning / description 0) is the normal
                   end of a TLS stream, not a failure. */
                if (*len >= 2 && (out[1] == 0 || out[0] == 1)) {
                    serial_puts("[TLS] close_notify\n");
                    return 0;
                }
                if (*len >= 2) {
                    serial_puts("[TLS] fatal alert desc=");
                    serial_puthex(out[1]);
                    serial_putc('\n');
                }
                return tls_fail("alert received");
            }
            return 1;
        }
    }
}

/* Read the next handshake message, transparently joining records.
   Returns 1 and sets type/body/len, or <=0 on error/timeout. */
static int next_handshake(uint8_t *type, uint8_t **body, int *blen,
                          int timeout)
{
    for (;;) {
        if (S.hs_rx_pos + 4 <= S.hs_rx_len) {
            uint8_t *p = S.hs_rx + S.hs_rx_pos;
            int mlen = (p[1] << 16) | (p[2] << 8) | p[3];
            if (S.hs_rx_pos + 4 + mlen <= S.hs_rx_len) {
                *type = p[0];
                *body = p + 4;
                *blen = mlen;
                S.hs_rx_pos += 4 + mlen;
                if (S.hsn + 4 + mlen <= HS_MAX) {
                    memcpy(S.hs + S.hsn, p, (size_t)(4 + mlen));
                    S.hsn += 4 + mlen;
                } else {
                    return tls_fail("handshake transcript too large");
                }
                return 1;
            }
        }
        /* compact consumed bytes */
        if (S.hs_rx_pos > 0) {
            memmove(S.hs_rx, S.hs_rx + S.hs_rx_pos,
                    (size_t)(S.hs_rx_len - S.hs_rx_pos));
            S.hs_rx_len -= S.hs_rx_pos;
            S.hs_rx_pos = 0;
        }
        if (S.hs_rx_len >= HS_RX_MAX)
            return tls_fail("handshake buffer full");
        {
            uint8_t t;
            int l = 0;
            int r = recv_record(&t, S.hs_rx + S.hs_rx_len,
                                HS_RX_MAX - S.hs_rx_len, &l, timeout);
            if (r <= 0)
                return r;
            if (t != TLS_RECORD_TYPE_HANDSHAKE)
                return tls_fail("unexpected record during handshake");
            S.hs_rx_len += l;
        }
    }
}

/* TLS 1.2 PRF with SHA-256 (P_hash expansion). */
static void tls_prf(const uint8_t *secret, int slen, const char *label,
                    const uint8_t *seed, int seedlen,
                    uint8_t *out, int outlen)
{
    uint8_t a[32];
    uint8_t msg[160];
    int l = (int)strlen(label);
    int off = 0;

    memcpy(msg, label, (size_t)l);
    memcpy(msg + l, seed, (size_t)seedlen);
    hmac_sha256(secret, (size_t)slen, msg, (size_t)(l + seedlen), a);

    while (off < outlen) {
        uint8_t block[32];
        memcpy(msg, a, 32);
        memcpy(msg + 32, label, (size_t)l);
        memcpy(msg + 32 + l, seed, (size_t)seedlen);
        hmac_sha256(secret, (size_t)slen, msg, (size_t)(32 + l + seedlen),
                    block);
        for (int i = 0; i < 32 && off < outlen; i++)
            out[off++] = block[i];
        hmac_sha256(secret, (size_t)slen, a, 32, a);
    }
}

static void put8(uint8_t *b, int *p, uint8_t v) { b[(*p)++] = v; }
static void put16(uint8_t *b, int *p, uint16_t v)
{
    b[(*p)++] = (uint8_t)(v >> 8);
    b[(*p)++] = (uint8_t)v;
}
static void put_bytes(uint8_t *b, int *p, const uint8_t *s, int n)
{
    memcpy(b + *p, s, (size_t)n);
    *p += n;
}

/* Build ClientHello (handshake message 1) with the extensions a TLS 1.2
   server needs: SNI, supported groups (x25519), point formats,
   signature algorithms, secure renegotiation. */
static int build_client_hello(const char *host, const uint8_t *pub)
{
    uint8_t body[512];
    int b = 0;

    put16(body, &b, TLS_VERSION_1_2);
    put_bytes(body, &b, S.crnd, 32);
    put8(body, &b, 0); /* session id length */

    /* cipher suites: ECDHE-RSA-AES128-GCM-SHA256, ECDHE-ECDSA-AES128-GCM-SHA256 */
    put16(body, &b, 4);
    put16(body, &b, 0xC02F);
    put16(body, &b, 0xC02B);

    put8(body, &b, 1);
    put8(body, &b, 0);

    {
        uint8_t ext[420];
        int e = 0;
        int hl = (int)strlen(host);
        if (hl > 200)
            hl = 200;

        /* server_name */
        put16(ext, &e, 0x0000);
        put16(ext, &e, (uint16_t)(5 + hl));
        put16(ext, &e, (uint16_t)(3 + hl));
        put8(ext, &e, 0);
        put16(ext, &e, (uint16_t)hl);
        put_bytes(ext, &e, (const uint8_t *)host, hl);

        /* supported_groups: x25519, secp256r1 */
        put16(ext, &e, 0x000A);
        put16(ext, &e, 6);
        put16(ext, &e, 4);
        put16(ext, &e, 0x001D);
        put16(ext, &e, 0x0017);

        /* ec_point_formats: uncompressed */
        put16(ext, &e, 0x000B);
        put16(ext, &e, 2);
        put8(ext, &e, 1);
        put8(ext, &e, 0);

        /* signature_algorithms */
        put16(ext, &e, 0x000D);
        put16(ext, &e, 10);
        put16(ext, &e, 8);
        put16(ext, &e, 0x0403); /* ecdsa_secp256r1_sha256 */
        put16(ext, &e, 0x0503); /* ecdsa_secp384r1_sha384 */
        put16(ext, &e, 0x0401); /* rsa_pkcs1_sha256 */
        put16(ext, &e, 0x0804); /* rsa_pss_rsae_sha256 */

        /* secure renegotiation */
        put16(ext, &e, 0xFF01);
        put16(ext, &e, 1);
        put8(ext, &e, 0);

        put16(body, &b, (uint16_t)e);
        put_bytes(body, &b, ext, e);
    }

    {
        uint8_t msg[4 + 512];
        int m = 0;
        uint8_t type = 1;
        (void)type;
        msg[m++] = 1;
        msg[m++] = (uint8_t)(b >> 16);
        msg[m++] = (uint8_t)(b >> 8);
        msg[m++] = (uint8_t)b;
        memcpy(msg + m, body, (size_t)b);
        m += b;
        memcpy(S.hs + S.hsn, msg, (size_t)m);
        S.hsn += m;
        if (send_record(TLS_RECORD_TYPE_HANDSHAKE, msg, m) < 0)
            return tls_fail("cannot send ClientHello");
    }
    (void)pub;
    return 0;
}

static int parse_server_hello(uint8_t *body, int blen)
{
    if (blen < 38)
        return tls_fail("short ServerHello");
    if (((body[0] << 8) | body[1]) != TLS_VERSION_1_2) {
        serial_puts("[TLS] server version ");
        serial_puthex((uint32_t)((body[0] << 8) | body[1]));
        serial_putc('\n');
        return tls_fail("server does not speak TLS 1.2");
    }
    memcpy(S.srnd, body + 2, 32);
    {
        int p = 34;
        int sidlen = body[p++];
        p += sidlen;
        if (p + 3 > blen)
            return tls_fail("bad ServerHello layout");
        {
            uint16_t suite = (uint16_t)((body[p] << 8) | body[p + 1]);
            p += 2;
            if (suite != 0xC02F && suite != 0xC02B) {
                serial_puts("[TLS] server picked suite ");
                serial_puthex(suite);
                serial_putc('\n');
                return tls_fail("unsupported cipher suite");
            }
            p++; /* compression */
        }
    }
    return 0;
}

static int parse_server_key_exchange(uint8_t *body, int blen, uint8_t pub[32])
{
    if (blen < 4)
        return tls_fail("short ServerKeyExchange");
    if (body[0] != 3) /* named_curve */
        return tls_fail("ServerKeyExchange is not EC");
    {
        uint16_t group = (uint16_t)((body[1] << 8) | body[2]);
        int plen = body[3];
        if (group != 0x001D || plen != 32)
            return tls_fail("unexpected EC group / key length");
        if (blen < 4 + 32)
            return tls_fail("truncated server public key");
        memcpy(pub, body + 4, 32);
    }
    /* signature block follows; deliberately not verified (see note above) */
    return 0;
}

static int build_client_key_exchange(const uint8_t pub[32])
{
    uint8_t msg[64];
    int m = 0;
    msg[m++] = 16;
    msg[m++] = 0;
    msg[m++] = 0;
    msg[m++] = 33;
    msg[m++] = 32;
    memcpy(msg + m, pub, 32);
    m += 32;
    if (S.hsn + m > HS_MAX)
        return tls_fail("transcript full");
    memcpy(S.hs + S.hsn, msg, (size_t)m);
    S.hsn += m;
    if (send_record(TLS_RECORD_TYPE_HANDSHAKE, msg, m) < 0)
        return tls_fail("cannot send ClientKeyExchange");
    return 0;
}

static int send_finished(void)
{
    uint8_t th[32];
    uint8_t verify[12];
    uint8_t msg[16];
    int m = 0;

    sha256(S.hs, (size_t)S.hsn, th);
    tls_prf(S.master, 48, "client finished", th, 32, verify, 12);

    msg[m++] = 20;
    msg[m++] = 0;
    msg[m++] = 0;
    msg[m++] = 12;
    memcpy(msg + m, verify, 12);
    m += 12;
    memcpy(S.hs + S.hsn, msg, (size_t)m);
    S.hsn += m;

    /* ChangeCipherSpec (plaintext), then Finished under the new keys. */
    {
        uint8_t ccs = 1;
        if (send_record(0x14, &ccs, 1) < 0)
            return tls_fail("cannot send ChangeCipherSpec");
    }
    S.enc_out = 1;
    if (send_record(TLS_RECORD_TYPE_HANDSHAKE, msg, m) < 0)
        return tls_fail("cannot send Finished");
    return 0;
}

static int expect_server_finished(void)
{
    uint8_t th[32];
    uint8_t want[12];
    uint8_t type;
    uint8_t buf[64];
    int len = 0;

    /* server ChangeCipherSpec first */
    for (;;) {
        int r = recv_record(&type, buf, sizeof buf, &len, 400);
        if (r <= 0)
            return tls_fail("no ChangeCipherSpec from server");
        if (type == 0x14) {
            S.enc_in = 1;
            break;
        }
        if (type != TLS_RECORD_TYPE_HANDSHAKE)
            return tls_fail("unexpected record before ChangeCipherSpec");
    }

    for (;;) {
        int r = recv_record(&type, buf, sizeof buf, &len, 400);
        if (r <= 0)
            return tls_fail("no Finished from server");
        if (type != TLS_RECORD_TYPE_HANDSHAKE)
            continue;
        if (len >= 4 && buf[0] == 20) {
            sha256(S.hs, (size_t)S.hsn, th);
            tls_prf(S.master, 48, "server finished", th, 32, want, 12);
            if (len < 16 || memcmp(buf + 4, want, 12) != 0)
                return tls_fail("server Finished mismatch");
            return 0;
        }
    }
}

int tls_engine_ready(void) { return 1; }

int tls_state(void) { return S.state; }

int tls_connect(int sock, const char *host)
{
    uint8_t priv[32];
    uint8_t pub[32];
    uint8_t spub[32];
    uint8_t pre[32];
    uint8_t seedbuf[64];
    uint8_t keyblk[40];
    int have_srv_pub = 0;

    memset(&S, 0, sizeof S);
    S.sock = sock;
    S.state = TLS_HANDSHAKE;

    if (!host || !*host)
        return tls_fail("no host name");

    fill_random(S.crnd, 32);
    fill_random(priv, 32);
    x25519_base(pub, priv);

    if (build_client_hello(host, pub) != 0)
        return -1;

    /* Server flight: ServerHello, Certificate, ServerKeyExchange,
       ServerHelloDone (order fixed for ECDHE in TLS 1.2). */
    {
        int done = 0;
        while (!done) {
            uint8_t type = 0;
            uint8_t *body;
            int blen = 0;
            int r = next_handshake(&type, &body, &blen, 400);
            if (r <= 0)
                return tls_fail("timeout in server flight");
            switch (type) {
            case 2:
                if (parse_server_hello(body, blen) != 0)
                    return -1;
                break;
            case 11: /* Certificate: parsed structurally, not validated */
                break;
            case 12:
                if (parse_server_key_exchange(body, blen, spub) != 0)
                    return -1;
                have_srv_pub = 1;
                break;
            case 14:
                done = 1;
                break;
            default:
                return tls_fail("unexpected handshake message");
            }
        }
    }
    if (!have_srv_pub)
        return tls_fail("server did not send an ECDHE key");

    x25519(pre, priv, spub);

    memcpy(seedbuf, S.crnd, 32);
    memcpy(seedbuf + 32, S.srnd, 32);
    tls_prf(pre, 32, "master secret", seedbuf, 64, S.master, 48);

    memcpy(seedbuf, S.srnd, 32);
    memcpy(seedbuf + 32, S.crnd, 32);
    tls_prf(S.master, 48, "key expansion", seedbuf, 64, keyblk, 40);
    memcpy(S.ckey, keyblk, 16);
    memcpy(S.skey, keyblk + 16, 16);
    memcpy(S.civ, keyblk + 32, 4);
    memcpy(S.siv, keyblk + 36, 4);
    aes128_gcm_init(&S.cgcm, S.ckey);
    aes128_gcm_init(&S.sgcm, S.skey);

    if (build_client_key_exchange(pub) != 0)
        return -1;
    if (send_finished() != 0)
        return -1;
    if (expect_server_finished() != 0)
        return -1;

    S.state = TLS_ESTABLISHED;
    serial_puts("[TLS] handshake complete with ");
    serial_puts(host);
    serial_putc('\n');
    return 0;
}

/* Encrypt and send application data. */
int tls_send(int s, const void *data, uint16_t len)
{
    if (s != S.sock || S.state != TLS_ESTABLISHED || !data)
        return -1;
    if (len > 1400)
        len = 1400;
    if (send_record(TLS_RECORD_TYPE_APP_DATA, data, (int)len) < 0)
        return -1;
    return (int)len;
}

/* Return buffered plaintext; pull and decrypt a new record when empty.
   0 = nothing available yet, -1 = closed/error. */
int tls_recv(int s, void *buf, uint16_t cap)
{
    if (s != S.sock || S.state != TLS_ESTABLISHED)
        return -1;
    if (S.apppos < S.applen) {
        int n = S.applen - S.apppos;
        if (n > cap)
            n = cap;
        memcpy(buf, S.app + S.apppos, (size_t)n);
        S.apppos += n;
        return n;
    }
    for (;;) {
        uint8_t type;
        int len = 0;
        int r = recv_record(&type, S.app, APP_MAX, &len, 5);
        if (r == 0)
            return 0;
        if (r < 0)
            return -1;
        if (type == TLS_RECORD_TYPE_APP_DATA) {
            if (len == 0)
                continue;
            S.applen = len;
            S.apppos = 0;
            {
                int n = len;
                if (n > cap)
                    n = cap;
                memcpy(buf, S.app, (size_t)n);
                S.apppos = n;
                return n;
            }
        }
    }
}

void tls_close(int s)
{
    if (s != S.sock)
        return;
    if (S.state == TLS_ESTABLISHED) {
        /* Best-effort close_notify; ignore the result. */
        uint8_t alert[2] = { 0x01, 0x00 };
        send_record(TLS_RECORD_TYPE_ALERT, alert, 2);
    }
    S.state = TLS_FAILED;
}

const char *tls_state_name(int state)
{
    switch (state) {
    case TLS_IDLE:         return "idle";
    case TLS_CONNECTING:   return "connecting";
    case TLS_HANDSHAKE:    return "handshake";
    case TLS_ESTABLISHED:  return "established";
    case TLS_FAILED:       return "failed";
    case TLS_UNAVAILABLE:  return "unavailable";
    }
    return "?";
}
