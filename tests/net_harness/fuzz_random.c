/* Random malformed-frame fuzz under ASAN for the real kernel net.c.
   Includes net.c to reach the static ip_handle. Local VM only, no network. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../kernel/net.c"

static uint32_t rng = 0x2468ACE0u;
static uint32_t rnd(void)
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}

int main(void)
{
    uint8_t frame[1700];
    int rounds = 0;
    for (int trial = 0; trial < 4000; trial++) {
        uint16_t flen = (uint16_t)(rnd() % 1500);
        for (int i = 0; i < flen; i++)
            frame[i] = (uint8_t)rnd();
        /* keep ethertype IP most of the time so ip_handle runs */
        if (rnd() & 1) { frame[12] = 0x08; frame[13] = 0x00; }
        ip_handle(frame, flen);
        rounds++;

        /* exercise pure helpers with random byte strings */
        uint8_t b[8];
        for (int i = 0; i < 8; i++) b[i] = (uint8_t)rnd();
        net_checksum(b, (size_t)(rnd() % 9));
        char s[16];
        net_ip_str(b, s);
        s[0] = s[0];
    }
    printf("NET FUZZ OK: %d frames processed, no crash under ASAN\n", rounds);
    return 0;
}
