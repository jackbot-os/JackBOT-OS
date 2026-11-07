#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ui.h>
#include <kernel/fat32.h>
#include <kernel/recycle.h>
#include <kernel/i18n.h>
#include "i18n_data.h"
#include <kernel/kstring.h>
#include <kernel/jca.h>
#include <kernel/ps2.h>
#include <kernel/serial.h>

static struct ui_win W;
static struct fat_dirent dent[128];
static int ndent;
static int sel;
static int scroll;
static char cwd[128];
static char msg[96];
static int inited;

/* New-entry dialog: a filename is typed straight into this window, so the
   explorer needs its own keyboard capture (the desktop only reads keys while
   no application window is open). */
static char nm[32];
static int nmlen;
static int naming;
static int naming_dir;

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

/* Turn whatever the user typed into a FAT 8.3 name: uppercase, illegal
   characters folded to '_', base <= 8 and extension <= 3, and ".TXT" added
   when no extension was given so the result opens in the text viewer. */
static void make83(const char *in, char *out, int osz)
{
    char base[9];
    char ext[5];
    int bn = 0, en = 0, hasext = 0;
    const char *dot = strrchr(in, '.');
    if (dot && dot != in && dot[1])
        hasext = 1;

    for (const char *p = in; *p; p++) {
        if (hasext && p == dot)
            break;
        if (*p == '.')
            continue;
        char c = *p;
        if (c >= 'a' && c <= 'z')
            c -= 32;
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
              c == '-'))
            c = '_';
        if (bn < 8)
            base[bn++] = c;
    }
    if (bn == 0) {
        base[bn++] = 'N';
        base[bn++] = 'E';
        base[bn++] = 'W';
    }
    base[bn] = 0;

    if (hasext) {
        for (const char *p = dot + 1; *p; p++) {
            char c = *p;
            if (c >= 'a' && c <= 'z')
                c -= 32;
            if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
                c = '_';
            if (en < 3)
                ext[en++] = c;
        }
        ext[en] = 0;
    } else {
        ext[0] = 'T';
        ext[1] = 'X';
        ext[2] = 'T';
        ext[3] = 0;
    }

    out[0] = 0;
    append(out, osz, base);
    append(out, osz, ".");
    append(out, osz, ext);
}

static void create_named(void)
{
    if (!nmlen) {
        setmsg("Name is empty");
        return;
    }
    if (!fat_mounted()) {
        setmsg("No FAT32 volume mounted");
        naming = 0;
        return;
    }
    char name83[24];
    make83(nm, name83, sizeof name83);
    char fp[160];
    full_path(name83, fp, sizeof fp);
    int rc;
    if (naming_dir)
        rc = fat_mkdir(fp);
    else
        rc = fat_write_file(fp, "", 0);
    naming = 0;
    if (rc == 0) {
        msg[0] = 0;
        append(msg, sizeof msg, naming_dir ? "Folder created: " : "File created: ");
        append(msg, sizeof msg, name83);
    } else {
        setmsg("Create failed (write-protected or disk full?)");
    }
    reload();
}

static int name_key(char c)
{
    if (c == '\r' || c == '\n') {
        create_named();
        return 1;
    }
    if (c == 27) {
        naming = 0;
        return 1;
    }
    if (c == '\b' || c == 0x7f) {
        if (nmlen > 0)
            nm[--nmlen] = 0;
        return 1;
    }
    if (c >= 32 && c < 127 && nmlen < (int)sizeof nm - 1) {
        nm[nmlen++] = c;
        nm[nmlen] = 0;
    }
    return 1;
}

/* Keyboard is consumed here because the desktop only reads keys while no
   application window is open. */
static void name_input(void)
{
    char c;
    while (ps2_read_key(&c)) {
        if (!name_key(c))
            break;
    }
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
            /* A .jca file that starts with the JCAN magic is a native
               payload (C code compiled by tools/jcacc.py): run it instead
               of showing its bytes as text. */
            if (got >= 12 && viewbuf[0] == 'J' && viewbuf[1] == 'C' &&
                viewbuf[2] == 'A' && viewbuf[3] == 'N') {
                if (jca_run_native((const unsigned char *)viewbuf,
                                   (int)got) == 0) {
                    setmsg("Native JCA payload started");
                } else {
                    setmsg("Native JCA payload rejected");
                }
                return;
            }
            viewbuf[got] = 0;
            strcpy(viewname, dent[idx].name);
            viewing = 1;
            return;
        }
        setmsg(tr(STR_FILE_READ_FAIL));
    } else {
        setmsg(tr(STR_FILE_UNSUPPORTED));
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

    if (naming)
        name_input();

    int S = ui_scale();
    if (ui_window(&W, "File Explorer", mx, my, clicked))
        return 1;

    int bg = fb_rgb(30, 34, 46);
    int x0 = W.x + 20 * S;
    int roww = W.w - 40 * S;

    if (ui_button(W.x + W.w - 110 * S, W.y + 42 * S, 90 * S, 30 * S,
                  "Refresh", 0, mx, my, clicked))
        reload();
    if (ui_button(x0 + 300 * S, W.y + 42 * S, 90 * S, 30 * S, "New File",
                  0, mx, my, clicked) && !naming) {
        naming = 1;
        naming_dir = 0;
        nmlen = 0;
        nm[0] = 0;
        msg[0] = 0;
    }
    if (ui_button(x0 + 400 * S, W.y + 42 * S, 90 * S, 30 * S, "New Folder",
                  0, mx, my, clicked) && !naming) {
        naming = 1;
        naming_dir = 1;
        nmlen = 0;
        nm[0] = 0;
        msg[0] = 0;
    }
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
            setmsg(tr(STR_FILE_NO_FOLDER));
        } else {
            char fp[160];
            full_path(dent[sel].short83, fp, sizeof fp);
            int rc = recycle_delete(fp);
            if (rc == 0)
                setmsg(tr(STR_FILE_MOVED));
            else if (rc == -2)
                setmsg(tr(STR_FILE_TOO_BIG));
            else
                setmsg(tr(STR_FILE_DEL_FAIL));
            reload();
        }
    }

    int y = W.y + 84 * S;
    char path[140];
    path[0] = 0;
    append(path, sizeof path, tr(STR_FILE_LOCATION));
    append(path, sizeof path, cwd[0] ? cwd : tr(STR_FILE_ROOT));
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
        if (!naming && clicked && ui_inside(mx, my, x0, ry, roww, rh - 3 * S)) {
            if (sel == idx)
                open_entry(idx);
            else
                sel = idx;
        }
    }

    if (ndent == 0)
        gfx_draw_string_wide(x0, top + 20 * S, fb_rgb(150, 160, 180), bg,
                             tr(STR_FILE_EMPTY));

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
                    tr(STR_FILE_COUNT));
    gfx_draw_string(x0 + 270 * S, by + 4 * S, fb_rgb(160, 172, 194), bg, t);

    if (naming) {
        int dw = 420 * S;
        int dh = 120 * S;
        int dx = W.x + (W.w - dw) / 2;
        int dy = W.y + W.h / 2 - dh / 2;
        ui_rect(dx, dy, dw, dh, fb_rgb(22, 26, 36));
        ui_round_border(dx, dy, dw, dh, 6 * S, fb_rgb(120, 170, 240));
        gfx_draw_string(dx + 14 * S, dy + 12 * S, fb_rgb(235, 242, 255),
                        fb_rgb(22, 26, 36),
                        naming_dir ? "New folder name:" : "New file name:");
        ui_rect(dx + 14 * S, dy + 40 * S, dw - 28 * S, 26 * S,
                fb_rgb(14, 17, 26));
        char shown[40];
        int k = 0;
        for (int i = 0; i < nmlen && k < 38; i++)
            shown[k++] = nm[i];
        shown[k++] = '_';
        shown[k] = 0;
        gfx_draw_string(dx + 20 * S, dy + 45 * S, fb_rgb(235, 242, 255),
                        fb_rgb(14, 17, 26), shown);
        gfx_draw_string(dx + 14 * S, dy + 78 * S, fb_rgb(150, 160, 180),
                        fb_rgb(22, 26, 36), "Enter = create, Esc = cancel");
    }

    if (msg[0])
        gfx_draw_string_wide(x0, W.y + W.h - 32 * S,
                             fb_rgb(240, 200, 120), bg, msg);
    return 0;
}

