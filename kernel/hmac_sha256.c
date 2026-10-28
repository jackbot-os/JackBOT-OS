#include <kernel/hmac_sha256.h>
#include <kernel/sha256.h>
#include <kernel/kstring.h>
#include <kernel/serial.h>

#define BLOCK 64
#define MSG_CAP 2048

static void xor_pad(uint8_t pad[BLOCK], const uint8_t *k, size_t klen, uint8_t v)
{
    uint8_t kb[BLOCK];
    memset(kb, 0, BLOCK);
    if (klen > BLOCK)
        sha256(k, klen, kb);
    else
        memcpy(kb, k, klen);
    for (int i = 0; i < BLOCK; i++)
        pad[i] = kb[i] ^ v;
}

void hmac_sha256(const uint8_t *key, size_t klen,
                 const uint8_t *msg, size_t mlen, uint8_t out[32])
{
    uint8_t ipad[BLOCK], opad[BLOCK], inner[32];
    static uint8_t inner_buf[BLOCK + MSG_CAP];

    xor_pad(ipad, key, klen, 0x36);
    xor_pad(opad, key, klen, 0x5c);

    size_t n = mlen > MSG_CAP ? MSG_CAP : mlen;
    memcpy(inner_buf, ipad, BLOCK);
    if (n)
        memcpy(inner_buf + BLOCK, msg, n);
    sha256(inner_buf, BLOCK + n, inner);

    uint8_t outer_buf[BLOCK + 32];
    memcpy(outer_buf, opad, BLOCK);
    memcpy(outer_buf + BLOCK, inner, 32);
    sha256(outer_buf, BLOCK + 32, out);
}

void hmac_sha256_hex(const uint8_t *key, size_t klen,
                     const uint8_t *msg, size_t mlen, char out[65])
{
    uint8_t mac[32];
    static const char *hx = "0123456789abcdef";
    hmac_sha256(key, klen, msg, mlen, mac);
    for (int i = 0; i < 32; i++) {
        out[i * 2] = hx[mac[i] >> 4];
        out[i * 2 + 1] = hx[mac[i] & 15];
    }
    out[64] = 0;
}

static int vec(const uint8_t *key, size_t klen, const uint8_t *msg, size_t mlen,
               const uint8_t expect[32])
{
    uint8_t mac[32];
    hmac_sha256(key, klen, msg, mlen, mac);
    return memcmp(mac, expect, 32) == 0;
}

int hmac_sha256_selftest(void)
{
    static const uint8_t k1[20] = {
        0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,
        0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b
    };
    static const uint8_t m1[] = "Hi There";
    static const uint8_t e1[32] = {
        0xb0,0x34,0x4c,0x61,0xd8,0xdb,0x38,0x53,0x5c,0xa8,0xaf,0xce,
        0xaf,0x0b,0xf1,0x2b,0x88,0x1d,0xc2,0x00,0xc9,0x83,0x3d,0xa7,
        0x26,0xe9,0x37,0x6c,0x2e,0x32,0xcf,0xf7
    };

    static const uint8_t k2[] = "Jefe";
    static const uint8_t m2[] = "what do ya want for nothing?";
    static const uint8_t e2[32] = {
        0x5b,0xdc,0xc1,0x46,0xbf,0x60,0x75,0x4e,0x6a,0x04,0x24,0x26,
        0x08,0x95,0x75,0xc7,0x5a,0x00,0x3f,0x08,0x9d,0x27,0x39,0x83,
        0x9d,0xec,0x58,0xb9,0x64,0xec,0x38,0x43
    };

    int ok = vec(k1, sizeof k1, m1, sizeof m1 - 1, e1) &&
             vec(k2, sizeof k2 - 1, m2, sizeof m2 - 1, e2);
    serial_puts("[HMAC-SHA256] RFC4231 ");
    serial_puts(ok ? "PASS\n" : "FAIL\n");
    return ok;
}
