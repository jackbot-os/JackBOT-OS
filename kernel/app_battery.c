#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ui.h>
#include <kernel/acpi.h>
#include <kernel/i18n.h>
#include "i18n_data.h"

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

static void do_poweroff(void)
{
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

static void do_reboot(void)
{
    __asm__ volatile("mov $0xfe,%%al; mov $0x64,%%dx; outb %%al,%%dx"
                     : : : "ax", "dx");
    __asm__ volatile("mov $1,%%al; mov $0x92,%%dx; outb %%al,%%dx"
                     : : : "ax", "dx");
    __asm__ volatile("cli");
    for (;;)
        __asm__ volatile("hlt");
}

void battery_open(void)
{
    ui_fit(600, 460, &W);
    inited = 1;
}

int battery_frame(int mx, int my, int clicked)
{
    if (!inited)
        battery_open();
    int S = ui_scale();
    if (ui_window(&W, "Battery & Power", mx, my, clicked))
        return 1;

    int bg = fb_rgb(30, 34, 46);
    int x0 = W.x + 26 * S;
    int y = W.y + 52 * S;
    char line[96];

    const struct acpi_info *a = acpi_get();

    if (a->found) {
        if (a->has_battery) {
            ui_rect(x0, y, W.w - 52 * S, 56 * S, fb_rgb(40, 56, 44));
            gfx_draw_string_wide(x0 + 12 * S, y + 10 * S,
                                 fb_rgb(120, 222, 140), bg,
                                 tr(STR_BAT_DETECTED));
            gfx_draw_string_wide(x0 + 12 * S, y + 34 * S,
                                 fb_rgb(200, 210, 220), bg,
                                 tr(STR_BAT_AML));
        } else {
            ui_rect(x0, y, W.w - 52 * S, 56 * S, fb_rgb(44, 48, 58));
            gfx_draw_string_wide(x0 + 12 * S, y + 10 * S,
                                 fb_rgb(150, 210, 255), bg,
                                 tr(STR_BAT_NO_BATTERY));
            gfx_draw_string_wide(x0 + 12 * S, y + 34 * S,
                                 fb_rgb(160, 170, 190), bg,
                                 tr(STR_BAT_LAPTOP_HINT));
        }
    } else {
        gfx_draw_string_wide(x0, y, fb_rgb(232, 178, 104), bg,
                             tr(STR_BAT_NO_ACPI));
    }
    y += 76 * S;

    if (a->found) {
        line[0] = 0;
        append(line, tr(STR_BAT_REV));
        char t[8];
        u2dec((uint32_t)a->revision, t);
        append(line, t);
        append(line, tr(STR_BAT_TABLES));
        u2dec((uint32_t)a->table_count, t);
        append(line, t);
        append(line, tr(STR_BAT_SCI));
        u2dec((uint32_t)a->sci_int, t);
        append(line, t);
        gfx_draw_string_wide(x0, y, fb_rgb(200, 210, 226), bg, line);
        y += 26 * S;

        line[0] = 0;
        append(line, tr(STR_BAT_EC));
        append(line, a->has_ec ? tr(STR_BAT_PRESENT) : tr(STR_BAT_NONE));
        append(line, tr(STR_BAT_AC));
        append(line, a->has_ac_adapter ? tr(STR_BAT_PRESENT) : tr(STR_BAT_NONE));
        gfx_draw_string_wide(x0, y, fb_rgb(200, 210, 226), bg, line);
        y += 26 * S;

        line[0] = 0;
        append(line, tr(STR_BAT_PM1));
        if (a->pm1a_cnt) {
            char mm[16];
            u2dec(a->pm1a_cnt, mm);
            append(line, "0x");
            append(line, mm);
        } else {
            append(line, tr(STR_BAT_UNAVAILABLE));
        }
        gfx_draw_string_wide(x0, y, fb_rgb(200, 210, 226), bg, line);
        y += 26 * S;

        line[0] = 0;
        append(line, tr(STR_BAT_WAY));
        append(line, a->slp_typa != 0xFF ? tr(STR_BAT_S5)
                                        : tr(STR_BAT_ENUM));
        gfx_draw_string_wide(x0, y, fb_rgb(120, 222, 140), bg, line);
        y += 40 * S;
    }

    int bw = (W.w - 64 * S) / 2;
    if (ui_button(x0, y, bw, 40 * S, "Shut down (ACPI)", 0, mx, my, clicked))
        do_poweroff();
    if (ui_button(x0 + bw + 12 * S, y, bw, 40 * S, "Restart",
                  0, mx, my, clicked))
        do_reboot();

    gfx_draw_string_wide(x0, W.y + W.h - 36 * S, fb_rgb(140, 150, 168), bg,
                         tr(STR_BAT_FOOTER));
    return 0;
}
