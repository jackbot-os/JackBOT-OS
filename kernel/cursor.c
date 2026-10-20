#include <stdint.h>
#include <stddef.h>
#include <kernel/cursor.h>
#include <kernel/fb.h>
#include <kernel/svga.h>

/* Same 21x20 arrow silhouette as the VMSVGA hardware cursor. */
#define CUR_W 21
#define CUR_H 20
#define MAX_S 2

static const char *const cur_art[CUR_H] = {
    "X....................",
    "XX...................",
    "XXX..................",
    "XXXX.................",
    "XXXXX................",
    "XXXXXX...............",
    "XXXXXXX..............",
    "XXXXXXXX.............",
    "XXXXXXXXX............",
    "XXXXXXXXXX...........",
    "XXXXXXXXXXX..........",
    "XXXXXXXXXXXX.........",
    "XXXXXXXXXXXXX........",
    "XXXXXXXXXXXX.........",
    "XXXXXXXXXXX..........",
    "XXXXXX.XXXXX.........",
    "XXXXX...XXXX.........",
    "XXXX.....XXX.........",
    "XXX.......XX.........",
    "XX.........X.........",
};

static int g_scale = 1;
static int g_visible = 1;
static int g_have;          /* software backup valid */
static int g_px, g_py;      /* software cursor top-left */
static int g_pw, g_ph;      /* software cursor size in px */
static uint32_t g_back[CUR_W * CUR_H * MAX_S * MAX_S];

static int is_body(int x, int y)
{
    return x >= 0 && y >= 0 && x < CUR_W && y < CUR_H &&
           cur_art[y][x] == 'X';
}

static int is_edge(int x, int y)
{
    if (is_body(x, y))
        return 0;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            if (is_body(x + dx, y + dy))
                return 1;
    return 0;
}

/* Software path: restore the backing pixels saved at the old position. */
static void sw_restore(void)
{
    if (!g_have)
        return;
    for (int j = 0; j < g_ph; j++)
        for (int i = 0; i < g_pw; i++)
            fb_putpixel(g_px + i, g_py + j, g_back[j * g_pw + i]);
    g_have = 0;
}

/* Software path: save backing pixels and stamp the arrow at (hx,hy). */
static void sw_draw(int hx, int hy)
{
    int s = g_scale;
    int w = CUR_W * s, h = CUR_H * s;
    int W = (int)fb_width(), H = (int)fb_height();
    if (hx < 0 || hy < 0 || hx + w > W || hy + h > H)
        return;

    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            g_back[j * w + i] = fb_getpixel(hx + i, hy + j);
    g_have = 1;
    g_px = hx; g_py = hy; g_pw = w; g_ph = h;

    uint32_t white = fb_rgb(255, 255, 255);
    uint32_t black = fb_rgb(0, 0, 0);
    for (int cy = 0; cy < CUR_H; cy++) {
        for (int cx = 0; cx < CUR_W; cx++) {
            uint32_t c = 0;
            if (is_body(cx, cy))
                c = black;
            else if (is_edge(cx, cy))
                c = white;
            else
                continue;
            for (int sy = 0; sy < s; sy++)
                for (int sx = 0; sx < s; sx++)
                    fb_putpixel(hx + cx * s + sx, hy + cy * s + sy, c);
        }
    }
}

void cursor_init(void)
{
    g_have = 0;
    g_visible = 1;
    if (svga_gpu_present())
        svga_cursor_init();
}

void cursor_set_scale(int s)
{
    if (s < 1) s = 1;
    if (s > MAX_S) s = MAX_S;
    g_scale = s;
}

void cursor_show(int visible)
{
    g_visible = visible;
    if (svga_gpu_present())
        svga_cursor_show(visible);
    if (!visible)
        sw_restore();
}

void cursor_begin_frame(void)
{
    if (svga_gpu_present())
        return;
    if (g_visible)
        sw_restore();
}

void cursor_end_frame(int mx, int my)
{
    if (svga_gpu_present()) {
        if (g_visible) {
            svga_cursor_move(mx, my);
            svga_cursor_flush();
        }
        return;
    }
    if (g_visible)
        sw_draw(mx, my);
}
