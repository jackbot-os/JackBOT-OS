#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ui.h>
#include <kernel/fat32.h>
#include <kernel/recycle.h>

static struct ui_win W;
static struct recycle_item items[RECYCLE_MAX];
static int nitems;
static int scroll;
static char msg[96];
static int inited;

static void append(char *s, const char *a)
{
    int p = 0;
    while (s[p]) p++;
    while (*a) s[p++] = *a++;
    s[p] = 0;
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

static void fmt_size(uint32_t b, char *out)
{
    if (b < 1024u) {
        u2dec(b, out);
        append(out, " B");
    } else if (b < 1024u * 1024u) {
        u2dec(b / 1024u, out);
        append(out, " KB");
    } else {
        u2dec(b / (1024u * 1024u), out);
        append(out, " MB");
    }
}

static void setmsg(const char *m)
{
    int p = 0;
    while (m[p] && p < 95) { msg[p] = m[p]; p++; }
    msg[p] = 0;
}

static void reload(void)
{
    nitems = recycle_list(items, RECYCLE_MAX);
    if (scroll > nitems - 8 && scroll > 0)
        scroll = nitems - 8;
    if (scroll < 0)
        scroll = 0;
}

void recycleapp_open(void)
{
    ui_fit(720, 520, &W);
    msg[0] = 0;
    scroll = 0;
    recycle_init();
    reload();
    inited = 1;
}

int recycleapp_frame(int mx, int my, int clicked)
{
    if (!inited)
        recycleapp_open();
    int S = ui_scale();
    if (ui_window(&W, "Recycle Bin", mx, my, clicked))
        return 1;

    int bg = fb_rgb(30, 34, 46);
    int x0 = W.x + 22 * S;
    int roww = W.w - 44 * S;

    if (ui_button(W.x + W.w - 110 * S, W.y + 42 * S, 88 * S, 30 * S,
                  "Refresh", 0, mx, my, clicked))
        reload();
    if (ui_button(W.x + W.w - 206 * S, W.y + 42 * S, 88 * S, 30 * S,
                  "Empty", 0, mx, my, clicked)) {
        if (recycle_empty() == 0)
            setmsg("回收站已清空");
        else
            setmsg("清空失败（磁盘未就绪）");
        reload();
    }

    int y = W.y + 82 * S;
    uint64_t total = 0;
    for (int i = 0; i < nitems; i++)
        total += items[i].size;
    char line[96];
    char sz[24];
    fmt_size((uint32_t)total, sz);
    line[0] = 0;
    append(line, "回收站项目 ");
    char t[12];
    u2dec((uint32_t)nitems, t);
    append(line, t);
    append(line, " 个，占用 ");
    append(line, sz);
    gfx_draw_string_wide(x0, y, fb_rgb(150, 210, 255), bg, line);
    y += 26 * S;

    if (nitems == 0) {
        gfx_draw_string_wide(x0, y + 30 * S, fb_rgb(150, 160, 180), bg,
                             "回收站是空的");
        gfx_draw_string_wide(x0, y + 58 * S, fb_rgb(130, 140, 158), bg,
                             "在文件管理器里删除的文件会先放到这里，可随时还原。");
        if (msg[0])
            gfx_draw_string_wide(x0, W.y + W.h - 36 * S,
                                 fb_rgb(120, 222, 140), bg, msg);
        return 0;
    }

    const int ROWS = 9;
    int rh = 34 * S;
    int top = y;
    for (int r = 0; r < ROWS; r++) {
        int idx = scroll + r;
        if (idx >= nitems)
            break;
        int ry = top + r * rh;
        ui_rect(x0, ry, roww, rh - 4 * S, fb_rgb(38, 44, 60));
        gfx_draw_string_wide(x0 + 10 * S, ry + 6 * S,
                             fb_rgb(226, 230, 240), bg, items[idx].orig);
        fmt_size(items[idx].size, sz);
        gfx_draw_string(x0 + roww - 250 * S, ry + 6 * S,
                        fb_rgb(170, 182, 206), bg, sz);
        if (ui_button(x0 + roww - 120 * S, ry + 2 * S, 104 * S, 26 * S,
                      "Restore", 0, mx, my, clicked)) {
            if (recycle_restore(idx) == 0)
                setmsg("已还原文件");
            else
                setmsg("还原失败（目标可能已存在或磁盘问题）");
            reload();
        }
    }

    int by = top + ROWS * rh + 4 * S;
    if (ui_button(x0, by, 90 * S, 28 * S, "Up", 0, mx, my, clicked)) {
        if (scroll > 0) scroll--;
    }
    if (ui_button(x0 + 100 * S, by, 90 * S, 28 * S, "Down", 0, mx, my,
                  clicked)) {
        if (scroll + ROWS < nitems) scroll++;
    }
    line[0] = 0;
    u2dec((uint32_t)(scroll + 1), t);
    append(line, t);
    append(line, "-");
    int hi = scroll + ROWS;
    if (hi > nitems) hi = nitems;
    u2dec((uint32_t)hi, t);
    append(line, t);
    append(line, " / ");
    u2dec((uint32_t)nitems, t);
    append(line, t);
    gfx_draw_string(x0 + 210 * S, by + 6 * S, fb_rgb(160, 172, 194), bg, line);

    if (msg[0])
        gfx_draw_string_wide(x0, W.y + W.h - 32 * S,
                             fb_rgb(120, 222, 140), bg, msg);
    return 0;
}
