#include <stdint.h>
#include <stddef.h>
#include <kernel/ttf.h>
#include <kernel/fb.h>
#include <kernel/heap.h>
#include <kernel/serial.h>
#include <kernel/fat32.h>
#include <kernel/payload.h>
#include <kernel/i18n.h>

void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int v, size_t n);
unsigned long strlen(const char *s);

/* ---- freestanding math shims (x87 doubles, no libm) ---- */

static double ttf_floor(double x)
{
    long long i = (long long)x;
    double r = (double)i;
    if (r > x)
        r -= 1.0;
    return r;
}

static double ttf_ceil(double x)
{
    long long i = (long long)x;
    double r = (double)i;
    if (r < x)
        r += 1.0;
    return r;
}

static double ttf_fabs(double x)
{
    union { double d; uint64_t u; } v;
    v.d = x;
    v.u &= ~(1ULL << 63);
    return v.d;
}

static double ttf_sqrt(double x)
{
    if (x <= 0)
        return 0;
    double r = x;
    for (int i = 0; i < 30; i++)
        r = 0.5 * (r + x / r);
    return r;
}

static double ttf_fmod(double x, double y)
{
    if (y == 0)
        return 0;
    long long t = (long long)(x / y);
    return x - (double)t * y;
}

static const double TTF_PI = 3.14159265358979323846;

static double ttf_cos(double x)
{
    x = ttf_fmod(x, 2.0 * TTF_PI);
    if (x > TTF_PI)
        x -= 2.0 * TTF_PI;
    if (x < -TTF_PI)
        x += 2.0 * TTF_PI;
    double x2 = x * x, term = 1, s = 1;
    for (int n = 1; n <= 9; n++) {
        term *= -x2 / (double)((2 * n - 1) * (2 * n));
        s += term;
    }
    return s;
}

static double ttf_atan(double t)
{
    int neg = t < 0;
    if (neg)
        t = -t;
    int inv = t > 1;
    if (inv)
        t = 1.0 / t;
    double t2 = t * t, term = t, s = 0;
    for (int n = 0; n < 40; n++) {
        double d = (double)(2 * n + 1);
        s += (n & 1) ? -term / d : term / d;
        term *= t2;
    }
    if (inv)
        s = TTF_PI / 2.0 - s;
    return neg ? -s : s;
}

static double ttf_atan2(double y, double x)
{
    if (x > 0)
        return ttf_atan(y / x);
    if (x < 0)
        return y >= 0 ? ttf_atan(y / x) + TTF_PI
                      : ttf_atan(y / x) - TTF_PI;
    return y >= 0 ? TTF_PI / 2.0 : -TTF_PI / 2.0;
}

static double ttf_acos(double x)
{
    if (x >= 1)
        return 0;
    if (x <= -1)
        return TTF_PI;
    return ttf_atan2(ttf_sqrt(1 - x * x), x);
}

static double ttf_ln(double x)
{
    int k = 0;
    while (x > 1.5) { x *= 0.5; k++; }
    while (x < 0.75) { x *= 2; k--; }
    double u = (x - 1) / (x + 1), u2 = u * u, term = u, s = 0;
    for (int i = 0; i < 16; i++) {
        s += term / (double)(2 * i + 1);
        term *= u2;
    }
    return 2.0 * s + (double)k * 0.6931471805599453;
}

static double ttf_exp(double x)
{
    double t = x * 1.4426950408889634;
    double n = ttf_floor(t);
    double f = t - n;
    double L = f * 0.6931471805599453;
    double acc = 1, term = 1;
    for (int i = 1; i < 16; i++) {
        term *= L / (double)i;
        acc += term;
    }
    int ni = (int)n;
    if (ni >= 0)
        for (int i = 0; i < ni; i++)
            acc *= 2.0;
    else
        for (int i = 0; i < -ni; i++)
            acc *= 0.5;
    return acc;
}

static double ttf_pow(double x, double y)
{
    if (x <= 0)
        return 0;
    long long yi = (long long)y;
    if ((double)yi == y) {
        double r = 1;
        int n = yi < 0 ? (int)-yi : (int)yi;
        for (int i = 0; i < n; i++)
            r *= x;
        return yi < 0 ? 1.0 / r : r;
    }
    return ttf_exp(y * ttf_ln(x));
}

/* ---- stb_truetype freestanding binding ---- */

#define STBTT_ifloor(x)   ((int)ttf_floor(x))
#define STBTT_iceil(x)    ((int)ttf_ceil(x))
#define STBTT_sqrt(x)     ttf_sqrt(x)
#define STBTT_pow(x, y)   ttf_pow(x, y)
#define STBTT_fmod(x, y)  ttf_fmod(x, y)
#define STBTT_cos(x)      ttf_cos(x)
#define STBTT_acos(x)     ttf_acos(x)
#define STBTT_fabs(x)     ttf_fabs(x)
#define STBTT_malloc(x, u) ((void)(u), kmalloc(x))
#define STBTT_free(x, u)   ((void)(u), kfree(x))
#define STBTT_assert(x)   ((void)0)
#define STBTT_strlen(x)   strlen(x)
#define STBTT_memcpy      memcpy
#define STBTT_memset      memset

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

/* ---- loaded faces ---- */

static uint8_t *face_data[TTF_FONT_COUNT];
static stbtt_fontinfo face[TTF_FONT_COUNT];
static int face_ok[TTF_FONT_COUNT];

/* fallback faces for scripts the primary UI fonts lack:
   CJK, Korean, Arabic, Thai, Devanagari (codepoint lookup chain) */
#define FB_COUNT 5
static uint8_t *fb_data[FB_COUNT];
static stbtt_fontinfo fb_face[FB_COUNT];
static int fb_ok[FB_COUNT];
static const char *fb_disk[FB_COUNT] = {
    "CJK.TTF", "KR.TTF", "AR.TTF", "TH.TTF", "HI.TTF"
};
static const char *fb_pay[FB_COUNT] = {
    PAYLOAD_FCJK, PAYLOAD_FKR, PAYLOAD_FAR, PAYLOAD_FTH, PAYLOAD_FHI
};
static const int fb_keyfont[FB_COUNT] = { 15, 16, 17, 18, 19 };

/* per-language font, loaded from disk as <LANGCODE>.TTF (205 languages) */
static uint8_t *dyn_data;
static stbtt_fontinfo dyn_face;
static int dyn_state;             /* 0 = not tried, 1 = loaded, -1 = failed */
static char dyn_for[12];

static int g_ready;

static int cur_font;
static int cur_scale = 1;
static int base_px = 16;
static float cur_st;
static int cur_ascent, cur_descent, cur_line;

/* ---- glyph cache (LRU open addressing) ---- */

#define GC_SLOTS 769

typedef struct {
    uint8_t used;
    uint64_t key;
    uint32_t stamp;
    uint8_t *bmp;
    int16_t w, h, xoff, yoff, adv;
} gc_entry;

static gc_entry gc[GC_SLOTS];
static uint32_t gc_clock;

static uint64_t gc_make_key(int srcfont, int px, uint32_t cp)
{
    return ((uint64_t)(uint8_t)srcfont << 40)
         | ((uint64_t)(uint16_t)px << 24)
         | (uint64_t)(cp & 0xFFFFFFu);
}

static uint8_t *load_file(const char *name, uint32_t *outlen)
{
    if (!fat_mounted())
        return 0;
    uint32_t sz = 0;
    if (fat_file_size(name, &sz) != 0 || sz < 512 || sz > 16u * 1024 * 1024)
        return 0;
    uint8_t *buf = kmalloc((size_t)sz + 1);
    if (!buf)
        return 0;
    uint32_t got = 0;
    if (fat_read_file(name, buf, sz + 1, &got) != 0 || got < 512) {
        kfree(buf);
        return 0;
    }
    buf[got] = 0;
    if (outlen)
        *outlen = got;
    return buf;
}

static uint8_t *load_face_data(const char *fname, const char *pname,
                               uint32_t *outlen, int *from_payload)
{
    uint8_t *d = load_file(fname, outlen);
    if (d) {
        if (from_payload)
            *from_payload = 0;
        return d;
    }
    const struct payload_mod *m = payload_find(pname);
    if (m && m->data && m->size >= 512) {
        if (outlen)
            *outlen = m->size;
        if (from_payload)
            *from_payload = 1;
        return (uint8_t *)(uintptr_t)m->data;
    }
    return 0;
}

static int open_face(uint8_t *data, stbtt_fontinfo *fi)
{
    int off = stbtt_GetFontOffsetForIndex(data, 0);
    if (off < 0)
        return 0;
    return stbtt_InitFont(fi, data, off) ? 1 : 0;
}

static void recompute_metrics(void)
{
    int px = base_px * cur_scale;
    stbtt_fontinfo *fi = &face[cur_font];
    cur_st = stbtt_ScaleForPixelHeight(fi, (float)px);
    int asc, desc, gap;
    stbtt_GetFontVMetrics(fi, &asc, &desc, &gap);
    cur_ascent = (int)(asc * cur_st + 0.5f);
    cur_descent = (int)(-desc * cur_st + 0.5f);
    cur_line = cur_ascent + cur_descent + (int)(gap * cur_st + 0.5f);
    if (cur_line < px)
        cur_line = px;
}

int ttf_init(void)
{
    static int done;
    if (done)
        return g_ready ? TTF_FONT_COUNT : 0;
    done = 1;

    static const char *names[TTF_FONT_COUNT] = {
        "SANS.TTF", "MONO.TTF", "MONOB.TTF"
    };
    static const char *pnames[TTF_FONT_COUNT] = {
        PAYLOAD_FSANS, PAYLOAD_FMONO, PAYLOAD_FMONOB
    };
    int n = 0;
    for (int i = 0; i < TTF_FONT_COUNT; i++) {
        uint32_t len = 0;
        int from_payload = 0;
        uint8_t *d = load_face_data(names[i], pnames[i], &len, &from_payload);
        if (d && open_face(d, &face[i])) {
            face_data[i] = d;
            face_ok[i] = 1;
            n++;
            serial_puts("[TTF] loaded ");
            serial_puts(names[i]);
            serial_putc('\n');
        } else {
            if (d && !from_payload)
                kfree(d);
            serial_puts("[TTF] missing ");
            serial_puts(names[i]);
            serial_putc('\n');
        }
    }

    for (int i = 0; i < FB_COUNT; i++) {
        uint32_t flen = 0;
        int from_payload = 0;
        uint8_t *fd = load_face_data(fb_disk[i], fb_pay[i], &flen,
                                     &from_payload);
        if (fd && open_face(fd, &fb_face[i])) {
            fb_data[i] = fd;
            fb_ok[i] = 1;
            serial_puts("[TTF] loaded fallback ");
            serial_puts(fb_disk[i]);
            serial_putc('\n');
        } else {
            if (fd && !from_payload)
                kfree(fd);
            serial_puts("[TTF] missing fallback ");
            serial_puts(fb_disk[i]);
            serial_putc('\n');
        }
    }

    g_ready = face_ok[TTF_FONT_SANS];
    if (g_ready) {
        cur_font = TTF_FONT_SANS;
        recompute_metrics();
    }
    serial_puts(g_ready ? "[TTF] vector font engine ON\n"
                       : "[TTF] vector font engine OFF (bitmap fallback)\n");
    return n;
}

int ttf_ready(void) { return g_ready; }

void ttf_set_font(int i)
{
    if (i < 0 || i >= TTF_FONT_COUNT || !face_ok[i])
        return;
    cur_font = i;
    if (g_ready)
        recompute_metrics();
}

int ttf_get_font(void) { return cur_font; }

void ttf_set_scale(int s)
{
    if (s < 1)
        s = 1;
    if (s > 16)
        s = 16;
    cur_scale = s;
    if (g_ready)
        recompute_metrics();
}

int ttf_get_scale(void) { return cur_scale; }

void ttf_set_base_px(int px)
{
    if (px < 8)
        px = 8;
    if (px > 200)
        px = 200;
    base_px = px;
    if (g_ready)
        recompute_metrics();
}

int ttf_base_px(void) { return base_px; }
int ttf_pixel_height(void) { return base_px * cur_scale; }

void ttf_set_pixel_height(int px)
{
    base_px = px < 8 ? 8 : (px > 200 ? 200 : px);
    cur_scale = 1;
    if (g_ready)
        recompute_metrics();
}

int ttf_ascent(void) { return g_ready ? cur_ascent : 0; }
int ttf_line_height(void) { return g_ready ? cur_line : 0; }

/* try to load the font of the currently selected language: <CODE>.TTF */
static int dyn_load(void)
{
    char want[12];
    const char *c = i18n_code(i18n_lang());
    int n = 0;
    if (!c || !c[0])
        return -1;
    for (; c[n] && n < 6; n++)
        want[n] = (c[n] >= 'a' && c[n] <= 'z') ? (char)(c[n] - 32) : c[n];
    want[n++] = '.';
    want[n++] = 'T';
    want[n++] = 'T';
    want[n++] = 'F';
    want[n] = 0;

    for (int i = 0; i < 12; i++) {
        if (dyn_for[i] != want[i]) {
            dyn_state = 0;
            break;
        }
        if (!want[i])
            break;
    }
    if (dyn_state != 0)
        return dyn_state;

    for (int i = 0; i < 12; i++) {
        dyn_for[i] = want[i];
        if (!want[i])
            break;
    }

    uint32_t len = 0;
    uint8_t *d = load_file(want, &len);
    if (!d) {
        dyn_state = -1;
        return -1;
    }
    if (!open_face(d, &dyn_face)) {
        kfree(d);
        dyn_state = -1;
        return -1;
    }
    dyn_data = d;
    dyn_state = 1;
    serial_puts("[TTF] lang font ");
    serial_puts(want);
    serial_puts(" loaded\n");
    return 1;
}

/* returns the source face + key-font id for a codepoint */
static stbtt_fontinfo *src_face(uint32_t cp, int *keyfont)
{
    if (stbtt_FindGlyphIndex(&face[cur_font], (int)cp) != 0) {
        *keyfont = cur_font;
        return &face[cur_font];
    }
    for (int i = 0; i < FB_COUNT; i++) {
        if (fb_ok[i] &&
            stbtt_FindGlyphIndex(&fb_face[i], (int)cp) != 0) {
            *keyfont = fb_keyfont[i];
            return &fb_face[i];
        }
    }
    /* per-language font on disk, last resort before the base face */
    if (dyn_load() == 1 && stbtt_FindGlyphIndex(&dyn_face, (int)cp) != 0) {
        *keyfont = 30;
        return &dyn_face;
    }
    *keyfont = cur_font;
    return &face[cur_font];
}

static gc_entry *gc_lookup(int srcfont, int px, uint32_t cp)
{
    int kf;
    stbtt_fontinfo *sfi = src_face(cp, &kf);
    (void)srcfont;
    uint64_t key = gc_make_key(kf, px, cp);
    uint32_t home = (uint32_t)(key % GC_SLOTS);
    gc_entry *empty = 0;
    uint32_t empty_idx = 0;
    for (uint32_t step = 0; step < GC_SLOTS; step++) {
        uint32_t idx = (home + step) % GC_SLOTS;
        gc_entry *e = &gc[idx];
        if (!e->used) {
            if (!empty) { empty = e; empty_idx = idx; }
            break;
        }
        if (e->key == key)
            return e;
    }

    if (!empty) {
        uint32_t best = home;
        uint32_t oldest = gc[home].stamp;
        for (uint32_t step = 1; step < GC_SLOTS; step++) {
            uint32_t idx = (home + step) % GC_SLOTS;
            if (gc[idx].stamp < oldest) {
                oldest = gc[idx].stamp;
                best = idx;
            }
        }
        empty = &gc[best];
        empty_idx = best;
        if (empty->bmp)
            kfree(empty->bmp);
    }

    stbtt_fontinfo *fi = sfi;
    float st = stbtt_ScaleForPixelHeight(fi, (float)px);
    int w = 0, h = 0, xo = 0, yo = 0;
    unsigned char *bmp =
        stbtt_GetCodepointBitmap(fi, st, st, (int)cp, &w, &h, &xo, &yo);
    int aw = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(fi, (int)cp, &aw, &lsb);

    gc_entry *e = empty;
    (void)empty_idx;
    e->used = 1;
    e->key = key;
    e->w = (int16_t)w;
    e->h = (int16_t)h;
    e->xoff = (int16_t)xo;
    e->yoff = (int16_t)yo;
    e->adv = (int)(aw * st + 0.5f);
    e->bmp = 0;
    if (bmp && w > 0 && h > 0) {
        size_t bytes = (size_t)w * (size_t)h;
        uint8_t *dst = kmalloc(bytes);
        if (dst) {
            memcpy(dst, bmp, bytes);
            e->bmp = dst;
        }
        kfree(bmp);
    }
    e->stamp = ++gc_clock;
    return e;
}

int ttf_advance(uint32_t cp)
{
    if (!g_ready)
        return 0;
    int px = base_px * cur_scale;
    int kf;
    stbtt_fontinfo *fi = src_face(cp, &kf);
    int aw = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(fi, (int)cp, &aw, &lsb);
    float st = stbtt_ScaleForPixelHeight(fi, (float)px);
    return (int)(aw * st + 0.5f);
}

static int trans_mode;

void ttf_set_transparent(int on) { trans_mode = on ? 1 : 0; }

int ttf_draw_cp(int x, int y, uint32_t fg, uint32_t bg, uint32_t cp)
{
    if (!g_ready)
        return 0;
    int px = base_px * cur_scale;
    gc_entry *g = gc_lookup(cur_font, px, cp);
    int baseline = y + cur_ascent;
    int gx = x + g->xoff;
    int gy = baseline + g->yoff;

    if (g->bmp) {
        for (int ry = 0; ry < g->h; ry++) {
            int py = gy + ry;
            const uint8_t *row = g->bmp + (size_t)ry * g->w;
            for (int rx = 0; rx < g->w; rx++) {
                uint8_t cov = row[rx];
                int pxx = gx + rx;
                if (cov) {
                    uint32_t under = trans_mode ? fb_getpixel(pxx, py) : bg;
                    fb_putpixel(pxx, py, fb_mix(fg, under, cov));
                }
                /* No background fill for the empty part of the glyph cell.
                   Filling it produced a dark block behind every text run
                   whenever the caller's idea of the background (opaque panel
                   colour) disagreed with what is really under the glyph (a
                   glass panel over wallpaper). The cell is left untouched,
                   so text can never carry its own rectangle with it. */
            }
        }
    }
    g->stamp = ++gc_clock;
    return g->adv > 0 ? g->adv : px / 2;
}

void ttf_clear_cache(void)
{
    for (int i = 0; i < GC_SLOTS; i++) {
        if (gc[i].bmp) {
            kfree(gc[i].bmp);
            gc[i].bmp = 0;
        }
        gc[i].used = 0;
        gc[i].key = 0;
    }
    gc_clock = 0;
}
