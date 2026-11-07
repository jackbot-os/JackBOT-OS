#include <stdint.h>
#include <stddef.h>
#include <kernel/x25519.h>

/* Curve25519 arithmetic in the classic 16x16-bit limb representation
   (the representation used by TweetNaCl and ref10).  All operations are
   branch-free with respect to secret data: the scalarmult ladder uses
   constant-time conditional swaps. */

typedef int64_t i64;
typedef i64 gf[16];

static const gf k121665 = {0xDB41, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static void car25519(gf o)
{
    for (int i = 0; i < 16; i++) {
        i64 c;
        o[i] += (i64)1 << 16;
        c = o[i] >> 16;
        if (i < 15)
            o[i + 1] += c - 1;
        else
            o[0] += 38 * (c - 1);
        o[i] -= c << 16;
    }
}

static void sel25519(gf p, gf q, int b)
{
    i64 c = ~(i64)(b - 1);
    for (int i = 0; i < 16; i++) {
        i64 t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

static void pack25519(uint8_t *o, const gf n)
{
    gf m, t;
    for (int i = 0; i < 16; i++)
        t[i] = n[i];
    car25519(t);
    car25519(t);
    car25519(t);
    for (int j = 0; j < 2; j++) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; i++) {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        int b = (int)((m[15] >> 16) & 1);
        m[14] &= 0xffff;
        sel25519(t, m, 1 - b);
    }
    for (int i = 0; i < 16; i++) {
        o[2 * i] = (uint8_t)(t[i] & 0xff);
        o[2 * i + 1] = (uint8_t)(t[i] >> 8);
    }
}

static void unpack25519(gf o, const uint8_t *n)
{
    for (int i = 0; i < 16; i++)
        o[i] = n[2 * i] + ((i64)n[2 * i + 1] << 8);
    o[15] &= 0x7fff;
}

static void fadd(gf o, const gf a, const gf b)
{
    for (int i = 0; i < 16; i++)
        o[i] = a[i] + b[i];
}

static void fsub(gf o, const gf a, const gf b)
{
    for (int i = 0; i < 16; i++)
        o[i] = a[i] - b[i];
}

static void fmul(gf o, const gf a, const gf b)
{
    i64 t[31];
    for (int i = 0; i < 31; i++)
        t[i] = 0;
    for (int i = 0; i < 16; i++)
        for (int j = 0; j < 16; j++)
            t[i + j] += a[i] * b[j];
    for (int i = 0; i < 15; i++)
        t[i] += 38 * t[i + 16];
    for (int i = 0; i < 16; i++)
        o[i] = t[i];
    car25519(o);
    car25519(o);
}

static void fsqr(gf o, const gf a)
{
    fmul(o, a, a);
}

/* o = i^(2^255 - 21) = i^(p-2), i.e. the modular inverse for non-zero i. */
static void finv(gf o, const gf i)
{
    gf c;
    for (int a = 0; a < 16; a++)
        c[a] = i[a];
    for (int a = 253; a >= 0; a--) {
        fsqr(c, c);
        if (a != 2 && a != 4)
            fmul(c, c, i);
    }
    for (int a = 0; a < 16; a++)
        o[a] = c[a];
}

int x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32])
{
    uint8_t z[32];
    gf x1, x2, z2, x3, z3, a, b, aa, bb, e, c, d, da, cb;
    int swap = 0;

    for (int i = 0; i < 32; i++)
        z[i] = scalar[i];
    z[0] &= 248;
    z[31] &= 127;
    z[31] |= 64;

    unpack25519(x1, point);

    for (int i = 0; i < 16; i++) {
        x2[i] = 0;
        z2[i] = 0;
        x3[i] = x1[i];
        z3[i] = 0;
    }
    x2[0] = 1;
    z3[0] = 1;

    for (int pos = 254; pos >= 0; pos--) {
        int bit = (z[pos >> 3] >> (pos & 7)) & 1;
        swap ^= bit;
        sel25519(x2, x3, swap);
        sel25519(z2, z3, swap);
        swap = bit;

        fadd(a, x2, z2);
        fsqr(aa, a);
        fsub(b, x2, z2);
        fsqr(bb, b);
        fsub(e, aa, bb);
        fadd(c, x3, z3);
        fsub(d, x3, z3);
        fmul(da, d, a);
        fmul(cb, c, b);

        fadd(x3, da, cb);
        fsqr(x3, x3);
        fsub(z3, da, cb);
        fsqr(z3, z3);
        fmul(z3, z3, x1);

        fmul(x2, aa, bb);
        {
            gf t;
            fmul(t, e, k121665);
            fadd(t, aa, t);
            fmul(z2, e, t);
        }
    }
    sel25519(x2, x3, swap);
    sel25519(z2, z3, swap);

    finv(z2, z2);
    fmul(x2, x2, z2);
    pack25519(out, x2);
    return 0;
}

int x25519_base(uint8_t out[32], const uint8_t scalar[32])
{
    static const uint8_t base[32] = {9};
    return x25519(out, scalar, base);
}
