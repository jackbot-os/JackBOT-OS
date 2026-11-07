#include <stdint.h>
#include <stddef.h>
#include <kernel/part.h>
#include <kernel/ata.h>
#include <kernel/blk.h>
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
    if (!blk_present())
        return -1;
    return blk_read(0, 1, mbr512) == 0 ? 0 : -1;
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
    if (blk_write(0, 1, mbr) != 0)
        return -1;
    return blk_flush();
}

/* ---- whole-disk enumeration: MBR primaries + EBR chain, or GPT ---- */

static uint64_t rd64(const uint8_t *p)
{
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}

/* GPT type GUIDs. The first three fields are stored little-endian in the
   on-disk structure, so the byte order below is the on-disk order. */
static const uint8_t GPT_BASIC_DATA[16] = {
    0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
    0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7
};
static const uint8_t GPT_EFI_SYSTEM[16] = {
    0x28, 0x73, 0x2A, 0xC1, 0x1D, 0xF8, 0xD2, 0x11,
    0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B
};
static const uint8_t GPT_MICROSOFT_RESERVED[16] = {
    0x16, 0xE3, 0xC9, 0xE3, 0x5C, 0x0B, 0xB8, 0x4D,
    0x81, 0x7D, 0xF2, 0xFA, 0x57, 0x5E, 0xB3, 0x02
};

static int guid_eq(const uint8_t *a, const uint8_t *b)
{
    for (int i = 0; i < 16; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}

static int guid_zero(const uint8_t *a)
{
    for (int i = 0; i < 16; i++)
        if (a[i])
            return 0;
    return 1;
}

static int sig8(const uint8_t *p, const char *s)
{
    for (int i = 0; i < 8; i++)
        if (p[i] != (uint8_t)s[i])
            return 0;
    return 1;
}

/* GPT names are UTF-16LE. Only ASCII is carried over; anything else is
   dropped rather than shown as mojibake. */
static void gpt_name(const uint8_t *u16, char *out, int cap)
{
    int o = 0;
    for (int i = 0; i + 1 < 36 && o < cap - 1; i += 2) {
        uint16_t c = (uint16_t)(u16[i] | ((uint16_t)u16[i + 1] << 8));
        if (!c)
            break;
        if (c >= 0x20 && c < 0x7F)
            out[o++] = (char)c;
    }
    out[o] = 0;
}

static int part_scan_gpt(part_reader_t rd, struct part_info *out, int max)
{
    static uint8_t hdr[512];
    static uint8_t ent[512];
    int n = 0;

    if (rd(1, hdr) != 0)
        return 0;
    if (!sig8(hdr, "EFI PART"))
        return 0;

    uint64_t ent_lba = rd64(hdr + 72);
    uint32_t ent_n = rd32(hdr + 80);
    uint32_t ent_sz = rd32(hdr + 84);
    if (ent_sz < 128 || ent_sz > 512 || ent_n == 0 || ent_n > 256)
        return 0;

    int per = (int)(512 / ent_sz);
    for (uint32_t s = 0; (int)(s * (uint32_t)per) < (int)ent_n; s++) {
        if (rd(ent_lba + s, ent) != 0)
            break;
        for (int k = 0; k < per; k++) {
            uint32_t idx = s * (uint32_t)per + (uint32_t)k;
            if ((int)idx >= (int)ent_n || n >= max)
                break;
            const uint8_t *e = ent + (size_t)k * ent_sz;
            if (guid_zero(e))
                continue;
            uint64_t first = rd64(e + 32);
            uint64_t last = rd64(e + 40);
            if (last < first)
                continue;
            struct part_info *pi = &out[n];
            for (unsigned i = 0; i < sizeof *pi; i++)
                ((uint8_t *)pi)[i] = 0;
            pi->scheme = PART_SCHEME_GPT;
            pi->start_lba = (uint32_t)first;
            pi->num_sectors = (uint32_t)(last - first + 1);
            pi->is_fat = (uint8_t)(guid_eq(e, GPT_BASIC_DATA) ||
                                   guid_eq(e, GPT_EFI_SYSTEM));
            pi->boot = (uint8_t)(guid_eq(e, GPT_EFI_SYSTEM) ? 0x80 : 0);
            if (guid_eq(e, GPT_MICROSOFT_RESERVED))
                pi->is_fat = 0;
            gpt_name(e + 56, pi->name, (int)sizeof pi->name);
            n++;
        }
    }
    return n;
}

static int part_scan_mbr(part_reader_t rd, struct part_info *out, int max)
{
    static uint8_t sec[512];
    int n = 0;
    uint32_t ext_base[4];
    int ext_n = 0;

    if (rd(0, sec) != 0)
        return 0;

    for (int i = 0; i < 4 && n < max; i++) {
        const uint8_t *en = sec + TABLE_OFFSET + i * ENTRY_BYTES;
        uint8_t type = en[4];
        uint32_t start = rd32(en + 8);
        uint32_t num = rd32(en + 12);
        if (type == 0 || num == 0)
            continue;
        if (part_type_is_extended(type)) {
            if (ext_n < 4)
                ext_base[ext_n++] = start;
            struct part_info *pi = &out[n++];
            for (unsigned b = 0; b < sizeof *pi; b++)
                ((uint8_t *)pi)[b] = 0;
            pi->scheme = PART_SCHEME_MBR;
            pi->type = type;
            pi->start_lba = start;
            pi->num_sectors = num;
            continue;
        }
        struct part_info *pi = &out[n++];
        for (unsigned b = 0; b < sizeof *pi; b++)
            ((uint8_t *)pi)[b] = 0;
        pi->scheme = PART_SCHEME_MBR;
        pi->boot = en[0];
        pi->type = type;
        pi->start_lba = start;
        pi->num_sectors = num;
        pi->is_fat = (uint8_t)part_type_is_fat(type);
    }

    /* Walk each extended partition's EBR chain: entry 0 is the logical
       partition (relative to the EBR), entry 1 points at the next EBR
       (relative to the extended base). Stop on a repeat or after 8 links
       so a corrupt chain cannot spin forever. */
    for (int b = 0; b < ext_n; b++) {
        uint32_t ebr = ext_base[b];
        uint32_t prev = 0;
        for (int link = 0; link < 8 && n < max; link++) {
            static uint8_t eb[512];
            if (ebr == 0 || ebr == prev)
                break;
            if (rd(ebr, eb) != 0)
                break;
            if (rd16(eb + 510) != PART_MBR_SIGNATURE)
                break;
            prev = ebr;

            const uint8_t *e0 = eb + TABLE_OFFSET;
            uint8_t t0 = e0[4];
            uint32_t rel0 = rd32(e0 + 8);
            uint32_t cnt0 = rd32(e0 + 12);
            if (t0 != 0 && cnt0 != 0) {
                struct part_info *pi = &out[n++];
                for (unsigned q = 0; q < sizeof *pi; q++)
                    ((uint8_t *)pi)[q] = 0;
                pi->scheme = PART_SCHEME_MBR;
                pi->boot = e0[0];
                pi->type = t0;
                pi->start_lba = ebr + rel0;
                pi->num_sectors = cnt0;
                pi->is_logical = 1;
                pi->is_fat = (uint8_t)part_type_is_fat(t0);
            }

            const uint8_t *e1 = eb + TABLE_OFFSET + ENTRY_BYTES;
            if (!part_type_is_extended(e1[4]))
                break;
            uint32_t next_rel = rd32(e1 + 8);
            if (next_rel == 0)
                break;
            ebr = ext_base[b] + next_rel;
        }
    }
    return n;
}

int part_scan_reader(part_reader_t rd, struct part_info *out, int max)
{
    static uint8_t sec[512];
    int is_gpt = 0;

    if (!rd || !out || max <= 0)
        return 0;
    if (rd(0, sec) != 0)
        return 0;
    if (rd16(sec + 510) != PART_MBR_SIGNATURE)
        return 0;

    for (int i = 0; i < 4; i++) {
        const uint8_t *en = sec + TABLE_OFFSET + i * ENTRY_BYTES;
        if (en[4] == 0xEE)
            is_gpt = 1;
    }
    if (is_gpt) {
        int n = part_scan_gpt(rd, out, max);
        if (n > 0)
            return n;
        /* Fall back to the protective MBR view if the GPT header is bad. */
    }
    return part_scan_mbr(rd, out, max);
}

static int blk_reader(uint64_t lba, uint8_t *out512)
{
    return blk_read(lba, 1, out512) == 0 ? 0 : -1;
}

int part_scan(struct part_info *out, int max)
{
    if (!blk_present())
        return 0;
    return part_scan_reader(blk_reader, out, max);
}

const char *part_scheme_name(uint8_t scheme)
{
    return scheme == PART_SCHEME_GPT ? "GPT" : "MBR";
}
