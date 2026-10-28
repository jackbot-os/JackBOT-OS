#ifndef JACKBOT_HMAC_SHA256_H
#define JACKBOT_HMAC_SHA256_H

#include <stdint.h>
#include <stddef.h>

void hmac_sha256(const uint8_t *key, size_t klen,
                 const uint8_t *msg, size_t mlen,
                 uint8_t out[32]);

void hmac_sha256_hex(const uint8_t *key, size_t klen,
                     const uint8_t *msg, size_t mlen,
                     char out[65]);

int hmac_sha256_selftest(void);

#endif
