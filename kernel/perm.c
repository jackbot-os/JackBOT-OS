#include <stdint.h>
#include <stddef.h>
#include <kernel/config.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ui.h>
#include <kernel/i18n.h>
#include "i18n_data.h"
#include <kernel/perm.h>

int perm_granted(int which)
{
    switch (which) {
    case PERM_MIC: return config_mic_perm();
    case PERM_VOICE: return config_voice_perm();
    case PERM_CAMERA: return config_cam_perm();
    case PERM_LOCATION: return config_loc_perm();
    case PERM_NETWORK: return config_net_perm();
    }
    return 0;
}

void perm_allow(int which)
{
    switch (which) {
    case PERM_MIC: config_set_mic_perm(1); break;
    case PERM_VOICE: config_set_voice_perm(1); break;
    case PERM_CAMERA: config_set_cam_perm(1); break;
    case PERM_LOCATION: config_set_loc_perm(1); break;
    case PERM_NETWORK: config_set_net_perm(1); break;
    }
    config_save();
}

void perm_deny(int which)
{
    switch (which) {
    case PERM_MIC: config_set_mic_perm(0); break;
    case PERM_VOICE: config_set_voice_perm(0); break;
    case PERM_CAMERA: config_set_cam_perm(0); break;
    case PERM_LOCATION: config_set_loc_perm(0); break;
    case PERM_NETWORK: config_set_net_perm(0); break;
    }
    config_save();
}

static const char *perm_label(int which)
{
    switch (which) {
    case PERM_MIC: return tr(STR_PERM_MIC);
    case PERM_VOICE: return tr(STR_PERM_VOICE);
    case PERM_CAMERA: return tr(STR_PERM_CAMERA);
    case PERM_LOCATION: return tr(STR_PERM_LOCATION);
    case PERM_NETWORK: return tr(STR_PERM_NETWORK);
    }
    return "";
}

int perm_prompt(int which, int win_x, int win_y, int win_w, int win_h,
                int mx, int my, int clicked)
{
    int S = ui_scale();
    int dlg_w = win_w * 3 / 5;
    if (dlg_w < 280 * S) dlg_w = 280 * S;
    if (dlg_w > win_w) dlg_w = win_w;
    int dlg_h = 180 * S;
    int dx = win_x + (win_w - dlg_w) / 2;
    int dy = win_y + (win_h - dlg_h) / 2;

    /* dim the window content behind the modal */
    for (int yy = 0; yy < win_h; yy += 2)
        ui_rect(win_x, win_y + yy, win_w, 1, fb_rgb(8, 10, 16));

    int rad = 8 * S;
    ui_round_rect(dx, dy, dlg_w, dlg_h, rad, fb_rgb(30, 34, 48));
    ui_round_border(dx, dy, dlg_w, dlg_h, rad, fb_rgb(120, 150, 210));

    gfx_draw_string(dx + 16 * S, dy + 14 * S,
                    fb_rgb(235, 240, 255), fb_rgb(30, 34, 48),
                    tr(STR_PERMISSION_TITLE));

    /* body: "wants to use <resource>" */
    char body[96];
    int p = 0;
    const char *req = tr(STR_PERMISSION_REQUEST);
    while (*req && p < 60) body[p++] = *req++;
    body[p++] = ' ';
    const char *lab = perm_label(which);
    while (*lab && p < 92) body[p++] = *lab++;
    body[p] = 0;
    gfx_draw_string(dx + 16 * S, dy + 52 * S,
                    fb_rgb(205, 215, 235), fb_rgb(30, 34, 48), body);

    int bw = (dlg_w - 48 * S) / 2;
    int by = dy + dlg_h - 52 * S;
    if (ui_button(dx + 16 * S, by, bw, 36 * S,
                  tr(STR_PERMISSION_ALLOW), 0, mx, my, clicked)) {
        perm_allow(which);
        return 0;
    }
    if (ui_button(dx + 16 * S + bw + 16 * S, by, bw, 36 * S,
                  tr(STR_PERMISSION_DENY), 0, mx, my, clicked)) {
        perm_deny(which);
        return 0;
    }
    return 1;
}
