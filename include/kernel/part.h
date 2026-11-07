#ifndef KERNEL_PART_H
#define KERNEL_PART_H
#include <stdint.h>

#define PART_TYPE_FAT12     0x01
#define PART_TYPE_FAT16S    0x04
#define PART_TYPE_EXT       0x05
#define PART_TYPE_FAT16     0x06
#define PART_TYPE_NTFS      0x07
#define PART_TYPE_FAT32_CHS 0x0B
#define PART_TYPE_FAT32     0x0C
#define PART_TYPE_FAT16_LBA 0x0E
#define PART_TYPE_EXT_LBA   0x0F
#define PART_TYPE_LINUXSWAP 0x82
#define PART_TYPE_LINUX     0x83
#define PART_ALIGN_LBA 2048
#define PART_MAX 4
#define PART_MBR_SIGNATURE 0xAA55

#define PART_SYS 0
#define PART_DATA 1

/* A partition found by scanning the whole disk: primary MBR slots, every
   logical partition behind an extended partition, or GPT entries. */
#define PART_SCAN_MAX 24
#define PART_SCHEME_MBR 0
#define PART_SCHEME_GPT 1

struct part_info {
    uint8_t  scheme;      /* PART_SCHEME_MBR / PART_SCHEME_GPT */
    uint8_t  boot;        /* 0x80 when flagged bootable */
    uint8_t  type;        /* MBR type byte, or 0 for GPT */
    uint8_t  is_logical;  /* MBR logical partition (inside extended) */
    uint8_t  is_fat;      /* recognised FAT / GPT basic-data */
    uint32_t start_lba;
    uint32_t num_sectors;
    char     name[40];    /* GPT partition name, UTF-8 best effort */
};

/* Enumerate every partition on the disk, MBR (with EBR chain) or GPT.
   part_scan_reader() takes a sector-0..N reader so the same parser can be
   driven by the ATA driver, the AHCI driver, or a test harness. */
typedef int (*part_reader_t)(uint64_t lba, uint8_t *out512);
int part_scan(struct part_info *out, int max);
int part_scan_reader(part_reader_t rd, struct part_info *out, int max);
const char *part_scheme_name(uint8_t scheme);

struct part_entry {
    uint8_t boot;
    uint8_t type;
    uint32_t start_lba;
    uint32_t num_sectors;
};

struct part_layout {
    int count;
    struct part_entry e[PART_MAX];
};

int part_read_mbr(uint8_t *mbr512);
int part_parse(const uint8_t *mbr512, struct part_layout *out);
const char *part_type_name(uint8_t type);
int part_type_is_fat(uint8_t type);
int part_type_is_extended(uint8_t type);
int part_find_system(const struct part_layout *pl, uint32_t *start,
                     uint32_t *sectors);
int part_plan(uint64_t total_sectors, struct part_layout *out);
/* sys_mb == 0 -> automatic (3/4 rule); otherwise requested system partition
   size in MiB, clamped to a sane minimum and alignment. */
int part_plan_custom(uint64_t total_sectors, uint32_t sys_mb,
                     struct part_layout *out);
uint32_t part_min_sys_mb(void);
void part_encode(const struct part_layout *pl, const uint8_t *bootcode440,
                 uint8_t *mbr512);
int part_write_mbr(const uint8_t *bootcode440, const struct part_layout *pl);

#endif
