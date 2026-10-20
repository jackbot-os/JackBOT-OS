#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ui.h>
#include <kernel/ps2.h>
#include <kernel/py.h>

#define CODE_CAP 3000
#define OUT_CAP  1600

static struct ui_win W;
static int inited;
static char code[CODE_CAP];
static int clen;
static char out[OUT_CAP];
static int ran;

void python_open(void)
{
    ui_fit(780, 540, &W);
    clen = 0;
    code[0] = 0;
    out[0] = 0;
    ran = 0;
    py_init();
    inited = 1;
}

static void run_code(void)
{
    if (clen == 0) {
        const char *m = "Type Python code, then Run.";
        int i = 0;
        while (m[i] && i < OUT_CAP - 1) { out[i] = m[i]; i++; }
        out[i] = 0;
        ran = 1;
        return;
    }
    char src[CODE_CAP];
    int i = 0;
    while (i < clen && i < CODE_CAP - 1) { src[i] = code[i]; i++; }
    src[i] = 0;
    py_exec(src, out, OUT_CAP);
    ran = 1;
}

static void clear_code(void)
{
    clen = 0;
    code[0] = 0;
    out[0] = 0;
    ran = 0;
}

static void newline_indent(void)
{
    if (clen >= CODE_CAP - 8)
        return;
    code[clen++] = '\n';
    int start = clen - 1;
    (void)start;
    int line = clen - 2;
    while (line >= 0 && code[line] != '\n')
        line--;
    line++;
    int p = line;
    while (p < clen - 1 && (code[p] == ' ' || code[p] == '\t')) {
        code[clen++] = code[p];
        p++;
        if (clen >= CODE_CAP - 8)
            break;
    }
    if (clen > 0 && (code[clen - 1] == ':' )) {
        int n = 0;
        while (n < 4 && clen < CODE_CAP - 2) { code[clen++] = ' '; n++; }
    }
}

static void draw_text_block(const char *s, int slen, int x0, int y0, int x1, int rows,
                            unsigned fg, unsigned bg)
{
    int S = ui_scale();
    int cw = 8 * S, lh = 16 * S;
    int maxc = (x1 - x0) / cw;
    if (maxc < 10) maxc = 10;
    int total = 1;
    for (int i = 0; i < slen; i++)
        if (s[i] == '\n' || s[i] == '\r')
            total++;
    int line_idx = 0;
    int skip = total - rows;
    if (skip < 0) skip = 0;
    int x = x0, y = y0 + 2 * S, col = 0;
    for (int i = 0; i < slen; i++) {
        char c = s[i];
        if (c == '\n' || c == '\r') {
            if (line_idx >= skip) {
                x = x0; y += lh; col = 0;
            }
            line_idx++;
            continue;
        }
        if (col >= maxc) {
            if (line_idx >= skip) { x = x0; y += lh; }
            col = 0;
            if (line_idx < skip) { col++; continue; }
        }
        if (line_idx < skip) { col++; continue; }
        if (y > y0 + rows * lh)
            break;
        char ch[2];
        ch[0] = (c >= 32) ? c : ' ';
        ch[1] = 0;
        gfx_draw_string(x, y, fg, bg, ch);
        x += cw;
        col++;
    }
}

int python_frame(int mx, int my, int clicked)
{
    if (!inited)
        python_open();
    int S = ui_scale();
    if (ui_window(&W, "Python Console", mx, my, clicked))
        return 1;

    int x0 = W.x + 14 * S;
    int x1 = W.x + W.w - 14 * S;
    int edit_y0 = W.y + 44 * S;
    int edit_h = 250 * S;
    int out_y0 = edit_y0 + edit_h + 12 * S;
    int btn_y = W.y + W.h - 48 * S;

    char c;
    while (ps2_read_key(&c)) {
        if (c == '\b' || c == 0x7f) {
            if (clen > 0)
                clen--;
        } else if (c == '\r' || c == '\n') {
            newline_indent();
        } else if (c == '\t') {
            int n = 0;
            while (n < 4 && clen < CODE_CAP - 2) { code[clen++] = ' '; n++; }
        } else if (c >= 32 && c < 127) {
            if (clen < CODE_CAP - 1)
                code[clen++] = c;
        }
    }

    ui_rect(x0, edit_y0, x1 - x0, edit_h, fb_rgb(20, 24, 34));
    ui_border(x0, edit_y0, x1 - x0, edit_h, fb_rgb(70, 90, 120));
    gfx_draw_string(x0 + 6 * S, edit_y0 + 4 * S,
                    fb_rgb(140, 170, 210), fb_rgb(20, 24, 34), "Code (Enter wraps, Tab indent):");
    if (clen == 0)
        gfx_draw_string(x0 + 6 * S, edit_y0 + 22 * S,
                        fb_rgb(90, 110, 140), fb_rgb(20, 24, 34),
                        "def fib(n):");
    draw_text_block(code, clen, x0 + 6 * S, edit_y0 + 20 * S, x1 - 6 * S,
                    edit_h / (16 * S) - 2, fb_rgb(210, 225, 245), fb_rgb(20, 24, 34));

    int out_h = btn_y - out_y0 - 8 * S;
    ui_rect(x0, out_y0, x1 - x0, out_h, fb_rgb(12, 16, 24));
    ui_border(x0, out_y0, x1 - x0, out_h, fb_rgb(70, 90, 120));
    gfx_draw_string(x0 + 6 * S, out_y0 + 4 * S,
                    fb_rgb(120, 220, 160), fb_rgb(12, 16, 24), "Output:");
    {
        int ol = 0;
        while (out[ol]) ol++;
        draw_text_block(out, ol, x0 + 6 * S, out_y0 + 20 * S, x1 - 6 * S,
                        out_h / (16 * S) - 1, fb_rgb(190, 235, 200), fb_rgb(12, 16, 24));
    }
    (void)ran;

    int bw = 130 * S, bh = 36 * S;
    if (ui_button(x0, btn_y, bw, bh, "Run", 0, mx, my, clicked))
        run_code();
    if (ui_button(x0 + bw + 12 * S, btn_y, bw, bh, "Clear", 0, mx, my, clicked))
        clear_code();
    return 0;
}
