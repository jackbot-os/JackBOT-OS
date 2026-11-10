#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ui.h>
#include <kernel/usb.h>
#include <kernel/i18n.h>
#include "i18n_data.h"
#include "../../drivers/usb/xhci.h"

static struct ui_win W;
static int inited;

static void append(char *s, const char *a)
{
    int p = 0;
    while (s[p]) p++;
    while (*a) s[p++] = *a++;
    s[p] = 0;
}

static void u2dec(uint32_t v, char *out)
{
    char tmp[16];
    int n = 0;
    if (v == 0) { out[0] = '0'; out[1] = 0; return; }
    while (v) { tmp[n++] = '0' + (v % 10); v /= 10; }
    int p = 0;
    while (n) out[p++] = tmp[--n];
    out[p] = 0;
}

static void hex4(uint16_t v, char *o)
{
    static const char hx[16] = "0123456789ABCDEF";
    for (int i = 0; i < 4; i++)
        o[3 - i] = hx[(v >> (i * 4)) & 0xF];
    o[4] = 0;
}

void usbdev_open(void)
{
    ui_fit(720, 540, &W);
    usb_scan();
    inited = 1;
}

int usbdev_frame(int mx, int my, int clicked)
{
    if (!inited)
        usbdev_open();
    int S = ui_scale();
    if (ui_window(&W, "USB Devices", mx, my, clicked))
        return 1;

    int bg = fb_rgb(30, 34, 46);
    int x0 = W.x + 22 * S;
    int y = W.y + 50 * S;
    int roww = W.w - 44 * S;

    if (ui_button(W.x + W.w - 110 * S, W.y + 44 * S, 88 * S, 30 * S,
                  "Refresh", 0, mx, my, clicked))
        usb_scan();

    int nhc = usb_hc_count();
    int ndev = usb_total_connected();
    char line[96];
    line[0] = 0;
    append(line, tr(STR_USB_CTRL));
    char t[12];
    u2dec((uint32_t)nhc, t);
    append(line, t);
    append(line, tr(STR_USB_CONN));
    u2dec((uint32_t)ndev, t);
    append(line, t);
    append(line, tr(STR_USB_UNITS));
    gfx_draw_string_wide(x0, y, fb_rgb(150, 210, 255), bg, line);
    y += 30 * S;

    if (nhc == 0) {
        gfx_draw_string_wide(x0, y + 20 * S, fb_rgb(232, 178, 104), bg,
                             tr(STR_USB_NO_HC));
        return 0;
    }

    for (int h = 0; h < nhc; h++) {
        const struct usb_hc *c = usb_hc_get(h);
        if (!c)
            continue;

        ui_rect(x0, y, roww, 26 * S, fb_rgb(42, 50, 68));
        char hv[8], dv[8];
        hex4(c->vendor, hv);
        hex4(c->device, dv);
        line[0] = 0;
        append(line, usb_kind_full_name(c->kind));
        append(line, "  ");
        append(line, hv);
        append(line, ":");
        append(line, dv);
        gfx_draw_string(x0 + 8 * S, y + 5 * S, fb_rgb(255, 208, 110), bg, line);
        y += 30 * S;

        line[0] = 0;
        append(line, tr(STR_USB_BUS));
        char n[8];
        u2dec(c->bus, n); append(line, n);
        append(line, ":");
        u2dec(c->dev, n); append(line, n);
        append(line, ".");
        u2dec(c->func, n); append(line, n);
        if (c->io_base) {
            append(line, "  I/O ");
            char io[8];
            u2dec((uint32_t)c->io_base, io);
            append(line, io);
        }
        if (c->mem_base) {
            append(line, "  MEM ");
            char mm[16];
            u2dec((uint32_t)c->mem_base, mm);
            append(line, mm);
        }
        append(line, "  IRQ ");
        u2dec(c->irq, n); append(line, n);
        gfx_draw_string(x0 + 8 * S, y, fb_rgb(170, 182, 206), bg, line);
        y += 24 * S;

        int shown = c->nports;
        if (shown > 10) shown = 10;
        for (int p = 0; p < shown; p++) {
            char num[8];
            u2dec((uint32_t)(p + 1), num);
            line[0] = 0;
            append(line, tr(STR_USB_PORT));
            append(line, num);
            if (c->conn[p]) {
                append(line, tr(STR_USB_PORT_CONN));
                append(line, usb_speed_name(c->speed[p]));
                gfx_draw_string_wide(x0 + 8 * S, y,
                                     fb_rgb(120, 222, 140), bg, line);
            } else {
                append(line, tr(STR_USB_PORT_EMPTY));
                gfx_draw_string_wide(x0 + 8 * S, y,
                                     fb_rgb(120, 130, 146), bg, line);
            }
            if (c->oc[p]) {
                gfx_draw_string_wide(x0 + roww - 90 * S, y,
                                     fb_rgb(235, 110, 110), bg,
                                     tr(STR_USB_OVERCURRENT));
            } else if (!c->pwr[p] && c->nports) {
                gfx_draw_string_wide(x0 + roww - 100 * S, y,
                                     fb_rgb(150, 150, 120), bg,
                                     tr(STR_USB_NO_POWER));
            }
            y += 22 * S;
        }
        if (c->nports > shown) {
            line[0] = 0;
            append(line, tr(STR_USB_OTHER));
            u2dec((uint32_t)(c->nports - shown), t);
            append(line, t);
            append(line, tr(STR_USB_SKIP));
            gfx_draw_string_wide(x0 + 8 * S, y, fb_rgb(140, 150, 168), bg, line);
            y += 22 * S;
        }
        y += 8 * S;
    }

    /* Devices the xHCI driver actually enumerated (descriptor read over EP0),
       as opposed to the port bits above which only say "something is plugged
       in". */
    int nde = xhci_dev_count();
    if (nde > 0) {
        gfx_draw_string_wide(x0, y, fb_rgb(150, 210, 255), bg,
                             "ENUMERATED DEVICES");
        y += 26 * S;
        for (int i = 0; i < nde; i++) {
            const struct usb_dev_info *d = xhci_dev_get(i);
            if (!d)
                continue;
            char hv[8], pv[8];
            hex4(d->vid, hv);
            hex4(d->pid, pv);
            line[0] = 0;
            append(line, hv);
            append(line, ":");
            append(line, pv);
            append(line, "  port ");
            char n2[8];
            u2dec(d->port, n2);
            append(line, n2);
            append(line, "  speed ");
            append(line, usb_speed_name(d->speed));
            gfx_draw_string(x0 + 8 * S, y, fb_rgb(255, 208, 110), bg, line);
            y += 24 * S;

            line[0] = 0;
            append(line, "class ");
            char n3[8];
            u2dec(d->dev_class, n3);
            append(line, n3);
            append(line, "  subclass ");
            u2dec(d->dev_subclass, n3);
            append(line, n3);
            append(line, "  slot ");
            u2dec(d->slot, n3);
            append(line, n3);
            gfx_draw_string(x0 + 16 * S, y, fb_rgb(170, 182, 206), bg, line);
            y += 26 * S;
        }
    }

    gfx_draw_string_wide(x0, W.y + W.h - 40 * S, fb_rgb(140, 150, 168), bg,
                         tr(STR_USB_FOOTER));
    return 0;
}
