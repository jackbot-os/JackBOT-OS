#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ui.h>
#include <kernel/clock.h>
#include <kernel/map_data.h>

/* Offline world map. The coastlines are baked into the kernel image from
   Natural Earth 110m land (public domain) and drawn with an equirectangular
   projection, so the app works with no network at all. */

static struct ui_win W;
static int inited;
static int zoom = 1;          /* 1 = whole world, 8 = closest */
static int c_lon = 20, c_lat = 15;   /* view centre, degrees */
static int last_w = -1, last_h = -1;

struct city { int lon, lat; const char *name; };
static const struct city CITIES[] = {
    {  116,  40, "Beijing" },
    {    0,  51, "London" },
    {  -74,  41, "New York" },
    {  151, -34, "Sydney" },
    {   31,  30, "Cairo" },
    {  -46, -23, "Sao Paulo" },
    {   37,  56, "Moscow" },
    {  140,  36, "Tokyo" },
    {  104,   1, "Singapore" },
    {   77,  29, "New Delhi" },
    {   18,  -34, "Cape Town" },
};
#define CITY_COUNT ((int)(sizeof(CITIES) / sizeof(CITIES[0])))

static int px_of(int lon, int ox, int w, int aw)
{
    /* w = pixels per degree of the visible span, ox = left edge */
    return ox + aw / 2 + (int)((int64_t)(lon - c_lon) * w);
}

static int py_of(int lat, int h, int oy, int ah)
{
    return oy + ah / 2 - (int)((int64_t)(lat - c_lat) * h);
}

static void line(int x0, int y0, int x1, int y1, uint32_t c, uint8_t a,
                 int clipx, int clipy, int clipw, int cliph)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    for (int guard = 0; guard < 6000; guard++) {
        if (x0 >= clipx && x0 < clipx + clipw && y0 >= clipy && y0 < clipy + cliph)
            fb_putpixel_blend(x0, y0, c, a);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = err * 2;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}

void maps_open(void)
{
    ui_fit(780, 540, &W);
    zoom = 1;
    c_lon = 20;
    c_lat = 15;
    inited = 1;
    last_w = -1;
}

int maps_frame(int mx, int my, int clicked)
{
    if (!inited)
        maps_open();
    int S = ui_scale();
    if (ui_window(&W, "Maps", mx, my, clicked))
        return 1;

    int head = W.y + 46 * S;
    int foot = W.y + W.h - 34 * S;
    int ox = W.x + 12 * S, oy = head;
    int aw = W.w - 24 * S, ah = foot - head;
    if (aw < 100 || ah < 100)
        return 0;

    /* when the window is resized by ui_fit keep the pixel scale sane */
    int wdeg = 360 / zoom;                        /* degrees visible across */
    int ppd_x = aw / (wdeg ? wdeg : 1);           /* px per degree, x */
    int ppd_y = ah / (180 / zoom);

    /* clamp the centre so the view never walks off the world */
    int half_lon = wdeg / 2, half_lat = 90 / zoom;
    if (c_lon > 180 - half_lon) c_lon = 180 - half_lon;
    if (c_lon < -180 + half_lon) c_lon = -180 + half_lon;
    if (c_lat > 90 - half_lat) c_lat = 90 - half_lat;
    if (c_lat < -90 + half_lat) c_lat = -90 + half_lat;

    /* ocean */
    ui_rect(ox, oy, aw, ah, fb_rgb(12, 34, 58));

    /* graticule every 30 degrees, equator brighter */
    for (int lon = -180; lon <= 180; lon += 30) {
        int x = px_of(lon, ox, ppd_x, aw);
        if (x >= ox && x < ox + aw)
            line(x, oy, x, oy + ah - 1, fb_rgb(40, 74, 108), 150, ox, oy, aw, ah);
    }
    for (int lat = -90; lat <= 90; lat += 30) {
        int y = py_of(lat, ppd_y, oy, ah);
        if (y >= oy && y < oy + ah)
            line(ox, y, ox + aw - 1, y, lat == 0 ? fb_rgb(70, 130, 190)
                                                : fb_rgb(40, 74, 108),
                 lat == 0 ? 220 : 150, ox, oy, aw, ah);
    }

    /* land outlines */
    for (int i = 0; i < MAP_RING_COUNT; i++) {
        int off = (int)MAP_RINGS[i].off;
        int n = (int)MAP_RINGS[i].n;
        int px = 0, py = 0;
        for (int k = 0; k < n; k++) {
            int lon = MAP_PTS[off + k * 2];
            int lat = MAP_PTS[off + k * 2 + 1];
            int x = px_of(lon / 10, ox, ppd_x, aw);
            int y = py_of(lat / 10, ppd_y, oy, ah);
            if (k == 0) {
                px = x; py = y;
                continue;
            }
            /* skip wildly long segments that wrap around the projection */
            if (x - px > aw || px - x > aw) {
                px = x; py = y;
                continue;
            }
            line(px, py, x, y, fb_rgb(120, 214, 200), 230, ox, oy, aw, ah);
            px = x; py = y;
        }
    }

    /* cities */
    for (int i = 0; i < CITY_COUNT; i++) {
        int x = px_of(CITIES[i].lon, ox, ppd_x, aw);
        int y = py_of(CITIES[i].lat, ppd_y, oy, ah);
        if (x < ox + 4 || x >= ox + aw - 4 || y < oy + 4 || y >= oy + ah - 4)
            continue;
        ui_rect(x - 2, y - 2, 4, 4, fb_rgb(255, 190, 90));
        if (zoom >= 2)
            gfx_draw_string(x + 6, y - 6, fb_rgb(235, 240, 250),
                            fb_rgb(12, 34, 58), CITIES[i].name);
    }

    /* readout: cursor position when it is over the ocean/land, else centre */
    int rlon = c_lon, rlat = c_lat;
    if (mx >= ox && mx < ox + aw && my >= oy && my < oy + ah) {
        rlon = c_lon + (int)(((int64_t)(mx - ox - aw / 2)) / (ppd_x ? ppd_x : 1));
        rlat = c_lat - (int)(((int64_t)(my - oy - ah / 2)) / (ppd_y ? ppd_y : 1));
    }

    char buf[80];
    int p = 0;
    for (p = 0; "Lon " [p]; p++) buf[p] = "Lon "[p];
    if (rlon < 0) { buf[p++] = '-'; rlon = -rlon; }
    else buf[p++] = '+';
    buf[p++] = '0' + (rlon / 100) % 10;
    buf[p++] = '0' + (rlon / 10) % 10;
    buf[p++] = '0' + rlon % 10;
    buf[p++] = ' ';
    buf[p++] = 'L'; buf[p++] = 'a'; buf[p++] = 't'; buf[p++] = ' ';
    if (rlat < 0) { buf[p++] = '-'; rlat = -rlat; }
    else buf[p++] = '+';
    buf[p++] = '0' + (rlat / 10) % 10;
    buf[p++] = '0' + rlat % 10;
    buf[p++] = ' ';
    buf[p++] = ' '; buf[p++] = 'Z'; buf[p++] = 'o'; buf[p++] = 'o'; buf[p++] = 'm';
    buf[p++] = ' '; buf[p++] = 'x'; buf[p++] = '0' + zoom;
    buf[p] = 0;
    gfx_draw_string(ox + 6, foot + 8 * S, fb_rgb(180, 210, 245),
                    fb_rgb(26, 30, 42), buf);

    /* controls: zoom in/out plus a four-way pan pad */
    int bx = W.x + W.w - 120 * S, by = foot + 4 * S;
    if (ui_button(bx, by, 28 * S, 22 * S, "+", 0, mx, my, clicked)) {
        if (zoom < 8) { zoom++; }
    }
    if (ui_button(bx + 32 * S, by, 28 * S, 22 * S, "-", 0, mx, my, clicked)) {
        if (zoom > 1) { zoom--; }
    }
    int px0 = W.x + W.w - 210 * S;
    if (ui_button(px0, by, 22 * S, 22 * S, "<", 0, mx, my, clicked)) c_lon -= 30 / zoom;
    if (ui_button(px0 + 26 * S, by, 22 * S, 22 * S, ">", 0, mx, my, clicked)) c_lon += 30 / zoom;
    if (ui_button(px0 + 52 * S, by, 22 * S, 22 * S, "^", 0, mx, my, clicked)) c_lat += 20 / zoom;
    if (ui_button(px0 + 78 * S, by, 22 * S, 22 * S, "v", 0, mx, my, clicked)) c_lat -= 20 / zoom;

    (void)last_w; (void)last_h;
    return 0;
}
