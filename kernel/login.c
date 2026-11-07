#include <stdint.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/users.h>
#include <kernel/ps2.h>
#include <kernel/cursor.h>
#include <kernel/i18n.h>
#include <kernel/serial.h>
#include "i18n_data.h"

/* Boot-time account gate.
   - No account at all: return immediately (the machine stays usable).
   - Accounts exist: show the picker, let the user choose. Ticking the
     "always sign in as this user" box stores that choice, so later boots
     go straight in.
   - The chosen account's password is only asked when it actually has one;
     a password is optional by design. */

static void put(uint32_t x, int y, uint32_t fg, uint32_t bg, const char *s)
{
    gfx_draw_string((int)x, y, fg, bg, s);
}

static int inside(int mx, int my, int x, int y, int w, int h)
{
    return mx >= x && mx < x + w && my >= y && my < y + h;
}

static void password_screen(int who)
{
    int W = (int)fb_width(), H = (int)fb_height();
    char pass[33];
    int plen = 0, bad = 0, mx = 0, my = 0;
    const char *uname = users_name(who);
    for (;;) {
        int cx, cy, ck;
        ps2_mouse_xy(&mx, &my);
        ck = ps2_mouse_click(&cx, &cy);
        if (ck) { mx = cx; my = cy; }

        cursor_begin_frame();
        fb_invalidate();
        fb_fill_rect(0, 0, W, H, fb_rgb(12, 22, 40));
        int pw = 380, ph = 250;
        int px = (W - pw) / 2, py = (H - ph) / 2;
        fb_fill_rect(px, py, pw, ph, fb_rgb(22, 30, 48));
        fb_fill_rect(px, py, pw, 3, fb_rgb(70, 140, 230));
        put(px + 24, py + 24, fb_rgb(160, 210, 255), fb_rgb(22, 30, 48),
            "JackBOT OS");
        put(px + 24, py + 60, fb_rgb(235, 240, 250), fb_rgb(22, 30, 48), uname);
        put(px + 24, py + 98, fb_rgb(150, 170, 200), fb_rgb(22, 30, 48),
            tr(STR_PASSWORD));

        char shown[34];
        int n = plen > 30 ? 30 : plen;
        for (int i = 0; i < n; i++)
            shown[i] = '*';
        shown[n] = 0;
        fb_fill_rect(px + 24, py + 120, pw - 48, 30, fb_rgb(14, 20, 32));
        put(px + 32, py + 126, fb_rgb(225, 235, 250), fb_rgb(14, 20, 32), shown);
        if (bad)
            put(px + 24, py + 160, fb_rgb(240, 120, 120), fb_rgb(22, 30, 48),
                tr(STR_TRY_AGAIN));
        put(px + 24, py + 200, fb_rgb(140, 160, 190), fb_rgb(22, 30, 48),
            tr(STR_PRESS_ENTER));
        cursor_end_frame(mx, my);
        fb_present_dirty();

        char k;
        while (ps2_read_key(&k)) {
            if (k == '\r' || k == '\n') {
                if (users_auth(who, pass))
                    return;
                plen = 0;
                bad = 1;
            } else if (k == '\b' || k == 0x7f) {
                if (plen > 0)
                    pass[--plen] = 0;
                bad = 0;
            } else if (k >= 32 && k < 127 && plen < 32) {
                pass[plen++] = k;
                pass[plen] = 0;
                bad = 0;
            }
        }
    }
}

void login_run(void)
{
    int n = users_count();
    if (n <= 0)
        return;

    int auto_u = users_auto();
    if (auto_u >= 0) {
        /* Remembered account: skip the list, still honour its password. */
        if (users_has_pass(auto_u))
            password_screen(auto_u);
        return;
    }

    int W = (int)fb_width(), H = (int)fb_height();
    int sel = users_current();
    if (sel < 0 || sel >= n)
        sel = 0;
    int remember = 0;
    int mx = W / 2, my = H / 2;

    int row_h = 54, cw = 460;
    int rows_h = n * (row_h + 8);
    int ch = 96 + rows_h + 84;
    int card_x = (W - cw) / 2;
    int card_y = (H - ch) / 2 - 30;
    int list_x = card_x + 24, list_y = card_y + 88, list_w = cw - 48;

    int ck_x = card_x + 24, ck_y = list_y + rows_h + 12;
    int bt_w = 130, bt_h = 32;
    int bt_x = card_x + cw - bt_w - 24, bt_y = ck_y + 4;

    for (;;) {
        int cx2, cy2, ck;
        ps2_mouse_xy(&mx, &my);
        ck = ps2_mouse_click(&cx2, &cy2);
        if (ck) { mx = cx2; my = cy2; }

        cursor_begin_frame();
        fb_invalidate();
        fb_fill_rect(0, 0, W, H, fb_rgb(10, 16, 30));
        fb_fill_rect(0, 0, W, H / 3, fb_rgb(15, 26, 48));
        fb_fill_rect(card_x, card_y, cw, ch, fb_rgb(22, 30, 48));
        fb_fill_rect(card_x, card_y, cw, 3, fb_rgb(70, 140, 230));
        put(card_x + 24, card_y + 22, fb_rgb(160, 210, 255), fb_rgb(22, 30, 48),
            "JackBOT OS");
        put(card_x + 24, card_y + 50, fb_rgb(210, 220, 240), fb_rgb(22, 30, 48),
            tr(STR_SELECT_USER));

        for (int i = 0; i < n; i++) {
            int ry = list_y + i * (row_h + 8);
            int on = (i == sel);
            fb_fill_rect(list_x, ry, list_w, row_h,
                         on ? fb_rgb(38, 78, 138) : fb_rgb(28, 36, 54));
            /* account chip */
            fb_fill_rect(list_x + 10, ry + 10, row_h - 20, row_h - 20,
                         fb_rgb(64, 96, 150));
            const char *nm = users_name(i);
            char ini[2];
            ini[0] = nm[0];
            ini[1] = 0;
            put(list_x + 22, ry + (row_h - 16) / 2,
                fb_rgb(235, 245, 255), fb_rgb(64, 96, 150), ini);
            int tx = list_x + row_h + 6;
            put(tx, ry + 10, fb_rgb(235, 242, 255),
                on ? fb_rgb(38, 78, 138) : fb_rgb(28, 36, 54), nm);
            if (users_has_pass(i))
                put(tx, ry + 30, fb_rgb(160, 190, 225),
                    on ? fb_rgb(38, 78, 138) : fb_rgb(28, 36, 54),
                    tr(STR_PASSWORD));
        }

        /* always-sign-in checkbox */
        fb_fill_rect(ck_x, ck_y + 4, 16, 16, fb_rgb(14, 20, 32));
        if (remember)
            fb_fill_rect(ck_x + 3, ck_y + 7, 10, 10, fb_rgb(90, 200, 140));
        put(ck_x + 26, ck_y + 4, fb_rgb(200, 212, 232), fb_rgb(22, 30, 48),
            tr(STR_ALWAYS_SIGN_IN));

        fb_fill_rect(bt_x, bt_y, bt_w, bt_h, fb_rgb(52, 110, 190));
        put(bt_x + 24, bt_y + 8, fb_rgb(240, 248, 255), fb_rgb(52, 110, 190),
            tr(STR_SIGN_IN));
        cursor_end_frame(mx, my);
        fb_present_dirty();

        int act = -1, sign = 0;
        if (ck) {
            for (int i = 0; i < n; i++) {
                int ry = list_y + i * (row_h + 8);
                if (inside(mx, my, list_x, ry, list_w, row_h)) {
                    sel = i;
                    sign = 1;
                }
            }
            if (inside(mx, my, ck_x - 4, ck_y, 260, 24))
                remember ^= 1;
            if (inside(mx, my, bt_x, bt_y, bt_w, bt_h))
                sign = 1;
        }

        char k;
        while (ps2_read_key(&k)) {
            if (k == JB_KEY_DOWN && sel + 1 < n)
                sel++;
            else if (k == JB_KEY_UP && sel > 0)
                sel--;
            else if (k == ' ')
                remember ^= 1;
            else if (k == '\r' || k == '\n')
                sign = 1;
            else if (k == 27)
                act = sel;
        }
        if (act >= 0)
            sel = act;

        if (sign) {
            users_select(sel, remember);
            if (users_has_pass(sel))
                password_screen(sel);
            return;
        }
    }
}
