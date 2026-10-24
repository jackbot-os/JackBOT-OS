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

/* Filled rounded rectangle: middle band + two capped end bands. */
void ui_round_rect(int x, int y, int w, int h, int r, uint32_t c)
{
    if (w <= 0 || h <= 0)
        return;
    if (r <= 0 || r * 2 >= w || r * 2 >= h) {
        ui_rect(x, y, w, h, c);
        return;
    }
    fb_fill_rect(x + r, y, w - 2 * r, h, c);
    fb_fill_rect(x, y + r, w, h - 2 * r, c);
    for (int yy = 0; yy < r; yy++) {
        int span = 0;
        while (span * span + (r - 1 - yy) * (r - 1 - yy) < r * r)
            span++;
        fb_fill_rect(x + r - span + 1, y + yy, 2 * (span - 1), 1, c);
        fb_fill_rect(x + r - span + 1, y + h - 1 - yy, 2 * (span - 1), 1, c);
    }
}

/* Rounded 1px outline drawn from the rounded mask edges. */
void ui_round_border(int x, int y, int w, int h, int r, uint32_t c)
{
    if (r <= 0 || r * 2 >= w || r * 2 >= h) {
        ui_border(x, y, w, h, c);
        return;
    }
    for (int i = r; i < w - r; i++) {
        fb_putpixel(x + i, y, c);
        fb_putpixel(x + i, y + h - 1, c);
    }
    for (int j = r; j < h - r; j++) {
        fb_putpixel(x, y + j, c);
        fb_putpixel(x + w - 1, y + j, c);
    }
    for (int yy = 0; yy < r; yy++) {
        int span = 0;
        while (span * span + (r - 1 - yy) * (r - 1 - yy) < r * r)
            span++;
        int sx = x + r - span + 1;
        int ex = x + w - 1 - r + span - 1;
        fb_putpixel(sx, y + yy, c);
        fb_putpixel(ex, y + yy, c);
        fb_putpixel(sx, y + h - 1 - yy, c);
        fb_putpixel(ex, y + h - 1 - yy, c);
    }
}

int ui_inside(int px, int py, int x, int y, int w, int h)
{
    return px >= x && px < x + w && py >= y && py < y + h;
}

int ui_button(int x, int y, int w, int h, const char *label, int sel,
              int mx, int my, int clicked)
{
    int S = us;
    int hov = ui_inside(mx, my, x, y, w, h);
    uint32_t base, edge;
    if (sel) {
        base = fb_rgb(38, 128, 96);
        edge = fb_rgb(110, 220, 170);
    } else if (hov) {
        base = fb_rgb(52, 104, 190);
        edge = fb_rgb(140, 180, 250);
    } else {
        base = fb_rgb(44, 58, 92);
        edge = fb_rgb(100, 124, 175);
    }
    ui_round_rect(x, y, w, h, 4 * S, base);
    ui_round_border(x, y, w, h, 4 * S, edge);
    int tw = gfx_text_width(label);
    gfx_draw_string(x + (w - tw) / 2,
                    y + (h - gfx_text_height()) / 2,
                    fb_rgb(240, 245, 255), base, label);
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
    int rad = 7 * S;
    /* window body */
    ui_round_rect(win->x, win->y, win->w, win->h, rad, fb_rgb(24, 27, 38));
    ui_round_border(win->x, win->y, win->w, win->h, rad,
                    fb_rgb(70, 110, 180));
    /* blue title bar (rounded top, square bottom edge) */
    ui_rect(win->x + rad, win->y, win->w - 2 * rad, 26 * S,
            fb_rgb(30, 64, 122));
    ui_rect(win->x, win->y + rad, win->w, 26 * S - rad,
            fb_rgb(30, 64, 122));
    ui_rect(win->x, win->y + 26 * S, win->w, 2 * S,
            fb_rgb(64, 120, 200));
    gfx_draw_string(win->x + 12 * S, win->y + 5 * S,
                    fb_rgb(235, 242, 255), fb_rgb(30, 64, 122), title);
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
