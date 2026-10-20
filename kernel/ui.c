#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ui.h>

static int us = 1;

void ui_set_scale(int s) { us = s; }
int ui_scale(void) { return us; }

void ui_rect(int x, int y, int w, int h, uint32_t c)
{
    if (w <= 0 || h <= 0)
        return;
    fb_fill_rect(x, y, w, h, c);
}

void ui_border(int x, int y, int w, int h, uint32_t c)
{
    for (int i = 0; i < w; i++) {
        fb_putpixel(x + i, y, c);
        fb_putpixel(x + i, y + h - 1, c);
    }
    for (int j = 0; j < h; j++) {
        fb_putpixel(x, y + j, c);
        fb_putpixel(x + w - 1, y + j, c);
    }
}

int ui_inside(int px, int py, int x, int y, int w, int h)
{
    return px >= x && px < x + w && py >= y && py < y + h;
}

int ui_button(int x, int y, int w, int h, const char *label, int sel,
              int mx, int my, int clicked)
{
    int hov = ui_inside(mx, my, x, y, w, h);
    uint32_t base;
    if (sel)
        base = fb_rgb(60, 120, 90);
    else if (hov)
        base = fb_rgb(70, 110, 190);
    else
        base = fb_rgb(48, 60, 92);
    ui_rect(x, y, w, h, base);
    ui_border(x, y, w, h, fb_rgb(120, 140, 190));
    int tw = gfx_text_width(label);
    gfx_draw_string(x + (w - tw) / 2,
                    y + (h - gfx_text_height()) / 2,
                    fb_rgb(235, 240, 250), base, label);
    return clicked && hov;
}

void ui_fit(int design_w, int design_h, struct ui_win *win)
{
    int s = us;
    int W = (int)fb_width();
    int H = (int)fb_height();
    int bar = 28 * s;
    int Hc = H - bar;
    int m = 10 * s;
    int w = design_w * s;
    int h = design_h * s;
    if (w > W - 2 * m)
        w = W - 2 * m;
    if (h > Hc - 2 * m)
        h = Hc - 2 * m;
    win->w = w;
    win->h = h;
    win->x = (W - w) / 2;
    win->y = (Hc - h) / 2;
    if (win->x < m) win->x = m;
    if (win->y < m) win->y = m;
}

static struct ui_win last_win;
static int last_valid;

int ui_window(struct ui_win *win, const char *title,
              int mx, int my, int clicked)
{
    int S = us;
    last_win = *win;
    last_valid = 1;
    ui_rect(win->x, win->y, win->w, win->h, fb_rgb(30, 34, 46));
    ui_border(win->x, win->y, win->w, win->h, fb_rgb(90, 110, 160));
    ui_rect(win->x, win->y, win->w, 26 * S, fb_rgb(42, 52, 78));
    gfx_draw_string(win->x + 10 * S, win->y + 5 * S,
                    fb_rgb(235, 240, 250), fb_rgb(42, 52, 78), title);
    return ui_button(win->x + win->w - 66 * S, win->y + 3 * S,
                     56 * S, 20 * S, "X", 0, mx, my, clicked);
}

int ui_close_hit(int mx, int my)
{
    int S = us;
    if (!last_valid)
        return 0;
    return ui_inside(mx, my, last_win.x + last_win.w - 66 * S,
                     last_win.y + 3 * S, 56 * S, 20 * S);
}
