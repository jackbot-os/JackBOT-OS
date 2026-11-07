#ifndef JACKBOT_X25519_H
#define JACKBOT_X25519_H

#include <stdint.h>

/* Curve25519 (X25519) Diffie-Hellman for the TLS 1.2 ECDHE key exchange.
   Self-contained, no external crypto library, fixed-time Montgomery ladder. */

int x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32]);
int x25519_base(uint8_t out[32], const uint8_t scalar[32]);

#endif
