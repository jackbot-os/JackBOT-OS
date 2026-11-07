/* Random parser fuzz under ASAN for the real kernel http.c.
   Includes http.c to reach static parsers. Local VM only, no network. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../kernel/http.c"

static uint32_t rng = 0x13579BDFu;
static uint32_t rnd(void)
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}

static void rand_str(char *out, int cap)
{
    int n = (int)(rnd() % cap);
    for (int i = 0; i < n; i++)
        out[i] = (char)("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/:.- "[rnd() % 46]);
    out[n] = 0;
}

int main(void)
{
    char url[400], host[80], path[140], loc[200], np[140];
    struct http_client base;
    struct http_client *c = &cl[0];
    int rounds = 0;

    for (int trial = 0; trial < 6000; trial++) {
        uint16_t port;
        rand_str(url, sizeof url);
        memset(host, 0, sizeof host); memset(path, 0, sizeof path);
        parse_abs_url(url, host, sizeof host, path, sizeof path, &port);

        rand_str(np, sizeof np);
        normalize_path(np, sizeof np);

        rand_str(loc, sizeof loc);
        memset(&base, 0, sizeof base);
        strcpy(base.host, "base.com");
        strcpy(base.path, "/dir/page.html");
        base.port = 80;
        resolve_location(loc, &base, host, sizeof host, path, sizeof path, &port);

        memset(c, 0, sizeof *c);
        int n = (int)(rnd() % 400);
        for (int i = 0; i < n; i++)
            c->resp[i] = (uint8_t)rnd();
        c->resp_len = (uint16_t)n;
        parse_response(c);

        rounds++;
    }
    printf("HTTP FUZZ OK: %d random parser inputs processed, no crash under ASAN\n", rounds);
    return 0;
}
