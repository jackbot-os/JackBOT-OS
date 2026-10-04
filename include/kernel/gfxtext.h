#ifndef GFXTEXT_H
#define GFXTEXT_H
#include <stdint.h>

#define FONT_H 16

struct font_set {
    const char *name;
    const uint8_t (*g8)[16];
    const uint32_t *cp16;
    uint32_t n16;
    const uint8_t (*g16)[32];
};

void gfx_init_fonts(void);
int gfx_font_count(void);
int gfx_set_font(int i);
int gfx_get_font(void);
const char *gfx_font_name(int i);
void gfx_set_scale(int s);
int gfx_get_scale(void);

int gfx_draw_cp(int x, int y, uint32_t fg, uint32_t bg, uint32_t cp);
int gfx_draw_string(int x, int y, uint32_t fg, uint32_t bg, const char *s);
int gfx_text_width(const char *s);
int gfx_text_height(void);

#endif
