#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ui.h>
#include <kernel/acpi.h>
#include <kernel/persist.h>
#include <kernel/session.h>
#include <kernel/serial.h>

static struct ui_win W;
static char msg[72];
static int inited;

extern uint64_t timer_get_ticks(void);

static void pwr_banner(const char *l1, uint32_t c1, const char *l2, uint32_t c2)
{
    int w = (int)fb_width();
    int h = (int)fb_height();
    gfx_set_scale(w >= 1600 ? 2 : 1);
    fb_clear(fb_rgb(10, 14, 26));
    int tw = gfx_text_width(l1);
    gfx_draw_string((w - tw) / 2, h / 2 - 42, c1, 0, l1);
    if (l2 && l2[0]) {
        tw = gfx_text_width(l2);
        gfx_draw_string((w - tw) / 2, h / 2 + 8, c2, 0, l2);
    }
    fb_present_all();
}

static void pwr_settle(uint64_t ticks)
{
    uint64_t end = timer_get_ticks() + ticks;
    while (timer_get_ticks() < end)
        __asm__ volatile("pause");
}

/* Save what can be saved, say honestly what happened, then cut the power.
   This is not ACPI S4: without a swap area the kernel cannot freeze RAM, so
   the session snapshot on disk is what survives a power cut. */
static void pwr_off(void)
{
    char line[80];

    pwr_banner("Saving your session", fb_rgb(238, 244, 255),
               "flushing disk cache and writing the snapshot", fb_rgb(150, 168, 200));

    int flushed = persist_flush(400);
    int saved = session_save();

    if (saved >= 0) {
        char *p = line;
        const char *a = "snapshot written, ";
        while (*a)
            *p++ = *a++;
        char n[8];
        int i = 0, v = saved;
        if (!v)
            n[i++] = '0';
        while (v && i < 7) {
            n[i++] = (char)('0' + (v % 10));
            v /= 10;
        }
        while (i)
            *p++ = n[--i];
        const char *b = " bytes on the boot volume";
        while (*b)
            *p++ = *b++;
        *p = 0;
        serial_puts("[PWR] session saved bytes=");
        serial_puthex((uint64_t)saved);
        serial_putc('\n');
        pwr_banner("Shutting down", fb_rgb(238, 244, 255), line,
                   fb_rgb(150, 210, 170));
    } else {
        const char *m = (saved == -1)
            ? "no writable volume, so nothing could be saved"
            : "snapshot write failed, nothing was saved";
        serial_puts("[PWR] session not saved (");
        serial_puthex((uint64_t)(saved == -1 ? 1 : 2));
        serial_puts(")\n");
        pwr_banner("Shutting down", fb_rgb(238, 244, 255), m,
                   fb_rgb(230, 170, 120));
    }
    if (flushed < 0)
        serial_puts("[PWR] disk cache flush timed out\n");

    pwr_settle(120);

    /* 优先走真实 ACPI S5（FADT PM1 控制块）；失败再回退虚拟机端口。 */
    if (acpi_poweroff() != 0) {
        __asm__ volatile("mov $0x2000,%%ax; mov $0x604,%%dx; outw %%ax,%%dx"
                         : : : "ax", "dx");
        __asm__ volatile("mov $0x3400,%%ax; mov $0x4004,%%dx; outw %%ax,%%dx"
                         : : : "ax", "dx");
    }
    __asm__ volatile("cli");
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
