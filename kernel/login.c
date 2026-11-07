#include <stdint.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/users.h>
#include <kernel/ps2.h>
#include "i18n_data.h"

/* Lock screen: shown only when the current account actually has a password.
   Accounts without a password boot straight to the desktop (password is
   optional, by design). */
void login_run(void)
{
    if (!users_has_pass(users_current()))
        return;

    int W = (int)fb_width(), H = (int)fb_height();
    char pass[33];
    int plen = 0, bad = 0;
    char c;
    const char *uname = users_name(users_current());

    for (;;) {
        fb_fill_rect(0, 0, W, H, fb_rgb(12, 22, 40));
        int pw = 360, ph = 230;
        int px = (W - pw) / 2, py = (H - ph) / 2;
        fb_fill_rect(px, py, pw, ph, fb_rgb(22, 30, 48));
        fb_fill_rect(px, py, pw, 3, fb_rgb(70, 140, 230));
        gfx_draw_string(px + 24, py + 22, fb_rgb(160, 210, 255),
                        fb_rgb(22, 30, 48), "JackBOT OS");
        gfx_draw_string(px + 24, py + 58, fb_rgb(235, 240, 250),
                        fb_rgb(22, 30, 48), uname);
        gfx_draw_string(px + 24, py + 96, fb_rgb(150, 170, 200),
                        fb_rgb(22, 30, 48), "Password");

        char shown[34];
        int n = plen > 30 ? 30 : plen;
        for (int i = 0; i < n; i++)
            shown[i] = '*';
        shown[n] = 0;
        fb_fill_rect(px + 24, py + 118, pw - 48, 30, fb_rgb(14, 20, 32));
        gfx_draw_string(px + 32, py + 124, fb_rgb(225, 235, 250),
                        fb_rgb(14, 20, 32), shown);
        if (bad)
            gfx_draw_string(px + 24, py + 160, fb_rgb(240, 120, 120),
                            fb_rgb(22, 30, 48), "Try again");
        gfx_draw_string(px + 24, py + 196, fb_rgb(140, 160, 190),
                        fb_rgb(22, 30, 48), "Enter = sign in");

        while (ps2_read_key(&c)) {
            if (c == '\r' || c == '\n') {
                if (users_auth(users_current(), pass))
                    return;
                plen = 0;
                bad = 1;
            } else if (c == '\b' || c == 0x7f) {
                if (plen > 0)
                    pass[--plen] = 0;
                bad = 0;
            } else if (c >= 32 && c < 127 && plen < 32) {
                pass[plen++] = c;
                pass[plen] = 0;
                bad = 0;
            }
        }
    }
}
