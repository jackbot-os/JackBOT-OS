#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ui.h>
#include <kernel/ps2.h>
#include <kernel/ac97.h>

static struct ui_win W;
static char msg[80];
static int inited;
static int vol = 70;
static int dragging;

void audio_open(void)
{
    ui_fit(600, 420, &W);
    msg[0] = 0;
    dragging = 0;
    inited = 1;
}

static void vol_num(int v, char *out)
{
    char tmp[4];
    int n = 0;
    if (v == 0)
        tmp[n++] = '0';
    while (v) {
        tmp[n++] = '0' + v % 10;
        v /= 10;
    }
    int p = 0;
    for (int i = n - 1; i >= 0; i--)
        out[p++] = tmp[i];
    out[p++] = '%';
    out[p] = 0;
}

int audio_frame(int mx, int my, int clicked)
{
    if (!inited)
        audio_open();
    int S = ui_scale();
    if (ui_window(&W, "Audio", mx, my, clicked))
        return 1;

    int x = W.x + 30 * S, y = W.y + 56 * S;
    int bw = W.w - 60 * S;
    int have = ac97_present();
    gfx_draw_string(x, y, have ? fb_rgb(150, 230, 170) : fb_rgb(240, 200, 120),
        fb_rgb(30, 34, 46),
        have ? "Sound card: Intel AC97 (48000 Hz, stereo 16-bit)."
             : "No sound card: AC97/HD Audio driver not built yet.");

    int trackY = y + 72 * S, trackH = 12 * S;
    int trackX = x + 80 * S, trackW = bw - 160 * S;
    gfx_draw_string(x, trackY - 4 * S, fb_rgb(150, 210, 255),
                    fb_rgb(30, 34, 46), "Volume");
    ui_rect(trackX, trackY, trackW, trackH, fb_rgb(50, 56, 72));
    int hx = trackX + (int)((long)trackW * vol / 100);
    ui_rect(hx - 6 * S, trackY - 8 * S, 12 * S, trackH + 16 * S,
            fb_rgb(120, 160, 220));
    char num[8];
    vol_num(vol, num);
    gfx_draw_string(trackX + trackW + 14 * S, trackY - 4 * S,
                    fb_rgb(220, 225, 235), fb_rgb(30, 34, 46), num);

    int down = ps2_mouse_down();
    if (down && ui_inside(mx, my, trackX - 10 * S, trackY - 10 * S,
                          trackW + 20 * S, trackH + 20 * S))
        dragging = 1;
    if (!down)
        dragging = 0;
    if (dragging) {
        int nv = (int)((long)(mx - trackX) * 100 / trackW);
        if (nv < 0) nv = 0;
        if (nv > 100) nv = 100;
        vol = nv;
    }

    int dy2 = trackY + 64 * S;
    gfx_draw_string(x, dy2, fb_rgb(150, 210, 255), fb_rgb(30, 34, 46),
                    "Output devices:");
    ui_rect(x, dy2 + 28 * S, bw, 34 * S, fb_rgb(38, 44, 60));
    gfx_draw_string(x + 14 * S, dy2 + 37 * S, fb_rgb(180, 230, 200),
                    fb_rgb(38, 44, 60),
                    have ? "AC97 codec (SigmaTel STAC9700)"
                         : "(none detected)");

    if (msg[0])
        gfx_draw_string(x, W.y + W.h - 122 * S, fb_rgb(240, 200, 120),
                        fb_rgb(30, 34, 46), msg);

    int gap = 20 * S;
    int half = (bw - gap) / 2;
    int by = W.y + W.h - 78 * S;
    if (have && ui_button(x, by, half, 38 * S,
                          "Play boot", 0, mx, my, clicked)) {
        const char *m = "Playing boot sound...";
        int q = 0;
        while (m[q] && q < 70) { msg[q] = m[q]; q++; }
        msg[q] = 0;
        ac97_play_boot();
        const char *d = "Boot sound finished.";
        q = 0;
        while (d[q] && q < 70) { msg[q] = d[q]; q++; }
        msg[q] = 0;
        return 0;
    }
    if (have && ui_button(x + half + gap, by, half, 38 * S,
                          "Play warn", 0, mx, my, clicked)) {
        const char *m = "Playing warning sound...";
        int q = 0;
        while (m[q] && q < 70) { msg[q] = m[q]; q++; }
        msg[q] = 0;
        ac97_play_warn();
        const char *d = "Warning sound finished.";
        q = 0;
        while (d[q] && q < 70) { msg[q] = d[q]; q++; }
        msg[q] = 0;
        return 0;
    }
    if (!have && ui_button(x, by, bw, 38 * S,
                           "Test sound", 0, mx, my, clicked)) {
        const char *m = "No audio device to play a test tone.";
        int q = 0;
        while (m[q] && q < 70) {
            msg[q] = m[q];
            q++;
        }
        msg[q] = 0;
    }
    return 0;
}
