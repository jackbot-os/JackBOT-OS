#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ui.h>
#include <kernel/ps2.h>
#include <kernel/voice.h>
#include <kernel/ac97.h>
#include <kernel/perm.h>

static struct ui_win W;
static int inited;
static char input[96];
static int inlen;
static char shown_reply[160];
static int mic_ok;
static int ask_perm;

void voice_open(void)
{
    ui_fit(620, 480, &W);
    inited = 1;
    inlen = 0;
    input[0] = 0;
    shown_reply[0] = 0;
    mic_ok = ac97_present();
    ask_perm = 0;
}

static void read_keys(void)
{
    char c;
    while (ps2_read_key(&c)) {
        if (c == '\r' || c == '\n') {
            if (inlen > 0) {
                input[inlen] = 0;
                if (voice_submit(input)) {
                    const char *r = voice_reply();
                    int p = 0;
                    while (r[p] && p < (int)sizeof(shown_reply) - 1) {
                        shown_reply[p] = r[p];
                        p++;
                    }
                    shown_reply[p] = 0;
                }
                inlen = 0;
                input[0] = 0;
            }
        } else if (c == '\b' || c == 0x7f) {
            if (inlen > 0)
                input[--inlen] = 0;
        } else if (c >= 32 && inlen < (int)sizeof(input) - 1) {
            input[inlen++] = c;
            input[inlen] = 0;
        }
    }
}

/* word wrap a string into up to maxlines rows of maxw pixels */
static void wrap_draw(int x, int y, const char *s, uint32_t fg,
                      uint32_t bg, int maxw, int lineh, int maxlines)
{
    char line[96];
    int lp = 0, row = 0;
    for (const char *p = s; *p && row < maxlines; p++) {
        line[lp++] = *p;
        line[lp] = 0;
        if (gfx_text_width(line) > maxw && lp > 1) {
            line[lp - 1] = 0;
            gfx_draw_string(x, y + row * lineh, fg, bg, line);
            row++;
            lp = 0;
            line[lp++] = *p;
            line[lp] = 0;
        }
        if (*p == '\n') {
            line[lp - 1] = 0;
            gfx_draw_string(x, y + row * lineh, fg, bg, line);
            row++;
            lp = 0;
        }
    }
    if (lp > 0 && row < maxlines)
        gfx_draw_string(x, y + row * lineh, fg, bg, line);
}

int voice_frame(int mx, int my, int clicked)
{
    if (!inited)
        voice_open();
    int S = ui_scale();
    if (ui_window(&W, "Jbot Voice", mx, my, clicked))
        return 1;

    voice_tick();
    read_keys();

    int x = W.x + 30 * S, y = W.y + 52 * S;
    int bw = W.w - 60 * S;
    uint32_t good = fb_rgb(150, 230, 170);
    uint32_t warn = fb_rgb(240, 200, 120);
    uint32_t dim = fb_rgb(160, 170, 190);

    gfx_draw_string(x, y, mic_ok ? good : warn, fb_rgb(30, 34, 46),
                    mic_ok ? "Mic: AC97 capture channel ready"
                           : "Mic: no AC97 device in this VM");

    /* Tap to talk */
    int btn_y = y + 34 * S;
    int listening = voice_state() == VOICE_LISTEN;
    if (ui_button(x, btn_y, bw, 44 * S,
                  listening ? "Listening... (2 s)" : "Tap to talk",
                  listening, mx, my, clicked)) {
        if (!listening) {
            if (perm_granted(PERM_MIC))
                voice_listen_start();
            else
                ask_perm = 1;
        }
        return 0;
    }

    /* live level meter */
    int lvl = listening ? voice_level() : 0;
    int meter_y = btn_y + 56 * S;
    gfx_draw_string(x, meter_y, fb_rgb(150, 210, 255), fb_rgb(30, 34, 46),
                    "Input level");
    int tx = x + 100 * S, tw = bw - 100 * S;
    ui_rect(tx, meter_y, tw, 16 * S, fb_rgb(46, 52, 68));
    int fill = tw * lvl / 100;
    uint32_t lc = lvl >= VOICE_VAD_THRESHOLD ? fb_rgb(90, 220, 120)
                                             : fb_rgb(230, 180, 70);
    if (fill > 0)
        ui_rect(tx, meter_y, fill, 16 * S, lc);
    if (voice_state() == VOICE_HEARD)
        gfx_draw_string(x, meter_y + 26 * S, good, fb_rgb(30, 34, 46),
                        "Voice detected. Speech-to-text is the next stage;");
    if (voice_state() == VOICE_HEARD || voice_state() == VOICE_NOSPEECH)
        gfx_draw_string(x, meter_y + 26 * S + (voice_state()==VOICE_HEARD?20*S:0),
                        warn, fb_rgb(30, 34, 46),
                        voice_state() == VOICE_HEARD
                            ? "type the command below for now."
                            : "No speech picked up. Check mic / VM audio input.");

    /* text command box */
    int box_y = meter_y + 80 * S;
    gfx_draw_string(x, box_y, dim, fb_rgb(30, 34, 46),
                    "Command (then Enter):");
    ui_rect(x, box_y + 22 * S, bw, 28 * S, fb_rgb(18, 21, 30));
    ui_border(x, box_y + 22 * S, bw, 28 * S, fb_rgb(110, 130, 180));
    char shown[98];
    int p = 0;
    while (p < inlen && p < 96) { shown[p] = input[p]; p++; }
    shown[p++] = '_';
    shown[p] = 0;
    gfx_draw_string(x + 8 * S, box_y + 29 * S, fb_rgb(220, 230, 245),
                    fb_rgb(18, 21, 30), shown);

    /* reply area */
    int ry = box_y + 64 * S;
    ui_rect(x, ry, bw, 80 * S, fb_rgb(22, 26, 38));
    gfx_draw_string(x + 8 * S, ry + 6 * S, fb_rgb(120, 190, 255),
                    fb_rgb(22, 26, 38), "Jbot:");
    if (shown_reply[0])
        wrap_draw(x + 8 * S, ry + 26 * S, shown_reply,
                 fb_rgb(210, 225, 240), fb_rgb(22, 26, 38),
                 bw - 16 * S, 18 * S, 3);
    else
        gfx_draw_string(x + 8 * S, ry + 28 * S, dim, fb_rgb(22, 26, 38),
                        "Try: time / calc 8*9 / hello / help");

    if (ask_perm) {
        if (perm_prompt(PERM_MIC, W.x, W.y, W.w, W.h, mx, my, clicked) == 0)
            ask_perm = 0;
    }

    return 0;
}
