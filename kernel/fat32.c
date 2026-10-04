#include <stdint.h>
#include <stddef.h>
#include <kernel/fat32.h>
#include <kernel/ata.h>
#include <kernel/heap.h>
#include <kernel/serial.h>

#define EOC 0x0FFFFFF8

static int is_mounted;
static uint16_t bps;
static uint8_t spc;
static uint16_t reserved;
static uint8_t nfats;
static uint32_t fat_size;
static uint32_t root_cluster;
static uint32_t fat_start;
static uint32_t data_start;
static uint32_t cluster_count;
static uint32_t total_sectors;
static uint8_t *cbuf;
static uint8_t *dbuf;
static uint8_t sbuf[512];

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) |
                      ((uint32_t)p[3] << 24));
}
static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF;
    p[3] = (v >> 24) & 0xFF;
}
static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
}
static int mymemcmp(const void *a, const void *b, uint32_t n)
{
    const uint8_t *x = a, *y = b;
    for (uint32_t i = 0; i < n; i++)
        if (x[i] != y[i])
            return x[i] - y[i];
    return 0;
}
static void mymemset(void *a, uint8_t v, uint32_t n)
{
    uint8_t *x = a;
    for (uint32_t i = 0; i < n; i++)
        x[i] = v;
}
static void mymemcpy(void *dst, const void *src, uint32_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    for (uint32_t i = 0; i < n; i++)
        d[i] = s[i];
}

static uint32_t cluster_lba(uint32_t c)
{
    return data_start + (c - 2) * spc;
}
static int read_cluster(uint32_t c, uint8_t *buf)
{
    return ata_read(cluster_lba(c), spc, buf);
}
static int write_cluster(uint32_t c, const uint8_t *buf)
{
    return ata_write(cluster_lba(c), spc, buf);
}

static uint32_t fat_get(uint32_t c)
{
    uint32_t off = (c * 4) % bps;
    uint32_t sec = fat_start + (c * 4) / bps;
    ata_read(sec, 1, sbuf);
    return rd32(sbuf + off) & 0x0FFFFFFF;
}
static void fat_set(uint32_t c, uint32_t v)
{
    uint32_t off = (c * 4) % bps;
    for (uint8_t f = 0; f < nfats; f++) {
        uint32_t sec = reserved + (uint32_t)f * fat_size + (c * 4) / bps;
        ata_read(sec, 1, sbuf);
        uint32_t old = rd32(sbuf + off);
        wr32(sbuf + off, (old & 0xF0000000) | (v & 0x0FFFFFFF));
        ata_write(sec, 1, sbuf);
    }
}

static uint32_t alloc_one(void)
{
    for (uint32_t c = 2; c < cluster_count + 2; c++) {
        if (fat_get(c) == 0) {
            fat_set(c, EOC);
            return c;
        }
    }
    return 0;
}
static uint32_t alloc_chain(uint32_t prev, uint32_t count)
{
    uint32_t first = 0, last = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t nc = alloc_one();
        if (!nc)
            break;
        if (!first)
            first = nc;
        if (last)
            fat_set(last, nc);
        last = nc;
    }
    if (prev && first)
        fat_set(prev, first);
    return first;
}
static void free_chain(uint32_t c)
{
    while (c >= 2 && c < EOC) {
        uint32_t n = fat_get(c);
        fat_set(c, 0);
        c = n;
    }
}

static void fmt_name(const char *in, uint8_t out[11])
{
    mymemset(out, ' ', 11);
    int bi = 0, ei = 0, dot = 0;
    for (const char *p = in; *p; p++) {
        char ch = *p;
        if (ch == '.') {
            dot = 1;
            continue;
        }
        if (ch >= 'a' && ch <= 'z')
            ch -= 32;
        if (!dot) {
            if (bi < 8)
                out[bi++] = (uint8_t)ch;
        } else {
            if (ei < 3)
                out[8 + ei++] = (uint8_t)ch;
        }
    }
}

typedef struct {
    uint32_t cluster;
    uint32_t off;
} dpos_t;

static int dir_find(uint32_t dirc, const uint8_t nm[11], int want_free,
                    dpos_t *dp)
{
    uint32_t c = dirc;
    uint32_t csize = (uint32_t)spc * bps;
    while (c >= 2 && c < EOC) {
        if (read_cluster(c, cbuf) < 0)
            return -1;
        for (uint32_t o = 0; o < csize; o += 32) {
            uint8_t *e = cbuf + o;
            if (want_free) {
                if (e[0] == 0x00 || e[0] == 0xE5) {
                    dp->cluster = c;
                    dp->off = o;
                    return 0;
                }
            } else {
                if (e[0] == 0x00)
                    return -1;
                if (e[0] != 0xE5 && !(e[11] & 0x0F) &&
                    !mymemcmp(e, nm, 11)) {
                    dp->cluster = c;
                    dp->off = o;
                    return 0;
                }
            }
        }
        c = fat_get(c);
    }
    if (want_free) {
        uint32_t nc = alloc_chain(dirc, 1);
        if (!nc)
            return -1;
        mymemset(cbuf, 0, csize);
        write_cluster(nc, cbuf);
        dp->cluster = nc;
        dp->off = 0;
        return 0;
    }
    return -1;
}

int fat_mount(void)
{
    if (!ata_present())
        return -1;
    if (ata_read(0, 1, sbuf) < 0)
        return -1;

    bps = rd16(sbuf + 11);
    spc = sbuf[13];
    reserved = rd16(sbuf + 14);
    nfats = sbuf[16];
    fat_size = rd32(sbuf + 36);
    root_cluster = rd32(sbuf + 44);
    total_sectors = rd32(sbuf + 32);
    if (!total_sectors)
        total_sectors = rd32(sbuf + 32);

    if (bps != 512 || !spc || !nfats || !fat_size)
        return -1;

    fat_start = reserved;
    data_start = reserved + (uint32_t)nfats * fat_size;
    cluster_count = (total_sectors - data_start) / spc;

    cbuf = kmalloc((uint32_t)spc * bps);
    dbuf = kmalloc((uint32_t)spc * bps);
    if (!cbuf || !dbuf)
        return -1;

    is_mounted = 1;
    serial_puts("[FAT] mounted clusters=");
    serial_puthex(cluster_count);
    serial_putc('\n');
    return 0;
}

int fat_mounted(void)
{
    return is_mounted;
}

int fat_exists(const char *name83)
{
    if (!is_mounted)
        return -1;
    uint8_t nm[11];
    dpos_t dp;
    fmt_name(name83, nm);
    return dir_find(root_cluster, nm, 0, &dp) == 0 ? 1 : 0;
}

int fat_write_file(const char *name83, const void *data, uint32_t len)
{
    if (!is_mounted)
        return -1;
    uint8_t nm[11];
    dpos_t dp;
    fmt_name(name83, nm);

    if (dir_find(root_cluster, nm, 0, &dp) == 0) {
        uint8_t *old = cbuf + dp.off;
        uint32_t fc = (uint32_t)rd16(old + 26) |
                      ((uint32_t)rd16(old + 20) << 16);
        write_cluster(dp.cluster, cbuf);
        free_chain(fc);
    }

    if (dir_find(root_cluster, nm, 1, &dp) < 0)
        return -1;

    uint32_t csize = (uint32_t)spc * bps;
    uint32_t need = len ? (len + csize - 1) / csize : 1;
    uint32_t first = alloc_chain(0, need);
    if (!first)
        return -1;

    const uint8_t *src = data;
    uint32_t remain = len;
    uint32_t c = first;
    for (uint32_t i = 0; i < need; i++) {
        mymemset(dbuf, 0, csize);
        uint32_t n = remain > csize ? csize : remain;
        if (n)
            mymemcpy(dbuf, src, n);
        src += n;
        remain -= n;
        write_cluster(c, dbuf);
        c = fat_get(c);
    }

    uint8_t *e = cbuf + dp.off;
    mymemset(e, 0, 32);
    mymemcpy(e, nm, 11);
    e[11] = 0x20;
    wr16(e + 20, (uint16_t)(first >> 16));
    wr16(e + 26, (uint16_t)first);
    wr32(e + 28, len);
    write_cluster(dp.cluster, cbuf);
    return 0;
}

int fat_read_file(const char *name83, void *buf, uint32_t max,
                  uint32_t *outlen)
{
    if (!is_mounted)
        return -1;
    uint8_t nm[11];
    dpos_t dp;
    fmt_name(name83, nm);
    if (dir_find(root_cluster, nm, 0, &dp) < 0)
        return -1;

    uint8_t *e = cbuf + dp.off;
    uint32_t size = rd32(e + 28);
    uint32_t c = (uint32_t)rd16(e + 26) |
                 ((uint32_t)rd16(e + 20) << 16);
    uint32_t csize = (uint32_t)spc * bps;
    uint8_t *dst = buf;
    uint32_t remain = size;
    uint32_t copied = 0;

    while (c >= 2 && c < EOC) {
        if (read_cluster(c, dbuf) < 0)
            return -1;
        uint32_t n = remain > csize ? csize : remain;
        if (copied + n > max)
            n = (max > copied) ? max - copied : 0;
        if (n)
            mymemcpy(dst, dbuf, n);
        dst += n;
        copied += n;
        remain -= (remain > csize ? csize : remain);
        if (!remain)
            break;
        c = fat_get(c);
    }
    if (outlen)
        *outlen = copied;
    return 0;
}

int fat_delete(const char *name83)
{
    if (!is_mounted)
        return -1;
    uint8_t nm[11];
    dpos_t dp;
    fmt_name(name83, nm);
    if (dir_find(root_cluster, nm, 0, &dp) < 0)
        return -1;

    uint8_t *e = cbuf + dp.off;
    uint32_t fc = (uint32_t)rd16(e + 26) |
                  ((uint32_t)rd16(e + 20) << 16);
    free_chain(fc);
    e[0] = 0xE5;
    write_cluster(dp.cluster, cbuf);
    return 0;
}

int fat_mkdir(const char *name83)
{
    if (!is_mounted)
        return -1;
    uint8_t nm[11];
    dpos_t dp;
    fmt_name(name83, nm);
    if (dir_find(root_cluster, nm, 0, &dp) == 0)
        return -1;
    if (dir_find(root_cluster, nm, 1, &dp) < 0)
        return -1;

    uint32_t nc = alloc_chain(0, 1);
    if (!nc)
        return -1;
    uint32_t csize = (uint32_t)spc * bps;
    mymemset(dbuf, 0, csize);
    uint8_t *en = dbuf;
    mymemset(en, ' ', 11);
    en[0] = '.';
    en[11] = 0x10;
    wr16(en + 20, (uint16_t)(nc >> 16));
    wr16(en + 26, (uint16_t)nc);
    uint8_t *en2 = dbuf + 32;
    mymemset(en2, ' ', 11);
    en2[0] = '.';
    en2[1] = '.';
    en2[11] = 0x10;
    wr16(en2 + 20, (uint16_t)(root_cluster >> 16));
    wr16(en2 + 26, (uint16_t)root_cluster);
    write_cluster(nc, dbuf);

    uint8_t *e = cbuf + dp.off;
    mymemset(e, 0, 32);
    mymemcpy(e, nm, 11);
    e[11] = 0x10;
    wr16(e + 20, (uint16_t)(nc >> 16));
    wr16(e + 26, (uint16_t)nc);
    write_cluster(dp.cluster, cbuf);
    return 0;
}

uint32_t fat_total_bytes(void)
{
    if (!is_mounted)
        return 0;
    return cluster_count * (uint32_t)spc * bps;
}

uint32_t fat_free_bytes(void)
{
    if (!is_mounted)
        return 0;
    uint32_t freec = 0;
    uint32_t per = bps / 4;
    for (uint32_t s = 0; s < fat_size; s++) {
        ata_read(fat_start + s, 1, sbuf);
        for (uint32_t i = 0; i < per; i++) {
            uint32_t cl = s * per + i;
            if (cl < 2)
                continue;
            if (cl >= cluster_count + 2)
                break;
            if ((rd32(sbuf + i * 4) & 0x0FFFFFFF) == 0)
                freec++;
        }
    }
    return freec * (uint32_t)spc * bps;
}
