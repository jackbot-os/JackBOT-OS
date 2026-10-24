#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ui.h>
#include <kernel/acpi.h>

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
                                 "检测到电池设备（控制法电池 PNP0C0A）");
            gfx_draw_string_wide(x0 + 12 * S, y + 34 * S,
                                 fb_rgb(200, 210, 220), bg,
                                 "电量百分比/充电状态需要 AML 解释器，开发中");
        } else {
            ui_rect(x0, y, W.w - 52 * S, 56 * S, fb_rgb(44, 48, 58));
            gfx_draw_string_wide(x0 + 12 * S, y + 10 * S,
                                 fb_rgb(150, 210, 255), bg,
                                 "未检测到电池：台式机 / 虚拟机，使用外接电源");
            gfx_draw_string_wide(x0 + 12 * S, y + 34 * S,
                                 fb_rgb(160, 170, 190), bg,
                                 "笔记本实机上会在此显示电池与充电状态");
        }
    } else {
        gfx_draw_string_wide(x0, y, fb_rgb(232, 178, 104), bg,
                             "未找到 ACPI 表（电源信息不可用）");
    }
    y += 76 * S;

    if (a->found) {
        line[0] = 0;
        append(line, "ACPI 修订 ");
        char t[8];
        u2dec((uint32_t)a->revision, t);
        append(line, t);
        append(line, "，表数量 ");
        u2dec((uint32_t)a->table_count, t);
        append(line, t);
        append(line, "，SCI 中断 ");
        u2dec((uint32_t)a->sci_int, t);
        append(line, t);
        gfx_draw_string_wide(x0, y, fb_rgb(200, 210, 226), bg, line);
        y += 26 * S;

        line[0] = 0;
        append(line, "嵌入式控制器(EC)：");
        append(line, a->has_ec ? "检测到" : "无");
        append(line, "    交流电源：");
        append(line, a->has_ac_adapter ? "检测到" : "无");
        gfx_draw_string_wide(x0, y, fb_rgb(200, 210, 226), bg, line);
        y += 26 * S;

        line[0] = 0;
        append(line, "PM1 电源控制块：");
        if (a->pm1a_cnt) {
            char mm[16];
            u2dec(a->pm1a_cnt, mm);
            append(line, "0x");
            append(line, mm);
        } else {
            append(line, "未取得");
        }
        gfx_draw_string_wide(x0, y, fb_rgb(200, 210, 226), bg, line);
        y += 26 * S;

        line[0] = 0;
        append(line, "软关机方式：");
        append(line, a->slp_typa != 0xFF ? "ACPI S5 睡眠寄存器（真实）"
                                        : "ACPI 枚举 SLP_TYP（兼容）");
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
                         "关机优先向 FADT 的 PM1 控制块写 SLP_TYP|SLP_EN，兼容虚拟机与真机。");
    return 0;
}
