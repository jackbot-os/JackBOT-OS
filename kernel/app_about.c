#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ui.h>

static struct ui_win W;
static int inited;

void about_open(void)
{
    ui_fit(640, 620, &W);
    inited = 1;
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

int about_frame(int mx, int my, int clicked)
{
    if (!inited)
        about_open();
    int S = ui_scale();
    if (ui_window(&W, "About JackBOT", mx, my, clicked))
        return 1;

    int bg = fb_rgb(30, 34, 46);
    int x0 = W.x + 40 * S;
    int y = W.y + 56 * S;

    gfx_draw_string_wide(x0, y, fb_rgb(90, 150, 255), bg,
                         "JackBOT OS  v1.0.2");
    y += 40 * S;
    gfx_draw_string_wide(x0, y, fb_rgb(200, 210, 226), bg,
                         "自研 64 位操作系统  x86_64 架构");
    y += 30 * S;

    char res[40];
    u2dec(fb_width(), res);
    int p = 0;
    while (res[p]) p++;
    const char *mid = " x ";
    for (int k = 0; mid[k]; k++) res[p++] = mid[k];
    u2dec(fb_height(), res + p);
    gfx_draw_string_wide(x0, y, fb_rgb(200, 210, 226), bg, res);
    y += 44 * S;

    const char *feat[] = {
        "· 中文图形界面，真矢量字体抗锯齿",
        "· 图形化安装向导：真实分区与格式化",
        "· 盘符系统，可识别 NTFS / Linux 等分区",
        "· 鼠标、窗口、多任务与进程隔离",
        "· 网络协议栈与内置浏览器",
        "· 应用商店、截图、壁纸与主题",
    };
    for (int i = 0; i < 6; i++) {
        gfx_draw_string_wide(x0, y, fb_rgb(180, 192, 214), bg, feat[i]);
        y += 30 * S;
    }

    y += 10 * S;
    gfx_draw_string_wide(x0, y, fb_rgb(90, 150, 255), bg,
                         "Third-party components / 第三方组件");
    y += 28 * S;
    const char *lic[] = {
        "CPython 3.12.7 Interpreter - Original software by the",
        "Python Software Foundation; licensed under the PSF",
        "License (Python-2.0). Sources kept unmodified under",
        "third_party/cpython; JackBOT port layer: ports/jackbot.",
        "Fonts: DejaVu & Droid Sans Fallback, Noto Naskh Arabic,",
        "NanumGothic, Thai TLWG Garuda, Lohit Devanagari - under",
        "their own free licenses (Bitstream Vera / Apache / OFL).",
    };
    for (int i = 0; i < 7; i++) {
        gfx_draw_string_wide(x0, y, fb_rgb(150, 160, 182), bg, lic[i]);
        y += 24 * S;
    }

    y += 10 * S;
    gfx_draw_string_wide(x0, y, fb_rgb(140, 150, 172), bg,
                         "JackBOT (c) 2026  仅用于学习与虚拟机测试");
    return 0;
}
