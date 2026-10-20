#ifndef KERNEL_TTF_H
#define KERNEL_TTF_H
#include <stdint.h>

#define TTF_FONT_SANS  0
#define TTF_FONT_MONO  1
#define TTF_FONT_MONOB 2
#define TTF_FONT_COUNT  3

enum {
    TTF_PX_SMALL  = 13,
    TTF_PX_NORMAL = 17,
    TTF_PX_LARGE  = 24,
    TTF_PX_TITLE  = 34,
};

int  ttf_init(void);
int  ttf_ready(void);

void ttf_set_font(int i);
int  ttf_get_font(void);

void ttf_set_scale(int s);
int  ttf_get_scale(void);

void ttf_set_base_px(int px);
int  ttf_base_px(void);
int  ttf_pixel_height(void);
void ttf_set_pixel_height(int px);

int  ttf_draw_cp(int x, int y, uint32_t fg, uint32_t bg, uint32_t cp);
int  ttf_advance(uint32_t cp);
int  ttf_line_height(void);
int  ttf_ascent(void);

void ttf_clear_cache(void);

#endif
