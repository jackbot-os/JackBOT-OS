/* Random create/overwrite/read/delete fuzz under ASAN for the real
 * kernel fat32.c. Local VM only; in-memory disk, no real hardware, no network. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <kernel/fat32.h>
#include <kernel/protect.h>

extern void stub_disk_init(uint32_t sectors);
extern void stub_disk_format(void);

static uint32_t rng = 0x51EDC0DEu;
static uint32_t rnd(void)
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}

#define MAXF 40
static uint32_t flen[MAXF];
static uint8_t fdata[MAXF][6000];

int main(void)
{
    int ops = 0, files = 0;
    for (int round = 0; round < 200; round++) {
        stub_disk_init(8192);
        stub_disk_format();
        if (fat_mount_part(0, 8192) != 0) {
            printf("FAIL: round %d mount\n", round);
            return 1;
        }
        protect_set_actor(ACTOR_ADMIN);
        memset(flen, 0, sizeof flen);

        for (int op = 0; op < 300; op++) {
            uint32_t f = rnd() % MAXF;
            char nm[16];
            snprintf(nm, sizeof nm, "F%02d.TXT", f);
            uint32_t action = rnd() % 4;

            if (action == 0) {
                uint32_t len = rnd() % 6001;
                for (uint32_t i = 0; i < len; i++)
                    fdata[f][i] = (uint8_t)(rnd() & 0xff);
                if (fat_write_file(nm, fdata[f], len) == 0) {
                    flen[f] = len;
                    files++;
                }
            } else if (action == 1) {
                uint8_t out[6000];
                uint32_t got = 0;
                if (fat_read_file(nm, out, sizeof out, &got) == 0) {
                    if (got == flen[f] && memcmp(out, fdata[f], got) != 0) {
                        printf("FAIL: round %d F%02d data mismatch\n", round, f);
                        return 1;
                    }
                }
            } else if (action == 2) {
                fat_delete(nm);
                flen[f] = 0;
            } else {
                char dn[12];
                snprintf(dn, sizeof dn, "D%02d", f % 8);
                fat_mkdir(dn);
                char p[32];
                snprintf(p, sizeof p, "%s/%s", dn, nm);
                uint32_t len = rnd() % 400;
                uint8_t tmp[400];
                for (uint32_t i = 0; i < len; i++) tmp[i] = (uint8_t)(rnd() & 0xff);
                fat_write_file(p, tmp, len);
            }
            ops++;
        }
        fat_unmount();
    }
    printf("FAT FUZZ OK: %d rounds, %d operations, %d file writes, no crash under ASAN\n",
           200, ops, files);
    return 0;
}
