#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ui.h>
#include <kernel/pmm.h>
#include <kernel/i18n.h>
#include "i18n_data.h"

uint64_t timer_get_ticks(void);

static struct ui_win W;
static int inited;

static void u64dec(uint64_t v, char *out)
{
    char tmp[24];
    int n = 0;
    if (v == 0) { out[0] = '0'; out[1] = 0; return; }
    while (v) { tmp[n++] = '0' + (v % 10); v /= 10; }
    int p = 0;
    while (n) out[p++] = tmp[--n];
    out[p] = 0;
}

static void append(char *s, const char *a)
{
    int p = 0;
    while (s[p]) p++;
    while (*a) s[p++] = *a++;
    s[p] = 0;
}

static void two(char *s, int v)
{
    int p = 0;
    while (s[p]) p++;
    s[p++] = '0' + (v / 10);
    s[p++] = '0' + (v % 10);
    s[p] = 0;
}

void monitor_open(void)
{
    ui_fit(640, 420, &W);
    inited = 1;
}

int monitor_frame(int mx, int my, int clicked)
{
    if (!inited)
        monitor_open();
    int S = ui_scale();
    if (ui_window(&W, "System Monitor", mx, my, clicked))
        return 1;

    int bg = fb_rgb(30, 34, 46);
    int x0 = W.x + 36 * S;
    int y = W.y + 58 * S;
    int barw = W.w - 100 * S;

    uint64_t total = pmm_total_bytes();
    uint64_t freeb = pmm_free_bytes();
    uint64_t used = (total > freeb) ? total - freeb : 0;
    uint64_t tmb = total / (1024 * 1024);
    uint64_t fmb = freeb / (1024 * 1024);
    uint64_t umb = used / (1024 * 1024);
    int pct = total ? (int)(used * 100 / total) : 0;

    char line[64];
    char num[24];

    line[0] = 0;
    append(line, tr(STR_MON_TOTAL));
    u64dec(tmb, num);
    append(line, num);
    append(line, " MB");
    gfx_draw_string_wide(x0, y, fb_rgb(210, 220, 240), bg, line);
    y += 30 * S;

    line[0] = 0;
    append(line, tr(STR_MON_USED));
    u64dec(umb, num);
    append(line, num);
    append(line, " MB");
    gfx_draw_string_wide(x0, y, fb_rgb(210, 220, 240), bg, line);
    y += 30 * S;

    line[0] = 0;
    append(line, tr(STR_MON_FREE));
    u64dec(fmb, num);
    append(line, num);
    append(line, " MB");
    gfx_draw_string_wide(x0, y, fb_rgb(120, 222, 140), bg, line);
    y += 36 * S;

    ui_rect(x0, y, barw, 24 * S, fb_rgb(18, 22, 32));
    ui_border(x0, y, barw, 24 * S, fb_rgb(80, 94, 120));
    int fill = (int)(barw * pct / 100);
    int barcol = pct > 85 ? fb_rgb(230, 120, 110) : fb_rgb(90, 160, 255);
    if (fill > 0)
        ui_rect(x0 + 2, y + 2, fill - 4, 24 * S - 4, barcol);
    line[0] = 0;
    append(line, tr(STR_MON_USAGE));
    u64dec(pct, num);
    append(line, num);
    append(line, "%");
    gfx_draw_string_wide(x0, y + 34 * S, fb_rgb(200, 210, 226), bg, line);
    y += 70 * S;

    uint64_t sec = timer_get_ticks() / 100;
    int hh = (int)(sec / 3600);
    int mm = (int)((sec % 3600) / 60);
    int ss = (int)(sec % 60);
    line[0] = 0;
    append(line, tr(STR_MON_UPTIME));
    two(line, hh);
    append(line, ":");
    two(line, mm);
    append(line, ":");
    two(line, ss);
    gfx_draw_string_wide(x0, y, fb_rgb(210, 220, 240), bg, line);
    y += 30 * S;

    line[0] = 0;
    append(line, tr(STR_MON_RESOLUTION));
    u64dec(fb_width(), num);
    append(line, num);
    append(line, " x ");
    u64dec(fb_height(), num);
    append(line, num);
    gfx_draw_string_wide(x0, y, fb_rgb(210, 220, 240), bg, line);
    return 0;
}
