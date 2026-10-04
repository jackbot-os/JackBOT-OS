#ifndef GFXTEXT_H
#define GFXTEXT_H
#include <stdint.h>

int gfx_draw_char(int x, int y, uint32_t fg, uint32_t bg, char c);
int gfx_draw_string(int x, int y, uint32_t fg, uint32_t bg, const char *s);
int gfx_text_width(const char *s);
int gfx_text_height(void);

#endif
