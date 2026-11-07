#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/serial.h>
#include <kernel/i18n.h>
#include "i18n_data.h"

uint64_t timer_get_ticks(void);

#define SPLASH_TICKS 250

void splash_show(void)
{
    if (!fb_present())
        return;

    uint32_t black = fb_rgb(0, 0, 0);
    uint32_t white = fb_rgb(255, 255, 255);
    fb_clear(black);

    gfx_init_fonts();

    int W = (int)fb_width(), H = (int)fb_height();

    const char *msg = "Welcome";
    gfx_set_scale(1);
    int raw = gfx_text_width_wide(msg);
    int scale = (W * 48 / 100) / raw;
    if (scale < 1)
        scale = 1;
    if (scale > 16)
        scale = 16;
    gfx_set_scale(scale);

    int tw = gfx_text_width_wide(msg);
    int th = gfx_text_height();
    int x = (W - tw) / 2;
    int y = (H - th) / 2;
    gfx_draw_string_wide(x, y, white, black, msg);

    gfx_set_scale(2);
    const char *sub = tr(STR_SPLASH_SUBTITLE);
    int sw = gfx_text_width_wide(sub);
    int sh = gfx_text_height();
    gfx_draw_string_wide((W - sw) / 2, y + th + sh + 18, white, black, sub);
    gfx_set_scale(scale);

    fb_present_all();

    uint64_t t0 = timer_get_ticks();
    while (timer_get_ticks() - t0 < SPLASH_TICKS)
        __asm__ volatile("hlt");

    serial_puts("[SPLASH] welcome shown\n");
}
