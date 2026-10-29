/* Offline stub for the JackBOT OS FAT32 harness. Local VM only.
   A flat in-memory array stands in for the ATA block device; the real
   kernel fat32.c / protect.c / persist.c are compiled unchanged. */
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <kernel/ata.h>
#include <kernel/heap.h>
#include <kernel/serial.h>
#include <kernel/persist.h>

#define BPS 512u
#define SPC 8u
#define RESERVED 32u
#define NFATS 2u
#define ROOT_CLUSTER 2u

static uint8_t *disk;
static uint32_t total_sectors;
static int present;
static uint64_t tick;

static uint32_t calc_fat_size(uint32_t total)
{
    uint32_t fat_size = 1;
    uint32_t per_sec = BPS / 4;
    for (int it = 0; it < 16; it++) {
        int64_t data_sec = (int64_t)total - RESERVED - (int64_t)NFATS * fat_size;
        if (data_sec < 0) data_sec = 0;
        uint32_t clusters = (uint32_t)data_sec / SPC;
        uint32_t need = (clusters + ROOT_CLUSTER + per_sec - 1) / per_sec;
        if (need == fat_size) break;
        fat_size = need;
    }
    return fat_size;
}

void stub_disk_init(uint32_t sectors)
{
    total_sectors = sectors;
    if (disk) free(disk);
    disk = (uint8_t *)calloc(sectors, BPS);
    present = disk != 0;
}

uint32_t stub_fat_size(void)
{
    return calc_fat_size(total_sectors);
}

void stub_disk_format(void)
{
    uint32_t fat_size = calc_fat_size(total_sectors);
    uint32_t data_start = RESERVED + NFATS * fat_size;
    uint8_t b[BPS];
    memset(b, 0, BPS);
    b[0] = 0xEB; b[1] = 0x58; b[2] = 0x90;
    memcpy(b + 3, "MSDOS5.0", 8);
    b[11] = BPS & 0xFF; b[12] = BPS >> 8;
    b[13] = SPC;
    b[14] = RESERVED & 0xFF; b[15] = RESERVED >> 8;
    b[16] = NFATS;
    b[21] = 0xF8;
    b[32] = total_sectors & 0xFF;
    b[33] = (total_sectors >> 8) & 0xFF;
    b[34] = (total_sectors >> 16) & 0xFF;
    b[35] = (total_sectors >> 24) & 0xFF;
    b[36] = fat_size & 0xFF;
    b[37] = (fat_size >> 8) & 0xFF;
    b[38] = (fat_size >> 16) & 0xFF;
    b[39] = (fat_size >> 24) & 0xFF;
    b[44] = ROOT_CLUSTER & 0xFF;
    b[45] = (ROOT_CLUSTER >> 8) & 0xFF;
    b[46] = (ROOT_CLUSTER >> 16) & 0xFF;
    b[47] = (ROOT_CLUSTER >> 24) & 0xFF;
    b[510] = 0x55; b[511] = 0xAA;
    ata_write(0, 1, b);
    ata_write(6, 1, b);

    memset(b, 0, BPS);
    b[0] = 0xF8; b[1] = 0xFF; b[2] = 0xFF; b[3] = 0x0F;
    b[4] = 0xFF; b[5] = 0xFF; b[6] = 0xFF; b[7] = 0x0F;
    b[8] = 0xFF; b[9] = 0xFF; b[10] = 0xFF; b[11] = 0x0F;
    ata_write(RESERVED, 1, b);
    ata_write(RESERVED + fat_size, 1, b);

    (void)data_start;
}

int ata_init(void) { return present ? 0 : -1; }
int ata_present(void) { return present; }
uint64_t ata_sector_count(void) { return total_sectors; }

int ata_read(uint64_t lba, uint8_t count, void *buf)
{
    if (!present || !disk || !buf || count == 0) return -1;
    if (lba > total_sectors || (uint64_t)lba + count > total_sectors) return -1;
    memcpy(buf, disk + (size_t)lba * BPS, (size_t)count * BPS);
    return 0;
}

int ata_write(uint64_t lba, uint8_t count, const void *buf)
{
    if (!present || !disk || !buf || count == 0) return -1;
    if (lba > total_sectors || (uint64_t)lba + count > total_sectors) return -1;
    memcpy(disk + (size_t)lba * BPS, buf, (size_t)count * BPS);
    return 0;
}

int ata_flush_cache(void) { return 0; }

/* ---- heap backed by libc ---- */
void heap_init(void) {}
void *kmalloc(size_t size) { return malloc(size ? size : 1); }
void kfree(void *ptr) { free(ptr); }
void *kcalloc(size_t n, size_t size) { return calloc(n ? n : 1, size ? size : 1); }
void *krealloc(void *ptr, size_t size) { return realloc(ptr, size ? size : 1); }
uint64_t heap_bytes(void) { return 0; }

/* ---- serial to stdout ---- */
void serial_init(void) {}
void serial_putc(char c) { putchar(c); }
void serial_puts(const char *s) { fputs(s, stdout); }
void serial_puthex(uint64_t v)
{
    char buf[17];
    for (int i = 15; i >= 0; i--) {
        uint8_t n = (uint8_t)(v >> (i * 4));
        buf[15 - i] = (n < 10) ? (char)('0' + n) : (char)('a' + n - 10);
    }
    fwrite(buf, 1, 16, stdout);
}

uint64_t timer_get_ticks(void) { return ++tick; }

/* kstring symbols (strchr/strcpy/...) resolve to libc in userland;
   fat32.c only calls strchr and strcpy, both provided by libc. */
