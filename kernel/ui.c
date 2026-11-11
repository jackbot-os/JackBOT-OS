#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ui.h>

/* 0 = daylight palette, 255 = night palette (defined in kernel/desktop.c) */
int desk_night_tint(void);

/* Blend two RGB triples before packing, so the framebuffer format is only
   applied once and no unpacking is needed. */
static uint32_t mix3(int r1, int g1, int b1, int r2, int g2, int b2, int t)
{
    int r = r1 + (r2 - r1) * t / 255;
    int g = g1 + (g2 - g1) * t / 255;
    int b = b1 + (b2 - b1) * t / 255;
    return fb_rgb((uint8_t)r, (uint8_t)g, (uint8_t)b);
}

static int us = 1;
static int glass = 1;

void ui_set_scale(int s) { us = s; }
int ui_scale(void) { return us; }
void ui_set_glass(int on) { glass = on ? 1 : 0; }
int ui_glass(void) { return glass; }

/* Blend-fill helpers: same shapes as the solid versions, but every pixel is
   mixed with what is already on the framebuffer, so the wallpaper and any
   window behind the panel stay visible. */
static void fill_blend(int x, int y, int w, int h, uint32_t c, int cov)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            fb_putpixel_blend(x + i, y + j, c, (uint8_t)cov);
}

static void round_blend(int x, int y, int w, int h, int r, uint32_t c, int cov)
{
    if (w <= 0 || h <= 0)
        return;
    if (r <= 0 || r * 2 >= w || r * 2 >= h) {
        fill_blend(x, y, w, h, c, cov);
        return;
    }
    fill_blend(x + r, y, w - 2 * r, h, c, cov);
    fill_blend(x, y + r, w, h - 2 * r, c, cov);
    for (int yy = 0; yy < r; yy++) {
        int span = 0;
        while (span * span + (r - 1 - yy) * (r - 1 - yy) < r * r)
            span++;
        fill_blend(x + r - span + 1, y + yy, 2 * (span - 1), 1, c, cov);
        fill_blend(x + r - span + 1, y + h - 1 - yy, 2 * (span - 1), 1, c, cov);
    }
}

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

    /* Window chrome follows the clock, not the wallpaper: 0 = daylight palette,
       255 = night palette, with a one-hour ramp either side. The desktop picture
       is never touched by this. */
    int night = desk_night_tint();
    uint32_t body_c   = mix3(246, 249, 254,  24,  27,  38, night);
    uint32_t title_c  = mix3( 58, 112, 198,  30,  64, 122, night);
    uint32_t edge_c   = mix3(140, 176, 232,  70, 110, 180, night);
    uint32_t accent_c = mix3(150, 190, 240,  64, 120, 200, night);
    uint32_t tfg      = mix3( 26,  42,  72, 235, 242, 255, night);

    /* window body: frosted when liquid glass is on, solid otherwise */
    if (glass)
        round_blend(win->x, win->y, win->w, win->h, rad, body_c, 170);
    else
        ui_round_rect(win->x, win->y, win->w, win->h, rad, body_c);
    ui_round_border(win->x, win->y, win->w, win->h, rad, edge_c);
    /* title bar (rounded top, square bottom edge) */
    if (glass) {
        fill_blend(win->x + rad, win->y, win->w - 2 * rad, 26 * S, title_c, 175);
        fill_blend(win->x, win->y + rad, win->w, 26 * S - rad, title_c, 175);
    } else {
        ui_rect(win->x + rad, win->y, win->w - 2 * rad, 26 * S, title_c);
        ui_rect(win->x, win->y + rad, win->w, 26 * S - rad, title_c);
    }
    ui_rect(win->x, win->y + 26 * S, win->w, 2 * S, accent_c);
    gfx_draw_string(win->x + 12 * S, win->y + 5 * S, tfg, title_c, title);
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
