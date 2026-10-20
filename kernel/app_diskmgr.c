#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ui.h>
#include <kernel/part.h>

static struct ui_win W;
static struct part_layout PL;
static int loaded;

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

static const char *type_cn(uint8_t t)
{
    if (part_type_is_fat(t)) return "FAT 分区";
    if (t == PART_TYPE_NTFS) return "NTFS / exFAT";
    if (part_type_is_extended(t)) return "扩展分区";
    if (t == PART_TYPE_LINUX) return "Linux 分区";
    if (t == PART_TYPE_LINUXSWAP) return "Linux 交换";
    return "其他系统";
}

static void load_layout(void)
{
    static uint8_t mbr[512];
    loaded = 0;
    if (part_read_mbr(mbr) == 0 && part_parse(mbr, &PL) == 0)
        loaded = 1;
}

void diskmgr_open(void)
{
    ui_fit(800, 500, &W);
    load_layout();
}

int diskmgr_frame(int mx, int my, int clicked)
{
    if (!loaded)
        diskmgr_open();
    int S = ui_scale();
    if (ui_window(&W, "This PC", mx, my, clicked))
        return 1;

    int bg = fb_rgb(30, 34, 46);
    int x0 = W.x + 26 * S;
    int y0 = W.y + 52 * S;

    gfx_draw_string_wide(x0, y0, fb_rgb(150, 168, 200), bg,
                         "盘符     类型                容量        启动   状态");
    ui_rect(x0, y0 + 22 * S, W.w - 52 * S, 2 * S, fb_rgb(70, 84, 110));

    char letter = 'C';
    int row = 0;
    for (int i = 0; i < PL.count; i++) {
        struct part_entry *e = &PL.e[i];
        int isext = part_type_is_extended(e->type);
        int isfat = part_type_is_fat(e->type);
        int y = y0 + 36 * S + row * 42 * S;

        char dl[8];
        if (isext) {
            dl[0] = '-'; dl[1] = '-'; dl[2] = 0;
        } else {
            dl[0] = letter++; dl[1] = ':'; dl[2] = 0;
        }
        gfx_draw_string_wide(x0, y, fb_rgb(255, 208, 110), bg, dl);

        gfx_draw_string_wide(x0 + 95 * S, y, fb_rgb(222, 230, 242), bg,
                             type_cn(e->type));

        uint32_t mb = (uint32_t)(((uint64_t)e->num_sectors) / 2048);
        char num[16];
        u2dec(mb, num);
        char cap[24];
        int q = 0;
        for (int k = 0; num[k] && q < 16; k++) cap[q++] = num[k];
        cap[q++] = ' '; cap[q++] = 'M'; cap[q++] = 'B'; cap[q] = 0;
        gfx_draw_string_wide(x0 + 310 * S, y, fb_rgb(200, 210, 226), bg, cap);

        int active = e->boot == 0x80;
        gfx_draw_string_wide(x0 + 470 * S, y,
                             active ? fb_rgb(120, 222, 140) : fb_rgb(120, 130, 146),
                             bg, active ? "是" : "否");
        gfx_draw_string_wide(x0 + 550 * S, y,
                             isfat ? fb_rgb(120, 222, 140) : fb_rgb(232, 178, 104),
                             bg, isfat ? "可访问" : "仅可见");
        row++;
    }

    if (!PL.count)
        gfx_draw_string_wide(x0, y0 + 60 * S, fb_rgb(232, 150, 150), bg,
                             "未检测到可用分区（可能没有安装硬盘）");

    int fy = W.y + W.h - 66 * S;
    gfx_draw_string_wide(x0, fy, fb_rgb(150, 166, 192), bg,
                         "说明：盘符从 C 开始分配；Windows/NTFS、Linux、扩展分区等其他系统分区都会列出。");
    gfx_draw_string_wide(x0, fy + 22 * S, fb_rgb(150, 166, 192), bg,
                         "FAT 分区可读写；其他格式当前仅可见，写入支持在开发中。");
    return 0;
}
