#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ui.h>

static struct ui_win W;
static char msg[80];
static int inited;

static const char *installed[6] = {
    "Wallpaper", "Settings", "Jbot", "Screenshot", "Calendar", "Backup"
};

void store_open(void)
{
    ui_fit(640, 480, &W);
    msg[0] = 0;
    inited = 1;
}

int store_frame(int mx, int my, int clicked)
{
    if (!inited)
        store_open();
    int S = ui_scale();
    if (ui_window(&W, "App Store", mx, my, clicked))
        return 1;

    int x = W.x + 28 * S, y = W.y + 46 * S;
    int bw = W.w - 56 * S;
    gfx_draw_string(x, y, fb_rgb(240, 200, 120), fb_rgb(30, 34, 46),
                    "Offline: downloads need the network stack.");

    int py = y + 40 * S;
    gfx_draw_string(x, py, fb_rgb(150, 210, 255), fb_rgb(30, 34, 46),
                    "Installed (built-in):");
    py += 28 * S;
    for (int i = 0; i < 6; i++) {
        int ry = py + i * 36 * S;
        ui_rect(x, ry, bw, 32 * S, fb_rgb(38, 44, 60));
        gfx_draw_string(x + 14 * S, ry + 8 * S, fb_rgb(215, 225, 240),
                        fb_rgb(38, 44, 60), installed[i]);
        gfx_draw_string(x + bw - 100 * S, ry + 8 * S, fb_rgb(140, 200, 160),
                        fb_rgb(38, 44, 60), "installed");
    }

    if (msg[0])
        gfx_draw_string(x, W.y + 350 * S, fb_rgb(240, 200, 120),
                        fb_rgb(30, 34, 46), msg);
    if (ui_button(x, W.y + W.h - 78 * S, bw, 38 * S,
                  "Browse / refresh catalog", 0, mx, my, clicked)) {
        const char *m = "Cannot reach catalog (network stack unavailable).";
        int p = 0;
        while (m[p] && p < 70) {
            msg[p] = m[p];
            p++;
        }
        msg[p] = 0;
    }
    return 0;
}
