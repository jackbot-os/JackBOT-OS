#include <stdint.h>
#include <stddef.h>
#include <kernel/installer_gui.h>
#include <kernel/installer.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ps2.h>
#include <kernel/serial.h>
#include <kernel/cursor.h>
#include <kernel/svga.h>
#include <kernel/i18n.h>
#include "i18n_data.h"

uint64_t timer_get_ticks(void);

#define PAGE_LANGUAGE 0
#define PAGE_WELCOME 1
#define PAGE_REGION 2
#define PAGE_DISK 3
#define PAGE_WORKING 4
#define PAGE_DONE 5
#define PAGE_ERROR 6

#define COL_BG_TOP 0x05060a
#define COL_BG_BOT 0x10131b
#define COL_CARD 0x151a26
#define COL_CARD_EDGE 0x3d4d6e
#define COL_TEXT 0xeef2f8
#define COL_DIM 0x9aa8c0
#define COL_ACCENT 0x3b82f6
#define COL_ACCENT_HI 0x60a5fa
#define COL_BTN 0x263149
#define COL_BTN_HI 0x35466b
#define COL_TRACK 0x20293a
#define COL_OK 0x22c55e
#define COL_ERR 0xef4444
#define COL_SEL 0x1c3a63
#define COL_LIST_BG 0x0d1320

#define ROW_H 40
#define PAD 52
#define CARD_R 18

static int page;
static int card_x, card_y, card_w, card_h;
static int g_pct;
static int g_mode;
static struct disk_info g_di;
static int g_have_payload;
static int g_started;

static int primary_x, primary_y, btn_w, btn_h;
static int back_x, back_y;

/* Language picker state */
static int g_sel;
static int g_scroll;
static int list_top, list_bottom, list_x, list_w;
static int vis_rows;

/* Region (time zone) picker state */
static int g_tz_sel = 20;

/* Partitioning (manual system size) state */
static int g_manual;
static uint32_t g_manual_mb;
static int g_rad_x, g_rad_w, g_rad_h;
static int g_auto_y, g_manual_y;
static int g_minus_x, g_plus_x, g_step_y, g_step_btn;
static int g_cur_sys_mb;

static int is_rtl(void) { return i18n_rtl(); }

static void u2s(uint64_t v, char *out)
{
    char tmp[24];
    int n = 0;
    if (v == 0) {
        out[0] = '0';
        out[1] = 0;
        return;
    }
    while (v) {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    }
    for (int i = 0; i < n; i++)
        out[i] = tmp[n - 1 - i];
    out[n] = 0;
}

static void app(char *buf, int *len, const char *s)
{
    while (*s)
        buf[(*len)++] = *s++;
}

static int isqrt32(int v)
{
    if (v <= 0)
        return 0;
    int r = 0;
    int bit = 1 << 30;
    while (bit > v)
        bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else              r >>= 1;
        bit >>= 2;
    }
    return r;
}

static void round_fill(int x, int y, int w, int h, int r, uint32_t c)
{
    if (w <= 0 || h <= 0)
        return;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    for (int yy = 0; yy < h; yy++) {
        int dy = 0;
        if (yy < r)
            dy = r - yy;
        else if (yy >= h - r)
            dy = r - h + 1 + yy;
        int inset = 0;
        if (dy > 0) {
            int dx = isqrt32(r * r - dy * dy);
            inset = r - dx;
        }
        fb_fill_rect(x + inset, y + yy, w - 2 * inset, 1, c);
    }
}

/* 1px rounded edge over a fill color */
static void round_panel(int x, int y, int w, int h, int r,
                        uint32_t fill, uint32_t edge)
{
    round_fill(x, y, w, h, r, edge);
    round_fill(x + 1, y + 1, w - 2, h - 2, r - 1, fill);
}

/* ---------------- pointer (unified hardware/software cursor) ----------- */

static int text_w(const char *s)
{
    return is_rtl() ? gfx_text_width_rtl(s) : gfx_text_width_wide(s);
}

/* centered text */
static void text_c(int cx, int y, int sc, uint32_t fg, uint32_t bg,
                   const char *s)
{
    gfx_set_scale(sc);
    int w = text_w(s);
    if (is_rtl())
        gfx_draw_string_rtl(cx + w / 2, y, fg, bg, s);
    else
        gfx_draw_string_wide(cx - w / 2, y, fg, bg, s);
    gfx_set_scale(1);
}

/* side aligned text: LTR starts at x, RTL ends at x */
static void text_side(int x, int y, int sc, uint32_t fg, uint32_t bg,
                      const char *s)
{
    gfx_set_scale(sc);
    if (is_rtl())
        gfx_draw_string_rtl(x, y, fg, bg, s);
    else
        gfx_draw_string_wide(x, y, fg, bg, s);
    gfx_set_scale(1);
}

static void paint_background(void)
{
    int W = (int)fb_width(), H = (int)fb_height();
    int rt = (COL_BG_TOP >> 16) & 0xff, gt = (COL_BG_TOP >> 8) & 0xff,
        bt = COL_BG_TOP & 0xff;
    int rb = (COL_BG_BOT >> 16) & 0xff, gb = (COL_BG_BOT >> 8) & 0xff,
        bb = COL_BG_BOT & 0xff;
    for (int y = 0; y < H; y++) {
        int t = (int)((uint64_t)y * 255 / H);
        uint8_t r = (uint8_t)(rt + (rb - rt) * t / 255);
        uint8_t g = (uint8_t)(gt + (gb - gt) * t / 255);
        uint8_t b = (uint8_t)(bt + (bb - bt) * t / 255);
        fb_fill_rect(0, y, W, 1, fb_rgb(r, g, b));
    }
    /* subtle top accent line, modern dark look */
    fb_fill_rect(0, 0, W, 3, fb_rgb(59, 130, 246));
}

static void paint_card(void)
{
    round_panel(card_x, card_y, card_w, card_h, CARD_R,
                COL_CARD, COL_CARD_EDGE);
}

static int hit(int bx, int by, int bw, int bh, int mx, int my)
{
    return mx >= bx && mx < bx + bw && my >= by && my < by + bh;
}

static void draw_button(int bx, int by, int bw, int bh, const char *label,
                        int mx, int my, int clicked, int primary)
{
    int hover = hit(bx, by, bw, bh, mx, my);
    uint32_t base = primary ? (hover ? COL_ACCENT_HI : COL_ACCENT)
                            : (hover ? COL_BTN_HI : COL_BTN);
    if (clicked && hover)
        base = primary ? COL_ACCENT : COL_BTN;
    round_panel(bx, by, bw, bh, 12, base,
                hover ? COL_TEXT : COL_CARD_EDGE);
    gfx_set_scale(2);
    int tw = text_w(label);
    int th = gfx_text_height();
    if (is_rtl())
        gfx_draw_string_rtl(bx + bw / 2 + tw / 2,
                            by + (bh - th) / 2 + 2, COL_TEXT, base, label);
    else
        gfx_draw_string_wide(bx + bw / 2 - tw / 2,
                             by + (bh - th) / 2 + 2, COL_TEXT, base, label);
    gfx_set_scale(1);
    (void)clicked;
}

static void draw_progress(int pct)
{
    int px = card_x + 48;
    int pw = card_w - 96;
    int py = card_y + card_h - 150;
    int ph = 28;
    round_fill(px, py, pw, ph, ph / 2, COL_TRACK);
    int fw = pw * pct / 100;
    if (fw > ph)
        round_fill(px, py, fw, ph, ph / 2, COL_ACCENT);
    else if (fw > 0)
        round_fill(px, py, fw, ph, fw / 2, COL_ACCENT);
    char pct_s[8];
    u2s(pct, pct_s);
    char pct_lab[12];
    int i = 0;
    app(pct_lab, &i, pct_s);
    app(pct_lab, &i, "%");
    pct_lab[i] = 0;
    text_c(px + pw / 2, py - 34, 2, COL_TEXT, COL_CARD, pct_lab);
}

/* ---------------- page 1: language picker (English) ---------------- */

static void clamp_scroll(void)
{
    int max_scroll = I18N_NLANG - vis_rows;
    if (max_scroll < 0)
        max_scroll = 0;
    if (g_scroll < 0)
        g_scroll = 0;
    if (g_scroll > max_scroll)
        g_scroll = max_scroll;
    if (g_sel < g_scroll)
        g_scroll = g_sel;
    if (g_sel >= g_scroll + vis_rows)
        g_scroll = g_sel - vis_rows + 1;
}

static void render_language(void)
{
    int cx = card_x + card_w / 2;
    text_c(cx, card_y + 34, 3, COL_TEXT, COL_CARD, "Select Language");
    text_c(cx, card_y + 92, 2, COL_ACCENT_HI, COL_CARD,
           "Select your language, then choose Next.");

    /* list background */
    list_top = card_y + 150;
    list_bottom = card_y + card_h - 160;
    list_x = card_x + 40;
    list_w = card_w - 80;
    vis_rows = (list_bottom - list_top) / ROW_H;
    clamp_scroll();

    round_panel(list_x, list_top, list_w, vis_rows * ROW_H, 12,
                COL_LIST_BG, COL_CARD_EDGE);

    for (int r = 0; r < vis_rows; r++) {
        int idx = g_scroll + r;
        if (idx >= I18N_NLANG)
            break;
        int ry = list_top + r * ROW_H;
        int sel = (idx == g_sel);
        if (sel) {
            round_panel(list_x + 4, ry + 2, list_w - 8, ROW_H - 4, 8,
                        COL_SEL, COL_ACCENT);
        }
        const char *nm = i18n_name(idx);
        uint32_t fg = sel ? COL_TEXT : COL_DIM;
        int yoff = (ROW_H - (gfx_text_height())) / 2;
        gfx_set_scale(2);
        yoff = (ROW_H - gfx_text_height()) / 2;
        if (i18n_rtl_idx(idx))
            gfx_draw_string_rtl(list_x + list_w - 22, ry + yoff, fg,
                                sel ? COL_SEL : COL_LIST_BG, nm);
        else
            gfx_draw_string_wide(list_x + 22, ry + yoff, fg,
                                 sel ? COL_SEL : COL_LIST_BG, nm);
        gfx_set_scale(1);
    }

    /* scroll bar */
    if (I18N_NLANG > vis_rows) {
        int track_h = vis_rows * ROW_H;
        int bar_h = track_h * vis_rows / I18N_NLANG;
        if (bar_h < 24)
            bar_h = 24;
        int max_scroll = I18N_NLANG - vis_rows;
        int bar_y = list_top +
                    (track_h - bar_h) * g_scroll / (max_scroll ? max_scroll : 1);
        fb_fill_rect(list_x + list_w - 6, list_top, 4, track_h, COL_TRACK);
        fb_fill_rect(list_x + list_w - 6, bar_y, 4, bar_h, COL_ACCENT);
    }

    text_c(cx, card_y + card_h - 132, 1, COL_DIM, COL_CARD,
           "Up/Down to move - Enter to confirm");
    draw_button(primary_x, primary_y, btn_w, btn_h, "Next", 0, 0, 0, 1);
}

/* ---------------- page 2: welcome ---------------- */

static void render_welcome(void)
{
    int cx = card_x + card_w / 2;
    int left = card_x + PAD;
    int right = card_x + card_w - PAD;
    text_c(cx, card_y + 40, 3, COL_TEXT, COL_CARD, tr(STR_WELCOME_TO));
    text_c(cx, card_y + 100, 2, COL_ACCENT_HI, COL_CARD, tr(STR_SETUP_WIZARD));

    char mb[96];
    uint64_t mbytes = g_di.total_sectors / 2048;
    char num[24];
    u2s(mbytes, num);
    int k = 0;
    app(mb, &k, tr(STR_DISK_SIZE));
    app(mb, &k, num);
    app(mb, &k, tr(STR_UNIT_MB));
    mb[k] = 0;

    const char *state;
    if (!g_have_payload)
        state = tr(STR_ERR_MISSING);
    else if (g_di.state == INST_STATE_NO_DISK)
        state = tr(STR_STATUS_NO_DISK);
    else if (g_di.state == INST_STATE_INSTALLED)
        state = tr(STR_STATUS_INSTALLED);
    else if (g_di.state == INST_STATE_EMPTY)
        state = tr(STR_STATUS_EMPTY);
    else
        state = tr(STR_STATUS_FOREIGN);

    int ty = card_y + 170;
    text_side(is_rtl() ? right : left, ty, 2, COL_DIM, COL_CARD, mb);
    ty += 52;
    text_side(is_rtl() ? right : left, ty, 2, COL_DIM, COL_CARD, state);
    ty += 52;
    text_side(is_rtl() ? right : left, ty, 2, COL_DIM, COL_CARD,
              tr(STR_PARTITIONS_INFO));
    ty += 52;
    text_side(is_rtl() ? right : left, ty, 2, COL_DIM, COL_CARD,
              tr(STR_UPGRADE_WARNING));

    draw_button(back_x, back_y, btn_w, btn_h, tr(STR_BACK), 0, 0, 0, 0);
    draw_button(primary_x, primary_y, btn_w, btn_h, "Continue", 0, 0, 0, 1);
}

static void fmt_capacity(uint64_t sectors, char *out)
{
    uint64_t mb = sectors / 2048;
    char num[24];
    int n = 0;
    if (mb >= 1024) {
        uint64_t gb = mb / 1024;
        uint64_t tenth = (mb % 1024) * 10 / 1024;
        u2s(gb, num);
        app(out, &n, num);
        app(out, &n, ".");
        num[0] = (char)('0' + (int)(tenth % 10));
        num[1] = 0;
        app(out, &n, num);
        app(out, &n, " GB");
    } else {
        u2s(mb, num);
        app(out, &n, num);
        app(out, &n, " MB");
    }
    out[n] = 0;
}

/* page 2: region & time zone */
static void tz_label(int hour, char *out)
{
    const char *d = "0123456789";
    int p = 0;
    out[p++] = 'U'; out[p++] = 'T'; out[p++] = 'C';
    int ah = hour;
    if (hour < 0) { out[p++] = '-'; ah = -hour; }
    else          { out[p++] = '+'; }
    if (ah < 10) { out[p++] = '0'; out[p++] = d[ah]; }
    else { out[p++] = d[ah / 10]; out[p++] = d[ah % 10]; }
    out[p] = 0;
}

static const char *tz_city(int hour)
{
    switch (hour) {
    case -12: return "Baker Island";
    case -10: return "Honolulu";
    case -8:  return "Los Angeles";
    case -5:  return "New York";
    case -3:  return "Sao Paulo";
    case 0:   return "London";
    case 1:   return "Berlin, Paris";
    case 3:   return "Moscow";
    case 5:   return "Karachi";
    case 6:   return "Dhaka";
    case 7:   return "Bangkok, Jakarta";
    case 8:   return "Beijing, Singapore";
    case 9:   return "Tokyo, Seoul";
    case 10:  return "Sydney";
    case 12:  return "Auckland";
    default:  return "";
    }
}

static void render_region(void)
{
    int H = (int)fb_height();
    int ui = (H >= 1800) ? 3 : (H >= 1000) ? 2 : 1;
    int cx = card_x + card_w / 2;
    int title_sc = ui >= 3 ? 4 : 3;
    int sub_sc = ui >= 3 ? 3 : 2;
    text_c(cx, card_y + 26, title_sc, COL_TEXT, COL_CARD, "Region & Time Zone");
    text_c(cx, card_y + 80, sub_sc, COL_ACCENT_HI, COL_CARD,
           "Choose your time zone, then choose Next.");

    int box_x = card_x + PAD;
    int box_w = card_w - 2 * PAD;
    int box_y = card_y + 120;
    int box_h = card_h - 250;
    if (box_h < 200)
        box_h = 200;
    round_panel(box_x, box_y, box_w, box_h, 12, COL_LIST_BG, COL_CARD_EDGE);

    int bw = (box_w - 3 * 26) / 2;
    int bh = 24 + 4 * ui;
    int step = (box_h - 24 - bh) / 12;
    if (step < bh + 2)
        step = bh + 2;
    int maxstep = bh + 8 + 4 * ui;
    if (step > maxstep)
        step = maxstep;
    int gx0 = box_x + 26;
    int gy0 = box_y + 12;
    for (int i = 0; i < 25; i++) {
        int col = i / 13, row = i % 13;
        int x = gx0 + col * (bw + 26);
        int y = gy0 + row * step;
        char lab[20];
        tz_label(i - 12, lab);
        int hour = i - 12;
        const char *ct = tz_city(hour);
        if (ct[0]) {
            char full[40];
            int p = 0;
            for (int k = 0; lab[k] && p < 12; k++) full[p++] = lab[k];
            full[p++] = ' ';
            for (int k = 0; ct[k] && p < 36; k++) full[p++] = ct[k];
            full[p] = 0;
            draw_button(x, y, bw, bh, full, 0, 0, 0, i == g_tz_sel);
        } else {
            draw_button(x, y, bw, bh, lab, 0, 0, 0, i == g_tz_sel);
        }
    }

    text_c(cx, card_y + card_h - 132, 1, COL_DIM, COL_CARD,
           "Up/Down to move - Enter to confirm");
    draw_button(back_x, back_y, btn_w, btn_h, "Back", 0, 0, 0, 0);
    draw_button(primary_x, primary_y, btn_w, btn_h, "Next", 0, 0, 0, 1);
}

/* page 3: disk & partitioning (where to install) */
static void render_disk(void)
{
    int cx = card_x + card_w / 2;
    int left = card_x + PAD;
    text_c(cx, card_y + 36, 3, COL_TEXT, COL_CARD, "Where to install");
    text_c(cx, card_y + 96, 2, COL_ACCENT_HI, COL_CARD,
           "Disk and partitioning");

    uint64_t total = g_di.total_sectors;
    if (g_manual_mb == 0) {
        struct part_layout def;
        part_plan(total, &def);
        g_manual_mb = def.e[0].num_sectors / 2048;
    }

    uint32_t req_mb = g_manual ? g_manual_mb : 0;
    struct part_layout plan;
    if (total > 0)
        part_plan_custom(total, req_mb, &plan);
    if (plan.count > 0)
        g_cur_sys_mb = plan.e[0].num_sectors / 2048;

    int box_x = card_x + PAD;
    int box_w = card_w - 2 * PAD;
    int box_y = card_y + 140;
    int box_h = card_h - 290;
    round_panel(box_x, box_y, box_w, box_h, 12, COL_LIST_BG, COL_CARD_EDGE);

    char line[160];
    int k = 0;
    char cap[32];
    fmt_capacity(total, cap);
    app(line, &k, "Disk: /dev/sda   Capacity: ");
    app(line, &k, cap);
    line[k] = 0;
    text_side(left, box_y + 20, 2, COL_TEXT, COL_LIST_BG, line);

    g_rad_x = left;
    g_rad_w = box_w - 48;
    g_rad_h = 40;
    g_auto_y = box_y + 60;
    g_manual_y = box_y + 108;
    draw_button(g_rad_x, g_auto_y, g_rad_w, g_rad_h,
                "Automatic (recommended): 75% system, 25% data",
                0, 0, 0, !g_manual);
    draw_button(g_rad_x, g_manual_y, g_rad_w, g_rad_h,
                "Manual: choose the system partition size",
                0, 0, 0, g_manual);

    int ty = g_manual_y + g_rad_h + 12;
    if (g_manual) {
        g_step_btn = 44;
        g_step_y = ty;
        g_minus_x = left + 20;
        g_plus_x = left + 20 + g_step_btn + 380;
        draw_button(g_minus_x, ty, g_step_btn, g_rad_h, "-", 0, 0, 0, 0);
        char sz[32];
        fmt_capacity(plan.e[0].num_sectors, sz);
        k = 0;
        app(line, &k, "System: ");
        app(line, &k, sz);
        app(line, &k, "   (the rest becomes the Data partition)");
        line[k] = 0;
        text_side(left + 20 + g_step_btn + 14, ty + 11, 2,
                  COL_TEXT, COL_LIST_BG, line);
        draw_button(g_plus_x, ty, g_step_btn, g_rad_h, "+", 0, 0, 0, 0);
        ty += g_rad_h + 14;
    }

    text_side(left, ty, 1, COL_ACCENT_HI, COL_LIST_BG, "Partition layout:");
    ty += 34;
    for (int i = 0; i < plan.count && i < 3; i++) {
        char sz[32];
        fmt_capacity(plan.e[i].num_sectors, sz);
        const char *role = (i == 0) ? "System  (boot + OS)"
                         : (i == 1) ? "Data    (files)"
                                    : "Partition";
        k = 0;
        app(line, &k, "  ");
        app(line, &k, role);
        app(line, &k, "   ");
        app(line, &k, part_type_name(plan.e[i].type));
        app(line, &k, "   ");
        app(line, &k, sz);
        line[k] = 0;
        text_side(left, ty, 2, COL_TEXT, COL_LIST_BG, line);
        ty += 36;
    }

    text_side(left, box_y + box_h - 30, 1, COL_ERR, COL_LIST_BG,
              "Warning: existing data on this disk will be erased.");

    draw_button(back_x, back_y, btn_w, btn_h, "Back", 0, 0, 0, 0);
    draw_button(primary_x, primary_y, btn_w, btn_h, "Install now", 0, 0,
                0, 1);
}

static void render_working(void)
{
    int cx = card_x + card_w / 2;
    text_c(cx, card_y + 60, 3, COL_TEXT, COL_CARD, tr(STR_INSTALLING_TITLE));
    text_c(cx, card_y + 140, 2, COL_DIM, COL_CARD, tr(STR_DO_NOT_POWER_OFF));
    draw_progress(g_pct);
}

static void render_done(void)
{
    int cx = card_x + card_w / 2;
    text_c(cx, card_y + 60, 3, COL_OK, COL_CARD, tr(STR_INSTALL_DONE));
    text_c(cx, card_y + 150, 2, COL_TEXT, COL_CARD, tr(STR_REMOVE_DISC));
    text_c(cx, card_y + 210, 1, COL_DIM, COL_CARD, tr(STR_FIRST_BOOT_NOTE));
    draw_button(primary_x, primary_y, btn_w, btn_h, tr(STR_REBOOT_NOW), 0, 0,
                0, 1);
}

static void render_error(void)
{
    int cx = card_x + card_w / 2;
    text_c(cx, card_y + 60, 3, COL_ERR, COL_CARD, tr(STR_INSTALL_FAILED));
    text_c(cx, card_y + 150, 2, COL_TEXT, COL_CARD, tr(STR_CHECK_LOG));
    draw_button(primary_x, primary_y, btn_w, btn_h, tr(STR_BACK), 0, 0, 0, 0);
}

static void render_full(void)
{
    cursor_begin_frame();
    paint_background();
    paint_card();
    if (page == PAGE_LANGUAGE)
        render_language();
    else if (page == PAGE_WELCOME)
        render_welcome();
    else if (page == PAGE_REGION)
        render_region();
    else if (page == PAGE_DISK)
        render_disk();
    else if (page == PAGE_WORKING)
        render_working();
    else if (page == PAGE_DONE)
        render_done();
    else
        render_error();
    fb_present_all();
}

static void progress_cb(const char *step, int pct)
{
    g_pct = pct;
    draw_progress(pct);
    fb_present_all();
    (void)step;
}

static void do_reboot(void)
{
    uint8_t s = 2;
    for (int i = 0; i < 100000; i++) {
        __asm__ volatile("inb $0x64,%0" : "=a"(s));
        if (!(s & 2))
            break;
    }
    __asm__ volatile("mov $0xfe,%%al; mov $0x64,%%dx; outb %%al,%%dx"
                     : : : "ax", "dx");
    __asm__ volatile("mov $1,%%al; mov $0x92,%%dx; outb %%al,%%dx"
                     : : : "ax", "dx");
    for (;;)
        __asm__ volatile("hlt");
}

static void apply_language(void)
{
    i18n_set_lang(g_sel);
    installer_set_language(g_sel);
    serial_puts("[INST] language selected idx=");
    serial_puthex((uint32_t)g_sel);
    serial_putc('\n');
}

void installer_gui_run(void)
{
    i18n_init();

    int W = (int)fb_width(), H = (int)fb_height();
    cursor_set_scale(H >= 1000 ? 2 : 1);
    cursor_init();
    if (svga_gpu_present())
        svga_cursor_show(1);
    card_w = W * 82 / 100;
    card_h = H * 88 / 100;
    if (card_w > W - 40)
        card_w = W - 40;
    if (card_h > H - 28)
        card_h = H - 28;
    card_x = (W - card_w) / 2;
    card_y = (H - card_h) / 2;
    btn_w = 248;
    btn_h = 58;
    primary_x = card_x + card_w - PAD - btn_w;
    primary_y = card_y + card_h - 84;
    back_x = card_x + PAD;
    back_y = primary_y;

    g_have_payload = installer_have_payload();
    installer_probe(&g_di);

    page = PAGE_LANGUAGE;
    g_sel = 0;
    g_scroll = 0;
    g_pct = 0;
    g_started = 0;
    g_tz_sel = 20;
    g_manual = 0;
    g_manual_mb = 0;
    ps2_mouse_flush();
    render_full();

    uint64_t arm_until = timer_get_ticks() + 300;

    for (;;) {
        cursor_begin_frame();
        int mx, my;
        ps2_mouse_xy(&mx, &my);
        int cx, cy;
        int clicked = ps2_mouse_click(&cx, &cy);
        if (clicked) {
            mx = cx;
            my = cy;
        }
        char kc;
        int enter = 0;
        int key_up = 0, key_down = 0;
        while (ps2_read_key(&kc)) {
            if (kc == '\r' || kc == '\n')
                enter = 1;
            else if (kc == 0x11)
                key_up = 1;
            else if (kc == 0x12)
                key_down = 1;
        }

        if (timer_get_ticks() < arm_until) {
            clicked = 0;
            enter = 0;
            key_up = key_down = 0;
        }

        if (page == PAGE_LANGUAGE) {
            int old_sel = g_sel;
            if (key_up)
                g_sel--;
            if (key_down)
                g_sel++;
            if (g_sel < 0)
                g_sel = 0;
            if (g_sel >= I18N_NLANG)
                g_sel = I18N_NLANG - 1;
            clamp_scroll();

            if (clicked && mx >= list_x && mx < list_x + list_w &&
                my >= list_top && my < list_top + vis_rows * ROW_H) {
                int idx = g_scroll + (my - list_top) / ROW_H;
                if (idx >= 0 && idx < I18N_NLANG) {
                    if (idx == g_sel) {
                        apply_language();
                        page = PAGE_WELCOME;
                        installer_probe(&g_di);
                        render_full();
                    } else {
                        g_sel = idx;
                    }
                }
                clicked = 0;
            }

            draw_button(primary_x, primary_y, btn_w, btn_h, "Next", mx, my,
                        clicked, 1);
            if (enter ||
                (clicked && hit(primary_x, primary_y, btn_w, btn_h, mx, my))) {
                apply_language();
                page = PAGE_WELCOME;
                installer_probe(&g_di);
                render_full();
            } else if (old_sel != g_sel || key_up || key_down) {
                render_full();
            }
        } else if (page == PAGE_WELCOME) {
            draw_button(back_x, back_y, btn_w, btn_h, tr(STR_BACK), mx, my,
                        clicked, 0);
            draw_button(primary_x, primary_y, btn_w, btn_h, "Continue",
                        mx, my, clicked, 1);
            if (clicked && hit(back_x, back_y, btn_w, btn_h, mx, my)) {
                page = PAGE_LANGUAGE;
                render_full();
            }
            if (enter ||
                (clicked && hit(primary_x, primary_y, btn_w, btn_h, mx, my))) {
                if (!g_have_payload || g_di.state == INST_STATE_NO_DISK) {
                    page = PAGE_ERROR;
                    render_full();
                } else {
                    installer_probe(&g_di);
                    page = PAGE_REGION;
                    render_full();
                }
                enter = 0;
                clicked = 0;
            }
        } else if (page == PAGE_REGION) {
            int old_tz = g_tz_sel;
            if (key_up)
                g_tz_sel--;
            if (key_down)
                g_tz_sel++;
            if (g_tz_sel < 0)
                g_tz_sel = 0;
            if (g_tz_sel > 24)
                g_tz_sel = 24;

            int box_x = card_x + PAD;
            int box_w = card_w - 2 * PAD;
            int H = (int)fb_height();
            int ui = (H >= 1800) ? 3 : (H >= 1000) ? 2 : 1;
            int box_y = card_y + 120;
            int box_h = card_h - 250;
            if (box_h < 200)
                box_h = 200;
            int bw = (box_w - 3 * 26) / 2;
            int bh = 24 + 4 * ui;
            int step = (box_h - 24 - bh) / 12;
            if (step < bh + 2)
                step = bh + 2;
            int maxstep = bh + 8 + 4 * ui;
            if (step > maxstep)
                step = maxstep;
            int gx0 = box_x + 26;
            int gy0 = box_y + 12;
            for (int i = 0; i < 25; i++) {
                int col = i / 13, row = i % 13;
                int x = gx0 + col * (bw + 26);
                int y = gy0 + row * step;
                if (clicked && hit(x, y, bw, bh, mx, my)) {
                    g_tz_sel = i;
                    clicked = 0;
                }
            }
            draw_button(back_x, back_y, btn_w, btn_h, "Back", mx, my,
                        clicked, 0);
            draw_button(primary_x, primary_y, btn_w, btn_h, "Next", mx, my,
                        clicked, 1);
            if (clicked && hit(back_x, back_y, btn_w, btn_h, mx, my)) {
                page = PAGE_WELCOME;
                render_full();
            }
            if (enter ||
                (clicked && hit(primary_x, primary_y, btn_w, btn_h, mx, my))) {
                installer_set_tz(g_tz_sel);
                page = PAGE_DISK;
                render_full();
                enter = 0;
                clicked = 0;
            } else if (old_tz != g_tz_sel) {
                render_full();
            }
        } else if (page == PAGE_DISK) {
            if (clicked &&
                hit(g_rad_x, g_auto_y, g_rad_w, g_rad_h, mx, my) && g_manual) {
                g_manual = 0;
                render_full();
                clicked = 0;
            }
            if (clicked &&
                hit(g_rad_x, g_manual_y, g_rad_w, g_rad_h, mx, my) &&
                !g_manual) {
                g_manual = 1;
                render_full();
                clicked = 0;
            }
            if (g_manual) {
                uint32_t total_mb = (uint32_t)(g_di.total_sectors / 2048);
                uint32_t step = total_mb >= 1024 ? 1024 : 128;
                uint32_t lo = part_min_sys_mb();
                uint32_t hi = total_mb > 8 ? total_mb - 8 : total_mb;
                if (clicked &&
                    hit(g_minus_x, g_step_y, g_step_btn, g_rad_h, mx, my)) {
                    if (g_manual_mb > lo + step)
                        g_manual_mb -= step;
                    else
                        g_manual_mb = lo;
                    render_full();
                    clicked = 0;
                }
                if (clicked &&
                    hit(g_plus_x, g_step_y, g_step_btn, g_rad_h, mx, my)) {
                    if (g_manual_mb + step <= hi)
                        g_manual_mb += step;
                    else
                        g_manual_mb = hi;
                    render_full();
                    clicked = 0;
                }
            }
            draw_button(back_x, back_y, btn_w, btn_h, tr(STR_BACK), mx, my,
                        clicked, 0);
            draw_button(primary_x, primary_y, btn_w, btn_h,
                        g_di.state == INST_STATE_INSTALLED
                            ? tr(STR_UPGRADE_REINSTALL)
                            : "Install now",
                        mx, my, clicked, 1);
            if (clicked && hit(back_x, back_y, btn_w, btn_h, mx, my)) {
                page = PAGE_REGION;
                render_full();
            }
            if (enter ||
                (clicked && hit(primary_x, primary_y, btn_w, btn_h, mx, my))) {
                installer_set_tz(g_tz_sel);
                installer_set_sys_mb(g_manual ? g_manual_mb : 0);
                g_mode = (g_di.state == INST_STATE_INSTALLED)
                             ? INST_MODE_UPGRADE
                             : INST_MODE_FRESH;
                page = PAGE_WORKING;
                g_pct = 0;
                render_full();
                serial_puts("[INST] GUI starting install\n");
                int rc = installer_install(g_mode, progress_cb);
                page = rc == 0 ? PAGE_DONE : PAGE_ERROR;
                render_full();
                enter = 0;
                clicked = 0;
            }
        } else if (page == PAGE_DONE) {
            draw_button(primary_x, primary_y, btn_w, btn_h, tr(STR_REBOOT_NOW),
                        mx, my, clicked, 1);
            if (enter ||
                (clicked && hit(primary_x, primary_y, btn_w, btn_h, mx, my)))
                do_reboot();
        } else if (page == PAGE_ERROR) {
            draw_button(primary_x, primary_y, btn_w, btn_h, tr(STR_BACK), mx,
                        my, clicked, 0);
            if (clicked && hit(primary_x, primary_y, btn_w, btn_h, mx, my)) {
                installer_probe(&g_di);
                page = PAGE_WELCOME;
                render_full();
            }
        }

        cursor_end_frame(mx, my);
        fb_present_all();
        __asm__ volatile("hlt");
    }
}
