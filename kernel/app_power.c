#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ui.h>

static struct ui_win W;
static char msg[72];
static int inited;

static void pwr_off(void)
{
    __asm__ volatile("mov $0x2000,%%ax; mov $0x604,%%dx; outw %%ax,%%dx"
                     : : : "ax", "dx");
    __asm__ volatile("mov $0x3400,%%ax; mov $0x4004,%%dx; outw %%ax,%%dx"
                     : : : "ax", "dx");
    for (;;)
        __asm__ volatile("hlt");
}

static void pwr_reboot(void)
{
    uint8_t s = 2;
    for (int i = 0; i < 100000; i++) {
        __asm__ volatile("inb $0x64,%0" : "=a"(s));
        if (!(s & 2))
            break;
    }
    __asm__ volatile("mov $0xfe,%%al; mov $0x64,%%dx; outb %%al,%%dx"
                     : : : "ax", "dx");
    __asm__ volatile("mov $1,%%al; mov $0x92,%%dx; outb %%al,%%dx"
                     : : : "ax", "dx");
    for (;;)
        __asm__ volatile("hlt");
}

void power_open(void)
{
    ui_fit(480, 320, &W);
    msg[0] = 0;
    inited = 1;
}

int power_frame(int mx, int my, int clicked)
{
    if (!inited)
        power_open();
    int S = ui_scale();
    if (ui_window(&W, "Power", mx, my, clicked))
        return 1;

    int bx = W.x + 40 * S, bw = W.w - 80 * S, bh = 40 * S;
    if (ui_button(bx, W.y + 58 * S, bw, bh, "Shut down", 0, mx, my, clicked))
        pwr_off();
    if (ui_button(bx, W.y + 112 * S, bw, bh, "Restart", 0, mx, my, clicked))
        pwr_reboot();
    if (ui_button(bx, W.y + 166 * S, bw, bh, "Check for updates",
                  0, mx, my, clicked)) {
        const char *m = "Updates need the network stack (no NIC driver yet).";
        int p = 0;
        while (m[p] && p < 71) {
            msg[p] = m[p];
            p++;
        }
        msg[p] = 0;
    }
    if (msg[0])
        gfx_draw_string(W.x + 22 * S, W.y + W.h - 46 * S,
                        fb_rgb(240, 200, 120), fb_rgb(30, 34, 46), msg);
    return 0;
}
