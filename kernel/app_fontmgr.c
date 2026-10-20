#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ui.h>
#include <kernel/config.h>

static struct ui_win W;
static char msg[80];
static int inited;

void fontmgr_open(void)
{
    ui_fit(620, 420, &W);
    msg[0] = 0;
    inited = 1;
}

int fontmgr_frame(int mx, int my, int clicked)
{
    if (!inited)
        fontmgr_open();
    int S = ui_scale();
    if (ui_window(&W, "Font Manager", mx, my, clicked))
        return 1;

    int n = gfx_font_count();
    int cur = gfx_get_font();
    int rh = 70 * S;
    for (int i = 0; i < n; i++) {
        int x = W.x + 26 * S;
        int y = W.y + 44 * S + i * (rh + 10 * S);
        int bw = W.w - 52 * S;
        uint32_t bg = i == cur ? fb_rgb(50, 80, 60) : fb_rgb(38, 44, 60);
        ui_rect(x, y, bw, rh, bg);
        ui_border(x, y, bw, rh, fb_rgb(110, 130, 170));
        gfx_draw_string(x + 12 * S, y + 6 * S,
                        fb_rgb(150, 210, 255), bg, gfx_font_name(i));
        int old = gfx_get_font();
        gfx_set_font(i);
        gfx_draw_string(x + 12 * S, y + 30 * S,
                        fb_rgb(230, 235, 245), bg, "Welcome 0123");
        gfx_set_font(old);
        if (clicked && ui_inside(mx, my, x, y, bw, rh)) {
            gfx_set_font(i);
            config_set_font(i);
            config_save();
            const char *pre = "Selected font ";
            int p = 0;
            while (pre[p] && p < 55) {
                msg[p] = pre[p];
                p++;
            }
            const char *nm = gfx_font_name(i);
            while (*nm && p < 70)
                msg[p++] = *nm++;
            msg[p] = 0;
        }
    }
    if (msg[0])
        gfx_draw_string(W.x + 26 * S, W.y + W.h - 38 * S,
                        fb_rgb(150, 220, 160), fb_rgb(30, 34, 46), msg);
    return 0;
}
