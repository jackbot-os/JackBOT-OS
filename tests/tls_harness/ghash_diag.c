/* GCM GHASH diagnostics: compare tag variants to find the encoding difference. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <kernel/aes_gcm.h>

static void hex2bin(const char *hex, uint8_t *out, int n)
{
    for (int i = 0; i < n; i++) {
        unsigned v;
        sscanf(hex + 2 * i, "%2x", &v);
        out[i] = (uint8_t)v;
    }
}

static void gf_mul(uint8_t x[16], const uint8_t h[16])
{
    uint8_t z[16], v[16];
    memset(z, 0, 16);
    memcpy(v, h, 16);
    for (int i = 0; i < 128; i++) {
        if ((x[i >> 3] >> (7 - (i & 7))) & 1)
            for (int j = 0; j < 16; j++) z[j] ^= v[j];
        int lsb = v[15] & 1;
        for (int j = 15; j > 0; j--) v[j] = (uint8_t)((v[j] >> 1) | (v[j - 1] << 7));
        v[0] >>= 1;
        if (lsb) v[0] ^= 0xe1;
    }
    memcpy(x, z, 16);
}

/* variant: 0 = bits big-endian, 1 = bytes big-endian, 2 = no length block,
   3 = little-endian bits */
static void ghash_v(uint8_t y[16], const uint8_t h[16], const uint8_t *c,
                    size_t len, uint64_t alen, int variant)
{
    uint8_t b[16];
    memset(y, 0, 16);
    size_t n = len;
    const uint8_t *p = c;
    while (n >= 16) {
        for (int i = 0; i < 16; i++) y[i] ^= p[i];
        gf_mul(y, h);
        p += 16;
        n -= 16;
    }
    if (n) {
        memset(b, 0, 16);
        memcpy(b, p, n);
        for (int i = 0; i < 16; i++) y[i] ^= b[i];
        gf_mul(y, h);
    }
    if (variant == 2) return;
    memset(b, 0, 16);
    uint64_t ab = (uint64_t)alen * 8, cb = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++) {
        if (variant == 0) {
            b[i] = (uint8_t)(ab >> (56 - 8 * i));
            b[8 + i] = (uint8_t)(cb >> (56 - 8 * i));
        } else if (variant == 1) {
            b[i] = (uint8_t)(alen >> (56 - 8 * i));
            b[8 + i] = (uint8_t)(len >> (56 - 8 * i));
        } else {
            b[i] = (uint8_t)(ab >> (8 * i));
            b[8 + i] = (uint8_t)(cb >> (8 * i));
        }
    }
    for (int i = 0; i < 16; i++) y[i] ^= b[i];
    gf_mul(y, h);
}

int main(void)
{
    uint8_t key[16], nonce[12], ct[60], h[16], zero[16], ek[16];
    struct aes128_gcm ctx;
    hex2bin("feffe9928665731c6d6a8f9467308308", key, 16);
    hex2bin("cafebabefacedbaddecaf888", nonce, 12);
    hex2bin("42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e"
            "21d514b25466931c7d8f6a5aac84aa051ba30b396a0aac973d58e091", ct, 60);

    aes128_gcm_init(&ctx, key);
    memcpy(h, ctx.h, 16);
    uint8_t j0[16];
    memset(j0, 0, 16);
    memcpy(j0, nonce, 12);
    j0[15] = 1;
    /* AES(J0) via seal of empty message: tag == E(J0) xor S, S=0 for empty */
    uint8_t tag[16], out[16];
    aes128_gcm_seal(&ctx, nonce, 0, 0, 0, 0, out, tag);
    memcpy(ek, tag, 16);

    printf("E(J0) = ");
    for (int i = 0; i < 16; i++) printf("%02x", ek[i]);
    printf("\n");

    for (int v = 0; v < 4; v++) {
        uint8_t y[16], t[16];
        ghash_v(y, h, ct, 60, 0, v);
        for (int i = 0; i < 16; i++) t[i] = y[i] ^ ek[i];
        printf("variant %d tag = ", v);
        for (int i = 0; i < 16; i++) printf("%02x", t[i]);
        printf("%s\n", (v == 0 && memcmp(t, "\x4d\x5c\x2a\xf3\x27\xcd\x64\xa6\x2c\xf3\x5a\xbd\x2b\xa6\xfa\xb4", 16) == 0) ? "  <== MATCH" : "");
    }
    return 0;
}
