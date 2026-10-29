#include <stdint.h>
#include <stddef.h>
#include <kernel/recycle.h>
#include <kernel/fat32.h>
#include <kernel/heap.h>
#include <kernel/kstring.h>
#include <kernel/serial.h>

/*
 * 回收站：文件移动到 \RECYCLED\Rxxxx.EXT，索引记于 \RECYCLED\INFO.JBT。
 * 行格式： 存储短名|原始短路径|字节数
 */

static char idx[8192];
static struct recycle_item cache[RECYCLE_MAX];
static int cache_n;

static uint32_t load_index(void)
{
    uint32_t got = 0;
    if (fat_read_file(RECYCLE_INDEX, idx, sizeof(idx) - 1, &got) != 0) {
        idx[0] = 0;
        return 0;
    }
    idx[got] = 0;
    return got;
}

static int save_index(uint32_t len)
{
    return fat_write_file(RECYCLE_INDEX, idx, len);
}

static void uint_to_dec(char *o, uint32_t v)
{
    char t[12];
    int n = 0;
    if (v == 0)
        t[n++] = '0';
    while (v) {
        t[n++] = (char)('0' + v % 10);
        v /= 10;
    }
    int i = 0;
    while (n)
        o[i++] = t[--n];
    o[i] = 0;
}

static void rcat(char *dst, int lsz, const char *src)
{
    if (lsz <= 1)
        return;
    int dl = (int)strlen(dst);
    int sl = (int)strlen(src);
    if (dl + sl >= lsz)
        sl = lsz - 1 - dl;
    if (sl <= 0)
        return;
    memcpy(dst + dl, src, sl);
    dst[dl + sl] = 0;
}

static int build_line(char *line, int lsz, const char *store, const char *orig,
                      uint32_t size)
{
    char num[16];
    line[0] = 0;
    rcat(line, lsz, store);
    rcat(line, lsz, "|");
    rcat(line, lsz, orig);
    rcat(line, lsz, "|");
    uint_to_dec(num, size);
    rcat(line, lsz, num);
    rcat(line, lsz, "\n");
    return (int)strlen(line);
}

static int parse_items(void)
{
    uint32_t total = load_index();
    cache_n = 0;
    uint32_t i = 0;
    while (i < total && cache_n < RECYCLE_MAX) {
        uint32_t start = i;
        while (i < total && idx[i] != '\n')
            i++;
        uint32_t end = i;
        if (i < total)
            i++;

        struct recycle_item *it = &cache[cache_n];
        memset(it, 0, sizeof(*it));

        uint32_t p = start;
        uint32_t f0 = p;
        while (p < end && idx[p] != '|')
            p++;
        int l0 = (int)(p - f0);
        if (l0 > 19) l0 = 19;
        memcpy(it->store, idx + f0, l0);
        it->store[l0] = 0;
        if (p < end) p++;

        uint32_t f1 = p;
        while (p < end && idx[p] != '|')
            p++;
        int l1 = (int)(p - f1);
        if (l1 > FAT_DENT_NAME_MAX - 1) l1 = FAT_DENT_NAME_MAX - 1;
        memcpy(it->orig, idx + f1, l1);
        it->orig[l1] = 0;
        if (p < end) p++;

        it->size = (uint32_t)atoi(idx + p);
        if (it->store[0]) {
            it->used = 1;
            cache_n++;
        }
    }
    return cache_n;
}

static int rewrite_all(void)
{
    uint32_t len = 0;
    idx[0] = 0;
    for (int i = 0; i < cache_n; i++) {
        char line[128];
        int n = build_line(line, sizeof line, cache[i].store, cache[i].orig,
                           cache[i].size);
        if (len + (uint32_t)n >= sizeof(idx))
            return -1;
        memcpy(idx + len, line, n);
        len += n;
    }
    return save_index(len);
}

int recycle_init(void)
{
    if (!fat_mounted())
        return -1;
    if (!fat_dir_exists(RECYCLE_DIR)) {
        if (fat_mkdir(RECYCLE_DIR) != 0)
            return -1;
    }
    return 0;
}

static void ext_of(const char *path, char out[4])
{
    const char *base = strrchr(path, '\\');
    base = base ? base + 1 : path;
    const char *dot = strrchr(base, '.');
    out[0] = 0;
    if (!dot || !dot[1])
        return;
    dot++;
    int k = 0;
    while (*dot && k < 3) {
        int c = *dot++;
        if (c >= 'a' && c <= 'z')
            c -= 32;
        out[k++] = (char)c;
    }
    out[k] = 0;
}

static void store_name(char *out, int seq, const char *ext)
{
    static const char digits[10] = "0123456789";
    memcpy(out, "RECYCLED\\R", 10);
    int n = 10;
    out[n++] = digits[(seq / 1000) % 10];
    out[n++] = digits[(seq / 100) % 10];
    out[n++] = digits[(seq / 10) % 10];
    out[n++] = digits[seq % 10];
    if (ext[0]) {
        out[n++] = '.';
        int k = 0;
        while (ext[k] && k < 3)
            out[n++] = ext[k++];
    }
    out[n] = 0;
}

int recycle_delete(const char *path)
{
    if (!fat_mounted() || !path || !path[0])
        return -1;
    if (recycle_init() != 0)
        return -1;

    uint32_t size = 0;
    if (fat_file_size(path, &size) != 0)
        return -1;

    parse_items();
    char ext[4];
    ext_of(path, ext);

    char store[24];
    int seq = 0;
    for (seq = 0; seq < 10000; seq++) {
        store_name(store, seq, ext);
        uint32_t sz;
        if (fat_file_size(store, &sz) != 0) {
            int clash = 0;
            for (int k = 0; k < cache_n; k++)
                if (strcmp(cache[k].store, store) == 0) {
                    clash = 1;
                    break;
                }
            if (!clash)
                break;
        }
    }
    if (seq >= 10000)
        return -4;

    if (fat_move(path, store) != 0)
        return -2;

    uint32_t len = load_index();
    char line[128];
    int n = build_line(line, sizeof line, store, path, size);
    if (len + (uint32_t)n >= sizeof(idx))
        return -3;
    memcpy(idx + len, line, n);
    if (save_index(len + (uint32_t)n) != 0)
        return -5;

    serial_puts("[RECYCLE] moved ");
    serial_puts(store);
    serial_putc('\n');
    return 0;
}

int recycle_list(struct recycle_item *out, int max)
{
    if (!fat_mounted())
        return 0;
    parse_items();
    int n = cache_n < max ? cache_n : max;
    for (int i = 0; i < n; i++)
        out[i] = cache[i];
    return n;
}

int recycle_restore(int sel)
{
    if (parse_items() <= 0)
        return -1;
    if (sel < 0 || sel >= cache_n)
        return -1;
    if (fat_move(cache[sel].store, cache[sel].orig) != 0)
        return -2;
    for (int k = sel; k < cache_n - 1; k++)
        cache[k] = cache[k + 1];
    cache_n--;
    return rewrite_all();
}

int recycle_empty(void)
{
    if (!fat_mounted())
        return -1;
    parse_items();
    for (int i = 0; i < cache_n; i++)
        fat_delete(cache[i].store);
    cache_n = 0;
    fat_delete(RECYCLE_INDEX);
    return 0;
}

int recycle_count(void)
{
    if (!fat_mounted())
        return 0;
    return parse_items();
}
