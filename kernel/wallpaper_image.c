#include <stdint.h>
#include <stddef.h>
#include <kernel/wallpaper_image.h>
#include <kernel/fat32.h>
#include <kernel/fb.h>
#include <kernel/heap.h>
#include <kernel/desktop.h>
#include <kernel/kstring.h>

/* On-disk pictures are read whole (FAT32 has no partial-read API), but the
   decode itself is streamed: one converted row at a time goes straight into
   the wallpaper buffer, so no full-size pixel buffer is ever allocated. */
#define WIMG_MAX_FILE_BYTES (8u * 1024u * 1024u)
#define WIMG_MAX_DIM 1600

/* The default wallpaper is linked into the kernel image by objcopy. */
extern const unsigned char _binary_assets_donghu_1600x900_bmp_start[];
extern const unsigned char _binary_assets_donghu_1600x900_bmp_end[];

static const char *g_status = "";

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int32_t rd32s(const uint8_t *p) { return (int32_t)rd32(p); }

static int ext_is(const char *name, const char *want)
{
    const char *dot = strrchr(name, '.');
    if (!dot)
        return 0;
    dot++;
    for (int i = 0; ; i++) {
        char a = (char)tolower((int)(uint8_t)dot[i]);
        char b = want[i];
        if (b == 0)
            return a == 0;
        if (a != b)
            return 0;
    }
}

static const char *img_exts[5] = { "bmp", "png", "jpg", "jpeg", "gif" };

static int is_picture(const char *name)
{
    for (int i = 0; i < 5; i++)
        if (ext_is(name, img_exts[i]))
            return 1;
    return 0;
}

static void copy_str(char *dst, const char *src, int max)
{
    int p = 0;
    while (src[p] && p < max - 1) {
        dst[p] = src[p];
        p++;
    }
    dst[p] = 0;
}

int wimg_list(struct wimg_file *out, int max)
{
    if (!fat_mounted()) {
        g_status = "No disk mounted";
        return -1;
    }
    struct fat_dirent dents[32];
    int n = fat_list_dir("", dents, 32);
    if (n < 0) {
        g_status = "Cannot list directory";
        return -1;
    }
    int c = 0;
    for (int i = 0; i < n && c < max; i++) {
        if (dents[i].is_dir || !is_picture(dents[i].name))
            continue;
        copy_str(out[c].name, dents[i].name, WIMG_NAME_MAX);
        copy_str(out[c].short83, dents[i].short83, 13);
        c++;
    }
    g_status = c ? "Pictures found" : "No pictures on disk";
    return c;
}

/* Validate a BMP header and stream the pixels into the wallpaper buffer.
   Only a single row buffer is used, whatever the picture size. */
static int bmp_stream(const uint8_t *d, uint32_t len)
{
    uint32_t off;
    int32_t w, h;
    uint16_t planes, bpp;
    uint32_t comp;
    int aw, ah, topdown;
    uint32_t row;
    uint32_t *rb;

    if (!d || len < 54) {
        g_status = "File too small to be a BMP";
        return -1;
    }
    if (d[0] != 'B' || d[1] != 'M') {
        g_status = "Not a BMP file";
        return -1;
    }
    off = rd32(d + 10);
    w = rd32s(d + 18);
    h = rd32s(d + 22);
    planes = rd16(d + 26);
    bpp = rd16(d + 28);
    comp = rd32(d + 30);
    if (planes != 1) {
        g_status = "Unsupported BMP planes";
        return -1;
    }
    if (comp != 0) {
        g_status = "Compressed BMP not supported";
        return -1;
    }
    if (bpp != 24 && bpp != 32) {
        g_status = "Only 24/32-bit BMP supported";
        return -1;
    }
    aw = w < 0 ? -w : w;
    ah = h < 0 ? -h : h;
    if (aw <= 0 || ah <= 0) {
        g_status = "Bad BMP dimensions";
        return -1;
    }
    if (aw > WIMG_MAX_DIM || ah > WIMG_MAX_DIM) {
        g_status = "Image too large (max 1600 per side)";
        return -1;
    }
    row = ((uint32_t)bpp * (uint32_t)aw + 31u) / 32u * 4u;
    if (off > len || row * (uint32_t)ah > len - off) {
        g_status = "Truncated BMP data";
        return -1;
    }

    rb = (uint32_t *)kmalloc((size_t)aw * sizeof(uint32_t));
    if (!rb) {
        g_status = "Out of memory for the row buffer";
        return -1;
    }

    topdown = h < 0;
    wallpaper_stream_begin(aw, ah);
    for (int y = 0; y < ah; y++) {
        int src_row = topdown ? y : (ah - 1 - y);
        const uint8_t *rp = d + off + (uint64_t)row * (uint32_t)src_row;
        for (int x = 0; x < aw; x++) {
            uint8_t b, g, r;
            if (bpp == 24) {
                b = rp[x * 3];
                g = rp[x * 3 + 1];
                r = rp[x * 3 + 2];
            } else {
                b = rp[x * 4];
                g = rp[x * 4 + 1];
                r = rp[x * 4 + 2];
            }
            rb[x] = fb_rgb(r, g, b);
        }
        wallpaper_stream_row(y, rb, aw);
    }
    wallpaper_stream_end();
    kfree(rb);
    return 0;
}

/* Default wallpaper: the East Lake photo linked into the kernel. */
int wimg_apply_default(void)
{
    uint32_t len = (uint32_t)(_binary_assets_donghu_1600x900_bmp_end -
                              _binary_assets_donghu_1600x900_bmp_start);
    if (len < 54) {
        g_status = "Default wallpaper missing";
        return -1;
    }
    if (bmp_stream(_binary_assets_donghu_1600x900_bmp_start, len) != 0)
        return -1;
    g_status = "Default East Lake wallpaper";
    return 0;
}

int wimg_apply(const char *short83)
{
    if (!fat_mounted()) {
        g_status = "No disk mounted";
        return -1;
    }
    if (is_picture(short83) && !ext_is(short83, "bmp")) {
        g_status = "PNG/JPG/GIF decoder not ready; use a BMP picture";
        return -1;
    }
    uint32_t fsize = 0;
    if (fat_file_size(short83, &fsize) != 0 || fsize == 0) {
        g_status = "Cannot stat file";
        return -1;
    }
    if (fsize > WIMG_MAX_FILE_BYTES) {
        g_status = "Image file too big (limit 8 MB)";
        return -1;
    }
    uint8_t *raw = kmalloc(fsize);
    if (!raw) {
        g_status = "Out of memory";
        return -1;
    }
    uint32_t got = 0;
    if (fat_read_file(short83, raw, fsize, &got) != 0 || got != fsize) {
        g_status = "Cannot read file";
        kfree(raw);
        return -1;
    }
    int rc = bmp_stream(raw, got);
    kfree(raw);
    if (rc == 0)
        g_status = "Wallpaper applied";
    return rc;
}

const char *wimg_status(void) { return g_status; }
