#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ui.h>

static struct ui_win W;
static char msg[80];
static int inited;

void network_open(void)
{
    ui_fit(580, 420, &W);
    msg[0] = 0;
    inited = 1;
}

int network_frame(int mx, int my, int clicked)
{
    if (!inited)
        network_open();
    int S = ui_scale();
    if (ui_window(&W, "Network", mx, my, clicked))
        return 1;

    int x = W.x + 30 * S, y = W.y + 50 * S;
    int bw = W.w - 60 * S;
    gfx_draw_string(x, y, fb_rgb(240, 200, 120), fb_rgb(30, 34, 46),
        "No network interface: NIC driver + TCP/IP stack not built yet.");

    int py = y + 46 * S;
    ui_rect(x, py, bw, 46 * S, fb_rgb(38, 44, 60));
    ui_border(x, py, bw, 46 * S, fb_rgb(90, 100, 130));
    gfx_draw_string(x + 14 * S, py + 8 * S, fb_rgb(180, 190, 210),
                    fb_rgb(38, 44, 60), "Wired  (not available)");

    py += 56 * S;
    ui_rect(x, py, bw, 46 * S, fb_rgb(38, 44, 60));
    ui_border(x, py, bw, 46 * S, fb_rgb(90, 100, 130));
    gfx_draw_string(x + 14 * S, py + 8 * S, fb_rgb(180, 190, 210),
                    fb_rgb(38, 44, 60), "Wi-Fi  (not available)");

    py += 66 * S;
    gfx_draw_string(x, py, fb_rgb(160, 170, 190), fb_rgb(30, 34, 46),
                    "IP: -     Gateway: -");

    if (ui_button(x, py + 38 * S, bw, 40 * S, "Refresh",
                  0, mx, my, clicked)) {
        const char *m = "No NIC driver to scan.";
        int p = 0;
        while (m[p] && p < 70) {
            msg[p] = m[p];
            p++;
        }
        msg[p] = 0;
    }
    if (msg[0])
        gfx_draw_string(x, W.y + W.h - 40 * S, fb_rgb(240, 200, 120),
                        fb_rgb(30, 34, 46), msg);
    return 0;
}
