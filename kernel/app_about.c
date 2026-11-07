#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ui.h>
#include <kernel/i18n.h>
#include "i18n_data.h"

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
                         "JackBOT OS  v1.0.9");
    y += 40 * S;
    gfx_draw_string_wide(x0, y, fb_rgb(200, 210, 226), bg,
                         tr(STR_ABOUT_SUBTITLE));
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
        tr(STR_ABOUT_FEAT1),
        tr(STR_ABOUT_FEAT2),
        tr(STR_ABOUT_FEAT3),
        tr(STR_ABOUT_FEAT4),
        tr(STR_ABOUT_FEAT5),
        tr(STR_ABOUT_FEAT6),
    };
    for (int i = 0; i < 6; i++) {
        gfx_draw_string_wide(x0, y, fb_rgb(180, 192, 214), bg, feat[i]);
        y += 30 * S;
    }

    y += 10 * S;
    gfx_draw_string_wide(x0, y, fb_rgb(90, 150, 255), bg,
                         tr(STR_ABOUT_THIRD));
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
                         tr(STR_ABOUT_FOOTER));
    return 0;
}
