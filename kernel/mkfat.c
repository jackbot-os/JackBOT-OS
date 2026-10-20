#include <stdint.h>
#include <stddef.h>
#include <kernel/mkfat.h>
#include <kernel/ata.h>
#include <kernel/serial.h>

#define BPS 512
#define SPC 8
#define RESERVED 32
#define NFATS 2
#define ROOT_CLUSTER 2
#define BULK_SECTORS 32

static uint8_t bulk[BPS * BULK_SECTORS];
static uint8_t one[BPS];

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
}
static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF;
    p[3] = (v >> 24) & 0xFF;
}

static void put_name(uint8_t *dst, const char *src, int n)
{
    int i;
    for (i = 0; i < n; i++)
        dst[i] = ' ';
    for (i = 0; i < n && src[i]; i++)
        dst[i] = (uint8_t)src[i];
}

static void zero_range(uint32_t lba, uint32_t sectors)
{
    for (int i = 0; i < BPS * BULK_SECTORS; i++)
        bulk[i] = 0;
    while (sectors) {
        uint8_t n = sectors > BULK_SECTORS ? BULK_SECTORS : sectors;
        ata_write(lba, n, bulk);
        lba += n;
        sectors -= n;
    }
}

static uint32_t calc_fat_size(uint32_t total)
{
    uint32_t fat_size = 1;
    uint32_t entries_per_sec = BPS / 4;
    for (int it = 0; it < 16; it++) {
        int64_t data_sec =
            (int64_t)total - RESERVED - (int64_t)NFATS * fat_size;
        if (data_sec < 0)
            data_sec = 0;
        uint32_t clusters = (uint32_t)data_sec / SPC;
        uint32_t need =
            (clusters + ROOT_CLUSTER + entries_per_sec - 1) / entries_per_sec;
        if (need == fat_size)
            break;
        fat_size = need;
    }
    return fat_size;
}

static void build_boot(uint8_t *b, uint32_t hidden, uint32_t total,
                       uint32_t fat_size, uint32_t serial, const char *label)
{
    for (int i = 0; i < BPS; i++)
        b[i] = 0;
    b[0] = 0xEB;
    b[1] = 0x58;
    b[2] = 0x90;
    put_name(b + 3, "MSDOS5.0", 8);
    wr16(b + 11, BPS);
    b[13] = SPC;
    wr16(b + 14, RESERVED);
    b[16] = NFATS;
    wr16(b + 17, 0);
    wr16(b + 19, 0);
    b[21] = 0xF8;
    wr16(b + 22, 0);
    wr16(b + 24, 63);
    wr16(b + 26, 255);
    wr32(b + 28, hidden);
    wr32(b + 32, total);
    wr32(b + 36, fat_size);
    wr16(b + 40, 0);
    wr16(b + 42, 0);
    wr32(b + 44, ROOT_CLUSTER);
    wr16(b + 48, 1);
    wr16(b + 50, 6);
    b[64] = 0x80;
    b[65] = 0;
    b[66] = 0x29;
    wr32(b + 67, serial);
    put_name(b + 71, label, 11);
    put_name(b + 82, "FAT32   ", 8);
    wr16(b + 510, 0xAA55);
}

static void build_fsinfo(uint8_t *b)
{
    for (int i = 0; i < BPS; i++)
        b[i] = 0;
    wr32(b + 0, 0x41615252);
    wr32(b + 484, 0x61417272);
    wr32(b + 488, 0xFFFFFFFF);
    wr32(b + 492, 0xFFFFFFFF);
    wr16(b + 510, 0xAA55);
}

static void build_fat_head(uint8_t *b)
{
    for (int i = 0; i < BPS; i++)
        b[i] = 0;
    b[0] = 0xF8;
    b[1] = 0xFF;
    b[2] = 0xFF;
    b[3] = 0x0F;
    b[4] = 0xFF;
    b[5] = 0xFF;
    b[6] = 0xFF;
    b[7] = 0x0F;
    b[8] = 0xFF;
    b[9] = 0xFF;
    b[10] = 0xFF;
    b[11] = 0x0F;
}

int mkfat_format(uint32_t start_lba, uint32_t total_sectors,
                 const char *label)
{
    if (!ata_present() || total_sectors < RESERVED + 4096)
        return -1;

    uint32_t fat_size = calc_fat_size(total_sectors);
    uint32_t fat_start = RESERVED;
    uint32_t data_start = RESERVED + NFATS * fat_size;

    serial_puts("[MKFS] start=");
    serial_puthex(start_lba);
    serial_puts(" sectors=");
    serial_puthex(total_sectors);
    serial_puts(" fatsz=");
    serial_puthex(fat_size);
    serial_putc('\n');

    zero_range(start_lba, RESERVED);

    build_boot(one, start_lba, total_sectors, fat_size, 0x4A424F53,
               label ? label : "JACKBOT SYS");
    if (ata_write(start_lba + 0, 1, one) != 0)
        return -1;
    if (ata_write(start_lba + 6, 1, one) != 0)
        return -1;

    build_fsinfo(one);
    if (ata_write(start_lba + 1, 1, one) != 0)
        return -1;
    if (ata_write(start_lba + 7, 1, one) != 0)
        return -1;

    zero_range(start_lba + fat_start, NFATS * fat_size);

    build_fat_head(one);
    if (ata_write(start_lba + fat_start, 1, one) != 0)
        return -1;
    if (ata_write(start_lba + fat_start + fat_size, 1, one) != 0)
        return -1;

    uint32_t root_lba = start_lba + data_start;
    zero_range(root_lba, SPC);

    ata_flush_cache();
    serial_puts("[MKFS] FAT32 formatted, root cluster at ");
    serial_puthex(root_lba);
    serial_putc('\n');
    return 0;
}
