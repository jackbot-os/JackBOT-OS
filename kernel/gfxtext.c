#include <stdint.h>
#include <stddef.h>
#include <kernel/gfxtext.h>
#include <kernel/fb.h>
#include "font_glyph.h"

int gfx_text_height(void) { return FONT_H; }

int gfx_draw_char(int x, int y, uint32_t fg, uint32_t bg, char c)
{
    unsigned char code = (unsigned char)c;
    if (code >= 128)
        code = '?';
    const unsigned char *rows = font_glyphs[code];
    for (int ry = 0; ry < FONT_H; ry++) {
        unsigned char bits = rows[ry];
        for (int rx = 0; rx < FONT_W; rx++) {
            uint32_t col = (bits & (0x80 >> rx)) ? fg : bg;
            fb_putpixel(x + rx, y + ry, col);
        }
    }
    return FONT_W;
}

int gfx_draw_string(int x, int y, uint32_t fg, uint32_t bg, const char *s)
{
    int cx = x;
    while (*s) {
        if (*s == '\n') {
            cx = x;
            y += FONT_H;
        } else {
            gfx_draw_char(cx, y, fg, bg, *s);
            cx += FONT_W;
        }
        s++;
    }
    return cx - x;
}

int gfx_text_width(const char *s)
{
    int n = 0;
    while (*s++)
        n++;
    return n * FONT_W;
}
