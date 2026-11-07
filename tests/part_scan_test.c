/* Host-side test for the partition scanner in kernel/part.c.
 *
 * The kernel parsers take a sector reader, so the disk image here is a
 * plain in-memory array of 512-byte sectors. Builds on the Debian VM with:
 *   gcc -std=c11 -I include -I build tests/part_scan_test.c kernel/part.c \
 *       -o /tmp/ptest && /tmp/ptest
 *
 * Covers: MBR with two primaries, MBR with an extended partition holding
 * two logical partitions, and a GPT disk (protective MBR + header + entries
 * with an EFI system partition and a named basic-data partition).
 */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kernel/part.h>

/* ---- stubs for the ATA layer that kernel/part.c references ---- */

static uint8_t *g_disk;
static uint64_t g_sectors;

int ata_present(void) { return 1; }
int ata_read(uint64_t lba, uint8_t count, void *buf)
{
    if (lba + count > g_sectors)
        return -1;
    memcpy(buf, g_disk + lba * 512, count * 512);
    return 0;
}
int ata_write(uint64_t lba, uint8_t count, const void *buf)
{
    if (lba + count > g_sectors)
        return -1;
    memcpy(g_disk + lba * 512, buf, count * 512);
    return 0;
}
int ata_flush_cache(void) { return 0; }

/* The kernel's block layer is not linked here; part.c only needs these
   four entry points, backed by the same in-memory image. */
int blk_present(void) { return 1; }
int blk_read(uint64_t lba, uint32_t count, void *buf) { return ata_read(lba, (uint8_t)count, buf); }
int blk_write(uint64_t lba, uint32_t count, const void *buf) { return ata_write(lba, (uint8_t)count, buf); }
int blk_flush(void) { return 0; }

/* ---- helpers to lay out the fake disks ---- */

static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF); p[3] = (uint8_t)((v >> 24) & 0xFF);
}
static void wr64(uint8_t *p, uint64_t v)
{
    wr32(p, (uint32_t)v);
    wr32(p + 4, (uint32_t)(v >> 32));
}

static uint8_t *disk_new(uint64_t sectors)
{
    uint8_t *d = (uint8_t *)calloc((size_t)sectors, 512);
    if (!d) {
        fprintf(stderr, "out of memory\n");
        exit(2);
    }
    g_disk = d;
    g_sectors = sectors;
    return d;
}

static uint8_t *sec(uint8_t *disk, uint64_t lba) { return disk + lba * 512; }

static void mbr_entry(uint8_t *mbr, int i, uint8_t boot, uint8_t type,
                      uint32_t start, uint32_t count)
{
    uint8_t *en = mbr + 446 + i * 16;
    en[0] = boot;
    en[4] = type;
    wr32(en + 8, start);
    wr32(en + 12, count);
}

/* GPT type GUIDs in on-disk byte order. */
static const uint8_t GUID_BASIC[16] = {
    0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
    0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7
};
static const uint8_t GUID_EFI[16] = {
    0x28, 0x73, 0x2A, 0xC1, 0x1D, 0xF8, 0xD2, 0x11,
    0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B
};

static void gpt_entry(uint8_t *e, const uint8_t guid[16], uint64_t first,
                      uint64_t last, const char *name)
{
    memcpy(e, guid, 16);
    wr64(e + 32, first);
    wr64(e + 40, last);
    for (int i = 0; name[i] && i < 35; i++) {
        e[56 + i * 2] = (uint8_t)name[i];
        e[56 + i * 2 + 1] = 0;
    }
}

static void dump(const char *tag, struct part_info *pi, int n)
{
    printf("%s: %d partitions\n", tag, n);
    for (int i = 0; i < n; i++)
        printf("  [%d] %s%s type=0x%02x start=%u count=%u fat=%d boot=%d name=\"%s\"\n",
               i, part_scheme_name(pi[i].scheme),
               pi[i].is_logical ? "/logical" : "", pi[i].type,
               pi[i].start_lba, pi[i].num_sectors, pi[i].is_fat,
               pi[i].boot, pi[i].name);
}

static int fails;

static void expect(const char *what, int got, int want)
{
    if (got != want) {
        printf("FAIL %s: got %d want %d\n", what, got, want);
        fails++;
    } else {
        printf("ok   %s = %d\n", what, got);
    }
}

int main(void)
{
    struct part_info pi[PART_SCAN_MAX];
    int n;

    /* ---- case 1: MBR, two primaries ---- */
    {
        uint8_t *d = disk_new(64 * 1024);
        mbr_entry(sec(d, 0), 0, 0x80, 0x0C, 2048, 40960);
        mbr_entry(sec(d, 0), 1, 0x00, 0x83, 43008, 20480);
        wr16(sec(d, 0) + 510, 0xAA55);

        n = part_scan_reader(0, pi, PART_SCAN_MAX);          /* null reader */
        expect("null reader returns 0", n, 0);

        n = part_scan(pi, PART_SCAN_MAX);
        dump("MBR two primaries", pi, n);
        expect("mbr count", n, 2);
        expect("mbr[0] start", (int)pi[0].start_lba, 2048);
        expect("mbr[0] fat", pi[0].is_fat, 1);
        expect("mbr[0] boot", pi[0].boot, 0x80);
        expect("mbr[1] fat", pi[1].is_fat, 0);
        free(d);
    }

    /* ---- case 2: MBR with extended partition holding two logicals ---- */
    {
        uint8_t *d = disk_new(64 * 1024);
        mbr_entry(sec(d, 0), 0, 0x80, 0x0C, 2048, 20480);
        mbr_entry(sec(d, 0), 1, 0x00, 0x0F, 22528, 40960);   /* extended */
        wr16(sec(d, 0) + 510, 0xAA55);

        /* EBR 1 at 22528: logical at +2048, next EBR at +20480 */
        mbr_entry(sec(d, 22528), 0, 0x00, 0x07, 2048, 4096);
        mbr_entry(sec(d, 22528), 1, 0x00, 0x0F, 20480, 0);
        wr16(sec(d, 22528) + 510, 0xAA55);

        /* EBR 2 at 22528+20480: last logical, no next link */
        mbr_entry(sec(d, 22528 + 20480), 0, 0x00, 0x0C, 2048, 8192);
        wr16(sec(d, 22528 + 20480) + 510, 0xAA55);

        n = part_scan(pi, PART_SCAN_MAX);
        dump("MBR with EBR chain", pi, n);
        expect("ebr total", n, 4);                    /* primary, ext, 2 logical */
        expect("logical1 start", (int)pi[2].start_lba, 22528 + 2048);
        expect("logical1 is_logical", pi[2].is_logical, 1);
        expect("logical2 start", (int)pi[3].start_lba, 22528 + 20480 + 2048);
        expect("logical2 fat", pi[3].is_fat, 1);
        free(d);
    }

    /* ---- case 3: GPT, two entries ---- */
    {
        uint8_t *d = disk_new(128 * 1024);
        mbr_entry(sec(d, 0), 0, 0x00, 0xEE, 1, 200);
        wr16(sec(d, 0) + 510, 0xAA55);

        uint8_t *hdr = sec(d, 1);
        memcpy(hdr, "EFI PART", 8);
        wr32(hdr + 80, 4);        /* number of entries      */
        wr32(hdr + 84, 128);      /* entry size             */
        wr64(hdr + 72, 2);        /* entries start at LBA 2 */

        uint8_t *ents = sec(d, 2);
        gpt_entry(ents + 0, GUID_EFI, 2048, 4095, "EFI System");
        gpt_entry(ents + 128, GUID_BASIC, 4096, 8191, "JACKDATA");

        n = part_scan(pi, PART_SCAN_MAX);
        dump("GPT", pi, n);
        expect("gpt count", n, 2);
        expect("gpt[0] scheme", pi[0].scheme, PART_SCHEME_GPT);
        expect("gpt[0] efi fat", pi[0].is_fat, 1);
        expect("gpt[0] boot", pi[0].boot, 0x80);
        expect("gpt[0] start", (int)pi[0].start_lba, 2048);
        expect("gpt[1] count", (int)pi[1].num_sectors, 8192 - 4096);
        expect("gpt[1] name", (int)strlen(pi[1].name), 8);
        printf("     gpt[1] name text: \"%s\"\n", pi[1].name);
        free(d);
    }

    /* ---- case 4: no signature ---- */
    {
        uint8_t *d = disk_new(8);
        n = part_scan(pi, PART_SCAN_MAX);
        expect("blank disk count", n, 0);
        free(d);
    }

    printf(fails ? "PART TEST FAILED (%d)\n" : "PART TEST PASSED\n", fails);
    return fails ? 1 : 0;
}
