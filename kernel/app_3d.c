#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ui.h>
#include <kernel/gpu3d.h>
#include <kernel/i18n.h>
#include "i18n_data.h"

#define VW 400
#define VH 270

static struct ui_win W;
static int inited, ready;
static uint16_t ang_x, ang_y;

void gpu3d_open(void)
{
    ui_fit(460, 350, &W);
    ready = g3d_init(VW, VH);
    inited = 1;
}

int gpu3d_frame(int mx, int my, int clicked)
{
    if (!inited)
        gpu3d_open();
    int S = ui_scale();
    if (ui_window(&W, tr(STR_WT_3D), mx, my, clicked))
        return 1;

    int vx = W.x + (W.w - VW) / 2;
    int vy = W.y + 36 * S;

    ui_rect(vx, vy, VW, VH, fb_rgb(12, 14, 22));
    ui_border(vx, vy, VW, VH, fb_rgb(70, 90, 130));

    if (ready) {
        g3d_render(g3d_cube(65536), vx, vy, ang_x, ang_y);
        ang_x += 280;
        ang_y += 420;
    } else {
        gfx_draw_string(vx + 20, vy + VH / 2,
                        fb_rgb(230, 120, 120), fb_rgb(12, 14, 22),
                        tr(STR_3D_OOM));
    }

    if (ready)
        gfx_draw_string(vx, vy + VH + 12 * S,
                        fb_rgb(170, 190, 220), fb_rgb(30, 34, 46),
                        tr(STR_3D_PIPELINE));
    return 0;
}
