#include <stdint.h>
#include <stddef.h>
#include <kernel/part.h>
#include <kernel/ata.h>
#include <kernel/serial.h>

#define MBR_BYTES 512
#define TABLE_OFFSET 446
#define ENTRY_BYTES 16
#define HPC 255
#define SPT 63

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)(p[0] | ((uint32_t)p[1] << 8) |
                      ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}
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

static void lba_to_chs(uint32_t lba, uint8_t chs[3])
{
    uint32_t cyl = lba / (SPT * HPC);
    uint32_t rem = lba % (SPT * HPC);
    uint32_t head = rem / SPT;
    uint32_t sector = (rem % SPT) + 1;
    if (cyl > 1023) {
        chs[0] = 0xFE;
        chs[1] = 0xFF;
        chs[2] = 0xFF;
    } else {
        chs[0] = (uint8_t)head;
        chs[1] = (uint8_t)(sector | ((cyl >> 2) & 0xC0));
        chs[2] = (uint8_t)(cyl & 0xFF);
    }
}

int part_read_mbr(uint8_t *mbr512)
{
    if (!ata_present())
        return -1;
    return ata_read(0, 1, mbr512);
}

int part_parse(const uint8_t *mbr512, struct part_layout *out)
{
    out->count = 0;
    if (rd16(mbr512 + 510) != PART_MBR_SIGNATURE)
        return -1;

    for (int i = 0; i < PART_MAX; i++) {
        const uint8_t *en = mbr512 + TABLE_OFFSET + i * ENTRY_BYTES;
        uint8_t type = en[4];
        if (type == 0)
            continue;
        uint32_t start = rd32(en + 8);
        uint32_t num = rd32(en + 12);
        if (num == 0)
            continue;
        struct part_entry *e = &out->e[out->count++];
        e->boot = en[0];
        e->type = type;
        e->start_lba = start;
        e->num_sectors = num;
    }
    return 0;
}

int part_type_is_fat(uint8_t type)
{
    return type == PART_TYPE_FAT12 || type == PART_TYPE_FAT16S ||
           type == PART_TYPE_FAT16 || type == PART_TYPE_FAT32_CHS ||
           type == PART_TYPE_FAT32 || type == PART_TYPE_FAT16_LBA;
}

int part_type_is_extended(uint8_t type)
{
    return type == PART_TYPE_EXT || type == PART_TYPE_EXT_LBA;
}

const char *part_type_name(uint8_t type)
{
    switch (type) {
    case PART_TYPE_FAT12:     return "FAT12";
    case PART_TYPE_FAT16S:    return "FAT16";
    case PART_TYPE_FAT16:     return "FAT16";
    case PART_TYPE_FAT16_LBA: return "FAT16 LBA";
    case PART_TYPE_FAT32_CHS: return "FAT32";
    case PART_TYPE_FAT32:     return "FAT32 LBA";
    case PART_TYPE_NTFS:      return "NTFS/exFAT";
    case PART_TYPE_EXT:
    case PART_TYPE_EXT_LBA:   return "Extended";
    case PART_TYPE_LINUXSWAP: return "Linux Swap";
    case PART_TYPE_LINUX:     return "Linux";
    case 0x00:                return "Empty";
    default:                  return "Unknown";
    }
}

int part_find_system(const struct part_layout *pl, uint32_t *start,
                     uint32_t *sectors)
{
    for (int i = 0; i < pl->count; i++) {
        const struct part_entry *e = &pl->e[i];
        if (e->type == PART_TYPE_FAT32 && e->boot == 0x80) {
            if (start)
                *start = e->start_lba;
            if (sectors)
                *sectors = e->num_sectors;
            return 0;
        }
    }
    for (int i = 0; i < pl->count; i++) {
        const struct part_entry *e = &pl->e[i];
        if (e->type == PART_TYPE_FAT32) {
            if (start)
                *start = e->start_lba;
            if (sectors)
                *sectors = e->num_sectors;
            return 0;
        }
    }
    return -1;
}

uint32_t part_min_sys_mb(void)
{
    return 128;
}

int part_plan_custom(uint64_t total_sectors, uint32_t sys_mb,
                     struct part_layout *out)
{
    out->count = 0;
    if (total_sectors < (uint64_t)(PART_ALIGN_LBA + 8192))
        return -1;

    uint32_t usable = (uint32_t)(total_sectors - PART_ALIGN_LBA);
    uint32_t sys;
    if (sys_mb == 0) {
        sys = (usable * 3) / 4;
        sys -= sys % PART_ALIGN_LBA;
        if (sys < 8192)
            sys = 8192;
    } else {
        uint64_t want = (uint64_t)sys_mb * 2048;
        want -= want % PART_ALIGN_LBA;
        sys = (uint32_t)want;
        uint32_t min_sys = part_min_sys_mb() * 2048;
        if (sys < min_sys)
            sys = min_sys;
        if (sys > usable - 8192) {
            uint32_t alt = usable - 8192;
            alt -= alt % PART_ALIGN_LBA;
            if (alt >= min_sys)
                sys = alt;
            else {
                sys = usable;
                sys -= sys % PART_ALIGN_LBA;
            }
        }
    }

    uint32_t p2_start = PART_ALIGN_LBA + sys;
    uint32_t p2_num = (uint32_t)(total_sectors - p2_start);

    struct part_entry *sys_e = &out->e[0];
    sys_e->boot = 0x80;
    sys_e->type = PART_TYPE_FAT32;
    sys_e->start_lba = PART_ALIGN_LBA;
    sys_e->num_sectors = sys;
    out->count = 1;

    if (p2_num >= 8192) {
        p2_num -= p2_num % PART_ALIGN_LBA;
        if (p2_num >= 8192) {
            struct part_entry *data_e = &out->e[1];
            data_e->boot = 0x00;
            data_e->type = PART_TYPE_FAT32;
            data_e->start_lba = p2_start;
            data_e->num_sectors = p2_num;
            out->count = 2;
        }
    }
    return 0;
}

int part_plan(uint64_t total_sectors, struct part_layout *out)
{
    return part_plan_custom(total_sectors, 0, out);
}

void part_encode(const struct part_layout *pl, const uint8_t *bootcode440,
                 uint8_t *mbr512)
{
    for (int i = 0; i < MBR_BYTES; i++)
        mbr512[i] = 0;

    if (bootcode440) {
        for (int i = 0; i < 440; i++)
            mbr512[i] = bootcode440[i];
    }

    mbr512[440] = 0x00;
    mbr512[441] = 0x00;
    mbr512[442] = 0x00;
    mbr512[443] = 0x00;
    mbr512[444] = 0x00;
    mbr512[445] = 0x00;

    for (int i = 0; i < pl->count; i++) {
        const struct part_entry *e = &pl->e[i];
        uint8_t *en = mbr512 + TABLE_OFFSET + i * ENTRY_BYTES;
        en[0] = e->boot;
        uint8_t schs[3], echs[3];
        lba_to_chs(e->start_lba, schs);
        lba_to_chs(e->start_lba + e->num_sectors - 1, echs);
        en[1] = schs[0];
        en[2] = schs[1];
        en[3] = schs[2];
        en[4] = e->type;
        en[5] = echs[0];
        en[6] = echs[1];
        en[7] = echs[2];
        wr32(en + 8, e->start_lba);
        wr32(en + 12, e->num_sectors);
    }

    wr16(mbr512 + 510, PART_MBR_SIGNATURE);
}

int part_write_mbr(const uint8_t *bootcode440, const struct part_layout *pl)
{
    static uint8_t mbr[MBR_BYTES];
    part_encode(pl, bootcode440, mbr);
    if (ata_write(0, 1, mbr) != 0)
        return -1;
    return ata_flush_cache();
}
