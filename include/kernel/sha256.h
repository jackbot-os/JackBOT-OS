#ifndef JACKBOT_SHA256_H
#define JACKBOT_SHA256_H

#include <stdint.h>
#include <stddef.h>

void sha256(const uint8_t *data, size_t len, uint8_t out[32]);
void sha256_hex(const uint8_t *data, size_t len, char out[65]);

#endif
