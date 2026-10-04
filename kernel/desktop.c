#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ps2.h>
#include <kernel/i18n.h>
#include "i18n_data.h"
#include <kernel/heap.h>
#include <kernel/config.h>
#include <kernel/desktop.h>

#define N_WP 6
#define CAROUSEL_TICKS 250
#define CUR_W 11
#define CUR_H 18

uint64_t timer_get_ticks(void);

static int S;
static int BAR;
static int wp_index;
static int carousel;
static uint64_t last_cycle;
static int win_id;
static int win_painted;
static int need_redraw;
static uint32_t *wp_buf;
static int wp_dirty;

static const char *wp_names[N_WP] = {
    "Ocean Blue", "Sunset", "Teal", "Night Glow", "Stripes", "Grid",
};

static uint32_t lerp_c(uint32_t a, uint32_t b, int t)
{
    int ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255;
    int br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
    int r = ar + ((br - ar) * t) / 255;
    int g = ag + ((bg - ag) * t) / 255;
    int bl = ab + ((bb - ab) * t) / 255;
    return fb_rgb(r, g, bl);
}

static uint32_t wp_color(int idx, int x, int y, int w, int h)
{
    int u = (x * 255) / (w - 1);
    int v = (y * 255) / (h - 1);
    switch (idx) {
    case 0:
        return lerp_c(fb_rgb(18, 42, 96), fb_rgb(70, 140, 220), v);
    case 1: {
        int t = (u + v) / 2;
        return lerp_c(fb_rgb(90, 30, 110), fb_rgb(240, 140, 70), t);
    }
    case 2:
        return lerp_c(fb_rgb(8, 70, 78), fb_rgb(40, 180, 150), u);
    case 3: {
        int dx = x - w / 2, dy = y - h / 2;
        int d = (dx * dx + dy * dy);
        int t = 255 - (d / 1400);
        if (t < 0) t = 0;
        return lerp_c(fb_rgb(10, 10, 24), fb_rgb(70, 90, 170), t);
    }
    case 4: {
        int s = (x + y) / 24;
        return (s & 1) ? fb_rgb(40, 60, 120) : fb_rgb(120, 60, 140);
    }
    default: {
        int gx = (x / 48) & 1, gy = (y / 48) & 1;
        uint32_t c = (gx ^ gy) ? fb_rgb(34, 38, 52) : fb_rgb(44, 49, 66);
        int lx = x % 48, ly = y % 48;
        if (lx == 0 || ly == 0)
            c = fb_rgb(70, 80, 110);
        return c;
    }
    }
}

static void render_wallpaper(void)
{
    int w = (int)fb_width(), H = (int)fb_height(), h = H - BAR;
    if (wp_dirty) {
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                wp_buf[(uint64_t)y * w + x] =
                    wp_color(wp_index, x, y, w, h);
        for (int y = h; y < H; y++)
            for (int x = 0; x < w; x++)
                wp_buf[(uint64_t)y * w + x] = fb_rgb(20, 22, 30);
        wp_dirty = 0;
    }
    fb_blit_full(wp_buf);
}

static void draw_rect(int x, int y, int w, int h, uint32_t c)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            fb_putpixel(x + i, y + j, c);
}

static void draw_border(int x, int y, int w, int h, uint32_t c)
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

static int inside(int px, int py, int x, int y, int w, int h)
{
    return px >= x && px < x + w && py >= y && py < y + h;
}

static int button2(int x, int y, int w, int h, const char *label, int sel,
                   int mx, int my, int clicked)
{
    int hov = inside(mx, my, x, y, w, h);
    uint32_t base;
    if (sel)
        base = fb_rgb(60, 120, 90);
    else if (hov)
        base = fb_rgb(70, 110, 190);
    else
        base = fb_rgb(48, 60, 92);
    draw_rect(x, y, w, h, base);
    draw_border(x, y, w, h, fb_rgb(120, 140, 190));
    int tw = gfx_text_width(label);
    gfx_draw_string(x + (w - tw) / 2,
                    y + (h - gfx_text_height()) / 2,
                    fb_rgb(235, 240, 250), base, label);
    return clicked && hov;
}

static uint8_t cmos(int reg)
{
    uint8_t r = (uint8_t)reg, v;
    __asm__ volatile("outb %0, %1" : : "a"(r), "N"((uint16_t)0x70));
    __asm__ volatile("inb %1, %0" : "=a"(v) : "N"((uint16_t)0x71));
    return v;
}
static int bcd(uint8_t v) { return (v & 0xf) + ((v >> 4) * 10); }

static void draw_taskbar(int mx, int my, int clicked, int *wp_click,
                         int *set_click)
{
    int w = (int)fb_width(), top = (int)fb_height() - BAR;
    draw_rect(0, top, w, BAR, fb_rgb(24, 27, 36));
    gfx_draw_string(10 * S, top + 6 * S, fb_rgb(120, 200, 255),
                    fb_rgb(24, 27, 36), "JackBOT OS");
    if (button2(140 * S, top + 4 * S, 120 * S, BAR - 8 * S,
                tr(STR_WALLPAPER), 0, mx, my, clicked))
        *wp_click = 1;
    if (button2(270 * S, top + 4 * S, 110 * S, BAR - 8 * S,
                tr(STR_SETTINGS), 0, mx, my, clicked))
        *set_click = 1;

    int hh = bcd(cmos(4)), mm = bcd(cmos(2)), ss = bcd(cmos(0));
    char clk[16];
    const char *d = "0123456789";
    clk[0] = d[hh / 10]; clk[1] = d[hh % 10]; clk[2] = ':';
    clk[3] = d[mm / 10]; clk[4] = d[mm % 10]; clk[5] = ':';
    clk[6] = d[ss / 10]; clk[7] = d[ss % 10]; clk[8] = 0;
    int cw = gfx_text_width(clk);
    gfx_draw_string(w - cw - 12 * S, top + 6 * S, fb_rgb(220, 225, 235),
                    fb_rgb(24, 27, 36), clk);
}

static int pWX, pWY, pWW, pWH, CW, CH;

static void thumb_geom(int i, int *tx, int *ty)
{
    int col = i % 3, row = i / 3;
    *tx = pWX + 50 * S + col * (CW + 40 * S);
    *ty = pWY + 50 * S + row * (CH + 46 * S);
}

static void paint_personalization(void)
{
    draw_rect(pWX, pWY, pWW, pWH, fb_rgb(30, 34, 46));
    draw_border(pWX, pWY, pWW, pWH, fb_rgb(90, 110, 160));
    draw_rect(pWX, pWY, pWW, 26 * S, fb_rgb(42, 52, 78));
    gfx_draw_string(pWX + 10 * S, pWY + 5 * S, fb_rgb(235, 240, 250),
                    fb_rgb(42, 52, 78), tr(STR_PERSONALIZATION));
    for (int i = 0; i < N_WP; i++) {
        int tx, ty;
        thumb_geom(i, &tx, &ty);
        for (int yy = 0; yy < CH; yy++)
            for (int xx = 0; xx < CW; xx++)
                fb_putpixel(tx + xx, ty + yy,
                            wp_color(i, xx, yy, CW, CH));
        draw_border(tx, ty, CW, CH,
                    (i == wp_index) ? fb_rgb(255, 210, 80)
                                    : fb_rgb(120, 130, 160));
        int tw = gfx_text_width(wp_names[i]);
        gfx_draw_string(tx + (CW - tw) / 2, ty + CH + 4 * S,
                        fb_rgb(225, 230, 240), fb_rgb(30, 34, 46),
                        wp_names[i]);
    }
    gfx_draw_string(pWX + 230 * S, pWY + pWH - 36 * S,
                    fb_rgb(180, 190, 210), fb_rgb(30, 34, 46),
                    tr(STR_CLICK_HINT));
}

static void draw_personalization(int mx, int my, int clicked)
{
    if (!win_painted) {
        paint_personalization();
        win_painted = 1;
    }
    if (button2(pWX + pWW - 70 * S, pWY + 3 * S, 60 * S, 20 * S,
                tr(STR_CLOSE), 0, mx, my, clicked)) {
        win_id = 0;
        need_redraw = 1;
        return;
    }
    for (int i = 0; i < N_WP; i++) {
        int tx, ty;
        thumb_geom(i, &tx, &ty);
        if (clicked && inside(mx, my, tx, ty, CW, CH)) {
            wp_index = i;
            wp_dirty = 1;
            need_redraw = 1;
            win_painted = 0;
        }
    }
    const char *cl = carousel ? tr(STR_CAROUSEL_ON) : tr(STR_CAROUSEL_OFF);
    if (button2(pWX + 40 * S, pWY + pWH - 46 * S, 170 * S, 32 * S,
                cl, 0, mx, my, clicked)) {
        carousel ^= 1;
        last_cycle = timer_get_ticks();
        win_painted = 0;
    }
}

static int sWX, sWY, sWW, sWH;

static void paint_settings(void)
{
    draw_rect(sWX, sWY, sWW, sWH, fb_rgb(30, 34, 46));
    draw_border(sWX, sWY, sWW, sWH, fb_rgb(90, 110, 160));
    draw_rect(sWX, sWY, sWW, 26 * S, fb_rgb(42, 52, 78));
    gfx_draw_string(sWX + 10 * S, sWY + 5 * S, fb_rgb(235, 240, 250),
                    fb_rgb(42, 52, 78), tr(STR_SETTINGS));
    gfx_draw_string(sWX + 30 * S, sWY + 38 * S, fb_rgb(150, 210, 255),
                    fb_rgb(30, 34, 46), tr(STR_LANGUAGE));
    gfx_draw_string(sWX + 370 * S, sWY + 38 * S, fb_rgb(150, 210, 255),
                    fb_rgb(30, 34, 46), tr(STR_FONT));
}

static void draw_settings(int mx, int my, int clicked)
{
    if (!win_painted) {
        paint_settings();
        win_painted = 1;
    }
    if (button2(sWX + sWW - 70 * S, sWY + 3 * S, 60 * S, 20 * S,
                tr(STR_CLOSE), 0, mx, my, clicked)) {
        win_id = 0;
        need_redraw = 1;
        return;
    }
    int rh = 34 * S;
    for (int i = 0; i < i18n_count(); i++) {
        int x = sWX + 30 * S, y = sWY + 60 * S + i * (rh + 6 * S);
        if (button2(x, y, 300 * S, rh, i18n_name(i), i == i18n_lang(),
                    mx, my, clicked)) {
            i18n_set_lang(i);
            config_set_language(i);
            config_save();
            win_painted = 0;
            need_redraw = 1;
        }
    }
    for (int i = 0; i < gfx_font_count(); i++) {
        int x = sWX + 370 * S, y = sWY + 60 * S + i * (rh + 6 * S);
        if (button2(x, y, 280 * S, rh, gfx_font_name(i),
                    i == gfx_get_font(), mx, my, clicked)) {
            gfx_set_font(i);
            config_set_font(i);
            config_save();
            win_painted = 0;
            need_redraw = 1;
        }
    }
}

static uint32_t cur_save[CUR_W * CUR_H * 9];
static int cur_have, cur_sx, cur_sy;
static const uint8_t cur_bits[CUR_H] = {
    0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f, 0xff,
    0x7f, 0x3f, 0x1f, 0x3b, 0x17, 0x03, 0x00, 0x00, 0x00, 0x00,
};

static int cw_px(void) { return CUR_W * S; }
static int ch_px(void) { return CUR_H * S; }

static void cursor_restore(void)
{
    if (!cur_have)
        return;
    int w = cw_px(), h = ch_px();
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            fb_putpixel(cur_sx + i, cur_sy + j, cur_save[j * w + i]);
    cur_have = 0;
}

static void cursor_draw(int x, int y)
{
    int w = cw_px(), h = ch_px();
    if (x < 0 || y < 0 || x + w > (int)fb_width() ||
        y + h > (int)fb_height())
        return;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            cur_save[j * w + i] = fb_getpixel(x + i, y + j);
    cur_have = 1;
    cur_sx = x;
    cur_sy = y;
    for (int j = 0; j < CUR_H; j++) {
        uint8_t bits = cur_bits[j];
        for (int i = 0; i < CUR_W; i++) {
            uint32_t c = 0;
            if (bits & (1 << i))
                c = fb_rgb(255, 255, 255);
            else if (i == 0 || (bits & (1 << (i - 1))))
                c = fb_rgb(20, 20, 20);
            else
                continue;
            for (int sy = 0; sy < S; sy++)
                for (int sx = 0; sx < S; sx++)
                    fb_putpixel(x + i * S + sx, y + j * S + sy, c);
        }
    }
}

void desktop_init(void)
{
    S = (fb_height() >= 1800) ? 2 : 1;
    BAR = 28 * S;
    gfx_init_fonts();
    gfx_set_scale(S);
    i18n_init();
    i18n_set_lang(config_language());
    gfx_set_font(config_font());
    wp_index = 0;
    carousel = 0;
    win_id = 0;
    win_painted = 0;
    need_redraw = 1;
    last_cycle = timer_get_ticks();

    pWX = 212 * S; pWY = 70 * S; pWW = 600 * S; pWH = 470 * S;
    CW = 120 * S; CH = 75 * S;
    sWX = 160 * S; sWY = 60 * S; sWW = 700 * S; sWH = 480 * S;

    wp_buf = kcalloc((size_t)fb_width() * fb_height(), 4);
    wp_dirty = 1;
}

void desktop_run(void)
{
    int mx, my;
    ps2_mouse_xy(&mx, &my);
    int cx, cy;
    int clicked = ps2_mouse_click(&cx, &cy);
    if (clicked) {
        mx = cx;
        my = cy;
    }

    if (carousel) {
        uint64_t t = timer_get_ticks();
        if (t - last_cycle >= CAROUSEL_TICKS) {
            last_cycle = t;
            wp_index = (wp_index + 1) % N_WP;
            wp_dirty = 1;
            need_redraw = 1;
            win_painted = 0;
        }
    }

    cursor_restore();

    int wp_click = 0, set_click = 0;
    if (need_redraw) {
        render_wallpaper();
        need_redraw = 0;
    }
    draw_taskbar(mx, my, clicked, &wp_click, &set_click);
    if (wp_click) {
        win_id = 1;
        win_painted = 0;
    }
    if (set_click) {
        win_id = 2;
        win_painted = 0;
    }
    if (win_id == 1)
        draw_personalization(mx, my, clicked);
    else if (win_id == 2)
        draw_settings(mx, my, clicked);

    cursor_draw(mx, my);
}
