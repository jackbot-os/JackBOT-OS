#include <stdint.h>
#include <kernel/blk.h>
#include <kernel/part.h>
#include <kernel/fsprobe.h>
#include <kernel/kstring.h>

static void scpy(char *dst, const char *src, int cap)
{
    int i = 0;
    if (!dst || cap < 1)
        return;
    while (src && src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

/* Read sector 0 of a partition and work out what filesystem lives there.
   This is pure signature sniffing - it never assumes a partition is FAT just
   because the MBR type byte says so, which is how a mislabelled partition used
   to look "supported" and then fail on mount. */

static void sniff(const uint8_t *sec, char *kind, int cap)
{
    /* boot-sector strings sit at fixed offsets */
    if (memcmp(sec + 3, "EXFAT", 5) == 0) {
        scpy(kind, "exFAT", cap);
        return;
    }
    if (memcmp(sec + 3, "NTFS", 4) == 0) {
        scpy(kind, "NTFS", cap);
        return;
    }
    if (memcmp(sec + 82, "FAT32", 5) == 0) {
        scpy(kind, "FAT32", cap);
        return;
    }
    if (memcmp(sec + 54, "FAT16", 5) == 0) {
        scpy(kind, "FAT16", cap);
        return;
    }
    if (memcmp(sec + 54, "FAT12", 5) == 0) {
        scpy(kind, "FAT12", cap);
        return;
    }
    /* ext2/3/4 superblock magic 0x53EF at offset 0x438 */
    if (sec[0x438] == 0x53 && sec[0x439] == 0xEF) {
        scpy(kind, "ext2/3/4", cap);
        return;
    }
    /* HFS+ volume header magic */
    if (memcmp(sec + 0x400, "H+", 2) == 0) {
        scpy(kind, "HFS+", cap);
        return;
    }
    if (memcmp(sec + 0x420, "HX", 2) == 0) {
        scpy(kind, "HFSX", cap);
        return;
    }
    /* APFS container superblock magic 'NXSB' */
    if (memcmp(sec + 0x20, "NXSB", 4) == 0) {
        scpy(kind, "APFS", cap);
        return;
    }
    /* ISO 9660 primary volume descriptor starts at 0x8001 */
    if (memcmp(sec + 1, "CD001", 5) == 0) {
        scpy(kind, "ISO9660", cap);
        return;
    }
    if (sec[0x1FE] == 0x55 && sec[0x1FF] == 0xAA) {
        scpy(kind, "raw/no-FS", cap);
        return;
    }
    scpy(kind, "?", cap);
}

int fsp_part_kind(uint32_t start_lba, char *kind, int cap)
{
    static uint8_t sec[512];
    if (!kind || cap < 2)
        return -1;
    scpy(kind, "?", cap);
    if (!blk_present())
        return -1;
    if (blk_read(start_lba, 1, sec) != 0)
        return -1;
    sniff(sec, kind, cap);
    return 0;
}

int fsp_scan(struct fsp_entry *out, int max)
{
    struct part_info parts[PART_SCAN_MAX];
    int n, m = 0;

    if (!out || max <= 0)
        return -1;
    if (!blk_present())
        return -1;

    n = part_scan(parts, PART_SCAN_MAX);
    if (n <= 0)
        n = 0;

    for (int i = 0; i < n && m < max; i++) {
        out[m].part = parts[i];
        fsp_part_kind(parts[i].start_lba, out[m].kind, FSP_KIND_MAX);
        out[m].mountable =
            (strcmp(out[m].kind, "FAT32") == 0 ||
             strcmp(out[m].kind, "FAT16") == 0 ||
             strcmp(out[m].kind, "FAT12") == 0) ? 1 : 0;
        m++;
    }
    return m;
}
