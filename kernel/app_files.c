#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ui.h>
#include <kernel/fat32.h>
#include <kernel/recycle.h>
#include <kernel/kstring.h>

static struct ui_win W;
static struct fat_dirent dent[128];
static int ndent;
static int sel;
static int scroll;
static char cwd[128];
static char msg[96];
static int inited;

static char viewbuf[4096];
static int viewing;
static char viewname[FAT_DENT_NAME_MAX];

static void append(char *s, int osz, const char *a)
{
    if (osz <= 1)
        return;
    int p = 0;
    while (s[p]) p++;
    int q = 0;
    while (a[q] && p + q < osz - 1) { s[p + q] = a[q]; q++; }
    s[p + q] = 0;
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
        append(out, 20, " B");
    } else if (b < 1024u * 1024u) {
        u2dec(b / 1024u, out);
        append(out, 20, " KB");
    } else {
        u2dec(b / (1024u * 1024u), out);
        append(out, 20, " MB");
    }
}

static void setmsg(const char *m)
{
    int p = 0;
    while (m[p] && p < 95) { msg[p] = m[p]; p++; }
    msg[p] = 0;
}

static void full_path(const char *shortname, char *out, int osz)
{
    out[0] = 0;
    if (cwd[0]) {
        append(out, osz, cwd);
        append(out, osz, "\\");
        append(out, osz, shortname);
    } else {
        append(out, osz, shortname);
    }
}

static void reload(void)
{
    ndent = fat_list_dir(cwd, dent, 128);
    if (ndent < 0)
        ndent = 0;
    sel = -1;
    int maxscroll = ndent - 11;
    if (scroll > maxscroll && scroll > 0)
        scroll = maxscroll > 0 ? maxscroll : 0;
    if (scroll < 0)
        scroll = 0;
}

static int text_ext(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot || !dot[1])
        return 0;
    static const char *ext[] = {
        "TXT", "JCA", "PY", "SH", "XJH", "CFG", "LIB", "CONTROL",
        "MD", "C", "H", "S", "LOG", "JBT", "INI", 0
    };
    char e[8];
    int n = 0;
    for (dot++; *dot && n < 7; dot++, n++) {
        int c = *dot;
        if (c >= 'a' && c <= 'z')
            c -= 32;
        e[n] = (char)c;
    }
    e[n] = 0;
    for (int i = 0; ext[i]; i++)
        if (strcmp(e, ext[i]) == 0)
            return 1;
    return 0;
}

static void open_entry(int idx)
{
    if (idx < 0 || idx >= ndent)
        return;
    if (dent[idx].is_dir) {
        char np[128];
        full_path(dent[idx].short83, np, sizeof np);
        if (strlen(np) >= sizeof(cwd) - 1)
            return;
        strcpy(cwd, np);
        scroll = 0;
        reload();
        return;
    }
    if (text_ext(dent[idx].short83)) {
        char fp[160];
        full_path(dent[idx].short83, fp, sizeof fp);
        uint32_t got = 0;
        if (fat_read_file(fp, viewbuf, sizeof(viewbuf) - 1, &got) == 0) {
            viewbuf[got] = 0;
            strcpy(viewname, dent[idx].name);
            viewing = 1;
            return;
        }
        setmsg("无法读取该文件");
    } else {
        setmsg("该文件类型暂不支持打开（可打开文本/源码/脚本）");
    }
}

void files_open(void)
{
    ui_fit(760, 540, &W);
    cwd[0] = 0;
    msg[0] = 0;
    viewbuf[0] = 0;
    viewing = 0;
    sel = -1;
    scroll = 0;
    reload();
    inited = 1;
}

static int frame_view(int mx, int my, int clicked)
{
    int S = ui_scale();
    if (ui_window(&W, viewname, mx, my, clicked)) {
        viewing = 0;
        return 1;
    }
    int bg = fb_rgb(30, 34, 46);
    int x0 = W.x + 20 * S;
    if (ui_button(x0, W.y + 44 * S, 110 * S, 30 * S, "Back", 0, mx, my,
                  clicked)) {
        viewing = 0;
        return 0;
    }
    int y = W.y + 86 * S;
    int start = 0;
    int lines = 0;
    for (int i = 0; viewbuf[i] && lines < 22; i++) {
        if (viewbuf[i] == '\n' || viewbuf[i] == '\r') {
            char save = viewbuf[i];
            viewbuf[i] = 0;
            gfx_draw_string_wide(x0, y, fb_rgb(214, 222, 238), bg,
                                 viewbuf + start);
            viewbuf[i] = save;
            y += 20 * S;
            lines++;
            if (viewbuf[i] == '\r' && viewbuf[i + 1] == '\n')
                i++;
            start = i + 1;
        }
    }
    if (lines < 22 && viewbuf[start])
        gfx_draw_string_wide(x0, y, fb_rgb(214, 222, 238), bg,
                             viewbuf + start);
    return 0;
}

int files_frame(int mx, int my, int clicked)
{
    if (!inited)
        files_open();
    if (viewing)
        return frame_view(mx, my, clicked);

    int S = ui_scale();
    if (ui_window(&W, "File Explorer", mx, my, clicked))
        return 1;

    int bg = fb_rgb(30, 34, 46);
    int x0 = W.x + 20 * S;
    int roww = W.w - 40 * S;

    if (ui_button(W.x + W.w - 110 * S, W.y + 42 * S, 90 * S, 30 * S,
                  "Refresh", 0, mx, my, clicked))
        reload();
    if (ui_button(x0, W.y + 42 * S, 90 * S, 30 * S, "Up", 0, mx, my,
                  clicked)) {
        char *sl = strrchr(cwd, '\\');
        if (sl) *sl = 0;
        else cwd[0] = 0;
        scroll = 0;
        reload();
    }
    if (ui_button(x0 + 100 * S, W.y + 42 * S, 90 * S, 30 * S, "Open",
                  0, mx, my, clicked) && sel >= 0)
        open_entry(sel);
    if (ui_button(x0 + 200 * S, W.y + 42 * S, 90 * S, 30 * S, "Delete",
                  0, mx, my, clicked) && sel >= 0) {
        if (dent[sel].is_dir) {
            setmsg("回收站只处理文件，暂不支持删除文件夹");
        } else {
            char fp[160];
            full_path(dent[sel].short83, fp, sizeof fp);
            int rc = recycle_delete(fp);
            if (rc == 0)
                setmsg("已移入回收站");
            else if (rc == -2)
                setmsg("文件超过 8MB 或受保护，无法送入回收站");
            else
                setmsg("删除失败（受保护或磁盘问题）");
            reload();
        }
    }

    int y = W.y + 84 * S;
    char path[140];
    path[0] = 0;
    append(path, sizeof path, "位置：");
    append(path, sizeof path, cwd[0] ? cwd : "\\ 根目录");
    gfx_draw_string_wide(x0, y, fb_rgb(150, 210, 255), bg, path);
    y += 24 * S;

    const int ROWS = 11;
    int rh = 32 * S;
    int top = y;
    for (int r = 0; r < ROWS; r++) {
        int idx = scroll + r;
        if (idx >= ndent)
            break;
        int ry = top + r * rh;
        int hl = (idx == sel);
        ui_rect(x0, ry, roww, rh - 3 * S,
                hl ? fb_rgb(56, 78, 116) : fb_rgb(38, 44, 60));
        int ty = ry + 6 * S;
        if (dent[idx].is_dir) {
            gfx_draw_string(x0 + 8 * S, ty, fb_rgb(255, 208, 110), bg,
                            "[D]");
            gfx_draw_string_wide(x0 + 46 * S, ty, fb_rgb(255, 222, 150), bg,
                                 dent[idx].name);
        } else {
            gfx_draw_string(x0 + 8 * S, ty, fb_rgb(140, 170, 220), bg,
                            "[F]");
            gfx_draw_string_wide(x0 + 46 * S, ty, fb_rgb(222, 230, 242), bg,
                                 dent[idx].name);
            char sz[20];
            fmt_size(dent[idx].size, sz);
            gfx_draw_string(x0 + roww - 120 * S, ty, fb_rgb(168, 180, 204),
                            bg, sz);
        }
        if (clicked && ui_inside(mx, my, x0, ry, roww, rh - 3 * S)) {
            if (sel == idx)
                open_entry(idx);
            else
                sel = idx;
        }
    }

    if (ndent == 0)
        gfx_draw_string_wide(x0, top + 20 * S, fb_rgb(150, 160, 180), bg,
                             "（空目录）");

    int by = top + ROWS * rh + 2 * S;
    if (ui_button(x0, by, 90 * S, 26 * S, "Up", 0, mx, my, clicked)) {
        if (scroll > 0) scroll--;
    }
    if (ui_button(x0 + 100 * S, by, 90 * S, 26 * S, "Down", 0, mx, my,
                  clicked)) {
        if (scroll + ROWS < ndent) scroll++;
    }
    char t[12];
    u2dec((uint32_t)ndent, t);
    gfx_draw_string(x0 + 210 * S, by + 4 * S, fb_rgb(160, 172, 194), bg,
                    "项目数 ");
    gfx_draw_string(x0 + 270 * S, by + 4 * S, fb_rgb(160, 172, 194), bg, t);

    if (msg[0])
        gfx_draw_string_wide(x0, W.y + W.h - 32 * S,
                             fb_rgb(240, 200, 120), bg, msg);
    return 0;
}
