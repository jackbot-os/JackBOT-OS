#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ui.h>
#include <kernel/config.h>

static struct ui_win W;
static char msg[80];
static int inited;

void firewall_open(void)
{
    ui_fit(620, 480, &W);
    msg[0] = 0;
    inited = 1;
}

int firewall_frame(int mx, int my, int clicked)
{
    if (!inited)
        firewall_open();
    int S = ui_scale();
    if (ui_window(&W, "Firewall & Ports", mx, my, clicked))
        return 1;

    int x = W.x + 28 * S, y = W.y + 48 * S;
    int bw = W.w - 56 * S;
    int on = config_fw();
    if (ui_button(x, y, 190 * S, 40 * S,
                  on ? "Firewall: ON" : "Firewall: OFF",
                  on, mx, my, clicked)) {
        config_set_fw(!on);
        config_save();
        const char *m = on ? "Firewall will be OFF" : "Firewall will be ON";
        int p = 0;
        while (m[p] && p < 60) {
            msg[p] = m[p];
            p++;
        }
        msg[p] = 0;
    }
    gfx_draw_string(x + 210 * S, y + 10 * S, fb_rgb(240, 200, 120),
                    fb_rgb(30, 34, 46),
                    "Filtering needs the network stack; state saved.");

    int py = y + 66 * S;
    gfx_draw_string(x, py, fb_rgb(150, 210, 255), fb_rgb(30, 34, 46),
                    "Listening ports:");
    py += 26 * S;
    ui_rect(x, py, bw, 30 * S, fb_rgb(38, 44, 60));
    gfx_draw_string(x + 12 * S, py + 7 * S, fb_rgb(180, 190, 210),
                    fb_rgb(38, 44, 60), "(none - no network stack)");

    py += 48 * S;
    gfx_draw_string(x, py, fb_rgb(150, 210, 255), fb_rgb(30, 34, 46),
                    "Default policy:");
    static const char *rules[3] = {
        "Allow outbound (default)",
        "Block inbound (default)",
        "Allow loopback"
    };
    py += 26 * S;
    for (int i = 0; i < 3; i++) {
        int ry = py + i * 34 * S;
        ui_rect(x, ry, bw, 30 * S, fb_rgb(38, 44, 60));
        gfx_draw_string(x + 12 * S, ry + 7 * S, fb_rgb(200, 210, 225),
                        fb_rgb(38, 44, 60), rules[i]);
    }
    if (msg[0])
        gfx_draw_string(x, W.y + W.h - 40 * S, fb_rgb(150, 220, 160),
                        fb_rgb(30, 34, 46), msg);
    return 0;
}
