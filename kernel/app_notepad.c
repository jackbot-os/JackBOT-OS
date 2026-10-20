#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ui.h>
#include <kernel/ps2.h>

#define CAP 4000

static struct ui_win W;
static int inited;
static char buf[CAP];
static int len;

void notepad_open(void)
{
    ui_fit(700, 460, &W);
    len = 0;
    inited = 1;
}

static void redraw(void)
{
    int S = ui_scale();
    int x0 = W.x + 14 * S;
    int y0 = W.y + 50 * S;
    int x1 = W.x + W.w - 14 * S;
    int y1 = W.y + W.h - 14 * S;
    int cw = 8 * S, lh = 16 * S;
    int maxc = (x1 - x0) / cw;
    if (maxc < 10) maxc = 10;

    ui_rect(x0, y0, x1 - x0, y1 - y0, fb_rgb(22, 26, 36));
    ui_border(x0, y0, x1 - x0, y1 - y0, fb_rgb(64, 76, 100));

    int x = x0, y = y0 + 2 * S;
    int col = 0;
    for (int i = 0; i < len; i++) {
        char c = buf[i];
        if (c == '\n' || c == '\r' || col >= maxc) {
            x = x0; y += lh; col = 0;
            if (c == '\r' || c == '\n')
                continue;
        }
        if (y > y1 - lh)
            break;
        char s[2];
        s[0] = (c >= 32) ? c : ' ';
        s[1] = 0;
        gfx_draw_string(x, y, fb_rgb(220, 228, 240), fb_rgb(22, 26, 36), s);
        x += cw;
        col++;
    }
    if (y <= y1 - lh)
        gfx_draw_string(x, y, fb_rgb(120, 200, 255), fb_rgb(22, 26, 36), "_");
}

int notepad_frame(int mx, int my, int clicked)
{
    if (!inited)
        notepad_open();
    int S = ui_scale();
    if (ui_window(&W, "Notepad", mx, my, clicked))
        return 1;

    char c;
    while (ps2_read_key(&c)) {
        if (c == '\b' || c == 0x7f) {
            if (len > 0)
                len--;
        } else if (c == '\r' || c == '\n') {
            if (len < CAP - 1)
                buf[len++] = '\n';
        } else if (c >= 32 && c < 127) {
            if (len < CAP - 1)
                buf[len++] = c;
        }
    }
    redraw();
    (void)S;
    return 0;
}
