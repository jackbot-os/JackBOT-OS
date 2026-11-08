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
#include <kernel/wallpaper_image.h>
#include <kernel/perm.h>
#include <kernel/users.h>

#define N_WP 3
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
static int m_rows, m_cols, m_colw, m_rowh, m_titleh, m_pad;
static int m_rightw;
static int restore_win;
static int prev_mX, prev_mY, prev_mW, prev_mH;
static int menu_was_open;
static int gl_on = 1;

static void pass_modal(int who);

/* alpha for a panel: the stored value when liquid glass is on, else solid */
static uint8_t glc(uint8_t d) { return (uint8_t)(gl_on ? d : 255); }
static char search_buf[24];
static int search_len;
static int fmap[32];
static int fcount;
/* Wallpaper themes moved into Settings > Display, so the separate
   wallpaper entry is gone from the context menu too. */
#define CTX_ITEMS 3
static const char *ctx_label(int i)
{
    switch (i) {
    case 0: return tr(STR_CTX_SETTINGS);
    case 1: return tr(STR_CTX_REFRESH);
    default: return tr(STR_CTX_ABOUT);
    }
}
static int ctx_open, ctx_x, ctx_y, ctx_sel;
static uint32_t *wp_buf;
static int wp_dirty;
static int custom_wp;

#define JB_LINES 7
#define JB_LINE_MAX 72
#define JB_IN_MAX 64
static char jb_lines[JB_LINES][JB_LINE_MAX];
static int jb_nlines;
static char jb_in[JB_IN_MAX + 1];
static int jb_inlen;
static int jWX, jWY, jWW, jWH;
static int jb_dirty = 1;

#define SET_TABS 6
static int set_tab;

struct region_entry {
    const char *name;
    int tz_idx;
};

static const struct region_entry REGIONS[25] = {
    {"Baker Island", 0},
    {"Pago Pago", 1},
    {"Honolulu", 2},
    {"Anchorage", 3},
    {"Los Angeles", 4},
    {"Denver", 5},
    {"Chicago", 6},
    {"New York", 7},
    {"Halifax", 8},
    {"Sao Paulo", 9},
    {"South Georgia", 10},
    {"Azores", 11},
    {"London", 12},
    {"Berlin, Paris", 13},
    {"Cairo", 14},
    {"Moscow", 15},
    {"Dubai", 16},
    {"Karachi", 17},
    {"Dhaka", 18},
    {"Bangkok, Jakarta", 19},
    {"Beijing, Singapore", 20},
    {"Tokyo, Seoul", 21},
    {"Sydney", 22},
    {"Solomon Islands", 23},
    {"Auckland", 24},
};

static int wp_name_key(int i)
{
    return i == 0 ? STR_WP_EASTLAKE : (i == 1 ? STR_WP_OCEAN : STR_WP_SKY);
}

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
    int u = (w > 1) ? (x * 255) / (w - 1) : 0;
    int v = (h > 1) ? (y * 255) / (h - 1) : 0;
    if (idx == 1)
        return lerp_c(fb_rgb(8, 18, 48), fb_rgb(38, 112, 205), v);
    if (idx == 2)
        return lerp_c(fb_rgb(70, 150, 235), fb_rgb(150, 210, 255), (u + v) / 2);
    /* index 0 is the East Lake photo: the decoder fills wp_buf, this is
       only the fallback used if that decode ever fails. */
    return lerp_c(fb_rgb(8, 18, 48), fb_rgb(24, 60, 120), v);
}

static void render_wallpaper(void)
{
    int w = (int)fb_width(), H = (int)fb_height(), h = H - BAR;
    if (!wp_buf) {
        /* Wallpaper buffer allocation failed at init: fall back to a
           solid background instead of dereferencing NULL. */
        static int cleared;
        if (!cleared) {
            fb_clear(fb_rgb(8, 18, 48));
            cleared = 1;
        }
        return;
    }
    if (wp_dirty) {
        if (wp_index == 0) {
            /* East Lake is the built-in photo: decode it again so switching
               back from a gradient never leaves a painted-over buffer. */
            wimg_apply_default();
        } else {
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++)
                    wp_buf[(uint64_t)y * w + x] =
                        wp_color(wp_index, x, y, w, h);
        }
        for (int y = h; y < H; y++)
            for (int x = 0; x < w; x++)
                wp_buf[(uint64_t)y * w + x] = fb_rgb(16, 18, 28);
        wp_dirty = 0;
    }
    fb_blit_full(wp_buf);
}

/* Repaint one rectangle straight from the wallpaper surface. Used while a
   window is dragged: only the strip the window crosses gets restored instead
   of blitting all 1024x768 pixels every frame. */
static void restore_wp_rect(int x, int y, int w, int h)
{
    if (!wp_buf)
        return;
    fb_blit_rect(x, y, w, h, wp_buf, (int)fb_width());
}

/* ---- streaming wallpaper install -------------------------------------
   The decoder converts one source row and hands it over; we scale that row
   into every screen row that maps to it. Only a single row buffer exists on
   the decoder side, so a 1600x900 photo needs no 4 MB temporary image. */
static int st_sw, st_sh;

void wallpaper_stream_begin(int sw, int sh)
{
    int w = (int)fb_width(), H = (int)fb_height(), h = H - BAR;
    if (!wp_buf || sw <= 0 || sh <= 0 || w <= 0 || h <= 0)
        return;
    st_sw = sw;
    st_sh = sh;
    for (int y = h; y < H; y++)
        for (int x = 0; x < w; x++)
            wp_buf[(uint64_t)y * w + x] = fb_rgb(16, 18, 28);
}

void wallpaper_stream_row(int sy, const uint32_t *row, int sw)
{
    int w = (int)fb_width(), H = (int)fb_height(), h = H - BAR;
    if (!wp_buf || !row || st_sh <= 0 || st_sw <= 0)
        return;
    if (sy < 0 || sy >= st_sh)
        return;
    {
        int y0 = (int)((uint64_t)sy * (uint64_t)h / (uint64_t)st_sh);
        int y1 = (int)(((uint64_t)(sy + 1) * (uint64_t)h +
                        (uint64_t)st_sh - 1) / (uint64_t)st_sh);
        if (sy == st_sh - 1 && y1 < h)
            y1 = h;
        for (int y = y0; y < y1 && y < h; y++) {
            uint32_t *dst = wp_buf + (uint64_t)y * w;
            for (int x = 0; x < w; x++) {
                int sx = (int)((uint64_t)x * (uint64_t)st_sw / (uint64_t)w);
                if (sx >= sw)
                    sx = sw - 1;
                dst[x] = row[sx];
            }
        }
    }
}

void wallpaper_stream_end(void)
{
    custom_wp = 1;
    wp_dirty = 0;
    need_redraw = 1;
}

/* Install a decoded picture (any size) as the wallpaper. It is scaled to the
   screen with nearest-neighbour sampling and written straight into wp_buf. */
void wallpaper_set_custom(const uint32_t *src, int sw, int sh)
{
    int w = (int)fb_width(), H = (int)fb_height(), h = H - BAR;
    if (!src || sw <= 0 || sh <= 0)
        return;
    for (int y = 0; y < h; y++) {
        int sy = (int)((uint64_t)y * (uint64_t)sh / (uint64_t)h);
        for (int x = 0; x < w; x++) {
            int sx = (int)((uint64_t)x * (uint64_t)sw / (uint64_t)w);
            wp_buf[(uint64_t)y * w + x] = src[(uint64_t)sy * sw + sx];
        }
    }
    for (int y = h; y < H; y++)
        for (int x = 0; x < w; x++)
            wp_buf[(uint64_t)y * w + x] = fb_rgb(16, 18, 28);
    custom_wp = 1;
    wp_dirty = 0;
}

static int lower_ch(int c)
{
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

static int name_matches(const char *name)
{
    if (search_len == 0) return 1;
    const char *n = name;
    while (*n) {
        int k = 0;
        while (k < search_len && n[k] &&
               lower_ch((unsigned char)n[k]) ==
               lower_ch((unsigned char)search_buf[k]))
            k++;
        if (k == search_len) return 1;
        n++;
    }
    return 0;
}

static void rebuild_filter(void)
{
    fcount = 0;
    for (int id = APP_POWER; id < APP_LIMIT; id++) {
        if (name_matches(app_name(id)))
            fmap[fcount++] = id - APP_POWER;
    }
    if (menu_sel >= fcount) menu_sel = fcount ? fcount - 1 : 0;
}

static void menu_layout(void)
{
    int napps = fcount;
    int H = (int)fb_height();
    m_rowh = 34 * S;
    m_titleh = 32 * S;
    m_pad = 10 * S;
    m_colw = 212 * S;
    int avail_h = H - BAR - 2 * m_pad - 4 * S;
    int max_rows = (avail_h - m_titleh - 2 * m_pad) / m_rowh;
    if (max_rows < 1) max_rows = 1;
    int cols = (napps + max_rows - 1) / max_rows;
    if (cols < 1) cols = 1;
    int rows = (napps + cols - 1) / cols;
    m_rows = rows;
    m_cols = cols;
    m_rightw = 132 * S;
    mW = cols * m_colw + 2 * m_pad + m_rightw;
    mH = m_titleh + rows * m_rowh + 2 * m_pad;
    mX = 10 * S;
    mY = H - BAR - mH - 4 * S;
    if (mY < m_pad) mY = m_pad;
}

static void menu_item_geom(int idx, int *ix, int *iy, int *iw, int *ih)
{
    int col = idx / m_rows;
    int row = idx % m_rows;
    *ix = mX + m_pad + col * m_colw + 3 * S;
    *iy = mY + m_titleh + row * m_rowh + 2 * S;
    *iw = m_colw - 6 * S;
    *ih = m_rowh - 4 * S;
}

static void ctx_box(int *x, int *y, int *w, int *h)
{
    *w = 168 * S;
    *h = (16 + CTX_ITEMS * 30) * S;
    *x = ctx_x;
    *y = ctx_y;
    if (*x + *w > (int)fb_width()) *x = (int)fb_width() - *w - 2;
    if (*y + *h > (int)fb_height() - BAR)
        *y = (int)fb_height() - BAR - *h - 2;
}

static void ctx_item_geom(int idx, int x, int y, int w,
                          int *ix, int *iy, int *iw, int *ih)
{
    *ix = x + 8 * S;
    *iy = y + 8 * S + idx * 30 * S;
    *iw = w - 16 * S;
    *ih = 26 * S;
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

/* Frosted-glass fill: alpha-blends the panel colour over whatever is
   already on screen. This is the "liquid glass" look the menus use: the
   wallpaper and any open window show through, but no separate snapshot or
   extra framebuffer is needed (the desktop repaints the frame while a
   menu is open, so the blend is always taken from fresh pixels). */
static void draw_glass_rect(int x, int y, int w, int h, uint32_t c,
                            uint8_t cover)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)fb_width()) w = (int)fb_width() - x;
    if (y + h > (int)fb_height()) h = (int)fb_height() - y;
    if (w <= 0 || h <= 0)
        return;
    /* Blend against the wallpaper surface, never against the framebuffer.
       Sampling the frame made every repaint of a moved window mix in the
       window's own previous pixels, which smeared the photo into the
       "broken picture" mess seen while dragging. The wallpaper buffer is
       stable, so the panel is identical no matter how often it is redrawn. */
    int W = (int)fb_width();
    uint32_t cr = (c >> 16) & 255, cg = (c >> 8) & 255, cb = c & 255;
    if (!wp_buf) {
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++)
                fb_putpixel_blend(x + i, y + j, c, cover);
        return;
    }
    for (int j = 0; j < h; j++) {
        uint32_t *row = wp_buf + (uint64_t)(y + j) * W;
        for (int i = 0; i < w; i++) {
            uint32_t b = row[x + i];
            int br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
            int rr = (br * (255 - cover) + (int)cr * cover) / 255;
            int gg = (bg * (255 - cover) + (int)cg * cover) / 255;
            int bl = (bb * (255 - cover) + (int)cb * cover) / 255;
            fb_putpixel(x + i, y + j, fb_rgb(rr, gg, bl));
        }
    }
}

/* Rounded variant of draw_glass_rect: same wallpaper blend, but pixels
   outside the corner radius are left untouched so the tile keeps the
   rounded look of the design instead of a square glass plate. */
static void draw_glass_round_rect(int x, int y, int w, int h, int rad,
                                  uint32_t c, uint8_t cover)
{
    if (rad < 0)
        rad = 0;
    if (rad * 2 > w) rad = w / 2;
    if (rad * 2 > h) rad = h / 2;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)fb_width()) w = (int)fb_width() - x;
    if (y + h > (int)fb_height()) h = (int)fb_height() - y;
    if (w <= 0 || h <= 0)
        return;

    int W = (int)fb_width();
    uint32_t cr = (c >> 16) & 255, cg = (c >> 8) & 255, cb = c & 255;
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            /* corner test with a 1px soft edge */
            int alpha = 255;
            if (rad > 0) {
                int dx = 0, dy = 0;
                if (i < rad) dx = rad - i;
                else if (i >= w - rad) dx = i - (w - rad - 1);
                if (j < rad) dy = rad - j;
                else if (j >= h - rad) dy = j - (h - rad - 1);
                if (dx > 0 || dy > 0) {
                    if (dx * dx + dy * dy > rad * rad + rad)
                        continue;                     /* clearly outside */
                    int d2 = dx * dx + dy * dy;
                    int r2 = rad * rad;
                    if (d2 > r2)
                        alpha = (uint8_t)(255 - (d2 - r2) * 255 / (rad * 2 + 1));
                }
            }
            uint32_t base;
            if (wp_buf) {
                base = wp_buf[(uint64_t)(y + j) * W + (x + i)];
            } else {
                fb_putpixel_blend(x + i, y + j, c,
                                  (uint8_t)((cover * alpha) / 255));
                continue;
            }
            int br = (base >> 16) & 255, bg = (base >> 8) & 255, bb = base & 255;
            int mix = (cover * alpha) / 255;
            int rr = (br * (255 - mix) + (int)cr * mix) / 255;
            int gg = (bg * (255 - mix) + (int)cg * mix) / 255;
            int bl = (bb * (255 - mix) + (int)cb * mix) / 255;
            fb_putpixel(x + i, y + j, fb_rgb(rr, gg, bl));
        }
    }
}

/* Right-hand quick buttons inside the start menu. */
#define M_QUICK 5
static const char *m_quick_label(int i)
{
    switch (i) {
    case 0: return tr(STR_QUICK_SETTINGS);
    case 1: return tr(STR_QUICK_JBOT);
    case 2: return tr(STR_QUICK_BROWSER);
    case 3: return tr(STR_QUICK_FILES);
    default: return tr(STR_QUICK_BACK);
    }
}

static int m_quick_win(int i)
{
    switch (i) {
    case 0: return 2;
    case 1: return 3;
    case 2: return APP_BROWSER;
    case 3: return APP_FILES;
    default: return -1; /* Back */
    }
}

static void m_quick_geom(int i, int *qx, int *qy, int *qw, int *qh)
{
    int bh = 34 * S;
    int gap = 6 * S;
    int total = M_QUICK * bh + (M_QUICK - 1) * gap;
    int top = mY + m_titleh + ((mH - m_titleh) - total) / 2;
    if (top < mY + m_titleh + 4 * S)
        top = mY + m_titleh + 4 * S;
    *qx = mX + mW - m_rightw + 8 * S;
    *qy = top + i * (bh + gap);
    *qw = m_rightw - 16 * S;
    *qh = bh;
}

static int inside(int px, int py, int x, int y, int w, int h)
{
    return px >= x && px < x + w && py >= y && py < y + h;
}

static int button2(int x, int y, int w, int h, const char *label, int sel,
                   int mx, int my, int clicked)
{
    int hov = inside(mx, my, x, y, w, h);
    uint32_t base, edge;
    if (sel) {
        base = fb_rgb(38, 128, 96);
        edge = fb_rgb(110, 220, 170);
    } else if (hov) {
        base = fb_rgb(52, 104, 190);
        edge = fb_rgb(140, 180, 250);
    } else {
        base = fb_rgb(40, 72, 128);
        edge = fb_rgb(100, 130, 185);
    }
    ui_round_rect(x, y, w, h, 4 * S, base);
    ui_round_border(x, y, w, h, 4 * S, edge);
    int tw = gfx_text_width(label);
    gfx_draw_string(x + (w - tw) / 2,
                    y + (h - gfx_text_height()) / 2,
                    fb_rgb(240, 245, 255), base, label);
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

/* CMOS RTC read with the two correctness steps the raw port read skipped:
   wait out the update-in-progress window (0x0A bit 7) and read twice until
   two consecutive samples agree. Reading registers piecemeal while the chip
   is mid-update returns a mix of the old and new fields, which is what made
   the taskbar clock sit a minute behind. */
static int rtc_uip(void) { return cmos(0x0a) & 0x80; }

static void rtc_time(int *h, int *m, int *s)
{
    int sec = 0, min = 0, hr = 0, guard = 0;
    while (rtc_uip() && guard++ < 2000000)
        ;
    for (int round = 0; round < 4; round++) {
        sec = cmos(0x00);
        min = cmos(0x02);
        hr  = cmos(0x04);
        if (!rtc_uip())
            break;
    }
    uint8_t sb = cmos(0x0b);
    if (!(sb & 0x04)) {              /* BCD time (the usual default) */
        sec = bcd(sec);
        min = bcd(min);
        int pm = hr & 0x80;
        hr = bcd((uint8_t)(hr & 0x7f));
        if (!(sb & 0x02)) {          /* 12-hour mode: fold into 24-hour */
            if (pm && hr != 12)
                hr += 12;
            else if (!pm && hr == 12)
                hr = 0;
        }
    } else {                         /* binary time */
        hr &= 0x7f;
        if (!(sb & 0x02) && (cmos(0x04) & 0x80) && hr != 12)
            hr += 12;
    }
    *h = hr; *m = min; *s = sec;
}

/* Date for the taskbar: CMOS 0x09/0x08/0x07, same BCD/binary rule as the
   clock. Returns month/day as 1-12 and 1-31 so the caller only formats. */
static void rtc_date(int *mo, int *dy, int *yr)
{
    int mon = 0, day = 0, year = 0, guard = 0;
    while (rtc_uip() && guard++ < 2000000)
        ;
    for (int round = 0; round < 4; round++) {
        day  = cmos(0x07);
        mon  = cmos(0x08);
        year = cmos(0x09);
        if (!rtc_uip())
            break;
    }
    uint8_t sb = cmos(0x0b);
    if (!(sb & 0x04)) {
        day = bcd(day);
        mon = bcd(mon);
        year = bcd(year);
    }
    if (mon < 1 || mon > 12) mon = 1;
    if (day < 1 || day > 31) day = 1;
    *mo = mon; *dy = day; *yr = year;
}

static void draw_taskbar(int mx, int my, int clicked, int *start_click,
                         int *set_click, int *jb_click)
{
    int w = (int)fb_width(), top = (int)fb_height() - BAR;
    int bh = BAR - 8 * S;
    /* the glyph layer no longer paints its own background, so the bar can
       keep the glass setting instead of forcing a solid pass */

    int set_x = w * 32 / 100, jb_x = w * 44 / 100;
    if (set_x < 112 * S + 110 * S) set_x = 112 * S + 110 * S;
    jb_x = set_x + (128 * S);
    draw_rect(0, top, w, BAR, fb_rgb(16, 18, 28));
    /* accent line on the taskbar top edge */
    draw_rect(0, top, w, 2 * S, fb_rgb(46, 110, 200));
    /* Start: blue accent button */
    {
        int sx = 10 * S, sy = top + 4 * S, sw = 90 * S, sh = bh;
        int shov = inside(mx, my, sx, sy, sw, sh);
        uint32_t sb = start_open ? fb_rgb(60, 120, 210)
                     : shov ? fb_rgb(50, 105, 195)
                            : fb_rgb(28, 82, 168);
        ui_round_rect(sx, sy, sw, sh, 5 * S, sb);
        ui_round_border(sx, sy, sw, sh, 5 * S, fb_rgb(110, 170, 255));
        const char *slabel = tr(STR_START_BUTTON);
        int tw = gfx_text_width(slabel);
        gfx_draw_string(sx + (sw - tw) / 2, sy + (sh - gfx_text_height()) / 2,
                        fb_rgb(255, 255, 255), sb, slabel);
        if (clicked && shov)
            *start_click = 1;
    }
    gfx_draw_string(112 * S, top + 6 * S, fb_rgb(90, 200, 255),
                    fb_rgb(16, 18, 28), "JackBOT OS");
    gfx_set_transparent(gl_on);
    /* Wallpaper themes now live in Settings > Display; the old standalone
       wallpaper button is gone on purpose. */
    if (button2(set_x, top + 4 * S, 110 * S, bh,
                tr(STR_SETTINGS), 0, mx, my, clicked))
        *set_click = 1;
    if (button2(jb_x, top + 4 * S, 100 * S, bh,
                "Jbot", 0, mx, my, clicked))
        *jb_click = 1;

    int hh, mm, ss, mo, dy, yr;
    rtc_time(&hh, &mm, &ss);
    rtc_date(&mo, &dy, &yr);
    (void)yr;
    char clk[24];
    const char *d = "0123456789";
    clk[0] = d[mo / 10]; clk[1] = d[mo % 10]; clk[2] = '-';
    clk[3] = d[dy / 10]; clk[4] = d[dy % 10]; clk[5] = ' ';
    clk[6] = d[hh / 10]; clk[7] = d[hh % 10]; clk[8] = ':';
    clk[9] = d[mm / 10]; clk[10] = d[mm % 10]; clk[11] = ':';
    clk[12] = d[ss / 10]; clk[13] = d[ss % 10]; clk[14] = 0;
    int cw = gfx_text_width(clk);
    gfx_draw_string(w - cw - 12 * S, top + 6 * S, fb_rgb(225, 232, 245),
                    fb_rgb(16, 18, 28), clk);
}

static int pWX, pWY, pWW, pWH, CW, CH;
static struct wimg_file wfiles[WIMG_MAX_FILES];
static int wfile_count;

static void thumb_geom(int i, int *tx, int *ty)
{
    int col = i % 3, row = i / 3;
    *tx = pWX + 50 * S + col * (CW + 40 * S);
    *ty = pWY + 50 * S + row * (CH + 46 * S);
}

static void paint_personalization(void)
{
    int rad = 8 * S;
    wfile_count = wimg_list(wfiles, WIMG_MAX_FILES);
    if (wfile_count < 0)
        wfile_count = 0;
    draw_glass_rect(pWX, pWY, pWW, pWH, fb_rgb(24, 27, 38), glc(112));
    ui_round_border(pWX, pWY, pWW, pWH, rad, fb_rgb(70, 110, 180));
    draw_glass_rect(pWX + rad, pWY, pWW - 2 * rad, 26 * S,
                    fb_rgb(30, 64, 122), glc(150));
    draw_glass_rect(pWX, pWY + rad, pWW, 26 * S - rad,
                    fb_rgb(30, 64, 122), glc(150));
    ui_rect(pWX, pWY + 26 * S, pWW, 2 * S, fb_rgb(64, 120, 200));
    gfx_draw_string(pWX + 10 * S, pWY + 5 * S, fb_rgb(235, 240, 250),
                    fb_rgb(30, 64, 122), tr(STR_PERSONALIZATION));
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
        int tw = gfx_text_width(tr(wp_name_key(i)));
        gfx_draw_string(tx + (CW - tw) / 2, ty + CH + 4 * S,
                        fb_rgb(225, 230, 240), fb_rgb(30, 34, 46),
                        tr(wp_name_key(i)));
    }
    {
        int lx = pWX + 40 * S;
        int lw = pWW - 80 * S;
        int secy = pWY + 50 * S + 2 * (CH + 46 * S) + 12 * S;
        gfx_draw_string(lx, secy, fb_rgb(150, 210, 255), fb_rgb(24, 27, 38),
                        tr(STR_LOCAL_PICTURES));
        int ry0 = secy + 24 * S;
        if (wfile_count == 0) {
            ui_round_rect(lx, ry0, lw, 26 * S, 5 * S, fb_rgb(38, 44, 60));
            gfx_draw_string(lx + 12 * S, ry0 + 7 * S, fb_rgb(180, 190, 210),
                            fb_rgb(38, 44, 60), tr(STR_NO_PICTURES));
        } else {
            for (int i = 0; i < wfile_count && i < 6; i++) {
                int ry = ry0 + i * 28 * S;
                ui_round_rect(lx, ry, lw, 26 * S, 5 * S, fb_rgb(38, 44, 60));
                gfx_draw_string(lx + 12 * S, ry + 7 * S,
                                fb_rgb(210, 220, 235), fb_rgb(38, 44, 60),
                                wfiles[i].name);
            }
        }
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
            custom_wp = 0;
            wp_dirty = 1;
            need_redraw = 1;
            win_painted = 0;
        }
    }
    {
        int lx = pWX + 40 * S;
        int lw = pWW - 80 * S;
        int secy = pWY + 50 * S + 2 * (CH + 46 * S) + 12 * S;
        int ry0 = secy + 24 * S;
        for (int i = 0; i < wfile_count && i < 6; i++) {
            int ry = ry0 + i * 28 * S;
            if (clicked && ui_inside(mx, my, lx, ry, lw, 26 * S)) {
                wimg_apply(wfiles[i].short83);
                need_redraw = 1;
                win_painted = 0;
            }
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

static const char *set_tab_label(int i)
{
    switch (i) {
    case 0: return tr(STR_TAB_DISPLAY);
    case 1: return tr(STR_TAB_REGION);
    case 2: return tr(STR_TAB_AUDIO);
    case 3: return tr(STR_TAB_PERMISSIONS);
    case 4: return tr(STR_TAB_SYSTEM);
    default: return tr(STR_USERS);
    }
}

static void paint_settings(void)
{
    int rad = 8 * S;
    int title_h = 26 * S;
    int nav_w = 150 * S;
    draw_glass_rect(sWX, sWY, sWW, sWH, fb_rgb(24, 27, 38), glc(112));
    ui_round_border(sWX, sWY, sWW, sWH, rad, fb_rgb(70, 110, 180));
    draw_glass_rect(sWX + rad, sWY, sWW - 2 * rad, title_h,
                    fb_rgb(30, 64, 122), glc(150));
    draw_glass_rect(sWX, sWY + rad, sWW, title_h - rad,
                    fb_rgb(30, 64, 122), glc(150));
    ui_rect(sWX, sWY + title_h, sWW, 2 * S, fb_rgb(64, 120, 200));
    gfx_draw_string(sWX + 10 * S, sWY + 5 * S, fb_rgb(235, 240, 250),
                    fb_rgb(30, 64, 122), tr(STR_SETTINGS));

    /* left navigation rail */
    int nav_y = sWY + title_h + 2 * S;
    int nav_h = sWH - title_h - 2 * S;
    if (gl_on)
        draw_glass_rect(sWX, nav_y, nav_w, nav_h, fb_rgb(18, 21, 30), 128);
    else
        ui_rect(sWX, nav_y, nav_w, nav_h, fb_rgb(18, 21, 30));
    for (int i = 0; i < SET_TABS; i++) {
        int iy = nav_y + 12 * S + i * 40 * S;
        if (i == set_tab) {
            ui_round_rect(sWX + 8 * S, iy, nav_w - 16 * S, 32 * S, 6 * S,
                          fb_rgb(38, 86, 150));
            ui_round_border(sWX + 8 * S, iy, nav_w - 16 * S, 32 * S, 6 * S,
                            fb_rgb(90, 150, 220));
            gfx_draw_string(sWX + 20 * S, iy + 8 * S,
                            fb_rgb(235, 245, 255), fb_rgb(38, 86, 150),
                            set_tab_label(i));
        } else {
            gfx_draw_string(sWX + 20 * S, iy + 8 * S,
                            fb_rgb(150, 165, 190), fb_rgb(18, 21, 30),
                            set_tab_label(i));
        }
    }

    /* right content panel */
    int cx = sWX + nav_w;
    if (gl_on)
        draw_glass_rect(cx, nav_y, sWW - nav_w, nav_h, fb_rgb(24, 27, 38), 150);
    else
        ui_rect(cx, nav_y, sWW - nav_w, nav_h, fb_rgb(24, 27, 38));
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
    jb_cat(tmp, tr(STR_YOU_PREFIX), jb_in, JB_LINE_MAX);
    jb_add(tmp);
    char rp[JB_LINE_MAX];
    jbot_reply(jb_in, rp, sizeof(rp));
    jb_cat(tmp, tr(STR_JBOT_PREFIX), rp, JB_LINE_MAX);
    jb_add(tmp);
    jb_inlen = 0;
    jb_in[0] = 0;
}

static void paint_jbot(void)
{
    int rad = 8 * S;
    draw_glass_rect(jWX, jWY, jWW, jWH, fb_rgb(24, 27, 38), glc(112));
    ui_round_border(jWX, jWY, jWW, jWH, rad, fb_rgb(70, 110, 180));
    draw_glass_rect(jWX + rad, jWY, jWW - 2 * rad, 26 * S,
                    fb_rgb(30, 64, 122), glc(150));
    draw_glass_rect(jWX, jWY + rad, jWW, 26 * S - rad,
                    fb_rgb(30, 64, 122), glc(150));
    ui_rect(jWX, jWY + 26 * S, jWW, 2 * S, fb_rgb(64, 120, 200));
    gfx_draw_string(jWX + 10 * S, jWY + 5 * S, fb_rgb(235, 240, 250),
                    fb_rgb(30, 64, 122), "Jbot");
    int top = jWY + 34 * S;
    draw_rect(jWX + 8 * S, top, jWW - 16 * S, 300 * S,
              fb_rgb(20, 23, 33));
    for (int i = 0; i < jb_nlines; i++)
        gfx_draw_string(jWX + 14 * S, top + 8 * S + i * 40 * S,
                        fb_rgb(210, 220, 235), fb_rgb(20, 23, 33),
                        jb_lines[i]);
    int iby = jWY + jWH - 40 * S;
    ui_round_rect(jWX + 8 * S, iby, jWW - 150 * S, 26 * S, 5 * S,
                  fb_rgb(16, 19, 28));
    ui_round_border(jWX + 8 * S, iby, jWW - 150 * S, 26 * S, 5 * S,
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
                tr(STR_SEND_BUTTON), 0, mx, my, clicked)) {
        if (jb_inlen) { jb_send(); changed = 1; }
    }
    if (changed)
        jb_dirty = 1;
    /* The window body is only redrawn on jb_dirty, so a full-frame repaint
       would leave it erased (transparent window, icons gone) until the next
       keystroke set jb_dirty again. win_painted is cleared by every such
       repaint, so treat it as a repaint request too. */
    if (jb_dirty || !win_painted) {
        paint_jbot();
        jb_dirty = 0;
        win_painted = 1;
    }
}

static void set_u2s(int v, char *out)
{
    char tmp[8];
    int n = 0;
    if (v == 0)
        tmp[n++] = '0';
    while (v > 0) { tmp[n++] = '0' + v % 10; v /= 10; }
    for (int i = 0; i < n; i++)
        out[i] = tmp[n - 1 - i];
    out[n] = 0;
}

static int toggle_switch(int x, int y, int on, int mx, int my, int clicked)
{
    int sw_w = 46 * S, sw_h = 22 * S;
    int hit = clicked && inside(mx, my, x, y, sw_w, sw_h);
    ui_round_rect(x, y, sw_w, sw_h, 11 * S,
                  on ? fb_rgb(40, 150, 95) : fb_rgb(70, 78, 96));
    int kd = sw_h - 6 * S;
    if (on)
        ui_round_rect(x + sw_w - kd - 3 * S, y + 3 * S, kd, kd, kd / 2,
                      fb_rgb(240, 248, 255));
    else
        ui_round_rect(x + 3 * S, y + 3 * S, kd, kd, kd / 2,
                      fb_rgb(210, 220, 235));
    return hit;
}

static void draw_settings(int mx, int my, int clicked)
{
    if (!win_painted) {
        paint_settings();
        win_painted = 1;
    }
    int title_h = 26 * S, nav_w = 150 * S;
    if (button2(sWX + sWW - 70 * S, sWY + 3 * S, 60 * S, 20 * S,
                tr(STR_CLOSE), 0, mx, my, clicked)) {
        win_id = 0;
        need_redraw = 1;
        return;
    }

    /* navigation clicks */
    int nav_y = sWY + title_h + 2 * S;
    for (int i = 0; i < SET_TABS; i++) {
        int iy = nav_y + 12 * S + i * 40 * S;
        if (clicked && inside(mx, my, sWX + 8 * S, iy, nav_w - 16 * S,
                              32 * S)) {
            set_tab = i;
            win_painted = 0;
            need_redraw = 1;
            return;
        }
    }

    int cx = sWX + nav_w + 18 * S;
    int cy = sWY + title_h + 16 * S;
    int cw = sWW - nav_w - 36 * S;
    int row = 0;
    int row_h = 40 * S;

    if (set_tab == 0) {
        /* Display: live resolution (read-only) + wallpaper quick pick */
        char res[40];
        set_u2s((int)fb_width(), res);
        int p = 0;
        while (res[p]) p++;
        res[p++] = 'x';
        set_u2s((int)fb_height(), res + p);
        gfx_draw_string(cx, cy + row * row_h, fb_rgb(150, 210, 255),
                        fb_rgb(24, 27, 38), tr(STR_DISPLAY_RESOLUTION));
        gfx_draw_string(cx + 120 * S, cy + row * row_h,
                        fb_rgb(220, 230, 245), fb_rgb(24, 27, 38), res);
        row += 2;
        int bw = (cw - 2 * 12 * S) / 3, bh = 56 * S;
        for (int i = 0; i < N_WP; i++) {
            int col = i % 3, rr = i / 3;
            int bx = cx + col * (bw + 12 * S);
            int by = cy + row * row_h + rr * (bh + 26 * S);
            ui_round_rect(bx, by, bw, bh, 6 * S, fb_rgb(30, 36, 52));
            for (int yy = 0; yy < bh; yy += 2) {
                for (int xx = 0; xx < bw - 12 * S; xx += 2) {
                    uint32_t pc;
                    if (i == 0 && wp_buf && wp_index == 0) {
                        int sx = (int)((int64_t)(xx + 6 * S) * fb_width() / (bw - 12 * S));
                        int sy = (int)((int64_t)(yy + 6 * S) * (fb_height() - BAR) / bh);
                        if (sx >= (int)fb_width()) sx = (int)fb_width() - 1;
                        if (sy >= (int)fb_height()) sy = (int)fb_height() - 1;
                        pc = wp_buf[(uint64_t)sy * (int)fb_width() + sx];
                    } else {
                        pc = wp_color(i, xx, yy, bw - 12 * S, bh);
                    }
                    ui_rect(bx + 6 * S + xx, by + 6 * S + yy, 2, 2, pc);
                }
            }
            if (i == wp_index && !custom_wp)
                ui_round_border(bx, by, bw, bh, 6 * S, fb_rgb(255, 210, 80));
            if (clicked && inside(mx, my, bx, by, bw, bh)) {
                wp_index = i;
                custom_wp = 0;
                wp_dirty = 1;
                need_redraw = 1;
            }
            gfx_draw_string(bx + 8 * S, by + bh + 4 * S,
                            fb_rgb(210, 220, 235), fb_rgb(24, 27, 38),
                            tr(wp_name_key(i)));
        }
    } else if (set_tab == 1) {
        /* Region: manual country/region pick, linked to its time zone */
        gfx_draw_string(cx, cy, fb_rgb(150, 210, 255), fb_rgb(24, 27, 38),
                        tr(STR_REGION_MANUAL));
        int bw = (cw - 16 * S) / 2, bh = 28 * S;
        for (int i = 0; i < 25; i++) {
            int col = i / 13, rr = i % 13;
            int bx = cx + col * (bw + 16 * S);
            int by = cy + 32 * S + rr * (bh + 8 * S);
            if (button2(bx, by, bw, bh, REGIONS[i].name,
                        i == config_region(), mx, my, clicked)) {
                config_set_region(i);
                config_set_tz(REGIONS[i].tz_idx);
                config_save();
                need_redraw = 1;
            }
        }
    } else if (set_tab == 2) {
        /* Audio: volume slider + mute */
        gfx_draw_string(cx, cy + row * row_h, fb_rgb(150, 210, 255),
                        fb_rgb(24, 27, 38), tr(STR_AUDIO_VOLUME));
        char vol[8];
        set_u2s(config_volume(), vol);
        gfx_draw_string(cx + cw - 40 * S, cy + row * row_h,
                        fb_rgb(220, 230, 245), fb_rgb(24, 27, 38), vol);
        int track_w = cw - 120 * S;
        int ty = cy + row_h + 8 * S;
        ui_round_rect(cx, ty, track_w, 14 * S, 7 * S, fb_rgb(48, 54, 70));
        int fill = track_w * config_volume() / 100;
        if (fill > 0)
            ui_round_rect(cx, ty, fill, 14 * S, 7 * S, fb_rgb(70, 140, 230));
        if (clicked && inside(mx, my, cx, ty, track_w, 14 * S)) {
            int v = (mx - cx) * 100 / track_w;
            config_set_volume(v);
            config_save();
            need_redraw = 1;
        }
        if (button2(cx + track_w + 16 * S, ty - 6 * S, 40 * S, 26 * S,
                    "-", 0, mx, my, clicked)) {
            config_set_volume(config_volume() - 10);
            config_save();
            need_redraw = 1;
        }
        if (button2(cx + track_w + 60 * S, ty - 6 * S, 40 * S, 26 * S,
                    "+", 0, mx, my, clicked)) {
            config_set_volume(config_volume() + 10);
            config_save();
            need_redraw = 1;
        }
        int my2 = ty + 50 * S;
        gfx_draw_string(cx, my2, fb_rgb(210, 220, 235), fb_rgb(24, 27, 38),
                        tr(STR_AUDIO_MUTE));
        if (toggle_switch(cx + cw - 46 * S, my2 - 2 * S,
                          config_volume() == 0, mx, my, clicked)) {
            config_set_volume(config_volume() == 0 ? 70 : 0);
            config_save();
            need_redraw = 1;
        }
    } else if (set_tab == 3) {
        /* Permissions: real allow/block switches, persisted */
        const char *pl[PERM_COUNT] = {
            tr(STR_PERM_MIC), tr(STR_PERM_VOICE), tr(STR_PERM_CAMERA),
            tr(STR_PERM_LOCATION), tr(STR_PERM_NETWORK),
        };
        for (int i = 0; i < PERM_COUNT; i++) {
            int yy = cy + i * row_h;
            gfx_draw_string(cx, yy, fb_rgb(210, 220, 235),
                            fb_rgb(24, 27, 38), pl[i]);
            int on = perm_granted(i);
            if (toggle_switch(cx + cw - 46 * S, yy - 2 * S, on,
                              mx, my, clicked)) {
                if (on) perm_deny(i);
                else    perm_allow(i);
                need_redraw = 1;
            }
        }
    } else if (set_tab == 4) {
        int card_h = 56 * S;
        ui_round_rect(cx, cy, cw, card_h, 6 * S, fb_rgb(16, 19, 28));
        ui_round_border(cx, cy, cw, card_h, 6 * S, fb_rgb(60, 78, 110));
        gfx_draw_string(cx + 14 * S, cy + 8 * S, fb_rgb(235, 240, 250),
                        fb_rgb(16, 19, 28), i18n_name(i18n_lang()));
        gfx_draw_string(cx + 14 * S, cy + 32 * S, fb_rgb(140, 160, 190),
                        fb_rgb(16, 19, 28), tr(STR_LANGUAGE_LOCKED));
        /* Liquid glass switch: applies live and persists in JBOT.CFG */
        int gy = cy + card_h + 14 * S;
        gfx_draw_string(cx, gy, fb_rgb(210, 220, 235), fb_rgb(24, 27, 38),
                        tr(STR_GLASS));
        if (toggle_switch(cx + cw - 46 * S, gy - 2 * S, gl_on, mx, my,
                          clicked)) {
            gl_on = gl_on ? 0 : 1;
            ui_set_glass(gl_on);
            config_set_glass(gl_on);
            config_save();
            win_painted = 0;
            need_redraw = 1;
        }
        row = 3;
        gfx_draw_string(cx, cy + row * row_h + 22 * S, fb_rgb(150, 210, 255),
                        fb_rgb(24, 27, 38), tr(STR_SYSTEM_VERSION));
        gfx_draw_string(cx + 120 * S, cy + row * row_h + 22 * S,
                        fb_rgb(220, 230, 245), fb_rgb(24, 27, 38), "1.0.9");
        row += 2;
        gfx_draw_string(cx, cy + row * row_h + 22 * S, fb_rgb(150, 210, 255),
                        fb_rgb(24, 27, 38), tr(STR_FONT));
        int fy = cy + (row + 1) * row_h + 22 * S;
        int bw = (cw - 2 * 12 * S) / 3;
        for (int i = 0; i < gfx_font_count(); i++) {
            int bx = cx + i * (bw + 12 * S);
            if (button2(bx, fy, bw, 40 * S, gfx_font_name(i),
                        i == gfx_get_font(), mx, my, clicked)) {
                gfx_set_font(i);
                config_set_font(i);
                config_save();
                win_painted = 0;
                need_redraw = 1;
            }
        }
    } else {
        /* Users: real account list; pick one to switch, or add a new one.
           Passwords stay optional, so "add" never forces one. */
        gfx_draw_string(cx, cy, fb_rgb(150, 210, 255), fb_rgb(24, 27, 38),
                        tr(STR_USERS));
        int n = users_count();
        for (int i = 0; i < n; i++) {
            int ry = cy + (i + 1) * 34 * S;
            int busy = (i == users_current());
            int hit = clicked && inside(mx, my, cx, ry, cw - 120 * S, 28 * S);
            ui_round_rect(cx, ry, cw - 120 * S, 28 * S, 5 * S,
                          busy ? fb_rgb(38, 86, 150) : fb_rgb(30, 36, 52));
            gfx_draw_string(cx + 10 * S, ry + 6 * S,
                            busy ? fb_rgb(240, 248, 255) : fb_rgb(205, 215, 235),
                            busy ? fb_rgb(38, 86, 150) : fb_rgb(30, 36, 52),
                            users_name(i));
            if (busy)
                gfx_draw_string(cx + cw - 240 * S, ry + 6 * S,
                                fb_rgb(160, 200, 240), fb_rgb(38, 86, 150),
                                tr(STR_CURRENT_USER));
            if (hit) {
                users_switch(i);
                app_open(APP_FILES);
                need_redraw = 1;
            }
        }
        int by = cy + (n + 2) * 34 * S;
        if (button2(cx, by, 150 * S, 30 * S, tr(STR_NEW_USER), 0, mx, my,
                    clicked)) {
            char nm[16];
            int p = 0;
            const char *base = "User";
            for (; base[p]; p++) nm[p] = base[p];
            nm[p++] = (char)('0' + (users_count() + 1) % 10);
            nm[p] = 0;
            if (users_add(nm, "") >= 0)
                need_redraw = 1;
        }
        if (button2(cx + 170 * S, by, 170 * S, 30 * S, tr(STR_SET_PASSWORD), 0,
                    mx, my, clicked)) {
            pass_modal(users_current());
            win_painted = 0;
            need_redraw = 1;
        }
    }
}

static void pass_modal(int who)
{
    int W = (int)fb_width(), H = (int)fb_height();
    char buf[32];
    int len = 0, mx = W / 2, my = H / 2;
    for (;;) {
        int cx, cy, ck;
        ps2_mouse_xy(&mx, &my);
        ck = ps2_mouse_click(&cx, &cy);
        if (ck) { mx = cx; my = cy; }

        cursor_begin_frame();
        fb_invalidate();
        fb_fill_rect(0, 0, W, H, fb_rgb(10, 14, 24));
        int pw = 420, ph = 190;
        int px = (W - pw) / 2, py = (H - ph) / 2;
        fb_fill_rect(px, py, pw, ph, fb_rgb(24, 30, 46));
        fb_fill_rect(px, py, pw, 3, fb_rgb(70, 140, 230));
        gfx_draw_string(px + 20, py + 20, fb_rgb(235, 240, 250),
                        fb_rgb(24, 30, 46), users_name(who));
        gfx_draw_string(px + 20, py + 48, fb_rgb(150, 200, 245),
                        fb_rgb(24, 30, 46), tr(STR_SET_PASSWORD));
        gfx_draw_string(px + 20, py + 90, fb_rgb(190, 200, 220),
                        fb_rgb(24, 30, 46), tr(STR_PASSWORD_HINT));
        char shown[34];
        int n = len > 24 ? 24 : len;
        for (int i = 0; i < n; i++)
            shown[i] = '*';
        shown[n] = 0;
        fb_fill_rect(px + 20, py + 120, pw - 40, 28, fb_rgb(14, 20, 32));
        gfx_draw_string(px + 28, py + 126, fb_rgb(230, 238, 250),
                        fb_rgb(14, 20, 32), shown);
        cursor_end_frame(mx, my);
        fb_present_dirty();

        char k;
        while (ps2_read_key(&k)) {
            if (k == '\r' || k == '\n') {
                users_set_pass(who, buf);
                return;
            } else if (k == 27) {
                return;
            } else if (k == '\b' || k == 0x7f) {
                if (len > 0)
                    buf[--len] = 0;
            } else if (k >= 32 && k < 127 && len < 30) {
                buf[len++] = k;
                buf[len] = 0;
            }
        }
    }
}

static int pick_scale(void);
static void pass_modal(int who);

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
    gl_on = config_glass();
    ui_set_glass(gl_on);
    BAR = 28 * S;
    gfx_init_fonts();
    gfx_set_scale(S);
    ui_set_scale(S);
    cursor_set_scale(S);
    cursor_init();
    i18n_init();
    /* the language is stored as a code: a numeric index no longer identifies
       a language once the shipped table changes size */
    {
        const char *lc = config_language_code();
        if (lc[0])
            i18n_set_code(lc);
        else
            i18n_set_lang(config_language());
    }
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
        ui_fit(620, 560, &fit);
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
    jb_add(tr(STR_JBOT_GREETING));
    jb_dirty = 1;

    wp_buf = kcalloc((size_t)fb_width() * fb_height(), 4);
    serial_puts("[DESK] wp_buf=");
    serial_puthex((uint64_t)(uintptr_t)wp_buf);
    serial_puts(" need_bytes=");
    serial_puthex((uint64_t)fb_width() * fb_height() * 4);
    if (!wp_buf)
        serial_puts(" (ALLOC FAILED: wallpaper disabled)");
    serial_putc('\n');
    wp_dirty = 1;

    /* Default wallpaper: the East Lake photo linked into the kernel. It is
       decoded as a row stream straight into wp_buf (no full-size temp
       buffer). A user-chosen picture still replaces it at runtime. */
    if (wp_buf) {
        if (wimg_apply_default() == 0) {
            wp_dirty = 0;   /* the decoder already filled wp_buf */
            serial_puts("[DESK] default wallpaper: East Lake\n");
        } else {
            serial_puts("[DESK] default wallpaper failed: ");
            serial_puts(wimg_status());
            serial_putc('\n');
            wp_dirty = 1;
        }
    }
}

/* ---- Desktop shortcuts: click an icon to launch a program ---- */
#define DSK_ICONS 4
static const char *dsk_label(int i)
{
    switch (i) {
    case 0: return app_name(APP_FILES);
    case 1: return app_name(APP_BROWSER);
    case 2: return app_name(APP_PYTHON);
    default: return "Jbot";
    }
}
static const int dsk_app[DSK_ICONS] = {
    APP_FILES, APP_BROWSER, APP_PYTHON, 3
};

static void dsk_icon_geom(int i, int *x, int *y, int *w, int *h)
{
    int sz = 64 * S;
    *x = 26 * S;
    *y = 84 * S + i * (sz + 34 * S);
    *w = sz;
    *h = sz + 22 * S;
}

static int dsk_icon_covered(int i)
{
    int x, y, w, h;
    dsk_icon_geom(i, &x, &y, &w, &h);
    int wx, wy, ww, wh;
    switch (win_id) {
    case 1: wx = pWX; wy = pWY; ww = pWW; wh = pWH; break;
    case 2: wx = sWX; wy = sWY; ww = sWW; wh = sWH; break;
    case 3: wx = jWX; wy = jWY; ww = jWW; wh = jWH; break;
    default: return 0;
    }
    if (x + w <= wx || wx + ww <= x || y + h <= wy || wy + wh <= y)
        return 0;
    return 1;
}

static void draw_desktop_icons(int mx, int my, int clicked)
{
    for (int i = 0; i < DSK_ICONS; i++) {
        int x, y, w, h;
        dsk_icon_geom(i, &x, &y, &w, &h);
        /* Skip icons the open window already covers: a window body is only
           painted when it changes, so stamping an icon on top of it would
           punch a hole in the window. */
        if (dsk_icon_covered(i))
            continue;
        int hover = inside(mx, my, x, y, w, h);
        uint32_t tint = (i == 0) ? fb_rgb(70, 126, 204)
                      : (i == 1) ? fb_rgb(96, 146, 216)
                      : (i == 2) ? fb_rgb(52, 132, 120)
                                 : fb_rgb(120, 90, 190);
        draw_glass_round_rect(x, y, w, w, 12 * S, tint, hover ? 210 : 150);
        ui_round_border(x, y, w, w, 12 * S,
                        hover ? fb_rgb(200, 220, 255) : fb_rgb(140, 170, 220));
        {
            /* Label under the tile, not squeezed inside it: longer
               translations (Japanese, German, ...) used to overflow the
               64 px cell and run off the left edge of the screen. */
            const char *nm = dsk_label(i);
            int tw = gfx_text_width(nm);
            int th = gfx_text_height();
            int lx = x, ly = y + w + 2 * S;
            if (lx + tw > (int)fb_width() - 4 * S)
                lx = (int)fb_width() - 4 * S - tw;
            if (lx < 4 * S)
                lx = 4 * S;
            draw_glass_round_rect(lx - 4 * S, ly - 2 * S, tw + 8 * S, th + 4 * S,
                                  5 * S, fb_rgb(12, 16, 26), 205);
            gfx_draw_string(lx, ly, fb_rgb(240, 246, 255),
                            fb_rgb(12, 16, 26), nm);
        }
        if (clicked && hover) {
            int id = dsk_app[i];
            if (id >= APP_POWER && id < APP_LIMIT)
                app_open(id);
            win_id = id;
            win_painted = 0;
            drawn_win = -1;
            need_redraw = 1;
        }
    }
}

void desktop_run(void)
{
    int mx, my;
    int opened_app = 0;
    ps2_mouse_xy(&mx, &my);
    int cx, cy;
    int clicked = ps2_mouse_click(&cx, &cy);
    if (clicked) {
        mx = cx;
        my = cy;
    }
    int rcx, rcy;
    int rclicked = ps2_mouse_right_click(&rcx, &rcy);
    if (rclicked) {
        mx = rcx;
        my = rcy;
    }
    if (timer_get_ticks() < arm_until) {
        clicked = 0;
        rclicked = 0;
    }

    /* Lift the pointer off the frame before anything else paints. It used to
       be restored just before the UI drew, but dragging now repaints regions
       of the screen early in the frame, which would have stamped the stale
       saved pixels back over freshly restored wallpaper. */
    cursor_begin_frame();

    /* Text on a translucent panel must not paint an opaque box; toggle the
       glyph layer's background fill with the glass setting. */
    gfx_set_transparent(gl_on);

    net_poll();
    tcp_poll();

    if (carousel && !custom_wp) {
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

    /* Mouse wheel: with the menu open it moves the highlight; with no
       window open it cycles the wallpaper (a visible, cheap response). */
    {
        int wd = ps2_mouse_wheel();
        if (wd) {
            if (start_open) {
                if (wd > 0 && menu_sel > 0) menu_sel--;
                else if (wd < 0 && menu_sel + 1 < fcount) menu_sel++;
            } else if (win_id == 2 && inside(mx, my, sWX, sWY, sWW, sWH)) {
                /* wheel inside the Settings window walks its tabs */
                int nt = set_tab + (wd > 0 ? -1 : 1);
                if (nt >= 0 && nt <= 4 && nt != set_tab) {
                    set_tab = nt;
                    need_redraw = 1;
                }
            } else if (win_id == 0) {
                wp_index = (wd > 0) ? (wp_index + N_WP - 1) % N_WP
                                    : (wp_index + 1) % N_WP;
                custom_wp = 0;
                wp_dirty = 1;
                need_redraw = 1;
                win_painted = 0;
            }
        }
    }

    /* Window dragging: grab the title strip of the built-in windows. */
    {
        static int drag_win, drag_off_x, drag_off_y;
        int drag_started = drag_win;
        int title_h = 26 * S;
        if (ps2_mouse_down()) {
            if (!drag_win) {
                if (win_id == 1 && inside(mx, my, pWX, pWY, pWW, title_h)) {
                    drag_win = 1; drag_off_x = mx - pWX; drag_off_y = my - pWY;
                } else if (win_id == 2 && inside(mx, my, sWX, sWY, sWW, title_h)) {
                    drag_win = 2; drag_off_x = mx - sWX; drag_off_y = my - sWY;
                } else if (win_id == 3 && inside(mx, my, jWX, jWY, jWW, title_h)) {
                    drag_win = 3; drag_off_x = mx - jWX; drag_off_y = my - jWY;
                }
            }
        } else {
            drag_win = 0;
        }
        if (!drag_started && drag_win) {
            serial_puts("[DESK] drag start win=");
            serial_puthex((uint32_t)drag_win);
            serial_putc('\n');
        }
        if (drag_win) {
            int ox = (drag_win == 1) ? pWX : (drag_win == 2 ? sWX : jWX);
            int oy = (drag_win == 1) ? pWY : (drag_win == 2 ? sWY : jWY);
            int nx = mx - drag_off_x, ny = my - drag_off_y;
            int ww = (drag_win == 1) ? pWW : (drag_win == 2 ? sWW : jWW);
            int wh = (drag_win == 1) ? pWH : (drag_win == 2 ? sWH : jWH);
            if (nx < 0) nx = 0;
            if (ny < 0) ny = 0;
            if (nx + ww > (int)fb_width()) nx = (int)fb_width() - ww;
            if (ny + wh > (int)fb_height() - BAR) ny = (int)fb_height() - BAR - wh;
            if (drag_win == 1) { pWX = nx; pWY = ny; }
            else if (drag_win == 2) { sWX = nx; sWY = ny; }
            else { jWX = nx; jWY = ny; }
            /* Repaint only the silhouette the window sweeps: the union of its
               old and new rectangles, restored from the wallpaper surface.
               A full-screen wallpaper blit here was what made dragging feel
               heavy. The window body itself is repainted later in the frame
               because win_painted is cleared. */
            if (nx != ox || ny != oy) {
                /* Repaint the two silhouettes the window vacated and now
                   occupies, straight from the wallpaper surface. Restoring
                   both rects keeps the frame correct without a full-screen
                   flush (a whole-frame blit made dragging heavy); the stale
                   pointer pixels are dropped first so they cannot be saved
                   back over the fresh wallpaper. */
                cursor_invalidate();
                if (wp_buf) {
                    fb_blit_rect(ox, oy, ww, wh,
                                 wp_buf + (size_t)oy * fb_width() + ox,
                                 fb_width());
                    fb_blit_rect(nx, ny, ww, wh,
                                 wp_buf + (size_t)ny * fb_width() + nx,
                                 fb_width());
                }
                win_painted = 0;
                drawn_win = -1;
            }
        }
    }

    /* Overlay surfaces (start menu, context menu) blend over the frame, so the
       pixels under them must be rebuilt before they are composited. A full
       wallpaper rebuild is expensive, and doing it on every pointer move made
       the glass overlay blink as it was recomposited frame after frame. It is
       now only rebuilt when the overlay itself changes state. */
    {
        static int ov_open, ov_sel, ov_search;
        int now_open = start_open ? 1 : (ctx_open ? 2 : 0);
        if (now_open && (now_open != ov_open ||
                         menu_sel != ov_sel || search_len != ov_search)) {
            need_redraw = 1;
            drawn_win = -1;
        }
        ov_open = now_open;
        ov_sel = menu_sel; ov_search = search_len;
    }

    /* Pointer shape follows the context: hand over clickable items, I-beam
       over the menu search box, four-way arrow while a window is being
       dragged, plain arrow everywhere else. */
    {
        int shape = CUR_ARROW;
        if (win_id == 0 && !start_open) {
            for (int i = 0; i < DSK_ICONS; i++) {
                int ix, iy, iw, ih;
                dsk_icon_geom(i, &ix, &iy, &iw, &ih);
                if (inside(mx, my, ix, iy, iw, ih))
                    shape = CUR_HAND;
            }
        }
        if (start_open) {
            for (int k = 0; k < fcount; k++) {
                int ix, iy, iw, ih;
                menu_item_geom(k, &ix, &iy, &iw, &ih);
                if (inside(mx, my, ix, iy, iw, ih))
                    shape = CUR_HAND;
            }
            for (int q = 0; q < M_QUICK; q++) {
                int qx, qy, qw, qh;
                m_quick_geom(q, &qx, &qy, &qw, &qh);
                if (inside(mx, my, qx, qy, qw, qh))
                    shape = CUR_HAND;
            }
            if (inside(mx, my, mX + mW / 2, mY,
                       mW - mW / 2 - 14 * S - m_rightw, m_titleh))
                shape = CUR_TEXT;
        }
        if (ps2_mouse_down() && win_id >= 1 && win_id <= 3)
            shape = CUR_MOVE;
        cursor_set_shape(shape);
    }

    int start_click = 0, set_click = 0, jb_click = 0;
    if (need_redraw) {
        render_wallpaper();
        need_redraw = 0;
        drawn_win = -1;
        /* A full wallpaper repaint overwrites everything under the windows,
           so every window body must be repainted again. Without this the
           window frame (painted once into the back buffer) is erased and
           the desktop shows straight through it. */
        win_painted = 0;
    }
    if (win_id != last_win_id) {
        /* Switching windows must wipe the previous one off the screen: the
           bodies are opaque and drawn in place, so leaving its pixels behind
           would show through the new window. One wallpaper repaint covers it. */
        need_redraw = 1;
        win_painted = 0;
        drawn_win = -1;
        last_win_id = win_id;
    }
    draw_taskbar(mx, my, clicked, &start_click,
                 &set_click, &jb_click);
    if (start_click) {
        start_open ^= 1;
        menu_sel = 0;
        search_len = 0; search_buf[0] = 0;
        rebuild_filter();
        if (start_open)
            restore_win = win_id;
    }

    if (set_click) {
        win_id = 2; win_painted = 0; start_open = 0;
    }
    if (jb_click) {
        win_id = 3; win_painted = 0; start_open = 0; jb_dirty = 1;
    }

    /* Desktop shortcuts sit on the wallpaper. Draw them every frame (the
       wallpaper is repainted underneath them whenever the frame is rebuilt),
       but only take clicks while no window is open so a click on an icon
       hiding under a window cannot launch it. */
    if (!start_open)
        draw_desktop_icons(mx, my, win_id == 0 ? clicked : 0);

    /* Right-click menu: one panel only, and one click dismisses it. */
    if (rclicked) {
        if (ctx_open) {
            ctx_open = 0;
            need_redraw = 1;       /* wipe the panel off the wallpaper */
        } else if (my < (int)fb_height() - BAR) {
            ctx_open = 1;
            ctx_x = rcx; ctx_y = rcy;
            ctx_sel = 0;
            start_open = 0;
            /* A new menu must not be composited on top of the pixels of an
               earlier one: rebuild the frame first. */
            need_redraw = 1;
        }
    }
    if (ctx_open && clicked) {
        int bx, by, bw, bh;
        ctx_box(&bx, &by, &bw, &bh);
        if (!inside(mx, my, bx, by, bw, bh)) {
            ctx_open = 0;
            need_redraw = 1;   /* wipe the menu pixels off the wallpaper */
        } else {
            for (int i = 0; i < CTX_ITEMS; i++) {
                int ix, iy, iw, ih;
                ctx_item_geom(i, bx, by, bw, &ix, &iy, &iw, &ih);
                if (inside(mx, my, ix, iy, iw, ih)) {
                    if (i == 0) { win_id = 2; win_painted = 0; }
                    else if (i == 1) { need_redraw = 1; }
                    else { app_open(APP_ABOUT); win_id = APP_ABOUT;
                           win_painted = 1; }
                    ctx_open = 0;
                    need_redraw = 1;
                    break;
                }
            }
        }
    }

    if (win_id == 0) {
        char kc;
        while (ps2_read_key(&kc)) {
            if (kc == JB_KEY_F1) {
                win_id = 2; win_painted = 0; start_open = 0;
                break;
            } else if (kc == JB_KEY_F2) {
                win_id = 2; win_painted = 0; start_open = 0;
                break;
            } else if (kc == JB_KEY_F3) {
                win_id = 3; win_painted = 0; start_open = 0; jb_dirty = 1;
                break;
            }
            if (kc == 27 && ctx_open) {
                ctx_open = 0;
                continue;
            }
            if (kc == JB_KEY_WIN || kc == '\t') {
                start_open ^= 1;
                menu_sel = 0;
                search_len = 0; search_buf[0] = 0;
                rebuild_filter();
            } else if (start_open) {
                int col = menu_sel / m_rows;
                int row = menu_sel % m_rows;
                if (kc == JB_KEY_DOWN) {
                    if (row + 1 < m_rows && menu_sel + 1 < fcount)
                        menu_sel++;
                } else if (kc == JB_KEY_UP) {
                    if (menu_sel > 0)
                        menu_sel--;
                } else if (kc == JB_KEY_RIGHT) {
                    if (menu_sel + m_rows < fcount)
                        menu_sel += m_rows;
                } else if (kc == JB_KEY_LEFT) {
                    if (col > 0)
                        menu_sel -= m_rows;
                } else if (kc == '\n' || kc == '\r') {
                    if (fcount) {
                        int id = APP_POWER + fmap[menu_sel];
                        app_open(id);
                        win_id = id;
                        win_painted = 1;
                        start_open = 0;
                        mHit = 1;
                    }
                } else if (kc == 27) {
                    if (search_len) {
                        search_len = 0; search_buf[0] = 0;
                        menu_sel = 0; rebuild_filter();
                    } else {
                        start_open = 0;
                    }
                } else if (kc == '\b' || kc == 0x7f) {
                    if (search_len > 0) {
                        search_buf[--search_len] = 0;
                        rebuild_filter();
                    }
                } else if (kc >= 32 && search_len < (int)sizeof(search_buf) - 1) {
                    search_buf[search_len++] = kc;
                    search_buf[search_len] = 0;
                    menu_sel = 0;
                    rebuild_filter();
                }
            }
        }
    }

    if (start_open) {
        menu_layout();
        int rad = 10 * S;
        /* Liquid-glass panel: translucent, the wallpaper/window shows
           through. The frame is fully repainted while the menu is open,
           so the blend always samples fresh pixels (no ghosting). */
        draw_glass_rect(mX, mY, mW, mH, fb_rgb(24, 28, 40), glc(112));
        ui_round_border(mX, mY, mW, mH, rad, fb_rgb(96, 146, 216));
        /* translucent blue title strip */
        draw_glass_rect(mX + rad, mY, mW - 2 * rad, m_titleh,
                        fb_rgb(30, 64, 122), 150);
        draw_glass_rect(mX, mY + rad, mW, m_titleh - rad,
                        fb_rgb(30, 64, 122), 150);
        ui_rect(mX, mY + m_titleh, mW, 2 * S, fb_rgb(70, 126, 204));
        gfx_draw_string(mX + 14 * S, mY + 8 * S, fb_rgb(235, 242, 255),
                        fb_rgb(30, 64, 122), "JackBOT OS 1.0.9");
        /* search box on the right of the title strip */
        int sbx = mX + mW / 2;
        int sbw = mW - (sbx - mX) - 14 * S - m_rightw;
        int sbh = 22 * S;
        int sby = mY + (m_titleh - sbh) / 2;
        draw_glass_rect(sbx, sby, sbw, sbh, fb_rgb(18, 24, 38), 140);
        ui_round_border(sbx, sby, sbw, sbh, 5 * S, fb_rgb(96, 146, 216));
        if (search_len)
            gfx_draw_string(sbx + 10 * S, sby + (sbh - gfx_text_height()) / 2,
                            fb_rgb(235, 242, 255), fb_rgb(18, 24, 38),
                            search_buf);
        else
            gfx_draw_string(sbx + 10 * S, sby + (sbh - gfx_text_height()) / 2,
                            fb_rgb(130, 142, 164), fb_rgb(18, 24, 38),
                            tr(STR_SEARCH_HINT));
        if (fcount == 0)
            gfx_draw_string(mX + 14 * S, mY + m_titleh + 16 * S,
                            fb_rgb(180, 190, 208), fb_rgb(26, 30, 44),
                            tr(STR_NO_MATCHES));
        for (int k = 0; k < fcount; k++) {
            int i = fmap[k];
            int id = APP_POWER + i;
            int ix, iy, iw, ih;
            menu_item_geom(k, &ix, &iy, &iw, &ih);
            int hov = inside(mx, my, ix, iy, iw, ih);
            int hl = hov || k == menu_sel;
            if (hl) {
                draw_glass_rect(ix, iy, iw, ih, fb_rgb(52, 92, 160), 190);
                ui_round_border(ix, iy, iw, ih, 5 * S, fb_rgb(130, 175, 245));
            }
            gfx_draw_string(ix + 12 * S, iy + (ih - gfx_text_height()) / 2,
                            fb_rgb(224, 232, 246),
                            hl ? fb_rgb(52, 92, 160) : fb_rgb(26, 30, 44),
                            app_name(id));
            if (clicked && hov) {
                app_open(id);
                win_id = id;
                win_painted = 0;
                start_open = 0;
                mHit = 1;
                opened_app = 1;
                drawn_win = -1;
                need_redraw = 1;
            }
        }

        /* Right-hand quick buttons (glass), including "Back" which restores
           the window that was open before the menu was opened. */
        ui_rect(mX + mW - m_rightw, mY + m_titleh + 2 * S,
                (int)1, mH - m_titleh - 4 * S, fb_rgb(70, 126, 204));
        for (int q = 0; q < M_QUICK; q++) {
            int qx, qy, qw, qh;
            m_quick_geom(q, &qx, &qy, &qw, &qh);
            int hov = inside(mx, my, qx, qy, qw, qh);
            if (hov) {
                draw_glass_rect(qx, qy, qw, qh, fb_rgb(52, 92, 160), 190);
                ui_round_border(qx, qy, qw, qh, 6 * S, fb_rgb(130, 175, 245));
            }
            gfx_draw_string(qx + 12 * S, qy + (qh - gfx_text_height()) / 2,
                            fb_rgb(224, 232, 246),
                            hov ? fb_rgb(52, 92, 160) : fb_rgb(26, 30, 44),
                            m_quick_label(q));
            if (clicked && hov) {
                int w = m_quick_win(q);
                if (w < 0) {
                    win_id = restore_win;
                } else {
                    if (w >= APP_POWER && w < APP_LIMIT)
                        app_open(w);
                    win_id = w;
                }
                win_painted = 0;
                drawn_win = -1;
                need_redraw = 1;
                start_open = 0;
                opened_app = 1;
                mHit = 1;
            }
        }
    }

    if (start_open) {
        prev_mX = mX; prev_mY = mY; prev_mW = mW; prev_mH = mH;
        menu_was_open = 1;
    } else if (menu_was_open) {
        /* The frame is repainted while a menu is open, so the old menu
           pixels are already gone; only force one clean repaint of the
           window that is now active. */
        menu_was_open = 0;
        if (!opened_app) {
            drawn_win = -1;
            need_redraw = 1;
        }
    }
    if (mHit)
        mHit = 0;

    if (ctx_open) {
        int bx, by, bw, bh;
        ctx_box(&bx, &by, &bw, &bh);
        int crad = 8 * S;
        draw_glass_rect(bx, by, bw, bh, fb_rgb(24, 28, 40), glc(120));
        ui_round_border(bx, by, bw, bh, crad, fb_rgb(96, 146, 216));
        for (int i = 0; i < CTX_ITEMS; i++) {
            int ix, iy, iw, ih;
            ctx_item_geom(i, bx, by, bw, &ix, &iy, &iw, &ih);
            int hov = inside(mx, my, ix, iy, iw, ih);
            if (hov) {
                ui_round_rect(ix, iy, iw, ih, 5 * S, fb_rgb(52, 92, 160));
                ui_round_border(ix, iy, iw, ih, 5 * S,
                                fb_rgb(130, 175, 245));
            }
            gfx_draw_string(ix + 10 * S, iy + (ih - gfx_text_height()) / 2,
                            fb_rgb(224, 232, 246),
                            hov ? fb_rgb(52, 92, 160) : fb_rgb(26, 30, 44),
                            ctx_label(i));
        }
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
            /* A persistent window keeps its pixels between frames, so the
               old gate (repaint only when drawn_win changed, or on the timer
               tick) also swallowed every click and keystroke: the file
               manager repainted once and then never saw Delete/Open again.
               period == 0 now means "drive every frame" (same cost as the
               non-persistent apps, which is what these dialogs need), and a
               timer-driven app is additionally woken by input. */
            static int last_mx = -1, last_my = -1;
            int moved = (mx != last_mx || my != last_my);
            last_mx = mx; last_my = my;
            int need = (drawn_win != win_id) || moved ||
                       (period > 0 && (frame_cnt % period) == 0) ||
                       clicked || ps2_key_pending();
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

    /* One submit per frame. The dirty list now keeps the pointer, the taskbar
       and any window as separate rectangles, so the pointer no longer forces
       a nearly full-screen copy and there is no need to flush twice. A second
       flush per frame was itself visible as a flicker on the VMSVGA scanout. */
    cursor_end_frame(mx, my);
    fb_present_dirty();
}

