#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ui.h>
#include <kernel/heap.h>
#include <kernel/fat32.h>

static struct ui_win W;
static uint8_t *bmp;
static char msg[80];
static int inited;

static void w16(uint8_t *p, uint16_t v) { p[0] = v & 255; p[1] = v >> 8; }
static void w32(uint8_t *p, uint32_t v)
{
    p[0] = v & 255; p[1] = (v >> 8) & 255;
    p[2] = (v >> 16) & 255; p[3] = (v >> 24) & 255;
}

static int take_shot(void)
{
    int w = (int)fb_width(), h = (int)fb_height();
    uint32_t img = (uint32_t)w * h * 4;
    uint32_t fsize = 54 + img;
    if (!bmp)
        bmp = kcalloc(fsize, 1);
    if (!bmp)
        return -1;

    bmp[0] = 'B'; bmp[1] = 'M';
    w32(bmp + 2, fsize);
    w16(bmp + 6, 0); w16(bmp + 8, 0);
    w32(bmp + 10, 54);
    w32(bmp + 14, 40);
    w32(bmp + 18, (uint32_t)w);
    w32(bmp + 22, (uint32_t)h);
    w16(bmp + 26, 1);
    w16(bmp + 28, 32);
    w32(bmp + 30, 0);
    w32(bmp + 34, img);
    w32(bmp + 38, 2835); w32(bmp + 42, 2835);
    w32(bmp + 46, 0); w32(bmp + 50, 0);

    for (int y = 0; y < h; y++) {
        int sy = h - 1 - y;
        for (int x = 0; x < w; x++) {
            uint32_t c = fb_getpixel(x, sy);
            uint8_t *px = bmp + 54 + ((uint64_t)y * w + x) * 4;
            px[0] = c & 255;
            px[1] = (c >> 8) & 255;
            px[2] = (c >> 16) & 255;
            px[3] = 0;
        }
    }

    const char *d = "0123456789";
    char name[13];
    name[0] = 'S'; name[1] = 'H'; name[2] = 'O'; name[3] = 'T';
    name[8] = '.'; name[9] = 'B'; name[10] = 'M'; name[11] = 'P'; name[12] = 0;
    int n;
    for (n = 1; n <= 9999; n++) {
        name[4] = d[n / 1000 % 10];
        name[5] = d[n / 100 % 10];
        name[6] = d[n / 10 % 10];
        name[7] = d[n % 10];
        if (!fat_exists(name))
            break;
    }
    if (n > 9999)
        return -2;
    if (fat_write_file(name, bmp, fsize))
        return -3;

    const char *pre = "Saved ";
    int p = 0;
    while (pre[p]) {
        msg[p] = pre[p];
        p++;
    }
    for (int q = 0; name[q] && p < 70; q++)
        msg[p++] = name[q];
    msg[p] = 0;
    return 0;
}

void capture_open(void)
{
    ui_fit(520, 270, &W);
    msg[0] = 0;
    inited = 1;
}

int capture_frame(int mx, int my, int clicked)
{
    if (!inited)
        capture_open();
    int S = ui_scale();
    if (ui_window(&W, "Screenshot", mx, my, clicked))
        return 1;

    int bx = W.x + 40 * S, bw = W.w - 80 * S, bh = 42 * S;
    if (ui_button(bx, W.y + 56 * S, bw, bh, "Take screenshot",
                  0, mx, my, clicked)) {
        if (take_shot()) {
            const char *m = "Screenshot failed (disk missing or full).";
            int p = 0;
            while (m[p] && p < 70) {
                msg[p] = m[p];
                p++;
            }
            msg[p] = 0;
        }
    }
    if (ui_button(bx, W.y + 112 * S, bw, bh, "Record screen",
                  0, mx, my, clicked)) {
        const char *m = "Recording needs a video encoder (not built yet).";
        int p = 0;
        while (m[p] && p < 70) {
            msg[p] = m[p];
            p++;
        }
        msg[p] = 0;
    }
    if (msg[0])
        gfx_draw_string(W.x + 24 * S, W.y + W.h - 48 * S,
                        fb_rgb(150, 220, 160), fb_rgb(30, 34, 46), msg);
    return 0;
}
