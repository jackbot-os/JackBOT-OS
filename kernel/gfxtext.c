#include <stdint.h>
#include <stddef.h>
#include <kernel/gfxtext.h>
#include <kernel/fb.h>
#include <kernel/ttf.h>
#include "font_sans.h"
#include "font_mono.h"
#include "font_mono_bold.h"

static const struct font_set *fonts[3];
static const struct font_set *cur;
static int scale = 1;

void gfx_init_fonts(void)
{
    fonts[0] = &font_sans;
    fonts[1] = &font_mono;
    fonts[2] = &font_mono_bold;
    cur = fonts[0];
    scale = 1;
    ttf_init();
}

int gfx_font_count(void) { return 3; }
int gfx_get_font(void)
{
    for (int i = 0; i < 3; i++)
        if (fonts[i] == cur)
            return i;
    return 0;
}
int gfx_set_font(int i)
{
    if (i < 0 || i >= 3)
        return gfx_get_font();
    cur = fonts[i];
    ttf_set_font(i);
    return i;
}
const char *gfx_font_name(int i)
{
    return (i >= 0 && i < 3) ? fonts[i]->name : "?";
}
void gfx_set_scale(int s) { scale = (s >= 1 && s <= 16) ? s : 1; ttf_set_scale(scale); }
int gfx_get_scale(void) { return scale; }

static const uint8_t *find_glyph(uint32_t cp, int *w)
{
    if (cp < 256) {
        *w = 8;
        return cur->g8[cp];
    }
    uint32_t lo = 0, hi = cur->n16;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        uint32_t c = cur->cp16[mid];
        if (c == cp) {
            *w = 16;
            return cur->g16[mid];
        }
        if (c < cp)
            lo = mid + 1;
        else
            hi = mid;
    }
    *w = 8;
    return cur->g8['?'];
}

int gfx_draw_cp(int x, int y, uint32_t fg, uint32_t bg, uint32_t cp)
{
    if (ttf_ready())
        return ttf_draw_cp(x, y, fg, bg, cp);
    int w;
    const uint8_t *rows = find_glyph(cp, &w);
    int nbytes = (w + 7) / 8;
    for (int ry = 0; ry < FONT_H; ry++) {
        for (int rx = 0; rx < w; rx++) {
            int on = rows[ry * nbytes + rx / 8] & (0x80 >> (rx % 8));
            uint32_t col = on ? fg : bg;
            for (int sy = 0; sy < scale; sy++)
                for (int sx = 0; sx < scale; sx++)
                    fb_putpixel(x + rx * scale + sx,
                                y + ry * scale + sy, col);
        }
    }
    return w * scale;
}

static uint32_t decode_utf8(const char **p)
{
    unsigned char c = (unsigned char)**p;
    if (c < 0x80) {
        (*p)++;
        return c;
    }
    int n;
    uint32_t cp;
    if ((c & 0xE0) == 0xC0) {
        n = 1; cp = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
        n = 2; cp = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
        n = 3; cp = c & 0x07;
    } else {
        (*p)++;
        return '?';
    }
    (*p)++;
    for (int i = 0; i < n; i++) {
        unsigned char cc = (unsigned char)**p;
        if ((cc & 0xC0) != 0x80)
            return cp;
        cp = (cp << 6) | (cc & 0x3F);
        (*p)++;
    }
    return cp;
}

static int cp_advance(uint32_t cp)
{
    if (ttf_ready())
        return ttf_advance(cp);
    int w;
    find_glyph(cp, &w);
    return w * scale;
}

int gfx_draw_string(int x, int y, uint32_t fg, uint32_t bg, const char *s)
{
    int cx = x, cy = y;
    const char *p = s;
    while (*p) {
        if (*p == '\n') {
            cx = x;
            cy += gfx_text_height();
            p++;
            continue;
        }
        uint32_t cp = decode_utf8(&p);
        cx += gfx_draw_cp(cx, cy, fg, bg, cp);
    }
    return cx - x;
}

int gfx_text_width(const char *s)
{
    int width = 0;
    const char *p = s;
    while (*p) {
        if (*p == '\n') {
            p++;
            continue;
        }
        width += cp_advance(decode_utf8(&p));
    }
    return width;
}

int gfx_text_height(void)
{
    if (ttf_ready())
        return ttf_line_height();
    return FONT_H * scale;
}

#define WIDE_GAP 2

struct gw {
    const uint8_t *rows;
    int w;
    int l;
    int r;
};

static struct gw find_glyph_w(uint32_t cp)
{
    struct gw g;
    g.rows = 0;
    g.w = 8;
    uint32_t lo = 0, hi = cur->n16;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        uint32_t c = cur->cp16[mid];
        if (c == cp) {
            g.rows = cur->g16[mid];
            g.w = 16;
            break;
        }
        if (c < cp)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (!g.rows) {
        g.rows = (cp < 256) ? cur->g8[cp] : cur->g8['?'];
        g.w = 8;
    }
    int nbytes = (g.w + 7) / 8;
    g.l = g.w;
    g.r = -1;
    for (int ry = 0; ry < FONT_H; ry++) {
        for (int rx = 0; rx < g.w; rx++) {
            if (g.rows[ry * nbytes + rx / 8] & (0x80 >> (rx % 8))) {
                if (rx < g.l)
                    g.l = rx;
                if (rx > g.r)
                    g.r = rx;
            }
        }
    }
    if (g.r < 0) {
        g.l = 0;
        g.r = 0;
    }
    return g;
}

static int gfx_draw_cp_w(int x, int y, uint32_t fg, uint32_t bg, uint32_t cp)
{
    if (ttf_ready())
        return ttf_draw_cp(x, y, fg, bg, cp);
    struct gw g = find_glyph_w(cp);
    int nbytes = (g.w + 7) / 8;
    int ink = g.r - g.l + 1;
    for (int ry = 0; ry < FONT_H; ry++) {
        for (int rx = g.l; rx <= g.r; rx++) {
            int on = g.rows[ry * nbytes + rx / 8] & (0x80 >> (rx % 8));
            uint32_t col = on ? fg : bg;
            for (int sy = 0; sy < scale; sy++)
                for (int sx = 0; sx < scale; sx++)
                    fb_putpixel(x + (rx - g.l) * scale + sx,
                                y + ry * scale + sy, col);
        }
    }
    return (ink + WIDE_GAP) * scale;
}

int gfx_draw_string_wide(int x, int y, uint32_t fg, uint32_t bg, const char *s)
{
    int cx = x, cy = y;
    const char *p = s;
    while (*p) {
        if (*p == '\n') {
            cx = x;
            cy += gfx_text_height();
            p++;
            continue;
        }
        uint32_t cp = decode_utf8(&p);
        cx += gfx_draw_cp_w(cx, cy, fg, bg, cp);
    }
    return cx - x;
}

int gfx_text_width_wide(const char *s)
{
    if (ttf_ready()) {
        int width = 0;
        const char *p = s;
        while (*p) {
            if (*p == '\n') {
                p++;
                continue;
            }
            width += ttf_advance(decode_utf8(&p));
        }
        return width;
    }
    int width = 0;
    int n = 0;
    const char *p = s;
    while (*p) {
        if (*p == '\n') {
            p++;
            continue;
        }
        struct gw g = find_glyph_w(decode_utf8(&p));
        width += (g.r - g.l + 1) + WIDE_GAP;
        n++;
    }
    if (n)
        width -= WIDE_GAP;
    return width * scale;
}

/* ===================== RTL / Arabic shaping ===================== */

#define RTL_MAX_CP 384

struct aform {
    uint16_t cp;
    uint16_t iso, fin, ini, med; /* 0 = form does not exist */
};

/* Arabic Presentation Forms-B (U+FE70..U+FEFE), standard code point order.
   right-only letters have no ini/med; hamza (0621) is non-joining. */
static const struct aform atab[] = {
    {0x0622, 0xFE81, 0xFE82, 0,      0},
    {0x0623, 0xFE83, 0xFE84, 0,      0},
    {0x0624, 0xFE85, 0xFE86, 0,      0},
    {0x0625, 0xFE87, 0xFE88, 0,      0},
    {0x0626, 0xFE89, 0xFE8A, 0xFE8B, 0xFE8C},
    {0x0627, 0xFE8D, 0xFE8E, 0,      0},
    {0x0628, 0xFE8F, 0xFE90, 0xFE91, 0xFE92},
    {0x0629, 0xFE93, 0xFE94, 0,      0},
    {0x062A, 0xFE95, 0xFE96, 0xFE97, 0xFE98},
    {0x062B, 0xFE99, 0xFE9A, 0xFE9B, 0xFE9C},
    {0x062C, 0xFE9D, 0xFE9E, 0xFE9F, 0xFEA0},
    {0x062D, 0xFEA1, 0xFEA2, 0xFEA3, 0xFEA4},
    {0x062E, 0xFEA5, 0xFEA6, 0xFEA7, 0xFEA8},
    {0x062F, 0xFEA9, 0xFEAA, 0,      0},
    {0x0630, 0xFEAB, 0xFEAC, 0,      0},
    {0x0631, 0xFEAD, 0xFEAE, 0,      0},
    {0x0632, 0xFEAF, 0xFEB0, 0,      0},
    {0x0633, 0xFEB1, 0xFEB2, 0xFEB3, 0xFEB4},
    {0x0634, 0xFEB5, 0xFEB6, 0xFEB7, 0xFEB8},
    {0x0635, 0xFEB9, 0xFEBA, 0xFEBB, 0xFEBC},
    {0x0636, 0xFEBD, 0xFEBE, 0xFEBF, 0xFEC0},
    {0x0637, 0xFEC1, 0xFEC2, 0xFEC3, 0xFEC4},
    {0x0638, 0xFEC5, 0xFEC6, 0xFEC7, 0xFEC8},
    {0x0639, 0xFEC9, 0xFECA, 0xFECB, 0xFECC},
    {0x063A, 0xFECD, 0xFECE, 0xFECF, 0xFED0},
    {0x0641, 0xFED1, 0xFED2, 0xFED3, 0xFED4},
    {0x0642, 0xFED5, 0xFED6, 0xFED7, 0xFED8},
    {0x0643, 0xFED9, 0xFEDA, 0xFEDB, 0xFEDC},
    {0x0644, 0xFEDD, 0xFEDE, 0xFEDF, 0xFEE0},
    {0x0645, 0xFEE1, 0xFEE2, 0xFEE3, 0xFEE4},
    {0x0646, 0xFEE5, 0xFEE6, 0xFEE7, 0xFEE8},
    {0x0647, 0xFEE9, 0xFEEA, 0xFEEB, 0xFEEC},
    {0x0648, 0xFEED, 0xFEEE, 0,      0},
    {0x0649, 0xFEEF, 0xFEF0, 0,      0},
    {0x064A, 0xFEF1, 0xFEF2, 0xFEF3, 0xFEF4},
};

static const struct aform *afind(uint32_t cp)
{
    int lo = 0, hi = (int)(sizeof(atab) / sizeof(atab[0]));
    while (lo < hi) {
        int m = (lo + hi) / 2;
        if (atab[m].cp == cp)
            return &atab[m];
        if (atab[m].cp < cp)
            lo = m + 1;
        else
            hi = m;
    }
    return 0;
}

static int is_arabic_cp(uint32_t cp)
{
    return (cp >= 0x0600 && cp <= 0x06FF) ||
           (cp >= 0x0750 && cp <= 0x077F) ||
           (cp >= 0xFE70 && cp <= 0xFEFE);
}

/* letter joins the next letter on its left (dual joining + tatweel) */
static int joins_next(uint32_t cp)
{
    if (cp == 0x0640)
        return 1;
    const struct aform *a = afind(cp);
    return a && a->ini;
}
/* letter joins the previous letter on its right */
static int joins_prev(uint32_t cp)
{
    if (cp == 0x0640)
        return 1;
    const struct aform *a = afind(cp);
    return a && a->fin;
}

/* Returns number of shaped codepoints written to out. */
static int shape_arabic(const uint32_t *in, int n, uint32_t *out)
{
    int o = 0;
    for (int i = 0; i < n; i++) {
        uint32_t cp = in[i];
        uint32_t prev = 0, next = 0;
        int pi = i - 1;
        while (pi >= 0 && in[pi] >= 0x064B && in[pi] <= 0x0652)
            pi--;
        if (pi >= 0)
            prev = in[pi];
        int ni = i + 1;
        while (ni < n && in[ni] >= 0x064B && in[ni] <= 0x0652)
            ni++;
        if (ni < n)
            next = in[ni];

        int pj = (prev != 0) && joins_next(prev) && joins_prev(cp);
        int nj = (next != 0) && joins_next(cp) && joins_prev(next);

        /* lam + alef mandatory ligature.  Forms verified against Unicode
           Presentation Forms-B (FEF5..FEFC). */
        if (cp == 0x0644 &&
            (next == 0x0622 || next == 0x0623 ||
             next == 0x0625 || next == 0x0627)) {
            uint16_t lig;
            switch (next) {
            case 0x0622: lig = pj ? 0xFEF6 : 0xFEF5; break;
            case 0x0623: lig = pj ? 0xFEF8 : 0xFEF7; break;
            case 0x0625: lig = pj ? 0xFEFA : 0xFEF9; break;
            case 0x0627: lig = pj ? 0xFEFC : 0xFEFB; break;
            default:     lig = 0xFEFB; break;
            }
            out[o++] = lig;
            i++; /* consume alef */
            /* trailing combining marks after the alef */
            while (i + 1 < n && in[i + 1] >= 0x064B && in[i + 1] <= 0x0652)
                out[o++] = in[++i];
            continue;
        }

        const struct aform *a = afind(cp);
        if (a) {
            if (pj && nj)
                out[o++] = a->med ? a->med : a->fin;
            else if (pj)
                out[o++] = a->fin;
            else if (nj)
                out[o++] = a->ini ? a->ini : a->iso;
            else
                out[o++] = a->iso;
        } else {
            out[o++] = cp;
        }
    }
    return o;
}

/* Strong direction classes for a simplified bidi pass. */
enum { D_R, D_L, D_N };

static int cp_dir(uint32_t cp)
{
    if (is_arabic_cp(cp) || cp == 0x061F || cp == 0x060C)
        return D_R;
    if ((cp >= 0x0030 && cp <= 0x0039) ||
        (cp >= 0x0660 && cp <= 0x0669) ||
        (cp >= 0x06F0 && cp <= 0x06F9))
        return D_L;
    if (cp < 0x0600)
        return D_L;
    if ((cp >= 0x00400 && cp <= 0x024F))
        return D_L;
    return D_N;
}

static int decode_all(const char *s, uint32_t *cps)
{
    int n = 0;
    const char *p = s;
    while (*p && n < RTL_MAX_CP - 1)
        cps[n++] = decode_utf8(&p);
    cps[n] = 0;
    return n;
}

/* Shape + bidi, returning logical direction runs (each run keeps logical
   codepoint order). Visual order is the run list reversed. */
struct dir_run {
    int dir;
    int start;
    int len;
};

static int rtl_prepare(const char *s, uint32_t *shaped, struct dir_run *runs,
                       int maxruns)
{
    static uint32_t raw[RTL_MAX_CP];
    int n = decode_all(s, raw);
    n = shape_arabic(raw, n, shaped);

    int nr = 0;
    int cur = D_R;
    for (int i = 0; i < n; i++) {
        int d = cp_dir(shaped[i]);
        if (d == D_N)
            d = cur;
        if (nr == 0 || d != runs[nr - 1].dir) {
            if (nr >= maxruns)
                break;
            runs[nr].dir = d;
            runs[nr].start = i;
            runs[nr].len = 0;
            nr++;
            cur = d;
        }
        runs[nr - 1].len++;
    }
    return nr;
}

int gfx_text_width_rtl(const char *s)
{
    static uint32_t sh[RTL_MAX_CP];
    static struct dir_run runs[24];
    int nr = rtl_prepare(s, sh, runs, 24);
    int width = 0;
    for (int r = 0; r < nr; r++)
        for (int k = 0; k < runs[r].len; k++)
            width += cp_advance(sh[runs[r].start + k]);
    return width;
}

int gfx_draw_string_rtl(int right_x, int y, uint32_t fg, uint32_t bg,
                        const char *s)
{
    static uint32_t sh[RTL_MAX_CP];
    static struct dir_run runs[24];
    int nr = rtl_prepare(s, sh, runs, 24);

    int cx = right_x;
    /* Visual order: runs reversed. Within an R run codepoints are painted
       right-to-left; within an L run they stay left-to-right. */
    for (int r = nr - 1; r >= 0; r--) {
        if (runs[r].dir == D_R) {
            for (int k = 0; k < runs[r].len; k++) {
                uint32_t cp = sh[runs[r].start + k];
                int w = cp_advance(cp);
                gfx_draw_cp_w(cx - w, y, fg, bg, cp);
                cx -= w;
            }
        } else {
            int w = 0;
            for (int k = 0; k < runs[r].len; k++)
                w += cp_advance(sh[runs[r].start + k]);
            int x0 = cx - w;
            for (int k = 0; k < runs[r].len; k++)
                x0 += gfx_draw_cp_w(x0, y, fg, bg,
                                    sh[runs[r].start + k]);
            cx -= w;
        }
    }
    return right_x - cx;
}
