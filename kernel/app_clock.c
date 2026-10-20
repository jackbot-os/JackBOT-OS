#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ui.h>

static struct ui_win W;
static int inited;

static uint8_t cmos(int r)
{
    uint8_t v;
    __asm__ volatile("outb %0,$0x70" : : "a"((uint8_t)r));
    __asm__ volatile("inb $0x71,%0" : "=a"(v));
    return v;
}
static int bcdv(uint8_t v) { return (v & 15) + ((v >> 4) * 10); }

static int isleap(int y) { return (y % 4 == 0 && y % 100) || y % 400 == 0; }
static int mdays(int y, int m)
{
    static const int d[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    if (m == 2 && isleap(y))
        return 29;
    return d[m - 1];
}
static int dow(int y, int m, int d)
{
    static const int t[12] = {
        0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4
    };
    if (m < 3)
        y--;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

static const char *WD[7] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

static void num2(char *p, int v)
{
    const char *d = "0123456789";
    p[0] = d[v / 10];
    p[1] = d[v % 10];
}

void calendar_open(void)
{
    ui_fit(560, 500, &W);
    inited = 1;
}

int calendar_frame(int mx, int my, int clicked)
{
    if (!inited)
        calendar_open();
    int S = ui_scale();
    if (ui_window(&W, "Calendar & Clock", mx, my, clicked))
        return 1;

    int hh = bcdv(cmos(4)), mm = bcdv(cmos(2)), ss = bcdv(cmos(0));
    int dom = bcdv(cmos(7)), mo = bcdv(cmos(8));
    int yr = 2000 + bcdv(cmos(9));

    char t[16];
    num2(t, hh); t[2] = ':';
    num2(t + 3, mm); t[5] = ':';
    num2(t + 6, ss); t[8] = 0;

    int old = gfx_get_scale();
    gfx_set_scale(3);
    int tw = gfx_text_width(t);
    gfx_draw_string(W.x + (W.w - tw) / 2, W.y + 32 * S,
                    fb_rgb(235, 240, 250), fb_rgb(30, 34, 46), t);
    gfx_set_scale(old);

    char dt[40];
    num2(dt, yr / 100);
    num2(dt + 2, yr % 100);
    dt[4] = '-';
    num2(dt + 5, mo);
    dt[7] = '-';
    num2(dt + 8, dom);
    dt[10] = ' ';
    const char *w = WD[dow(yr, mo, dom)];
    int p = 11;
    while (w[p - 11]) {
        dt[p] = w[p - 11];
        p++;
    }
    dt[p] = 0;
    int dw = gfx_text_width(dt);
    gfx_draw_string(W.x + (W.w - dw) / 2, W.y + 120 * S,
                    fb_rgb(150, 210, 255), fb_rgb(30, 34, 46), dt);

    int gx0 = W.x + 30 * S, gy0 = W.y + 158 * S;
    int cw = (W.w - 60 * S) / 7, ch = 30 * S;
    for (int i = 0; i < 7; i++) {
        int wd2 = gfx_text_width(WD[i]);
        gfx_draw_string(gx0 + i * cw + (cw - wd2) / 2, gy0,
                        fb_rgb(180, 190, 210), fb_rgb(30, 34, 46), WD[i]);
    }

    int first = dow(yr, mo, 1);
    int nd = mdays(yr, mo);
    for (int i = 0; i < 42; i++) {
        int col = i % 7, row = i / 7;
        int day = i - first + 1;
        int cx = gx0 + col * cw, cy = gy0 + 26 * S + row * ch;
        if (day >= 1 && day <= nd) {
            int today = day == dom;
            ui_rect(cx + 2, cy + 2, cw - 4, ch - 4,
                    today ? fb_rgb(60, 110, 180) : fb_rgb(38, 44, 60));
            char num[4];
            const char *dd = "0123456789";
            if (day >= 10) {
                num[0] = dd[day / 10];
                num[1] = dd[day % 10];
                num[2] = 0;
            } else {
                num[0] = dd[day];
                num[1] = 0;
            }
            int nw = gfx_text_width(num);
            gfx_draw_string(cx + (cw - nw) / 2,
                            cy + (ch - gfx_text_height()) / 2,
                            today ? fb_rgb(255, 255, 255)
                                  : fb_rgb(210, 220, 235),
                            today ? fb_rgb(60, 110, 180)
                                  : fb_rgb(38, 44, 60), num);
        }
    }
    return 0;
}
