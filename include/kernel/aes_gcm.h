#ifndef JACKBOT_AES_GCM_H
#define JACKBOT_AES_GCM_H

#include <stdint.h>
#include <stddef.h>

/* AES-128 in GCM mode (NIST SP 800-38D), used by the TLS 1.2 record layer
   with a 12-byte nonce (4-byte fixed IV || 8-byte explicit nonce). */

struct aes128_gcm {
    uint8_t round_keys[176];
    uint8_t h[16];
};

void aes128_gcm_init(struct aes128_gcm *ctx, const uint8_t key[16]);
void aes128_gcm_seal(struct aes128_gcm *ctx, const uint8_t nonce[12],
                     const uint8_t *aad, size_t aad_len,
                     const uint8_t *plain, size_t len,
                     uint8_t *out, uint8_t tag[16]);
int aes128_gcm_open(struct aes128_gcm *ctx, const uint8_t nonce[12],
                    const uint8_t *aad, size_t aad_len,
                    const uint8_t *cipher, size_t len,
                    uint8_t *out, const uint8_t tag[16]);

#endif
