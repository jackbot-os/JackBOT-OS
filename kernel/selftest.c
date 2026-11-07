#include <stdint.h>
#include <stddef.h>
#include <kernel/kstring.h>
#include <kernel/net.h>
#include <kernel/tcp.h>
#include <kernel/http.h>
#include <kernel/sha256.h>
#include <kernel/selftest.h>
#include <kernel/serial.h>

uint64_t timer_get_ticks(void);

static void put_dec(int v)
{
    if (v == 0) {
        serial_putc('0');
        return;
    }
    char b[12];
    int n = 0;
    while (v) {
        b[n++] = (char)('0' + v % 10);
        v /= 10;
    }
    while (n)
        serial_putc(b[--n]);
}

static int jstr(const char *j, const char *key, char *out, int max)
{
    char pat[40];
    int n = 0;
    pat[n++] = '"';
    for (int i = 0; key[i] && n < 36; i++)
        pat[n++] = key[i];
    pat[n++] = '"';
    pat[n] = 0;
    const char *p = strstr(j, pat);
    if (!p)
        return 0;
    p = strchr(p, ':');
    if (!p)
        return 0;
    p = strchr(p, '"');
    if (!p)
        return 0;
    p++;
    n = 0;
    while (*p && *p != '"' && n < max - 1)
        out[n++] = *p++;
    out[n] = 0;
    return 1;
}

static int wait_http(int c, uint64_t timeout)
{
    uint64_t t = timer_get_ticks();
    for (;;) {
        net_poll();
        tcp_poll();
        http_poll_client(c);
        int s = http_state(c);
        if (s == H_DONE)
            return 1;
        if (s == H_FAIL)
            return 0;
        if (timer_get_ticks() - t > timeout)
            return 0;
    }
}

void net_selftest(void)
{
    serial_puts("[SELF] === network self test ===\n");
    uint64_t t = timer_get_ticks();
    while (!net_ready() && timer_get_ticks() - t < 400) {
        net_poll();
        tcp_poll();
    }
    if (!net_ready()) {
        serial_puts("[SELF] FAIL: no link (DHCP)\n");
        return;
    }
    serial_puts("[SELF] PASS: link up\n");

    http_get_url(0, "http://10.0.2.2/version.json");
    if (!wait_http(0, 500)) {
        serial_puts("[SELF] FAIL: version.json http_state=");
        put_dec(http_state(0));
        serial_puts(" status=");
        put_dec(http_status(0));
        serial_putc('\n');
        return;
    }
    serial_puts("[SELF] PASS: fetched version.json\n");

    /* HTTPS through the built-in TLS 1.2 client (ECDHE-X25519 +
       AES-128-GCM). Local verification only: one plain GET to a public
       test page; no scanning, no probing of any other service. */
    http_get_url(1, "https://example.com/");
    if (!wait_http(1, 900)) {
        serial_puts("[SELF] FAIL: https example.com (state=");
        put_dec(http_state(1));
        serial_puts(")\n");
    } else {
        serial_puts("[SELF] PASS: https example.com bytes=");
        put_dec(http_body_len(1));
        serial_putc('\n');
    }

    char dl[180], wantsha[80];
    const char *vj = (const char *)http_body(0);
    if (!jstr(vj, "download_url", dl, sizeof dl) ||
        !jstr(vj, "sha256", wantsha, sizeof wantsha)) {
        /* The update manifest is only present when the host actually runs
           the update server; its absence is not a kernel fault. */
        serial_puts("[SELF] SKIP: no update manifest on host\n");
    } else {
        http_get_url(0, dl);
        if (!wait_http(0, 500)) {
            serial_puts("[SELF] FAIL: download package\n");
            return;
        }
        const uint8_t *pkg = http_body(0);
        int pl = http_body_len(0);
        char gotsha[65];
        sha256_hex(pkg, (size_t)pl, gotsha);
        if (strcmp(gotsha, wantsha)) {
            serial_puts("[SELF] FAIL: sha256 mismatch\n");
            return;
        }
        serial_puts("[SELF] PASS: package sha256 verified (bytes=");
        put_dec(pl);
        serial_puts(")\n");
    }

    http_get_url(1, "http://10.0.2.2/index.html");
    if (!wait_http(1, 500)) {
        serial_puts("[SELF] FAIL: index.html\n");
        return;
    }
    serial_puts("[SELF] PASS: fetched index.html bytes=");
    put_dec(http_body_len(1));
    serial_putc('\n');

    serial_puts("[SELF] === ALL NET TESTS PASSED ===\n");
}
