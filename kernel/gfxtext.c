#include <stdint.h>
#include <stddef.h>
#include <kernel/gfxtext.h>
#include <kernel/fb.h>
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
    return i;
}
const char *gfx_font_name(int i)
{
    return (i >= 0 && i < 3) ? fonts[i]->name : "?";
}
void gfx_set_scale(int s) { scale = (s >= 1 && s <= 3) ? s : 1; }
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
            cy += FONT_H * scale;
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

int gfx_text_height(void) { return FONT_H * scale; }
