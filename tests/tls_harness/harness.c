/* Offline vector tests for X25519 (RFC 7748) and AES-128-GCM (NIST SP 800-38D).
   Built on the Debian build VM with the host gcc against the real kernel
   sources kernel/x25519.c and kernel/aes_gcm.c (libc supplies memcpy/memset). */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <kernel/x25519.h>
#include <kernel/aes_gcm.h>

static int fails;

static void hex2bin(const char *hex, uint8_t *out, int n)
{
    for (int i = 0; i < n; i++) {
        unsigned v;
        sscanf(hex + 2 * i, "%2x", &v);
        out[i] = (uint8_t)v;
    }
}

static int check(const char *name, const uint8_t *got, const uint8_t *want, int n)
{
    if (memcmp(got, want, n) != 0) {
        printf("FAIL %s\n  got  ", name);
        for (int i = 0; i < n; i++)
            printf("%02x", got[i]);
        printf("\n  want ");
        for (int i = 0; i < n; i++)
            printf("%02x", want[i]);
        printf("\n");
        fails++;
        return 0;
    }
    printf("PASS %s\n", name);
    return 1;
}

static void test_x25519(void)
{
    uint8_t scalar[32], point[32], out[32], want[32];

    hex2bin("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4", scalar, 32);
    hex2bin("e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c", point, 32);
    hex2bin("c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552", want, 32);
    x25519(out, scalar, point);
    check("RFC7748 vector 1", out, want, 32);

    hex2bin("4b66e9d4d1b4673c5ad22691957d6af5c11b6421e0ea01d42ca4169e7918ba0d", scalar, 32);
    hex2bin("e5210f12786811d3f4b7959d0538ae2c31dbe7106fc03c3efc4cd549c715a493", point, 32);
    hex2bin("95cbde9476e8907d7aade45cb4b873f88b595a68799fa152e6f8f7647aac7957", want, 32);
    x25519(out, scalar, point);
    check("RFC7748 vector 2", out, want, 32);

    /* ECDH agreement: a*B == b*A */
    uint8_t a[32], b[32], pub_a[32], pub_b[32], s1[32], s2[32];
    hex2bin("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", a, 32);
    hex2bin("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb", b, 32);
    x25519_base(pub_a, a);
    x25519_base(pub_b, b);
    x25519(s1, a, pub_b);
    x25519(s2, b, pub_a);
    check("ECDH shared secret", s1, s2, 32);
}

static void test_gcm(void)
{
    uint8_t key[16], nonce[12], out[64], tag[16], want[64], wanttag[16];
    struct aes128_gcm ctx;

    /* NIST GCM test case 2: empty plaintext */
    hex2bin("00000000000000000000000000000000", key, 16);
    hex2bin("000000000000000000000000", nonce, 12);
    aes128_gcm_init(&ctx, key);
    aes128_gcm_seal(&ctx, nonce, 0, 0, 0, 0, out, tag);
    hex2bin("58e2fccefa7e3061367f1d57a4e7455a", wanttag, 16);
    check("GCM case2 tag", tag, wanttag, 16);

    /* NIST GCM test case 3: 16-byte zero plaintext */
    hex2bin("0388dace60b6a392f328c2b971b2fe78", want, 16);
    hex2bin("ab6e47d42cec13bdf53a67b21257bddf", wanttag, 16);
    {
        uint8_t pt[16];
        memset(pt, 0, 16);
        aes128_gcm_seal(&ctx, nonce, 0, 0, pt, 16, out, tag);
        check("GCM case3 ciphertext", out, want, 16);
        check("GCM case3 tag", tag, wanttag, 16);
        uint8_t dec[16];
        int rc = aes128_gcm_open(&ctx, nonce, 0, 0, out, 16, dec, tag);
        if (rc != 0 || memcmp(dec, pt, 16) != 0) {
            printf("FAIL GCM case3 decrypt\n");
            fails++;
        } else {
            printf("PASS GCM case3 decrypt\n");
        }
        tag[0] ^= 1;
        if (aes128_gcm_open(&ctx, nonce, 0, 0, out, 16, dec, tag) == 0) {
            printf("FAIL GCM tamper detection\n");
            fails++;
        } else {
            printf("PASS GCM tamper detection\n");
        }
    }

    /* NIST GCM test case 3: 64-byte plaintext, no AAD */
    hex2bin("feffe9928665731c6d6a8f9467308308", key, 16);
    hex2bin("cafebabefacedbaddecaf888", nonce, 12);
    {
        uint8_t pt[64];
        hex2bin("d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a72"
                "1c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b391aafd255",
                pt, 64);
        hex2bin("42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e"
                "21d514b25466931c7d8f6a5aac84aa051ba30b396a0aac973d58e091473f5985",
                want, 64);
        hex2bin("4d5c2af327cd64a62cf35abd2ba6fab4", wanttag, 16);
        aes128_gcm_init(&ctx, key);
        aes128_gcm_seal(&ctx, nonce, 0, 0, pt, 64, out, tag);
        check("GCM case3 ciphertext(64B)", out, want, 64);
        check("GCM case3 tag(64B)", tag, wanttag, 16);
    }

    /* NIST GCM test case 4: 60-byte plaintext with 20-byte AAD */
    {
        uint8_t pt[60];
        uint8_t aad[20];
        hex2bin("d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a72"
                "1c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39",
                pt, 60);
        hex2bin("feedfacedeadbeeffeedfacedeadbeefabaddad2", aad, 20);
        hex2bin("42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e"
                "21d514b25466931c7d8f6a5aac84aa051ba30b396a0aac973d58e091",
                want, 60);
        hex2bin("5bc94fbc3221a5db94fae95ae7121a47", wanttag, 16);
        aes128_gcm_seal(&ctx, nonce, aad, 20, pt, 60, out, tag);
        check("GCM case4 ciphertext(60B+AAD)", out, want, 60);
        check("GCM case4 tag(60B+AAD)", tag, wanttag, 16);
        uint8_t dec[60];
        int rc = aes128_gcm_open(&ctx, nonce, aad, 20, out, 60, dec, tag);
        if (rc != 0 || memcmp(dec, pt, 60) != 0) {
            printf("FAIL GCM case4 decrypt(with AAD)\n");
            fails++;
        } else {
            printf("PASS GCM case4 decrypt(with AAD)\n");
        }
    }
}

int main(void)
{
    printf("=== x25519 ===\n");
    test_x25519();
    printf("=== aes128-gcm ===\n");
    test_gcm();
    printf(fails == 0 ? "ALL VECTOR TESTS PASS\n" : "VECTOR TESTS FAILED\n");
    return fails == 0 ? 0 : 1;
}
