#include <stdint.h>
#include <stddef.h>
#include <kernel/kstring.h>
#include <kernel/aes_gcm.h>

/* AES-128 (FIPS-197) + GCM (NIST SP 800-38D).
   Compact table-based implementation; the S-box and key schedule follow
   the standard, and GHASH uses the straightforward bit-serial multiply
   (GF(2^128) with the reflected polynomial 0xE1). */

static const uint8_t sbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b,
    0xfe, 0xd7, 0xab, 0x76, 0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0,
    0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0, 0xb7, 0xfd, 0x93, 0x26,
    0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2,
    0xeb, 0x27, 0xb2, 0x75, 0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0,
    0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84, 0x53, 0xd1, 0x00, 0xed,
    0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f,
    0x50, 0x3c, 0x9f, 0xa8, 0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5,
    0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2, 0xcd, 0x0c, 0x13, 0xec,
    0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14,
    0xde, 0x5e, 0x0b, 0xdb, 0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c,
    0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79, 0xe7, 0xc8, 0x37, 0x6d,
    0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f,
    0x4b, 0xbd, 0x8b, 0x8a, 0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e,
    0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e, 0xe1, 0xf8, 0x98, 0x11,
    0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f,
    0xb0, 0x54, 0xbb, 0x16
};

static const uint8_t rcon[11] = {
    0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
};

static uint8_t xtime(uint8_t x)
{
    return (uint8_t)((x << 1) ^ (((x >> 7) & 1) * 0x1b));
}

static void aes128_expand(uint8_t *rk, const uint8_t key[16])
{
    for (int i = 0; i < 16; i++)
        rk[i] = key[i];
    for (int i = 16; i < 176; i += 4) {
        uint8_t t[4];
        for (int j = 0; j < 4; j++)
            t[j] = rk[i - 4 + j];
        if ((i % 16) == 0) {
            uint8_t tmp = t[0];
            t[0] = sbox[t[1]] ^ rcon[i / 16];
            t[1] = sbox[t[2]];
            t[2] = sbox[t[3]];
            t[3] = sbox[tmp];
        }
        for (int j = 0; j < 4; j++)
            rk[i + j] = rk[i - 16 + j] ^ t[j];
    }
}

static void add_round_key(uint8_t s[16], const uint8_t *rk)
{
    for (int i = 0; i < 16; i++)
        s[i] ^= rk[i];
}

static void sub_bytes(uint8_t s[16])
{
    for (int i = 0; i < 16; i++)
        s[i] = sbox[s[i]];
}

static void shift_rows(uint8_t s[16])
{
    uint8_t t = s[1];
    s[1] = s[5]; s[5] = s[9]; s[9] = s[13]; s[13] = t;
    t = s[2]; s[2] = s[10]; s[10] = t;
    t = s[6]; s[6] = s[14]; s[14] = t;
    t = s[15]; s[15] = s[11]; s[11] = s[7]; s[7] = s[3]; s[3] = t;
}

static void mix_columns(uint8_t s[16])
{
    for (int c = 0; c < 4; c++) {
        uint8_t *p = s + 4 * c;
        uint8_t a = p[0], b = p[1], cc = p[2], d = p[3];
        uint8_t t = (uint8_t)(a ^ b ^ cc ^ d);
        p[0] ^= (uint8_t)(t ^ xtime((uint8_t)(a ^ b)));
        p[1] ^= (uint8_t)(t ^ xtime((uint8_t)(b ^ cc)));
        p[2] ^= (uint8_t)(t ^ xtime((uint8_t)(cc ^ d)));
        p[3] ^= (uint8_t)(t ^ xtime((uint8_t)(d ^ a)));
    }
}

static void aes128_encrypt_block(const uint8_t *rk, const uint8_t in[16],
                                 uint8_t out[16])
{
    uint8_t s[16];
    for (int i = 0; i < 16; i++)
        s[i] = in[i];
    add_round_key(s, rk);
    for (int round = 1; round <= 10; round++) {
        sub_bytes(s);
        shift_rows(s);
        if (round != 10)
            mix_columns(s);
        add_round_key(s, rk + 16 * round);
    }
    for (int i = 0; i < 16; i++)
        out[i] = s[i];
}

/* ---- GHASH ---- */

static void gf_mul(uint8_t x[16], const uint8_t h[16])
{
    uint8_t z[16];
    uint8_t v[16];
    memset(z, 0, 16);
    memcpy(v, h, 16);
    for (int i = 0; i < 128; i++) {
        if ((x[i >> 3] >> (7 - (i & 7))) & 1) {
            for (int j = 0; j < 16; j++)
                z[j] ^= v[j];
        }
        int lsb = v[15] & 1;
        for (int j = 15; j > 0; j--)
            v[j] = (uint8_t)((v[j] >> 1) | (v[j - 1] << 7));
        v[0] >>= 1;
        if (lsb)
            v[0] ^= 0xe1;
    }
    memcpy(x, z, 16);
}

static void ghash(uint8_t y[16], const uint8_t h[16],
                  const uint8_t *aad, size_t aad_len,
                  const uint8_t *cipher, size_t len)
{
    uint8_t block[16];
    memset(y, 0, 16);

    while (aad_len >= 16) {
        for (int i = 0; i < 16; i++)
            y[i] ^= aad[i];
        gf_mul(y, h);
        aad += 16;
        aad_len -= 16;
    }
    if (aad_len) {
        memset(block, 0, 16);
        for (size_t i = 0; i < aad_len; i++)
            block[i] = aad[i];
        for (int i = 0; i < 16; i++)
            y[i] ^= block[i];
        gf_mul(y, h);
    }

    while (len >= 16) {
        for (int i = 0; i < 16; i++)
            y[i] ^= cipher[i];
        gf_mul(y, h);
        cipher += 16;
        len -= 16;
    }
    if (len) {
        memset(block, 0, 16);
        for (size_t i = 0; i < len; i++)
            block[i] = cipher[i];
        for (int i = 0; i < 16; i++)
            y[i] ^= block[i];
        gf_mul(y, h);
    }
}

static void ghash_lengths(uint8_t y[16], const uint8_t h[16],
                          uint64_t aad_bits, uint64_t len_bits)
{
    uint8_t block[16];
    for (int i = 0; i < 8; i++) {
        block[i] = (uint8_t)(aad_bits >> (56 - 8 * i));
        block[8 + i] = (uint8_t)(len_bits >> (56 - 8 * i));
    }
    for (int i = 0; i < 16; i++)
        y[i] ^= block[i];
    gf_mul(y, h);
}

static void gcm_ctr(const uint8_t *rk, uint8_t counter[16],
                    const uint8_t *in, size_t len, uint8_t *out)
{
    uint8_t ks[16];
    while (len) {
        aes128_encrypt_block(rk, counter, ks);
        size_t n = len < 16 ? len : 16;
        for (size_t i = 0; i < n; i++)
            out[i] = in[i] ^ ks[i];
        /* increment the 32-bit counter field */
        for (int i = 15; i >= 12; i--) {
            if (++counter[i] != 0)
                break;
        }
        in += n;
        out += n;
        len -= n;
    }
}

void aes128_gcm_init(struct aes128_gcm *ctx, const uint8_t key[16])
{
    uint8_t zero[16];
    aes128_expand(ctx->round_keys, key);
    memset(zero, 0, 16);
    aes128_encrypt_block(ctx->round_keys, zero, ctx->h);
}

static void gcm_j0(uint8_t j0[16], const uint8_t nonce[12])
{
    for (int i = 0; i < 12; i++)
        j0[i] = nonce[i];
    j0[12] = 0;
    j0[13] = 0;
    j0[14] = 0;
    j0[15] = 1;
}

void aes128_gcm_seal(struct aes128_gcm *ctx, const uint8_t nonce[12],
                     const uint8_t *aad, size_t aad_len,
                     const uint8_t *plain, size_t len,
                     uint8_t *out, uint8_t tag[16])
{
    uint8_t j0[16];
    uint8_t ctr[16];
    uint8_t y[16];
    uint8_t ek[16];

    gcm_j0(j0, nonce);
    memcpy(ctr, j0, 16);
    for (int i = 15; i >= 12; i--) {
        if (++ctr[i] != 0)
            break;
    }
    gcm_ctr(ctx->round_keys, ctr, plain, len, out);

    ghash(y, ctx->h, aad, aad_len, out, len);
    ghash_lengths(y, ctx->h, (uint64_t)aad_len * 8, (uint64_t)len * 8);

    aes128_encrypt_block(ctx->round_keys, j0, ek);
    for (int i = 0; i < 16; i++)
        tag[i] = y[i] ^ ek[i];
}

int aes128_gcm_open(struct aes128_gcm *ctx, const uint8_t nonce[12],
                    const uint8_t *aad, size_t aad_len,
                    const uint8_t *cipher, size_t len,
                    uint8_t *out, const uint8_t tag[16])
{
    uint8_t j0[16];
    uint8_t ctr[16];
    uint8_t y[16];
    uint8_t ek[16];
    uint8_t want[16];

    gcm_j0(j0, nonce);
    ghash(y, ctx->h, aad, aad_len, cipher, len);
    ghash_lengths(y, ctx->h, (uint64_t)aad_len * 8, (uint64_t)len * 8);
    aes128_encrypt_block(ctx->round_keys, j0, ek);
    for (int i = 0; i < 16; i++)
        want[i] = y[i] ^ ek[i];

    uint8_t diff = 0;
    for (int i = 0; i < 16; i++)
        diff |= (uint8_t)(want[i] ^ tag[i]);
    if (diff)
        return -1;

    memcpy(ctr, j0, 16);
    for (int i = 15; i >= 12; i--) {
        if (++ctr[i] != 0)
            break;
    }
    {
        uint8_t ks[16];
        const uint8_t *in = cipher;
        size_t n = len;
        while (n) {
            aes128_encrypt_block(ctx->round_keys, ctr, ks);
            size_t k = n < 16 ? n : 16;
            for (size_t i = 0; i < k; i++)
                out[i] = in[i] ^ ks[i];
            for (int i = 15; i >= 12; i--) {
                if (++ctr[i] != 0)
                    break;
            }
            in += k;
            out += k;
            n -= k;
        }
    }
    return 0;
}
