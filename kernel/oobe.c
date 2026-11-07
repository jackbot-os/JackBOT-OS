#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ps2.h>
#include <kernel/cursor.h>
#include <kernel/svga.h>
#include <kernel/i18n.h>
#include <kernel/config.h>
#include <kernel/serial.h>

uint64_t timer_get_ticks(void);

#define P_LANG 0
#define P_KB   1
#define P_TZ   2
#define P_DONE 3

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
#define COL_SEL 0x1c3a63
#define COL_LIST_BG 0x0d1320

#define ROW_H 40
#define PAD 52
#define CARD_R 18

#define NK 12

static int page;
static int card_x, card_y, card_w, card_h;
static int primary_x, primary_y, btn_w, btn_h;
static int back_x, back_y;
static int sel_lang, sel_kb, sel_tz;
static int list_top, list_bottom, list_x, list_w, vis_rows;
static int scroll_lang, scroll_kb;

static const char *kb_names[NK] = {
    "US English (QWERTY)",
    "UK English",
    "German (Deutsch)",
    "French (Francais)",
    "Spanish (Espanol)",
    "Italian (Italiano)",
    "Portuguese (Portugues)",
    "Russian (Russkij)",
    "Japanese",
    "Korean",
    "Chinese",
    "Latin American Spanish"
};

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

static void round_panel(int x, int y, int w, int h, int r,
                        uint32_t fill, uint32_t edge)
{
    round_fill(x, y, w, h, r, edge);
    round_fill(x + 1, y + 1, w - 2, h - 2, r - 1, fill);
}

static int hit(int bx, int by, int bw, int bh, int mx, int my)
{
    return mx >= bx && mx < bx + bw && my >= by && my < by + bh;
}

static void text_c(int cx, int y, int sc, uint32_t fg, uint32_t bg,
                   const char *s)
{
    gfx_set_scale(sc);
    int w = gfx_text_width_wide(s);
    gfx_draw_string_wide(cx - w / 2, y, fg, bg, s);
    gfx_set_scale(1);
}

static void draw_button(int bx, int by, int bw, int bh, const char *label,
                        int mx, int my, int clicked, int primary)
{
    int hover = hit(bx, by, bw, bh, mx, my);
    uint32_t base = primary ? (hover ? COL_ACCENT_HI : COL_ACCENT)
                            : (hover ? COL_BTN_HI : COL_BTN);
    if (clicked && hover)
        base = primary ? COL_ACCENT : COL_BTN;
    round_panel(bx, by, bw, bh, 12, base, hover ? COL_TEXT : COL_CARD_EDGE);
    gfx_set_scale(2);
    int tw = gfx_text_width_wide(label);
    int th = gfx_text_height();
    gfx_draw_string_wide(bx + bw / 2 - tw / 2, by + (bh - th) / 2 + 2,
                         COL_TEXT, base, label);
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
    fb_fill_rect(0, 0, W, 3, fb_rgb(59, 130, 246));
}

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

static const char *kb_namefn(int i);

/* generic single-column picker list; namefn(i) returns row label */
static void render_list(int count, int sel, int scroll,
                        const char *(*namefn)(int), int is_lang,
                        int *out_scroll)
{
    list_top = card_y + 150;
    list_bottom = card_y + card_h - 160;
    list_x = card_x + 40;
    list_w = card_w - 80;
    vis_rows = (list_bottom - list_top) / ROW_H;
    int max_scroll = count - vis_rows;
    if (max_scroll < 0)
        max_scroll = 0;
    if (scroll < 0)
        scroll = 0;
    if (scroll > max_scroll)
        scroll = max_scroll;
    if (sel < scroll)
        scroll = sel;
    if (sel >= scroll + vis_rows)
        scroll = sel - vis_rows + 1;
    *out_scroll = scroll;

    round_panel(list_x, list_top, list_w, vis_rows * ROW_H, 12,
                COL_LIST_BG, COL_CARD_EDGE);

    for (int r = 0; r < vis_rows; r++) {
        int idx = scroll + r;
        if (idx >= count)
            break;
        int ry = list_top + r * ROW_H;
        int chosen = (idx == sel);
        if (chosen)
            round_panel(list_x + 4, ry + 2, list_w - 8, ROW_H - 4, 8,
                        COL_SEL, COL_ACCENT);
        const char *nm = namefn(idx);
        gfx_set_scale(2);
        int yoff = (ROW_H - gfx_text_height()) / 2;
        uint32_t fg = chosen ? COL_TEXT : COL_DIM;
        int rtl = is_lang && i18n_rtl_idx(idx);
        if (rtl)
            gfx_draw_string_rtl(list_x + list_w - 22, ry + yoff, fg,
                                chosen ? COL_SEL : COL_LIST_BG, nm);
        else
            gfx_draw_string_wide(list_x + 22, ry + yoff, fg,
                                 chosen ? COL_SEL : COL_LIST_BG, nm);
        gfx_set_scale(1);
    }

    if (count > vis_rows) {
        int track_h = vis_rows * ROW_H;
        int bar_h = track_h * vis_rows / count;
        if (bar_h < 24)
            bar_h = 24;
        int maxs = count - vis_rows;
        int bar_y = list_top + (track_h - bar_h) * scroll /
                    (maxs ? maxs : 1);
        fb_fill_rect(list_x + list_w - 6, list_top, 4, track_h, COL_TRACK);
        fb_fill_rect(list_x + list_w - 6, bar_y, 4, bar_h, COL_ACCENT);
    }
}

static void render_tz(int mx, int my, int clicked)
{
    int H = (int)fb_height();
    int ui = (H >= 1800) ? 3 : (H >= 1000) ? 2 : 1;
    int cx = card_x + card_w / 2;
    int title_sc = ui >= 3 ? 4 : 3;
    int sub_sc = ui >= 3 ? 3 : 2;
    text_c(cx, card_y + 26, title_sc, COL_TEXT, COL_CARD, "Time Zone & Region");
    text_c(cx, card_y + 80, sub_sc, COL_ACCENT_HI, COL_CARD,
           "Choose your time zone.");

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
        const char *ct = tz_city(i - 12);
        if (ct[0]) {
            char full[40];
            int p = 0;
            for (int k = 0; lab[k] && p < 12; k++) full[p++] = lab[k];
            full[p++] = ' ';
            for (int k = 0; ct[k] && p < 36; k++) full[p++] = ct[k];
            full[p] = 0;
            draw_button(x, y, bw, bh, full, mx, my, clicked, i == sel_tz);
        } else {
            draw_button(x, y, bw, bh, lab, mx, my, clicked, i == sel_tz);
        }
    }
    text_c(cx, card_y + card_h - 132, 1, COL_DIM, COL_CARD,
           "Up/Down to move - Enter to confirm");
}

static void render_full(int mx, int my, int clicked)
{
    cursor_begin_frame();
    paint_background();
    round_panel(card_x, card_y, card_w, card_h, CARD_R, COL_CARD,
                COL_CARD_EDGE);
    int cx = card_x + card_w / 2;

    if (page == P_LANG) {
        text_c(cx, card_y + 34, 3, COL_TEXT, COL_CARD, "Select Language");
        text_c(cx, card_y + 92, 2, COL_ACCENT_HI, COL_CARD,
               "Pick your display language.");
        render_list(i18n_count(), sel_lang, scroll_lang, i18n_name, 1,
                    &scroll_lang);
        text_c(cx, card_y + card_h - 132, 1, COL_DIM, COL_CARD,
               "Up/Down to move - Enter to confirm");
        draw_button(primary_x, primary_y, btn_w, btn_h, "Next", mx, my,
                    clicked, 1);
    } else if (page == P_KB) {
        text_c(cx, card_y + 34, 3, COL_TEXT, COL_CARD, "Keyboard Layout");
        text_c(cx, card_y + 92, 2, COL_ACCENT_HI, COL_CARD,
               "Choose your keyboard layout.");
        render_list(NK, sel_kb, scroll_kb, kb_namefn, 0, &scroll_kb);
        text_c(cx, card_y + card_h - 132, 1, COL_DIM, COL_CARD,
               "Up/Down to move - Enter to confirm");
        draw_button(back_x, back_y, btn_w, btn_h, "Back", mx, my, clicked, 0);
        draw_button(primary_x, primary_y, btn_w, btn_h, "Next", mx, my,
                    clicked, 1);
    } else if (page == P_TZ) {
        render_tz(mx, my, clicked);
        draw_button(back_x, back_y, btn_w, btn_h, "Back", mx, my, clicked, 0);
        draw_button(primary_x, primary_y, btn_w, btn_h, "Next", mx, my,
                    clicked, 1);
    } else {
        text_c(cx, card_y + 120, 4, COL_OK, COL_CARD, "Welcome");
        text_c(cx, card_y + 200, 3, COL_TEXT, COL_CARD,
               "Your JackBOT OS is ready.");
        text_c(cx, card_y + 270, 2, COL_DIM, COL_CARD,
               "Language, keyboard and time zone have been saved.");
        draw_button(primary_x, primary_y, btn_w, btn_h,
                    "Start using JackBOT OS", mx, my, clicked, 1);
        draw_button(back_x, back_y, btn_w, btn_h, "Back", mx, my, clicked, 0);
    }
    fb_present_all();
}

/* keyboard page name accessor (kept as real function to avoid a cast) */
static const char *kb_namefn(int i)
{
    if (i < 0 || i >= NK)
        return "";
    return kb_names[i];
}

static void tz_geometry(int idx, int *x, int *y, int *bw, int *bh)
{
    int H = (int)fb_height();
    int ui = (H >= 1800) ? 3 : (H >= 1000) ? 2 : 1;
    int box_x = card_x + PAD;
    int box_w = card_w - 2 * PAD;
    int box_y = card_y + 120;
    int box_h = card_h - 250;
    if (box_h < 200)
        box_h = 200;
    int col = idx / 13, row = idx % 13;
    *bw = (box_w - 3 * 26) / 2;
    *bh = 24 + 4 * ui;
    int step = (box_h - 24 - *bh) / 12;
    if (step < *bh + 2)
        step = *bh + 2;
    int maxstep = *bh + 8 + 4 * ui;
    if (step > maxstep)
        step = maxstep;
    *x = box_x + 26 + col * (*bw + 26);
    *y = box_y + 12 + row * step;
}

void oobe_run(void)
{
    gfx_set_font(0);
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

    sel_lang = config_language();
    sel_kb = config_keyboard();
    if (sel_kb < 0 || sel_kb >= NK)
        sel_kb = 0;
    sel_tz = config_tz();
    if (sel_tz < 0 || sel_tz > 24)
        sel_tz = 20;
    scroll_lang = 0;
    scroll_kb = 0;
    page = P_LANG;

    ps2_mouse_flush();
    render_full(0, 0, 0);
    uint64_t arm_until = timer_get_ticks() + 300;

    for (;;) {
        cursor_begin_frame();
        int mx, my;
        ps2_mouse_xy(&mx, &my);
        int cx, cy;
        int clicked = ps2_mouse_click(&cx, &cy);
        if (clicked) { mx = cx; my = cy; }
        char kc;
        int enter = 0, key_up = 0, key_down = 0;
        while (ps2_read_key(&kc)) {
            if (kc == '\r' || kc == '\n')
                enter = 1;
            else if (kc == 0x11)
                key_up = 1;
            else if (kc == 0x12)
                key_down = 1;
        }
        if (timer_get_ticks() < arm_until) {
            clicked = 0; enter = 0; key_up = key_down = 0;
        }

        if (page == P_LANG) {
            int old = sel_lang;
            if (key_up) sel_lang--;
            if (key_down) sel_lang++;
            if (sel_lang < 0) sel_lang = 0;
            if (sel_lang >= i18n_count()) sel_lang = i18n_count() - 1;
            render_list(i18n_count(), sel_lang, scroll_lang, i18n_name, 1,
                        &scroll_lang);
            for (int r = 0; r < vis_rows; r++) {
                int idx = scroll_lang + r;
                if (idx >= i18n_count())
                    break;
                int ry = list_top + r * ROW_H;
                if (clicked &&
                    hit(list_x, ry, list_w, ROW_H, mx, my)) {
                    sel_lang = idx;
                    clicked = 0;
                }
            }
            draw_button(primary_x, primary_y, btn_w, btn_h, "Next", mx, my,
                        clicked, 1);
            if (clicked &&
                hit(primary_x, primary_y, btn_w, btn_h, mx, my))
                enter = 1;
            if (old != sel_lang) {
                i18n_set_lang(sel_lang);
                config_set_language(sel_lang);
                config_set_language_code(i18n_code(sel_lang));
            }
            if (enter) {
                i18n_set_lang(sel_lang);
                config_set_language(sel_lang);
                config_set_language_code(i18n_code(sel_lang));
                page = P_KB;
                render_full(0, 0, 0);
                enter = 0; clicked = 0;
            }
        } else if (page == P_KB) {
            if (key_up) sel_kb--;
            if (key_down) sel_kb++;
            if (sel_kb < 0) sel_kb = 0;
            if (sel_kb >= NK) sel_kb = NK - 1;
            render_list(NK, sel_kb, scroll_kb, kb_namefn, 0, &scroll_kb);
            for (int r = 0; r < vis_rows; r++) {
                int idx = scroll_kb + r;
                if (idx >= NK)
                    break;
                int ry = list_top + r * ROW_H;
                if (clicked && hit(list_x, ry, list_w, ROW_H, mx, my)) {
                    sel_kb = idx;
                    clicked = 0;
                }
            }
            draw_button(back_x, back_y, btn_w, btn_h, "Back", mx, my,
                        clicked, 0);
            draw_button(primary_x, primary_y, btn_w, btn_h, "Next", mx, my,
                        clicked, 1);
            if (clicked && hit(back_x, back_y, btn_w, btn_h, mx, my)) {
                page = P_LANG;
                render_full(0, 0, 0);
                clicked = 0;
            }
            if (enter ||
                (clicked &&
                 hit(primary_x, primary_y, btn_w, btn_h, mx, my))) {
                config_set_keyboard(sel_kb);
                page = P_TZ;
                render_full(0, 0, 0);
                enter = 0; clicked = 0;
            }
        } else if (page == P_TZ) {
            int old = sel_tz;
            if (key_up) sel_tz--;
            if (key_down) sel_tz++;
            if (sel_tz < 0) sel_tz = 0;
            if (sel_tz > 24) sel_tz = 24;
            for (int i = 0; i < 25; i++) {
                int x, y, bw, bh;
                tz_geometry(i, &x, &y, &bw, &bh);
                if (clicked && hit(x, y, bw, bh, mx, my)) {
                    sel_tz = i;
                    clicked = 0;
                }
            }
            render_tz(mx, my, clicked);
            draw_button(back_x, back_y, btn_w, btn_h, "Back", mx, my,
                        clicked, 0);
            draw_button(primary_x, primary_y, btn_w, btn_h, "Next", mx, my,
                        clicked, 1);
            if (clicked && hit(back_x, back_y, btn_w, btn_h, mx, my)) {
                page = P_KB;
                render_full(0, 0, 0);
                clicked = 0;
            }
            if (enter ||
                (clicked &&
                 hit(primary_x, primary_y, btn_w, btn_h, mx, my))) {
                config_set_tz(sel_tz);
                page = P_DONE;
                render_full(0, 0, 0);
                enter = 0; clicked = 0;
            }
            if (old != sel_tz)
                render_full(0, 0, 0);
        } else {
            draw_button(back_x, back_y, btn_w, btn_h, "Back", mx, my,
                        clicked, 0);
            draw_button(primary_x, primary_y, btn_w, btn_h,
                        "Start using JackBOT OS", mx, my, clicked, 1);
            if (clicked && hit(back_x, back_y, btn_w, btn_h, mx, my)) {
                page = P_TZ;
                render_full(0, 0, 0);
                clicked = 0;
            }
            if (enter ||
                (clicked &&
                 hit(primary_x, primary_y, btn_w, btn_h, mx, my))) {
                config_set_language(sel_lang);
                config_set_language_code(i18n_code(sel_lang));
                config_set_keyboard(sel_kb);
                config_set_tz(sel_tz);
                config_set_oobe_done(1);
                config_save();
                i18n_set_lang(sel_lang);
                serial_puts("[OOBE] setup complete\n");
                break;
            }
        }

        cursor_end_frame(mx, my);
        fb_present_all();
    }
}
