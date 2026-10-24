#include <stdint.h>
#include <stddef.h>
#include <kernel/fat32.h>
#include <kernel/ata.h>
#include <kernel/heap.h>
#include <kernel/protect.h>
#include <kernel/persist.h>
#include <kernel/kstring.h>
#include <kernel/serial.h>

#define EOC 0x0FFFFFF8
#define MAX_PATH 64

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
static uint32_t part_offset;
static uint8_t *cbuf;
static uint8_t *dbuf;
static uint8_t sbuf[512];
static uint8_t fache[512];
static uint32_t fache_sec = 0xFFFFFFFFu;

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
    return part_offset + data_start + (c - 2) * spc;
}
static int read_cluster(uint32_t c, uint8_t *buf)
{
    return ata_read(cluster_lba(c), spc, buf);
}
static int write_cluster(uint32_t c, const uint8_t *buf)
{
    return ata_write(cluster_lba(c), spc, buf);
}

static void fache_load(uint32_t sec)
{
    if (sec != fache_sec) {
        ata_read(part_offset + sec, 1, fache);
        fache_sec = sec;
    }
}
static uint32_t fat_get(uint32_t c)
{
    uint32_t off = (c * 4) % bps;
    uint32_t sec = fat_start + (c * 4) / bps;
    fache_load(sec);
    return rd32(fache + off) & 0x0FFFFFFF;
}
static void fat_set(uint32_t c, uint32_t v)
{
    uint32_t off = (c * 4) % bps;
    uint32_t rel = (c * 4) / bps;
    fache_load(fat_start + rel);
    uint32_t old = rd32(fache + off);
    wr32(fache + off, (old & 0xF0000000) | (v & 0x0FFFFFFF));
    for (uint8_t f = 0; f < nfats; f++)
        ata_write(part_offset + fat_start + (uint32_t)f * fat_size + rel,
                  1, fache);
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

static int mkdir_in(uint32_t parent, const uint8_t nm[11])
{
    dpos_t dp;
    if (dir_find(parent, nm, 0, &dp) == 0)
        return -1;
    if (dir_find(parent, nm, 1, &dp) < 0)
        return -1;

    uint32_t nc = alloc_chain(0, 1);
    if (!nc)
        return -1;
    uint32_t csize = (uint32_t)spc * bps;
    mymemset(dbuf, 0, csize);
    uint8_t *a = dbuf;
    mymemset(a, ' ', 11);
    a[0] = '.';
    a[11] = 0x10;
    wr16(a + 20, (uint16_t)(nc >> 16));
    wr16(a + 26, (uint16_t)nc);
    uint8_t *b2 = dbuf + 32;
    mymemset(b2, ' ', 11);
    b2[0] = '.';
    b2[1] = '.';
    b2[11] = 0x10;
    wr16(b2 + 20, (uint16_t)(parent >> 16));
    wr16(b2 + 26, (uint16_t)parent);
    write_cluster(nc, dbuf);

    uint8_t *e = cbuf + dp.off;
    mymemset(e, 0, 32);
    mymemcpy(e, nm, 11);
    e[11] = 0x10;
    wr16(e + 20, (uint16_t)(nc >> 16));
    wr16(e + 26, (uint16_t)nc);
    write_cluster(dp.cluster, cbuf);
    return (int)nc;
}

static char *next_sep(char *s)
{
    char *a = strchr(s, '/');
    char *b = strchr(s, '\\');
    if (!a)
        return b;
    if (!b)
        return a;
    return a < b ? a : b;
}

static int resolve_path(const char *path, uint8_t finalname[11],
                        uint32_t *dirc, int create)
{
    if (!path)
        return -1;
    if (!next_sep((char *)path)) {
        fmt_name(path, finalname);
        *dirc = root_cluster;
        return 0;
    }
    char work[MAX_PATH];
    int n = 0;
    while (path[n] && n < MAX_PATH - 1) {
        work[n] = path[n];
        n++;
    }
    work[n] = 0;

    uint32_t cur = root_cluster;
    char *p = work;
    for (;;) {
        char *s = next_sep(p);
        if (!s) {
            fmt_name(p, finalname);
            *dirc = cur;
            return 0;
        }
        *s = 0;
        uint8_t dnm[11];
        fmt_name(p, dnm);
        dpos_t dp;
        if (dir_find(cur, dnm, 0, &dp) == 0) {
            if (!(cbuf[dp.off + 11] & 0x10))
                return -1;
            cur = (uint32_t)rd16(cbuf + dp.off + 26) |
                  ((uint32_t)rd16(cbuf + dp.off + 20) << 16);
        } else {
            if (!create)
                return -1;
            int nc = mkdir_in(cur, dnm);
            if (nc < 0)
                return -1;
            cur = (uint32_t)nc;
        }
        p = s + 1;
    }
}

static int mount_common(void)
{
    bps = rd16(sbuf + 11);
    spc = sbuf[13];
    reserved = rd16(sbuf + 14);
    nfats = sbuf[16];
    fat_size = rd32(sbuf + 36);
    root_cluster = rd32(sbuf + 44);
    total_sectors = rd32(sbuf + 32);
    if (!total_sectors)
        total_sectors = rd32(sbuf + 19);

    if (bps != 512 || !spc || !nfats || !fat_size)
        return -1;

    fat_start = reserved;
    data_start = reserved + (uint32_t)nfats * fat_size;
    cluster_count = (total_sectors - data_start) / spc;

    cbuf = kmalloc((uint32_t)spc * bps);
    dbuf = kmalloc((uint32_t)spc * bps);
    if (!cbuf || !dbuf)
        return -1;

    fache_sec = 0xFFFFFFFFu;
    is_mounted = 1;
    serial_puts("[FAT] mounted off=");
    serial_puthex(part_offset);
    serial_puts(" clusters=");
    serial_puthex(cluster_count);
    serial_putc('\n');
    return 0;
}

int fat_mount_part(uint32_t offset, uint32_t part_sectors)
{
    (void)part_sectors;
    if (!ata_present())
        return -1;
    part_offset = offset;
    if (ata_read(part_offset, 1, sbuf) < 0)
        return -1;
    return mount_common();
}

int fat_mount(void)
{
    return fat_mount_part(0, 0);
}

void fat_unmount(void)
{
    if (!is_mounted)
        return;
    if (cbuf)
        kfree(cbuf);
    if (dbuf)
        kfree(dbuf);
    cbuf = 0;
    dbuf = 0;
    is_mounted = 0;
    fache_sec = 0xFFFFFFFFu;
}

int fat_mounted(void)
{
    return is_mounted;
}

uint32_t fat_part_offset(void)
{
    return part_offset;
}

int fat_exists(const char *name83)
{
    if (!is_mounted)
        return -1;
    uint8_t nm[11];
    dpos_t dp;
    uint32_t dirc;
    if (resolve_path(name83, nm, &dirc, 0) < 0)
        return 0;
    return dir_find(dirc, nm, 0, &dp) == 0 ? 1 : 0;
}

int fat_file_size(const char *name83, uint32_t *size)
{
    if (!is_mounted)
        return -1;
    uint8_t nm[11];
    dpos_t dp;
    uint32_t dirc;
    if (resolve_path(name83, nm, &dirc, 0) < 0)
        return -1;
    if (dir_find(dirc, nm, 0, &dp) < 0)
        return -1;
    *size = rd32(cbuf + dp.off + 28);
    return 0;
}

int fat_write_file(const char *name83, const void *data, uint32_t len)
{
    if (!is_mounted)
        return -1;
    if (protect_check(name83, ACT_WRITE) != 0)
        return -1;
    persist_mark_dirty(name83);
    uint8_t nm[11];
    uint32_t dirc;
    if (resolve_path(name83, nm, &dirc, 1) < 0)
        return -1;
    dpos_t dp;

    if (dir_find(dirc, nm, 0, &dp) == 0) {
        uint8_t *old = cbuf + dp.off;
        uint32_t fc = (uint32_t)rd16(old + 26) |
                      ((uint32_t)rd16(old + 20) << 16);
        write_cluster(dp.cluster, cbuf);
        free_chain(fc);
    }

    if (dir_find(dirc, nm, 1, &dp) < 0)
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

static uint8_t lfn_checksum(const uint8_t *sn)
{
    uint32_t sum = 0;
    for (int i = 0; i < 11; i++) {
        sum = (((sum & 1) << 7) | (sum >> 1)) + sn[i];
        sum &= 0xFF;
    }
    return (uint8_t)sum;
}

static char upc(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
}

/* 由真实 basename 生成 8.3 短名；返回 LFN 条目数（0 表示纯 8.3） */
static int make_short_lfn(const char *base, uint8_t sn[11], int *flen)
{
    int len = 0;
    while (base[len])
        len++;
    int dot = -1;
    for (int i = len - 1; i >= 0; i--)
        if (base[i] == '.') {
            dot = i;
            break;
        }
    int ml = 0, el = 0;
    if (dot >= 0) {
        ml = dot;
        el = len - dot - 1;
    } else {
        ml = len;
    }
    mymemset(sn, ' ', 11);
    if (ml <= 8 && el <= 3) {
        for (int i = 0; i < ml; i++)
            sn[i] = (uint8_t)upc(base[i]);
        for (int i = 0; i < el; i++)
            sn[8 + i] = (uint8_t)upc(base[dot + 1 + i]);
        return 0;
    }
    for (int i = 0; i < 6 && i < ml; i++)
        sn[i] = (uint8_t)upc(base[i]);
    sn[6] = '~';
    sn[7] = '1';
    for (int i = 0; i < 3 && i < el; i++)
        sn[8 + i] = (uint8_t)upc(base[dot + 1 + i]);
    *flen = len;
    return (len + 12) / 13;
}

static void fill_lfn(uint8_t *e, int seq, const char *full, int flen,
                     uint8_t cksum)
{
    mymemset(e, 0, 32);
    e[0] = (uint8_t)seq;
    e[11] = 0x0F;
    e[13] = cksum;
    static const int offs[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
    int base_idx = (seq & 0x3F) - 1;
    for (int k = 0; k < 13; k++) {
        int idx = base_idx * 13 + k;
        uint16_t u;
        if (idx < flen)
            u = (uint16_t)(uint8_t)full[idx];
        else if (idx == flen)
            u = 0x0000;
        else
            u = 0xFFFF;
        e[offs[k]] = (uint8_t)(u & 0xFF);
        e[offs[k] + 1] = (uint8_t)(u >> 8);
    }
}

/* 在目录首簇内找连续 n 个空闲槽（新模块目录空间充足） */
static int dir_find_run(uint32_t dirc, int n, dpos_t *dp)
{
    if (read_cluster(dirc, cbuf) < 0)
        return -1;
    uint32_t csize = (uint32_t)spc * bps;
    int run = 0;
    for (uint32_t o = 0; o < csize; o += 32) {
        uint8_t *e = cbuf + o;
        if (e[0] == 0x00 || e[0] == 0xE5) {
            if (run == 0) {
                dp->cluster = dirc;
                dp->off = o;
            }
            if (++run >= n)
                return 0;
        } else {
            run = 0;
        }
    }
    return -1;
}

/* 写文件，支持真实长名 basename（目录部分仍为 8.3 路径） */
int fat_write_file_lfn(const char *dir83, const char *base,
                       const void *data, uint32_t len)
{
    if (!is_mounted)
        return -1;
    char tmp[MAX_PATH];
    int n = 0;
    for (; dir83[n] && n < MAX_PATH - 4; n++)
        tmp[n] = dir83[n];
    tmp[n++] = '/';
    tmp[n++] = 'X';
    tmp[n] = 0;
    uint8_t dnm[11];
    uint32_t dirc;
    if (resolve_path(tmp, dnm, &dirc, 1) < 0)
        return -1;

    uint8_t sn[11];
    int flen = 0;
    int nlfn = make_short_lfn(base, sn, &flen);
    dpos_t dp;
    if (dir_find_run(dirc, nlfn + 1, &dp) < 0)
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
        uint32_t cnt = remain > csize ? csize : remain;
        if (cnt)
            mymemcpy(dbuf, src, cnt);
        src += cnt;
        remain -= cnt;
        write_cluster(c, dbuf);
        c = fat_get(c);
    }

    uint8_t cksum = lfn_checksum(sn);
    for (int i = 0; i < nlfn; i++) {
        int group = nlfn - i;
        int seq = group;
        if (group == nlfn)
            seq |= 0x40;
        fill_lfn(cbuf + dp.off + i * 32, seq, base, flen, cksum);
    }
    uint8_t *e = cbuf + dp.off + nlfn * 32;
    mymemset(e, 0, 32);
    mymemcpy(e, sn, 11);
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
    uint32_t dirc;
    if (resolve_path(name83, nm, &dirc, 0) < 0)
        return -1;
    dpos_t dp;
    if (dir_find(dirc, nm, 0, &dp) < 0)
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
    if (protect_check(name83, ACT_DELETE) != 0)
        return -1;
    uint8_t nm[11];
    uint32_t dirc;
    if (resolve_path(name83, nm, &dirc, 0) < 0)
        return -1;
    dpos_t dp;
    if (dir_find(dirc, nm, 0, &dp) < 0)
        return -1;

    uint8_t *e = cbuf + dp.off;
    uint32_t fc = (uint32_t)rd16(e + 26) |
                  ((uint32_t)rd16(e + 20) << 16);
    if (e[11] & 0x10)
        free_chain(fc);
    else
        free_chain(fc);
    e[0] = 0xE5;
    write_cluster(dp.cluster, cbuf);
    return 0;
}

int fat_delete_many(const char **names83, int n)
{
    int removed = 0;
    for (int i = 0; i < n; i++) {
        if (names83[i] && fat_delete(names83[i]) == 0)
            removed++;
    }
    return removed;
}

int fat_mkdir(const char *name83)
{
    if (!is_mounted)
        return -1;
    uint8_t nm[11];
    uint32_t dirc;
    if (resolve_path(name83, nm, &dirc, 1) < 0)
        return -1;
    if (mkdir_in(dirc, nm) < 0)
        return -1;
    return 0;
}

void fat_debug_dump_dir(const char *dir83)
{
    if (!is_mounted)
        return;
    char tmp[MAX_PATH];
    int n = 0;
    for (; dir83[n] && n < MAX_PATH - 4; n++)
        tmp[n] = dir83[n];
    tmp[n++] = '/';
    tmp[n++] = 'X';
    tmp[n] = 0;
    uint8_t dnm[11];
    uint32_t dirc;
    if (resolve_path(tmp, dnm, &dirc, 0) < 0) {
        serial_puts("[DUMP] dir not found\n");
        return;
    }
    if (read_cluster(dirc, cbuf) < 0)
        return;
    uint32_t csize = (uint32_t)spc * bps;
    for (uint32_t o = 0; o < csize; o += 32) {
        uint8_t *e = cbuf + o;
        if (e[0] == 0x00)
            break;
        serial_puts("[D] slot=");
        serial_puthex(o / 32);
        serial_puts(" attr=");
        serial_puthex(e[11]);
        if (e[11] == 0x0F) {
            serial_puts(" LFN seq=");
            serial_puthex(e[0]);
            serial_puts(" c1=");
            serial_putc((char)e[1]);
        } else {
            serial_puts(" SHORT ");
            for (int i = 0; i < 11; i++) {
                char c = (e[i] == ' ') ? '_' : (char)e[i];
                serial_putc(c);
            }
        }
        serial_putc('\n');
    }
}

uint32_t fat_cluster_size(void)
{
    return is_mounted ? (uint32_t)spc * bps : 0;
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
        ata_read(part_offset + fat_start + s, 1, sbuf);
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

/* ================= 目录列举 / 文件移动 ================= */

static const int lfn_offs[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};

/* 由 32 字节目录项还原 8.3 短名（去掉空格填充，补回扩展名点号） */
static void short_name_from_entry(const uint8_t *e, char out[13])
{
    int p = 0;
    for (int i = 0; i < 8; i++) {
        if (e[i] == ' ')
            break;
        unsigned char c = e[i];
        if (i == 0 && c == 0x05)
            c = 0xE5; /* FAT 约定：首字节 0x05 表示真实 0xE5 */
        out[p++] = (char)c;
    }
    if (e[8] != ' ') {
        out[p++] = '.';
        for (int i = 8; i < 11; i++) {
            if (e[i] == ' ')
                break;
            out[p++] = (char)e[i];
        }
    }
    out[p] = 0;
}

/* 把目录路径解析为它自身的首簇号。"" / "/" 表示根目录。 */
static int resolve_dir_cluster(const char *dir83, uint32_t *outc)
{
    if (!dir83 || !dir83[0] || (dir83[0] == '/' && !dir83[1])) {
        *outc = root_cluster;
        return 0;
    }
    uint8_t nm[11];
    uint32_t parent;
    if (resolve_path(dir83, nm, &parent, 0) < 0)
        return -1;
    dpos_t dp;
    if (dir_find(parent, nm, 0, &dp) < 0)
        return -1;
    if (!(cbuf[dp.off + 11] & 0x10))
        return -1; /* 不是目录 */
    *outc = (uint32_t)rd16(cbuf + dp.off + 26) |
            ((uint32_t)rd16(cbuf + dp.off + 20) << 16);
    return 0;
}

int fat_list_dir(const char *dir83, struct fat_dirent *out, int max)
{
    if (!is_mounted || !out || max <= 0)
        return -1;

    uint32_t dirc;
    if (resolve_dir_cluster(dir83, &dirc) < 0)
        return -1;

    uint16_t lfn[21][13];
    uint8_t have[21];
    uint8_t cksum = 0;
    int anylfn = 0;
    mymemset(have, 0, sizeof have);

    int n = 0;
    uint32_t csize = (uint32_t)spc * bps;
    uint32_t c = dirc;
    while (c >= 2 && c < EOC) {
        if (read_cluster(c, cbuf) < 0)
            return -1;
        for (uint32_t o = 0; o < csize; o += 32) {
            uint8_t *e = cbuf + o;
            if (e[0] == 0x00)
                return n; /* 目录结束 */
            if (e[0] == 0xE5)
                continue; /* 已删除槽 */

            uint8_t attr = e[11];
            if (attr == 0x0F) {
                /* LFN 长名条目 */
                uint8_t seq = e[0] & 0x3F;
                if (e[0] & 0x40) {
                    mymemset(have, 0, sizeof have);
                    anylfn = 1;
                    cksum = e[13];
                }
                if (seq >= 1 && seq <= 20) {
                    for (int k = 0; k < 13; k++)
                        lfn[seq][k] =
                            (uint16_t)e[lfn_offs[k]] |
                            ((uint16_t)e[lfn_offs[k] + 1] << 8);
                    have[seq] = 1;
                }
                continue;
            }

            if (attr & 0x08) { /* 卷标 */
                mymemset(have, 0, sizeof have);
                anylfn = 0;
                continue;
            }

            char sn[13];
            short_name_from_entry(e, sn);
            int is_dir = (attr & 0x10) != 0;

            /* 跳过 "." 与 ".." */
            if (is_dir && e[0] == '.') {
                mymemset(have, 0, sizeof have);
                anylfn = 0;
                continue;
            }

            if (n >= max)
                return n;

            struct fat_dirent *de = &out[n];
            de->name[0] = 0;
            if (anylfn && lfn_checksum(e) == cksum) {
                int p = 0;
                for (int seq = 1; seq <= 20 && p < FAT_DENT_NAME_MAX - 1;
                     seq++) {
                    if (!have[seq])
                        break;
                    for (int k = 0; k < 13; k++) {
                        uint16_t u = lfn[seq][k];
                        if (u == 0x0000 || u == 0xFFFF)
                            goto lfn_done;
                        de->name[p++] = (char)(u & 0xFF);
                    }
                }
            lfn_done:
                de->name[p] = 0;
            }
            if (!de->name[0])
                strcpy(de->name, sn);
            strcpy(de->short83, sn);
            de->is_dir = (uint8_t)is_dir;
            de->size = rd32(e + 28);
            de->cluster = (uint32_t)rd16(e + 26) |
                          ((uint32_t)rd16(e + 20) << 16);
            n++;

            mymemset(have, 0, sizeof have);
            anylfn = 0;
        }
        c = fat_get(c);
    }
    return n;
}

int fat_dir_exists(const char *path83)
{
    if (!is_mounted)
        return 0;
    uint32_t c;
    return resolve_dir_cluster(path83, &c) == 0 ? 1 : 0;
}

#define FAT_MOVE_MAX (8u * 1024u * 1024u)

int fat_move(const char *src83, const char *dst83)
{
    if (!is_mounted || !src83 || !dst83)
        return -1;

    uint32_t size = 0;
    if (fat_file_size(src83, &size) != 0)
        return -1;
    if (size > FAT_MOVE_MAX)
        return -2;

    uint8_t *buf = kmalloc(size ? size : 1);
    if (!buf)
        return -3;

    uint32_t got = 0;
    int rc = -1;
    if (fat_read_file(src83, buf, size ? size : 1, &got) != 0)
        goto out;
    if (fat_write_file(dst83, buf, got) != 0)
        goto out;
    if (fat_delete(src83) != 0)
        goto out;
    rc = 0;
out:
    kfree(buf);
    return rc;
}
