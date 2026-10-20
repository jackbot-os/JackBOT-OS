#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ui.h>

static struct ui_win W;
static int inited;
static int64_t cur;
static int64_t acc;
static int op;
static int entering;
static int err;
static char disp[24];

static void i64(int64_t v, char *out)
{
    char tmp[24];
    int n = 0;
    int neg = 0;
    uint64_t u;
    if (v < 0) { neg = 1; u = (uint64_t)(-v); } else { u = (uint64_t)v; }
    if (u == 0) tmp[n++] = '0';
    while (u) { tmp[n++] = '0' + (u % 10); u /= 10; }
    int p = 0;
    if (neg) out[p++] = '-';
    while (n) out[p++] = tmp[--n];
    out[p] = 0;
}

static void refresh(void)
{
    if (err) {
        const char *e = "Error";
        int p = 0;
        while (e[p]) { disp[p] = e[p]; p++; }
        disp[p] = 0;
    } else {
        i64(cur, disp);
    }
}

static void reset(void)
{
    cur = 0; acc = 0; op = 0; entering = 0; err = 0;
    refresh();
}

static int64_t compute(int64_t a, int64_t b, int oper)
{
    switch (oper) {
    case '+': return a + b;
    case '-': return a - b;
    case '*': return a * b;
    case '/': return b == 0 ? 0 : a / b;
    default:  return b;
    }
}

static void digit(int d)
{
    if (err) reset();
    if (!entering) { cur = 0; entering = 1; }
    if (cur < 100000000000000LL)
        cur = cur * 10 + d;
    refresh();
}

static void setop(int o)
{
    if (err) { reset(); return; }
    if (op && entering)
        acc = compute(acc, cur, op);
    else
        acc = cur;
    op = o;
    entering = 0;
    cur = acc;
    refresh();
}

static void equals(void)
{
    if (err) { reset(); return; }
    if (op) {
        if (op == '/' && cur == 0) { err = 1; }
        else { cur = compute(acc, cur, op); }
    }
    op = 0;
    entering = 0;
    refresh();
}

void calc_open(void)
{
    ui_fit(360, 440, &W);
    reset();
    inited = 1;
}

int calc_frame(int mx, int my, int clicked)
{
    if (!inited)
        calc_open();
    int S = ui_scale();
    if (ui_window(&W, "Calculator", mx, my, clicked))
        return 1;

    int x0 = W.x + 14 * S;
    int y0 = W.y + 52 * S;
    int bw = 76 * S, bh = 54 * S, gap = 8 * S;
    int dw = bw * 4 + gap * 3;

    ui_rect(x0, y0, dw, 46 * S, fb_rgb(18, 22, 32));
    ui_border(x0, y0, dw, 46 * S, fb_rgb(90, 104, 132));
    int tw = gfx_text_width(disp);
    gfx_draw_string(x0 + dw - tw - 12 * S, y0 + 14 * S,
                    fb_rgb(235, 240, 250), fb_rgb(18, 22, 32), disp);

    static const char *keys[4][4] = {
        { "7", "8", "9", "/" },
        { "4", "5", "6", "*" },
        { "1", "2", "3", "-" },
        { "C", "0", "=", "+" }
    };

    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            int bx = x0 + c * (bw + gap);
            int by = y0 + 60 * S + r * (bh + gap);
            int hit = ui_button(bx, by, bw, bh, keys[r][c], 0, mx, my, clicked);
            if (hit) {
                char k = keys[r][c][0];
                if (k >= '0' && k <= '9')
                    digit(k - '0');
                else if (k == '+' || k == '-' || k == '*' || k == '/')
                    setop(k);
                else if (k == '=')
                    equals();
                else if (k == 'C')
                    reset();
            }
        }
    }
    return 0;
}
