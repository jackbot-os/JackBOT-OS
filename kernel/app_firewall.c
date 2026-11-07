#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ui.h>
#include <kernel/config.h>
#include <kernel/honeypot.h>
#include <kernel/net.h>

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

    int hy = y + 54 * S;
    int hpon = honeypot_enabled();
    if (ui_button(x, hy, 190 * S, 40 * S,
                  hpon ? "Honeypot: ON" : "Honeypot: OFF",
                  hpon, mx, my, clicked)) {
        honeypot_set(!hpon);
        msg[0] = 0;
    }
    gfx_draw_string(x + 210 * S, hy + 10 * S, fb_rgb(240, 200, 120),
                    fb_rgb(30, 34, 46),
                    "Passive observer only: it never answers or scans.");

    int py = hy + 54 * S;
    gfx_draw_string(x, py, fb_rgb(150, 210, 255), fb_rgb(30, 34, 46),
                    "Honeypot probes:");
    {
        char tbuf[28]; int p = 0;
        const char *pre = "Total observations: ";
        while (pre[p]) { tbuf[p] = pre[p]; p++; }
        uint64_t v = honeypot_total_hits();
        char dig[20]; int m = 0;
        if (v == 0) dig[m++] = '0';
        while (v > 0) { dig[m++] = (char)('0' + v % 10); v /= 10; }
        while (m > 0) tbuf[p++] = dig[--m];
        tbuf[p] = 0;
        gfx_draw_string(x + 300 * S, py, fb_rgb(180, 220, 255),
                        fb_rgb(30, 34, 46), tbuf);
    }
    py += 26 * S;
    int n = honeypot_entry_count();
    if (n == 0) {
        ui_rect(x, py, bw, 30 * S, fb_rgb(38, 44, 60));
        gfx_draw_string(x + 12 * S, py + 7 * S, fb_rgb(180, 190, 210),
                        fb_rgb(38, 44, 60), "(none recorded)");
        py += 40 * S;
    } else {
        const struct honeypot_entry *es = honeypot_entries();
        for (int i = 0; i < n && i < 5; i++) {
            uint8_t b[4];
            net_fmt_ip(es[i].src_ip, b);
            char ip[16];
            net_ip_str(b, ip);
            const char *tag = es[i].action == HP_ACT_TCP_SYN ? "TCP"
                            : es[i].action == HP_ACT_ICMP_ECHO ? "ICMP"
                            : "UDP";
            char num[12];
            {
                char t[12]; int m = 0, v = (int)es[i].hits;
                if (v == 0) t[m++] = '0';
                while (v > 0) { t[m++] = (char)('0' + v % 10); v /= 10; }
                int k = 0;
                while (m > 0) num[k++] = t[--m];
                num[k] = 0;
            }
            char row[96];
            int p = 0;
            int q = 0;
            while (ip[q] && p < 20) row[p++] = ip[q++];
            const char *sp1 = " ";
            while (*sp1 && p < 95) row[p++] = *sp1++;
            q = 0;
            while (tag[q] && p < 95) row[p++] = tag[q++];
            const char *pp = " port ";
            while (*pp && p < 95) row[p++] = *pp++;
            q = 0;
            char dp[8];
            {
                char t[8]; int m = 0, v = (int)es[i].dport;
                if (v == 0) t[m++] = '0';
                while (v > 0) { t[m++] = (char)('0' + v % 10); v /= 10; }
                int k = 0;
                while (m > 0) dp[k++] = t[--m];
                dp[k] = 0;
            }
            while (dp[q] && p < 95) row[p++] = dp[q++];
            const char *xn = " x";
            while (*xn && p < 95) row[p++] = *xn++;
            q = 0;
            while (num[q] && p < 95) row[p++] = num[q++];
            row[p] = 0;
            ui_rect(x, py, bw, 28 * S, fb_rgb(38, 44, 60));
            gfx_draw_string(x + 12 * S, py + 6 * S, fb_rgb(200, 210, 225),
                            fb_rgb(38, 44, 60), row);
            py += 32 * S;
        }
    }
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
