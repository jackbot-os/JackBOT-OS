#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ui.h>
#include <kernel/config.h>

static struct ui_win W;
static char msg[80];
static int inited;

static const char *city(int hour)
{
    switch (hour) {
    case -12: return "Baker Island";
    case -10: return "Honolulu";
    case -8: return "Los Angeles";
    case -5: return "New York";
    case -3: return "Sao Paulo";
    case 0: return "London";
    case 1: return "Berlin, Paris";
    case 3: return "Moscow";
    case 5: return "Karachi";
    case 6: return "Dhaka";
    case 7: return "Bangkok, Jakarta";
    case 8: return "Beijing, Singapore";
    case 9: return "Tokyo, Seoul";
    case 10: return "Sydney";
    case 12: return "Auckland";
    default: return "";
    }
}

static void tz_label(int hour, char *out)
{
    const char *d = "0123456789";
    int p = 0;
    out[p++] = 'U'; out[p++] = 'T'; out[p++] = 'C';
    int ah = hour;
    if (hour < 0) {
        out[p++] = '-';
        ah = -hour;
    } else {
        out[p++] = '+';
    }
    if (ah < 10) {
        out[p++] = '0';
        out[p++] = d[ah];
    } else {
        out[p++] = d[ah / 10];
        out[p++] = d[ah % 10];
    }
    out[p] = 0;
}

void region_open(void)
{
    ui_fit(700, 470, &W);
    msg[0] = 0;
    inited = 1;
}

int region_frame(int mx, int my, int clicked)
{
    if (!inited)
        region_open();
    int S = ui_scale();
    if (ui_window(&W, "Time Zone & Region", mx, my, clicked))
        return 1;

    int cur = config_tz();
    int bw = 300 * S, bh = 30 * S;
    int gx0 = W.x + 26 * S, gy0 = W.y + 44 * S;
    for (int i = 0; i < 25; i++) {
        int col = i / 13, row = i % 13;
        int x = gx0 + col * (bw + 24 * S);
        int y = gy0 + row * (bh + 8 * S);
        int hour = i - 12;
        char lab[12];
        tz_label(hour, lab);
        if (ui_button(x, y, bw, bh, lab, i == cur, mx, my, clicked)) {
            config_set_tz(i);
            config_save();
            const char *pre = "Zone set to ";
            int p = 0;
            while (pre[p] && p < 40) {
                msg[p] = pre[p];
                p++;
            }
            for (int q = 0; lab[q] && p < 58; q++)
                msg[p++] = lab[q];
            const char *c = city(hour);
            if (c[0]) {
                msg[p++] = ' ';
                for (int q = 0; c[q] && p < 72; q++)
                    msg[p++] = c[q];
            }
            msg[p] = 0;
        }
    }
    if (msg[0])
        gfx_draw_string(W.x + 26 * S, W.y + W.h - 40 * S,
                        fb_rgb(150, 220, 160), fb_rgb(30, 34, 46), msg);
    return 0;
}
