#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ui.h>
#include <kernel/ps2.h>
#include <kernel/bluetooth.h>
#include <kernel/usb.h>

static struct ui_win W;
static int inited;
static int drag_bright;
static char msg[96];

static void set_msg(const char *m)
{
    int q = 0;
    while (m[q] && q < (int)sizeof(msg) - 1) {
        msg[q] = m[q];
        q++;
    }
    msg[q] = 0;
}

void bluetooth_open(void)
{
    ui_fit(620, 520, &W);
    inited = 1;
    drag_bright = 0;
    set_msg("");
}

static uint32_t led_palette_color(int c)
{
    uint32_t v = bt_led_rgb(c);
    return fb_rgb((uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v);
}

int bluetooth_frame(int mx, int my, int clicked)
{
    if (!inited)
        bluetooth_open();
    int S = ui_scale();
    if (ui_window(&W, "Bluetooth & Lamps", mx, my, clicked))
        return 1;

    int x = W.x + 30 * S, y = W.y + 52 * S;
    int bw = W.w - 60 * S;

    int on = bt_power();
    uint32_t good = fb_rgb(150, 230, 170);
    uint32_t warn = fb_rgb(240, 200, 120);
    uint32_t dim = fb_rgb(160, 170, 190);

    gfx_draw_string(x, y, on ? good : warn, fb_rgb(30, 34, 46),
                    on ? "Bluetooth radio: ON"
                       : "Bluetooth radio: OFF");
    int na = bt_count();
    if (na > 0) {
        for (int i = 0; i < na; i++) {
            const struct bt_adapter *a = bt_get(i);
            if (!a)
                continue;
            char line[72];
            int p = 0;
            const char *t = "Adapter: ";
            while (*t) { line[p++] = *t++; }
            const char *nm = a->name;
            while (*nm && p < 60) { line[p++] = *nm++; }
            line[p] = 0;
            gfx_draw_string(x, y + 24 * S + i * 20 * S, good,
                            fb_rgb(30, 34, 46), line);
        }
    } else {
        gfx_draw_string(x, y + 24 * S, dim, fb_rgb(30, 34, 46),
                        "No PCI Bluetooth adapter found.");
        gfx_draw_string(x, y + 44 * S, dim, fb_rgb(30, 34, 46),
                        "USB BT dongles need the USB device stage (next).");
    }

    int by = y + 78 * S;
    int half = (bw - 20 * S) / 2;
    if (ui_button(x, by, half, 36 * S,
                  on ? "Power off" : "Power on", on, mx, my, clicked)) {
        if (bt_set_power(on ? 0 : 1) >= 0)
            set_msg(on ? "Radio powered down." : "Radio powered up.");
        return 0;
    }
    if (ui_button(x + half + 20 * S, by, half, 36 * S,
                  "Scan devices", 0, mx, my, clicked)) {
        if (bt_begin_scan() == 0)
            set_msg("Inquiry started (HCI transport pending).");
        else
            set_msg("Turn the radio on before scanning.");
        return 0;
    }

    int prog = bt_scan_progress();
    if (prog > 0 && prog < 100) {
        int pb_y = by + 46 * S;
        ui_rect(x, pb_y, bw, 14 * S, fb_rgb(50, 56, 72));
        ui_rect(x, pb_y, bw * prog / 100, 14 * S, fb_rgb(80, 140, 230));
    }

    int ly = by + 78 * S;
    gfx_draw_string(x, ly, fb_rgb(150, 210, 255), fb_rgb(30, 34, 46),
                    "Smart LED lamp");
    int sw = 56 * S, sh = 40 * S;
    int gap = 8 * S;
    for (int c = 0; c < BT_LED_COLORS; c++) {
        int col = c % 4, row = c / 4;
        int bx = x + col * (sw * 2 + gap);
        int bcy = ly + 24 * S + row * (sh + gap);
        int sel = bt_led_color() == c;
        ui_rect(bx, bcy, sw * 2, sh, led_palette_color(c));
        ui_border(bx, bcy, sw * 2, sh,
                  sel ? fb_rgb(255, 220, 90) : fb_rgb(110, 120, 150));
        if (sel) {
            gfx_draw_string(bx + 6 * S, bcy + 6 * S,
                            fb_rgb(20, 20, 20), led_palette_color(c), "x");
        }
        if (clicked && ui_inside(mx, my, bx, bcy, sw * 2, sh)) {
            bt_led_set(c);
            set_msg(c == BT_LED_OFF ? "Lamp off."
                                    : "Lamp color set.");
            return 0;
        }
    }

    int ty = ly + 24 * S + 2 * (sh + gap) + 14 * S;
    gfx_draw_string(x, ty, fb_rgb(200, 206, 220), fb_rgb(30, 34, 46),
                    "Brightness");
    int trackX = x + 110 * S, trackW = bw - 200 * S;
    int trackY = ty;
    ui_rect(trackX, trackY, trackW, 10 * S, fb_rgb(50, 56, 72));
    int hx = trackX + trackW * bt_led_brightness_get() / 100;
    ui_rect(hx - 5 * S, trackY - 7 * S, 10 * S, 24 * S,
            fb_rgb(255, 200, 90));
    int down = ps2_mouse_down();
    if (down && ui_inside(mx, my, trackX - 10 * S, trackY - 10 * S,
                          trackW + 20 * S, 30 * S))
        drag_bright = 1;
    if (!down)
        drag_bright = 0;
    if (drag_bright) {
        int nv = (mx - trackX) * 100 / trackW;
        if (nv < 0) nv = 0;
        if (nv > 100) nv = 100;
        bt_led_brightness(nv);
    }
    char num[8];
    int v = bt_led_brightness_get(), np = 0;
    char tmp[4];
    int tn = 0;
    if (v == 0) tmp[tn++] = '0';
    while (v) { tmp[tn++] = '0' + v % 10; v /= 10; }
    while (tn) num[np++] = tmp[--tn];
    num[np++] = '%';
    num[np] = 0;
    gfx_draw_string(trackX + trackW + 14 * S, ty, fb_rgb(220, 225, 235),
                    fb_rgb(30, 34, 46), num);

    int my2 = ty + 30 * S;
    int mw3 = (bw - 2 * gap) / 3;
    if (ui_button(x, my2, mw3, 34 * S, "Steady",
                  bt_led_mode_get() == BT_LED_STEADY, mx, my, clicked))
        bt_led_mode(BT_LED_STEADY);
    if (ui_button(x + mw3 + gap, my2, mw3, 34 * S, "Blink",
                  bt_led_mode_get() == BT_LED_BLINK, mx, my, clicked))
        bt_led_mode(BT_LED_BLINK);
    if (ui_button(x + 2 * (mw3 + gap), my2, mw3, 34 * S, "Breathe",
                  bt_led_mode_get() == BT_LED_BREATHE, mx, my, clicked))
        bt_led_mode(BT_LED_BREATHE);

    gfx_draw_string(x, W.y + W.h - 64 * S, dim,
                    fb_rgb(30, 34, 46),
                    "USB ports with devices are listed in USB Devices.");
    if (msg[0])
        gfx_draw_string(x, W.y + W.h - 40 * S, warn,
                        fb_rgb(30, 34, 46), msg);
    return 0;
}
