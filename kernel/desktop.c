#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ps2.h>
#include <kernel/i18n.h>
#include "i18n_data.h"
#include <kernel/heap.h>
#include <kernel/config.h>
#include <kernel/jbot.h>
#include <kernel/serial.h>
#include <kernel/ui.h>
#include <kernel/apps.h>
#include <kernel/desktop.h>
#include <kernel/cursor.h>
#include <kernel/net.h>
#include <kernel/tcp.h>

#define N_WP 6
#define CAROUSEL_TICKS 250

uint64_t timer_get_ticks(void);

static int S;
static int BAR;
static int wp_index;
static int carousel;
static uint64_t last_cycle;
static int win_id;
static int win_painted;
static int drawn_win = -1;
static int last_win_id = -1;
static int frame_cnt;
static int need_redraw;
static int start_open;
static int menu_sel;
static uint64_t arm_until;
static int mX, mY, mW, mH, mHit;
static uint32_t *wp_buf;
static int wp_dirty;

#define JB_LINES 7
#define JB_LINE_MAX 72
#define JB_IN_MAX 64
static char jb_lines[JB_LINES][JB_LINE_MAX];
static int jb_nlines;
static char jb_in[JB_IN_MAX + 1];
static int jb_inlen;
static int jWX, jWY, jWW, jWH;
static int jb_dirty = 1;

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

static void erase_menu_rect(int x, int y, int w, int h)
{
    int fw = (int)fb_width();
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            fb_putpixel(x + i, y + j, wp_buf[(uint64_t)(y + j) * fw + (x + i)]);
}

static void draw_rect(int x, int y, int w, int h, uint32_t c)
{
    fb_fill_rect(x, y, w, h, c);
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

static void draw_taskbar(int mx, int my, int clicked, int *start_click,
                         int *wp_click, int *set_click, int *jb_click)
{
    int w = (int)fb_width(), top = (int)fb_height() - BAR;
    int bh = BAR - 8 * S;
    int wp_x = w * 30 / 100, set_x = w * 42 / 100, jb_x = w * 54 / 100;
    if (wp_x < 112 * S + 110 * S) wp_x = 112 * S + 110 * S;
    set_x = wp_x + (128 * S);
    jb_x = set_x + (118 * S);
    draw_rect(0, top, w, BAR, fb_rgb(24, 27, 36));
    if (button2(10 * S, top + 4 * S, 90 * S, bh,
                "Start", start_open, mx, my, clicked))
        *start_click = 1;
    gfx_draw_string(112 * S, top + 6 * S, fb_rgb(120, 200, 255),
                    fb_rgb(24, 27, 36), "JackBOT OS");
    if (button2(wp_x, top + 4 * S, 120 * S, bh,
                tr(STR_WALLPAPER), 0, mx, my, clicked))
        *wp_click = 1;
    if (button2(set_x, top + 4 * S, 110 * S, bh,
                tr(STR_SETTINGS), 0, mx, my, clicked))
        *set_click = 1;
    if (button2(jb_x, top + 4 * S, 100 * S, bh,
                "Jbot", 0, mx, my, clicked))
        *jb_click = 1;

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
static int set_lang_scroll;

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

static void jb_copy(char *dst, const char *src, int max)
{
    int p = 0;
    while (src[p] && p < max - 1) {
        dst[p] = src[p];
        p++;
    }
    dst[p] = 0;
}

static void jb_add(const char *s)
{
    if (jb_nlines < JB_LINES) {
        jb_copy(jb_lines[jb_nlines], s, JB_LINE_MAX);
        jb_nlines++;
    } else {
        for (int i = 1; i < JB_LINES; i++)
            jb_copy(jb_lines[i - 1], jb_lines[i], JB_LINE_MAX);
        jb_copy(jb_lines[JB_LINES - 1], s, JB_LINE_MAX);
    }
}

static void jb_cat(char *dst, const char *pre, const char *s, int max)
{
    int p = 0;
    while (pre[p] && p < max - 1) {
        dst[p] = pre[p];
        p++;
    }
    while (*s && p < max - 1)
        dst[p++] = *s++;
    dst[p] = 0;
}

static void jb_send(void)
{
    if (jb_inlen == 0)
        return;
    char tmp[JB_LINE_MAX];
    jb_cat(tmp, "You: ", jb_in, JB_LINE_MAX);
    jb_add(tmp);
    char rp[JB_LINE_MAX];
    jbot_reply(jb_in, rp, sizeof(rp));
    jb_cat(tmp, "Jbot: ", rp, JB_LINE_MAX);
    jb_add(tmp);
    jb_inlen = 0;
    jb_in[0] = 0;
}

static void paint_jbot(void)
{
    draw_rect(jWX, jWY, jWW, jWH, fb_rgb(28, 31, 42));
    draw_border(jWX, jWY, jWW, jWH, fb_rgb(90, 110, 160));
    draw_rect(jWX, jWY, jWW, 26 * S, fb_rgb(42, 52, 78));
    gfx_draw_string(jWX + 10 * S, jWY + 5 * S, fb_rgb(235, 240, 250),
                    fb_rgb(42, 52, 78), "Jbot");
    int top = jWY + 34 * S;
    draw_rect(jWX + 8 * S, top, jWW - 16 * S, 300 * S,
              fb_rgb(20, 23, 33));
    for (int i = 0; i < jb_nlines; i++)
        gfx_draw_string(jWX + 14 * S, top + 8 * S + i * 40 * S,
                        fb_rgb(210, 220, 235), fb_rgb(20, 23, 33),
                        jb_lines[i]);
    int iby = jWY + jWH - 40 * S;
    draw_rect(jWX + 8 * S, iby, jWW - 150 * S, 26 * S,
              fb_rgb(16, 19, 28));
    draw_border(jWX + 8 * S, iby, jWW - 150 * S, 26 * S,
                fb_rgb(110, 130, 180));
    char shown[JB_IN_MAX + 2];
    jb_copy(shown, jb_in, JB_IN_MAX + 1);
    int p = jb_inlen;
    if (p < JB_IN_MAX)
        shown[p++] = '_';
    shown[p] = 0;
    gfx_draw_string(jWX + 14 * S, iby + 5 * S, fb_rgb(220, 230, 245),
                    fb_rgb(16, 19, 28), shown);
}

static void draw_jbot(int mx, int my, int clicked)
{
    char c;
    int changed = 0;
    while (ps2_read_key(&c)) {
        if (c == '\r' || c == '\n') {
            if (jb_inlen) { jb_send(); changed = 1; }
        } else if (c == '\b' || c == 0x7f) {
            if (jb_inlen > 0) {
                jb_in[--jb_inlen] = 0;
                changed = 1;
            }
        } else if (c >= 32 && jb_inlen < JB_IN_MAX) {
            jb_in[jb_inlen++] = c;
            jb_in[jb_inlen] = 0;
            changed = 1;
        }
    }
    if (button2(jWX + jWW - 70 * S, jWY + 3 * S, 60 * S, 20 * S,
                tr(STR_CLOSE), 0, mx, my, clicked)) {
        win_id = 0;
        need_redraw = 1;
        return;
    }
    int iby = jWY + jWH - 40 * S;
    if (button2(jWX + jWW - 130 * S, iby, 110 * S, 26 * S,
                "Send", 0, mx, my, clicked)) {
        if (jb_inlen) { jb_send(); changed = 1; }
    }
    if (changed)
        jb_dirty = 1;
    if (jb_dirty) {
        paint_jbot();
        jb_dirty = 0;
    }
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
    int rh = 30 * S, gap = 6 * S;
    int x0 = sWX + 24 * S, y0 = sWY + 62 * S;
    int y1 = sWY + sWH - 18 * S;
    int list_w = 300 * S;
    int item_w = list_w - 16 * S;
    int track_x = x0 + item_w + 4 * S;
    int track_w = 8 * S;
    int ar_h = 16 * S;
    int nvis = (y1 - y0) / (rh + gap);
    if (nvis < 1) nvis = 1;
    int nlang = i18n_count();
    int maxs = nlang - nvis;
    if (maxs < 0) maxs = 0;
    int cur = i18n_lang();
    if (set_lang_scroll > cur) set_lang_scroll = cur;
    if (cur >= set_lang_scroll + nvis)
        set_lang_scroll = cur - nvis + 1;
    if (set_lang_scroll < 0) set_lang_scroll = 0;
    if (set_lang_scroll > maxs) set_lang_scroll = maxs;

    draw_rect(x0, y0, list_w, y1 - y0, fb_rgb(16, 19, 28));

    if (nlang > nvis) {
        int mid_y0 = y0 + ar_h, mid_y1 = y1 - ar_h;
        if (clicked && inside(mx, my, track_x, y0, track_w, ar_h) &&
            set_lang_scroll > 0) {
            set_lang_scroll--;
            clicked = 0;
        }
        if (clicked && inside(mx, my, track_x, y1 - ar_h, track_w, ar_h) &&
            set_lang_scroll < maxs) {
            set_lang_scroll++;
            clicked = 0;
        }
        if (clicked && inside(mx, my, track_x, mid_y0, track_w,
                              mid_y1 - mid_y0)) {
            int thumb_h = mid_y1 - mid_y0;
            if (my < mid_y0 + thumb_h / 2) {
                set_lang_scroll -= nvis;
                if (set_lang_scroll < 0) set_lang_scroll = 0;
            } else {
                set_lang_scroll += nvis;
                if (set_lang_scroll > maxs) set_lang_scroll = maxs;
            }
            clicked = 0;
        }
        gfx_draw_string(track_x, y0 - 2 * S, fb_rgb(180, 200, 235),
                        fb_rgb(16, 19, 28), "^");
        gfx_draw_string(track_x, y1 - ar_h, fb_rgb(180, 200, 235),
                        fb_rgb(16, 19, 28), "v");
        int thumb_h = (mid_y1 - mid_y0) * nvis / nlang;
        if (thumb_h < 20 * S) thumb_h = 20 * S;
        int thumb_y = mid_y0 +
                      (mid_y1 - mid_y0 - thumb_h) * set_lang_scroll /
                      (maxs ? maxs : 1);
        fb_fill_rect(track_x, thumb_y, track_w, thumb_h, fb_rgb(80, 130, 220));
    }

    for (int r = 0; r < nvis; r++) {
        int idx = set_lang_scroll + r;
        if (idx >= nlang) break;
        int y = y0 + r * (rh + gap);
        if (button2(x0, y, item_w, rh, i18n_name(idx), idx == cur,
                    mx, my, clicked)) {
            i18n_set_lang(idx);
            config_set_language(idx);
            config_save();
            win_painted = 0;
            need_redraw = 1;
        }
    }

    int frh = 34 * S;
    for (int i = 0; i < gfx_font_count(); i++) {
        int x = sWX + 370 * S, y = sWY + 62 * S + i * (frh + 8 * S);
        if (button2(x, y, 280 * S, frh, gfx_font_name(i),
                    i == gfx_get_font(), mx, my, clicked)) {
            gfx_set_font(i);
            config_set_font(i);
            config_save();
            win_painted = 0;
            need_redraw = 1;
        }
    }
}

static int pick_scale(void)
{
    int W = (int)fb_width(), H = (int)fb_height();
    for (int s = 3; s >= 2; s--) {
        int bar = 28 * s;
        int hc = H - bar;
        int m = 12 * s;
        if (700 * s <= W - 2 * m && 480 * s <= hc - 2 * m)
            return s;
    }
    return 1;
}

void desktop_init(void)
{
    S = pick_scale();
    BAR = 28 * S;
    gfx_init_fonts();
    gfx_set_scale(S);
    ui_set_scale(S);
    cursor_set_scale(S);
    cursor_init();
    i18n_init();
    i18n_set_lang(config_language());
    gfx_set_font(config_font());
    wp_index = 0;
    carousel = 0;
    win_id = 0;
    win_painted = 0;
    need_redraw = 1;
    last_cycle = timer_get_ticks();
    menu_sel = 0;
    arm_until = timer_get_ticks() + 100;

    {
        struct ui_win fit;
        ui_fit(600, 470, &fit);
        pWX = fit.x; pWY = fit.y; pWW = fit.w; pWH = fit.h;
        ui_fit(700, 480, &fit);
        sWX = fit.x; sWY = fit.y; sWW = fit.w; sWH = fit.h;
        ui_fit(640, 440, &fit);
        jWX = fit.x; jWY = fit.y; jWW = fit.w; jWH = fit.h;
    }
    CW = 120 * S; CH = 75 * S;
    jb_nlines = 0;
    jb_inlen = 0;
    jb_in[0] = 0;
    jb_add("Jbot Quick (free). Type help for what I can do.");
    jb_dirty = 1;

    wp_buf = kcalloc((size_t)fb_width() * fb_height(), 4);
    serial_puts("[DESK] wp_buf=");
    serial_puthex((uint64_t)(uintptr_t)wp_buf);
    serial_puts(" need_bytes=");
    serial_puthex((uint64_t)fb_width() * fb_height() * 4);
    serial_putc('\n');
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
    if (timer_get_ticks() < arm_until)
        clicked = 0;

    net_poll();
    tcp_poll();

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

    frame_cnt++;

    cursor_begin_frame();

    int start_click = 0, wp_click = 0, set_click = 0, jb_click = 0;
    if (need_redraw) {
        render_wallpaper();
        need_redraw = 0;
        drawn_win = -1;
    }
    if (win_id != last_win_id) {
        drawn_win = -1;
        last_win_id = win_id;
    }
    draw_taskbar(mx, my, clicked, &start_click, &wp_click,
                 &set_click, &jb_click);
    if (start_click)
        start_open ^= 1;
    if (wp_click) {
        win_id = 1; win_painted = 0; start_open = 0;
    }
    if (set_click) {
        win_id = 2; win_painted = 0; start_open = 0;
    }
    if (jb_click) {
        win_id = 3; win_painted = 0; start_open = 0; jb_dirty = 1;
    }

    if (win_id == 0) {
        char kc;
        int napps = APP_LIMIT - APP_POWER;
        while (ps2_read_key(&kc)) {
            if (kc == JB_KEY_WIN || kc == '\t') {
                start_open ^= 1;
                menu_sel = 0;
            } else if (start_open) {
                if (kc == JB_KEY_DOWN) {
                    menu_sel = (menu_sel + 1) % napps;
                } else if (kc == JB_KEY_UP) {
                    menu_sel = (menu_sel + napps - 1) % napps;
                } else if (kc == '\n' || kc == '\r') {
                    int id = APP_POWER + menu_sel;
                    app_open(id);
                    win_id = id;
                    win_painted = 1;
                    start_open = 0;
                    mHit = 1;
                } else if (kc == 27) {
                    start_open = 0;
                }
            }
        }
    }

    if (start_open) {
        int mw = 240 * S;
        int mh = (APP_LIMIT - APP_POWER) * 34 * S + 10 * S;
        int bx = 10 * S, by = (int)fb_height() - BAR - mh - 4 * S;
        mX = bx; mY = by; mW = mw; mH = mh;
        draw_rect(bx, by, mw, mh, fb_rgb(34, 38, 52));
        draw_border(bx, by, mw, mh, fb_rgb(110, 130, 170));
        for (int id = APP_POWER; id < APP_LIMIT; id++) {
            int i = id - APP_POWER;
            int iy = by + 6 * S + i * 34 * S;
            int hov = inside(mx, my, bx + 4, iy, mw - 8, 30 * S);
            int hl = hov || (start_open && i == menu_sel);
            if (hl)
                draw_rect(bx + 4, iy, mw - 8, 30 * S, fb_rgb(58, 78, 120));
            gfx_draw_string(bx + 16 * S, iy + 7 * S, fb_rgb(220, 228, 240),
                            hl ? fb_rgb(58, 78, 120) : fb_rgb(34, 38, 52),
                            app_name(id));
            if (clicked && hov) {
                app_open(id);
                win_id = id;
                win_painted = 1;
                start_open = 0;
                mHit = 1;
            }
        }
    }

    if (mHit) {
        erase_menu_rect(mX, mY, mW, mH);
        mHit = 0;
    }

    if (win_id == 1)
        draw_personalization(mx, my, clicked);
    else if (win_id == 2)
        draw_settings(mx, my, clicked);
    else if (win_id == 3)
        draw_jbot(mx, my, clicked);
    else if (win_id >= APP_POWER && win_id < APP_LIMIT) {
        if (ps2_consume_key(27)) {
            win_id = 0;
            need_redraw = 1;
            drawn_win = -1;
        } else if (app_persistent(win_id)) {
            int period = app_refresh_period(win_id);
            int need = (drawn_win != win_id) ||
                       (period > 0 && (frame_cnt % period) == 0);
            if (need) {
                if (app_frame(win_id, mx, my, clicked)) {
                    win_id = 0;
                    need_redraw = 1;
                    drawn_win = -1;
                } else {
                    drawn_win = win_id;
                }
            } else if (clicked && ui_close_hit(mx, my)) {
                win_id = 0;
                need_redraw = 1;
                drawn_win = -1;
            }
        } else if (app_frame(win_id, mx, my, clicked)) {
            win_id = 0;
            need_redraw = 1;
            drawn_win = -1;
        }
    }

    cursor_end_frame(mx, my);
    fb_present_dirty();
}
