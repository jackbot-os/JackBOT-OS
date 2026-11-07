#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ui.h>
#include <kernel/pci.h>
#include <kernel/i18n.h>
#include "i18n_data.h"

static struct ui_win W;
static int inited;
static int sel;
static int scroll;

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

static void hex8(uint32_t v, char *o)
{
    for (int i = 0; i < 8; i++) {
        uint32_t n = (v >> ((7 - i) * 4)) & 0xF;
        o[i] = (char)(n < 10 ? '0' + n : 'A' + n - 10);
    }
    o[8] = 0;
}

static const char *class_name(uint8_t c, uint8_t s)
{
    switch (c) {
    case 0x01:
        if (s == 0x01) return tr(STR_PC_IDE);
        if (s == 0x06) return tr(STR_PC_SATA);
        if (s == 0x08) return tr(STR_PC_NVME);
        return tr(STR_PC_STORAGE);
    case 0x02:
        return s == 0x00 ? tr(STR_PC_ETHERNET) : tr(STR_PC_NETWORK);
    case 0x03:
        if (s == 0x00) return tr(STR_PC_VGA);
        if (s == 0x02) return tr(STR_PC_3D);
        return tr(STR_PC_DISPLAY);
    case 0x04:
        return tr(STR_PC_AUDIO);
    case 0x05:
        return tr(STR_PC_MEMORY);
    case 0x06:
        if (s == 0x00) return tr(STR_PC_HOST_BRIDGE);
        if (s == 0x01) return tr(STR_PC_ISA);
        if (s == 0x04) return tr(STR_PC_PCI_BRIDGE);
        return tr(STR_PC_BRIDGE);
    case 0x07:
        return tr(STR_PC_COMM);
    case 0x08:
        return tr(STR_PC_SYS_PERIPH);
    case 0x09:
        return tr(STR_PC_INPUT);
    case 0x0C:
        if (s == 0x03) return tr(STR_PC_USB);
        if (s == 0x05) return tr(STR_PC_SMBUS);
        return tr(STR_PC_SERIAL_BUS);
    case 0x0D:
        return tr(STR_PC_WIRELESS);
    case 0x11:
        return tr(STR_PC_CRYPTO);
    case 0x12:
        return tr(STR_PC_SIGNAL);
    default:
        return tr(STR_PC_OTHER);
    }
}

void devices_open(void)
{
    ui_fit(780, 560, &W);
    sel = -1;
    scroll = 0;
    inited = 1;
}

int devices_frame(int mx, int my, int clicked)
{
    if (!inited)
        devices_open();
    int S = ui_scale();
    if (ui_window(&W, "Device Manager", mx, my, clicked))
        return 1;

    int bg = fb_rgb(30, 34, 46);
    int x0 = W.x + 20 * S;
    int roww = W.w - 40 * S;
    int nd = pci_count();

    int y = W.y + 48 * S;
    char line[128];
    char t[12];
    line[0] = 0;
    append(line, tr(STR_DEV_PCI));
    u2dec((uint32_t)nd, t);
    append(line, t);
    append(line, tr(STR_DEV_CLICK));
    gfx_draw_string_wide(x0, y, fb_rgb(150, 210, 255), bg, line);
    y += 8 * S;

    const int ROWS = 13;
    int rh = 30 * S;
    int top = y;
    for (int r = 0; r < ROWS; r++) {
        int idx = scroll + r;
        if (idx >= nd)
            break;
        struct pci_dev *d = pci_get(idx);
        int ry = top + r * rh;
        int hl = (idx == sel);
        ui_rect(x0, ry, roww, rh - 3 * S,
                hl ? fb_rgb(56, 78, 116) : fb_rgb(38, 44, 60));
        int ty = ry + 6 * S;
        gfx_draw_string_wide(x0 + 8 * S, ty,
                             hl ? fb_rgb(255, 224, 150) : fb_rgb(224, 232, 244),
                             bg, class_name(d->class_code, d->subclass));
        char hv[8], dv[8];
        hex4(d->vendor, hv);
        hex4(d->device, dv);
        line[0] = 0;
        append(line, hv);
        append(line, ":");
        append(line, dv);
        gfx_draw_string(x0 + 300 * S, ty, fb_rgb(168, 182, 208), bg, line);
        line[0] = 0;
        u2dec((uint32_t)d->bus, t);
        append(line, t);
        append(line, ":");
        u2dec((uint32_t)d->dev, t);
        append(line, t);
        append(line, ".");
        u2dec((uint32_t)d->func, t);
        append(line, t);
        append(line, "  IRQ ");
        u2dec((uint32_t)d->irq_line, t);
        append(line, t);
        gfx_draw_string(x0 + 400 * S, ty, fb_rgb(150, 162, 186), bg, line);
        if (clicked && ui_inside(mx, my, x0, ry, roww, rh - 3 * S))
            sel = idx;
    }

    int by = top + ROWS * rh + 2 * S;
    if (ui_button(x0, by, 90 * S, 26 * S, "Up", 0, mx, my, clicked)) {
        if (scroll > 0) scroll--;
    }
    if (ui_button(x0 + 100 * S, by, 90 * S, 26 * S, "Down", 0, mx, my,
                  clicked)) {
        if (scroll + ROWS < nd) scroll++;
    }
    if (sel >= 0 && sel < nd) {
        struct pci_dev *d = pci_get(sel);
        char hv[8], dv[8];
        hex4(d->vendor, hv);
        hex4(d->device, dv);
        int dy = by + 36 * S;
        line[0] = 0;
        append(line, hv);
        append(line, ":");
        append(line, dv);
        append(line, "  ");
        append(line, class_name(d->class_code, d->subclass));
        gfx_draw_string_wide(x0, dy, fb_rgb(200, 212, 230), bg, line);
        dy += 22 * S;
        int drew = 0;
        line[0] = 0;
        for (int i = 0; i < 6 && drew < 3; i++) {
            if (!(d->bar[i] & 0x1) && d->bar[i]) {
                uint32_t m = pci_bar_mem(d, i);
                if (m) {
                    append(line, "BAR");
                    u2dec((uint32_t)i, t);
                    append(line, t);
                    append(line, "=MEM 0x");
                    char h[16];
                    hex8(m, h);
                    append(line, h);
                    append(line, "  ");
                    drew++;
                }
            } else if (d->bar[i]) {
                uint32_t io = pci_bar_io(d, i);
                if (io) {
                    append(line, "BAR");
                    u2dec((uint32_t)i, t);
                    append(line, t);
                    append(line, "=IO 0x");
                    hex4((uint16_t)io, hv);
                    append(line, hv);
                    append(line, "  ");
                    drew++;
                }
            }
        }
        if (!line[0])
            append(line, tr(STR_DEV_NO_RES));
        gfx_draw_string_wide(x0, dy, fb_rgb(160, 176, 204), bg, line);
    }
    return 0;
}
