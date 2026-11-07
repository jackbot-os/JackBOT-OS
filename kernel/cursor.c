#include <stdint.h>
#include <stddef.h>
#include <kernel/cursor.h>
#include <kernel/fb.h>
#include <kernel/svga.h>

/* Pointer shapes. The VMSVGA hardware cursor can only composite the arrow
   bitmap (one DEFINE_ALPHA_CURSOR is armed at boot), so the arrow stays on
   the GPU path while the other shapes are drawn in software on top of the
   frame buffer. */
#define CUR_W 21
#define CUR_H 20
#define MAX_S 2

static const char *const art_arrow[CUR_H] = {
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

static const char *const art_hand[CUR_H] = {
    "......X..............",
    ".....XX..............",
    ".....XX..............",
    ".....XX..............",
    ".....XX.....X........",
    ".....XX....XX........",
    ".....XX...XX.........",
    ".....XX..XX..........",
    ".....XXXXXX..........",
    "....XXXXXXX..........",
    "...XXXXXXXXX.........",
    "..XXXXXXXXXX.........",
    "..XXXXXXXXXX.........",
    "..XXXXXXXXXX.........",
    "..XXXXXXXXXX.........",
    "..XXXXXXXXXX.........",
    "..XXXXXXXXXX.........",
    "...XXXXXXXXX.........",
    "....XXXXXX...........",
    ".....................",
};

static const char *const art_text[CUR_H] = {
    "....XXXXXXX..........",
    "......XXX............",
    ".......X.............",
    ".......X.............",
    ".......X.............",
    ".......X.............",
    ".......X.............",
    ".......X.............",
    ".......X.............",
    ".......X.............",
    ".......X.............",
    ".......X.............",
    ".......X.............",
    ".......X.............",
    ".......X.............",
    ".......X.............",
    "......XXX............",
    "....XXXXXXX..........",
    ".....................",
    ".....................",
};

static const char *const art_move[CUR_H] = {
    "..........X..........",
    ".........XXX.........",
    "........XXXXX........",
    "..........X..........",
    "..........X..........",
    "..........X..........",
    "..X.......X.......X..",
    ".XXX......X......XXX.",
    "XXXXX.....X.....XXXXX",
    ".XXX......X......XXX.",
    "..X.......X.......X..",
    "..........X..........",
    "..........X..........",
    "..........X..........",
    "........XXXXX........",
    ".........XXX.........",
    "..........X..........",
    ".....................",
    ".....................",
    ".....................",
};

static const char *const *const arts[4] = {
    art_arrow, art_hand, art_text, art_move
};

static int g_scale = 1;
static int g_visible = 1;
static int g_shape = CUR_ARROW;
static int g_have;          /* software backup valid */
static int g_px, g_py;      /* software cursor top-left */
static int g_pw, g_ph;      /* software cursor size in px */
static uint32_t g_back[CUR_W * CUR_H * MAX_S * MAX_S];

static int is_body(int x, int y)
{
    if (x < 0 || y < 0 || x >= CUR_W || y >= CUR_H)
        return 0;
    return arts[g_shape][y][x] == 'X';
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

/* Software path: save backing pixels and stamp the shape at (hx,hy).
   The sprite is clipped to the screen instead of being dropped: a cursor
   pushed against the right or bottom edge used to disappear completely,
   which read as "the mouse is gone". */
static void sw_draw(int hx, int hy)
{
    int s = g_scale;
    int w = CUR_W * s, h = CUR_H * s;
    int W = (int)fb_width(), H = (int)fb_height();
    int x0 = hx < 0 ? 0 : hx;
    int y0 = hy < 0 ? 0 : hy;
    int x1 = hx + w > W ? W : hx + w;
    int y1 = hy + h > H ? H : hy + h;

    if (x1 <= x0 || y1 <= y0)
        return;

    for (int j = y0; j < y1; j++)
        for (int i = x0; i < x1; i++)
            g_back[(j - y0) * (x1 - x0) + (i - x0)] = fb_getpixel(i, j);
    g_have = 1;
    g_px = x0; g_py = y0; g_pw = x1 - x0; g_ph = y1 - y0;

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
            for (int sy = 0; sy < s; sy++) {
                for (int sx = 0; sx < s; sx++) {
                    int px = hx + cx * s + sx;
                    int py = hy + cy * s + sy;
                    if (px < x0 || px >= x1 || py < y0 || py >= y1)
                        continue;
                    fb_putpixel(px, py, c);
                }
            }
        }
    }
}

void cursor_init(void)
{
    g_have = 0;
    g_visible = 1;
    g_shape = CUR_ARROW;
    /* The pointer is always painted into the frame buffer, on every display
       path. VMSVGA's hardware cursor is composited by the GPU outside the
       scanout, so when that device refuses a define or a mode change drops
       it the pointer simply vanishes and nothing in the frame buffer can
       bring it back. The software sprite is drawn here instead; the
       hardware cursor is switched off so there is only one arrow. */
    if (svga_gpu_present())
        svga_cursor_show(0);
}

void cursor_set_scale(int s)
{
    if (s < 1) s = 1;
    if (s > MAX_S) s = MAX_S;
    if (s != g_scale && g_have)
        sw_restore();
    g_scale = s;
}

int cursor_shape(void)
{
    return g_shape;
}

void cursor_set_shape(int shape)
{
    if (shape < 0 || shape > CUR_MOVE || shape == g_shape)
        return;
    if (g_have)
        sw_restore();
    g_shape = shape;
}

void cursor_show(int visible)
{
    g_visible = visible;
    if (!visible)
        sw_restore();
}

void cursor_begin_frame(void)
{
    if (g_visible)
        sw_restore();
}

void cursor_end_frame(int mx, int my)
{
    if (g_visible)
        sw_draw(mx, my);
}
