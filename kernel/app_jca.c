#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ui.h>
#include <kernel/ps2.h>
#include <kernel/jca.h>

#define JCODE_CAP 2400
#define JOUT_CAP  2600

static struct ui_win W;
static int inited;
static char code[JCODE_CAP];
static int clen;
static char disp[JOUT_CAP];
static int compiled;
static int show_mode;
static struct jca_program prog;

static const char *EXAMPLE =
    "fn fib(n) {\n"
    "    if (n < 2) {\n"
    "        return n;\n"
    "    }\n"
    "    return fib(n - 1) + fib(n - 2);\n"
    "}\n"
    "print(fib(10));\n";

void jca_open(void)
{
    ui_fit(800, 560, &W);
    clen = 0;
    int i = 0;
    while (EXAMPLE[i] && i < JCODE_CAP - 1) { code[i] = EXAMPLE[i]; i++; }
    clen = i;
    code[clen] = 0;
    disp[0] = 0;
    compiled = 0;
    show_mode = 0;
    inited = 1;
}

static void do_compile(void)
{
    char src[JCODE_CAP];
    char err[200];
    int i = 0;
    while (i < clen && i < JCODE_CAP - 1) { src[i] = code[i]; i++; }
    src[i] = 0;
    int rc = jca_compile(src, &prog, err, sizeof err);
    if (rc < 0) {
        int n = 0;
        const char *h = "Compile error: ";
        int k = 0;
        while (h[k] && n < JOUT_CAP - 1) disp[n++] = h[k++];
        k = 0;
        while (err[k] && n < JOUT_CAP - 1) disp[n++] = err[k++];
        disp[n] = 0;
        compiled = 0;
    } else {
        jca_disasm(&prog, disp, JOUT_CAP);
        compiled = 1;
    }
    show_mode = 1;
}

static void do_run(void)
{
    if (!compiled)
        do_compile();
    if (!compiled)
        return;
    char out[JOUT_CAP];
    jca_run(&prog, out, JOUT_CAP);
    int n = 0;
    while (out[n] && n < JOUT_CAP - 1) { disp[n] = out[n]; n++; }
    disp[n] = 0;
    show_mode = 2;
}

static void do_clear(void)
{
    clen = 0;
    code[0] = 0;
    disp[0] = 0;
    compiled = 0;
}

static void newline_indent(void)
{
    if (clen >= JCODE_CAP - 8)
        return;
    code[clen++] = '\n';
    int line = clen - 2;
    while (line >= 0 && code[line] != '\n')
        line--;
    line++;
    int p = line;
    while (p < clen - 1 && (code[p] == ' ' || code[p] == '\t')) {
        code[clen++] = code[p];
        p++;
        if (clen >= JCODE_CAP - 8) break;
    }
    if (clen > 0 && code[clen - 1] == '{') {
        int n = 0;
        while (n < 4 && clen < JCODE_CAP - 2) { code[clen++] = ' '; n++; }
    }
}

static void draw_block(const char *s, int slen, int x0, int y0, int x1, int rows,
                       unsigned fg, unsigned bg)
{
    int S = ui_scale();
    int cw = 8 * S, lh = 16 * S;
    int maxc = (x1 - x0) / cw;
    if (maxc < 10) maxc = 10;
    int total = 1;
    for (int i = 0; i < slen; i++)
        if (s[i] == '\n' || s[i] == '\r') total++;
    int line_idx = 0;
    int skip = total - rows;
    if (skip < 0) skip = 0;
    int x = x0, y = y0 + 2 * S, col = 0;
    for (int i = 0; i < slen; i++) {
        char c = s[i];
        if (c == '\n' || c == '\r') {
            if (line_idx >= skip) { x = x0; y += lh; }
            line_idx++;
            col = 0;
            continue;
        }
        if (col >= maxc) {
            if (line_idx >= skip) { x = x0; y += lh; }
            if (line_idx < skip) { col++; continue; }
            col = 0;
        }
        if (line_idx < skip) { col++; continue; }
        if (y > y0 + rows * lh) break;
        char ch[2];
        ch[0] = (c >= 32) ? c : ' ';
        ch[1] = 0;
        gfx_draw_string(x, y, fg, bg, ch);
        x += cw;
        col++;
    }
}

int jca_frame(int mx, int my, int clicked)
{
    if (!inited)
        jca_open();
    int S = ui_scale();
    if (ui_window(&W, "JCA Studio", mx, my, clicked))
        return 1;

    int x0 = W.x + 14 * S;
    int x1 = W.x + W.w - 14 * S;
    int code_y = W.y + 44 * S;
    int code_h = 200 * S;
    int btn_y = code_y + code_h + 8 * S;
    int out_y = btn_y + 44 * S;

    char c;
    while (ps2_read_key(&c)) {
        if (c == '\b' || c == 0x7f) {
            if (clen > 0) clen--;
        } else if (c == '\r' || c == '\n') {
            newline_indent();
        } else if (c == '\t') {
            int n = 0;
            while (n < 4 && clen < JCODE_CAP - 2) { code[clen++] = ' '; n++; }
        } else if (c >= 32 && c < 127) {
            if (clen < JCODE_CAP - 1) code[clen++] = c;
        }
    }

    ui_rect(x0, code_y, x1 - x0, code_h, fb_rgb(20, 24, 34));
    ui_border(x0, code_y, x1 - x0, code_h, fb_rgb(70, 90, 120));
    gfx_draw_string(x0 + 6 * S, code_y + 4 * S,
                    fb_rgb(140, 170, 210), fb_rgb(20, 24, 34),
                    "JCA source (.jca) - fn/var/if/while/return, Tab indent:");
    draw_block(code, clen, x0 + 6 * S, code_y + 20 * S, x1 - 6 * S,
               code_h / (16 * S) - 1, fb_rgb(210, 225, 245), fb_rgb(20, 24, 34));

    int bw = 110 * S, bh = 34 * S;
    if (ui_button(x0, btn_y, bw, bh, "Compile", 0, mx, my, clicked))
        do_compile();
    if (ui_button(x0 + (bw + 8 * S), btn_y, bw, bh, "Run", 0, mx, my, clicked))
        do_run();
    if (ui_button(x0 + 2 * (bw + 8 * S), btn_y, bw, bh, "Example", 0, mx, my, clicked))
        jca_open();
    if (ui_button(x0 + 3 * (bw + 8 * S), btn_y, bw, bh, "Clear", 0, mx, my, clicked))
        do_clear();

    int out_h = W.y + W.h - 12 * S - out_y;
    ui_rect(x0, out_y, x1 - x0, out_h, fb_rgb(12, 16, 24));
    ui_border(x0, out_y, x1 - x0, out_h, fb_rgb(70, 90, 120));
    const char *title = show_mode == 1 ? "Bytecode (compiled):" :
                        (show_mode == 2 ? "Output:" : "Press Compile, then Run");
    unsigned title_col = show_mode == 1 ? fb_rgb(170, 150, 240) : fb_rgb(120, 220, 160);
    gfx_draw_string(x0 + 6 * S, out_y + 4 * S, title_col, fb_rgb(12, 16, 24), title);
    {
        int dl = 0;
        while (disp[dl]) dl++;
        unsigned col = show_mode == 1 ? fb_rgb(200, 180, 240) : fb_rgb(190, 235, 200);
        draw_block(disp, dl, x0 + 6 * S, out_y + 20 * S, x1 - 6 * S,
                   out_h / (16 * S) - 1, col, fb_rgb(12, 16, 24));
    }
    return 0;
}
