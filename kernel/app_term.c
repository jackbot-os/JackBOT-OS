#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ui.h>
#include <kernel/ps2.h>
#include <kernel/fat32.h>
#include <kernel/fsprobe.h>
#include <kernel/wlan.h>
#include <kernel/panic.h>
#include <kernel/session.h>
#include <kernel/py.h>

/* JackBOT OS terminal - English-only command interpreter.
   Built-in commands: help, ls, cd, cat, run <file>, Open <file>, clear, ver, exit
   "run" executes a .PY program through the in-kernel Python; "Open" shows a
   file or lists a directory through the FAT32 layer. */

#define LOG_CAP  6144
#define LINE_CAP 160
#define PATH_CAP 96

static struct ui_win W;
static int inited;

static char logbuf[LOG_CAP];
static int loglen;

static char line[LINE_CAP];
static int linelen;

static char cwd[PATH_CAP];      /* "" = root, otherwise an 8.3 path */

/* ---------------- small string helpers (no libc in kernel land) -------- */
static int slen(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void log_put(const char *s, int n)
{
    for (int i = 0; i < n && loglen < LOG_CAP - 1; i++)
        logbuf[loglen++] = s[i];
    logbuf[loglen] = 0;
}

static void log_str(const char *s)
{
    log_put(s, slen(s));
}

static void log_ch(char c)
{
    if (loglen < LOG_CAP - 1) { logbuf[loglen++] = c; logbuf[loglen] = 0; }
}

static void log_u32(uint32_t v)
{
    char t[12];
    int n = 0;
    if (v == 0) { log_ch('0'); return; }
    while (v && n < 11) { t[n++] = (char)('0' + (v % 10)); v /= 10; }
    while (n--) log_ch(t[n]);
}

static void log_nl(void)
{
    log_ch('\n');
}

static int eq(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == 0 && *b == 0;
}

static int eqi(const char *a, const char *b)
{
    while (*a && *b) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

static void copy_z(char *dst, const char *src, int cap)
{
    int i = 0;
    while (src[i] && i < cap - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static void cat_z(char *dst, const char *src, int cap)
{
    int n = slen(dst);
    int i = 0;
    while (src[i] && n < cap - 1) { dst[n++] = src[i]; i++; }
    dst[n] = 0;
}

/* build "cwd/name" (or just "name" at the root) */
static void join_path(char *out, const char *name)
{
    out[0] = 0;
    if (cwd[0]) {
        copy_z(out, cwd, PATH_CAP);
        cat_z(out, "/", PATH_CAP);
    }
    cat_z(out, name, PATH_CAP);
}

/* ---------------- output rendering ------------------------------------ */
static void draw_block(const char *s, int n, int x0, int y0, int x1, int rows,
                       unsigned fg, unsigned bg)
{
    int S = ui_scale();
    int cw = 8 * S, lh = 16 * S;
    int maxc = (x1 - x0) / cw;
    if (maxc < 8) maxc = 8;

    int total = 1;
    for (int i = 0; i < n; i++)
        if (s[i] == '\n') total++;

    int skip = total - rows;
    if (skip < 0) skip = 0;

    int x = x0, y = y0 + 2 * S, col = 0, li = 0;
    for (int i = 0; i < n; i++) {
        char c = s[i];
        if (c == '\n' || c == '\r') {
            if (li >= skip) { x = x0; y += lh; col = 0; }
            li++;
            continue;
        }
        if (col >= maxc) {
            if (li >= skip) { x = x0; y += lh; }
            col = 0;
            if (li < skip) { col++; continue; }
        }
        if (li < skip) { col++; continue; }
        if (y > y0 + rows * lh) break;
        char ch[2];
        ch[0] = (c >= 32 && c < 127) ? c : ' ';
        ch[1] = 0;
        gfx_draw_string(x, y, fg, bg, ch);
        x += cw;
        col++;
    }
}

/* ---------------- filesystem commands --------------------------------- */
static void cmd_ls(const char *path)
{
    struct fat_dirent ents[48];
    const char *dir = path[0] ? path : cwd;
    int n = fat_list_dir(dir, ents, 48);
    if (n < 0) { log_str("ls: cannot list that directory."); log_nl(); return; }
    if (n == 0) { log_str("(empty)"); log_nl(); return; }
    for (int i = 0; i < n; i++) {
        log_str(ents[i].name);
        if (ents[i].is_dir) {
            log_str("/");
        } else {
            log_str("  ");
            log_u32(ents[i].size);
            log_str(" B");
        }
        log_nl();
    }
}

static void cmd_cat(const char *name)
{
    char path[PATH_CAP];
    join_path(path, name);

    uint32_t size = 0;
    int isdir = 0;
    if (fat_stat_h(path, &size, &isdir) != 0) {
        log_str("cat: no such file: ");
        log_str(path);
        log_nl();
        return;
    }
    if (isdir) { cmd_ls(path); return; }

    int h = fat_open_h(path, 0);
    if (h < 0) { log_str("cat: cannot open."); log_nl(); return; }

    char chunk[128];
    uint32_t left = size > 4096 ? 4096 : size;
    while (left > 0) {
        int want = left > (uint32_t)sizeof(chunk) ? (int)sizeof(chunk) : (int)left;
        int got = fat_read_h(h, chunk, (uint32_t)want);
        if (got <= 0) break;
        for (int i = 0; i < got; i++) {
            char c = chunk[i];
            log_ch((c == '\t') ? ' ' : c);
        }
        left -= (uint32_t)got;
    }
    fat_close_h(h);
    if (size > 4096) { log_nl(); log_str("... (truncated at 4096 bytes)"); }
    log_nl();
}

static void cmd_cd(const char *arg)
{
    if (!arg[0]) { cwd[0] = 0; log_str("/"); log_nl(); return; }

    if (eq(arg, "..")) {
        int n = slen(cwd);
        while (n > 0 && cwd[n - 1] != '/') n--;
        if (n > 0) n--;
        cwd[n] = 0;
        log_str(cwd[0] ? cwd : "/");
        log_nl();
        return;
    }

    char path[PATH_CAP];
    if (arg[0] == '/') copy_z(path, arg + 1, PATH_CAP);
    else join_path(path, arg);

    int isdir = 0;
    uint32_t size = 0;
    if (fat_stat_h(path, &size, &isdir) != 0 || !isdir) {
        log_str("cd: not a directory: ");
        log_str(path);
        log_nl();
        return;
    }
    copy_z(cwd, path, PATH_CAP);
    log_str(cwd);
    log_nl();
}

/* ---------------- run / Open ------------------------------------------ */
static int ends_with(const char *s, const char *suf)
{
    int n = slen(s), m = slen(suf);
    if (m > n) return 0;
    const char *p = s + (n - m);
    return eqi(p, suf);
}

static void cmd_run(const char *name)
{
    char path[PATH_CAP];
    join_path(path, name);

    uint32_t size = 0;
    int isdir = 0;
    if (fat_stat_h(path, &size, &isdir) != 0 || isdir) {
        log_str("run: no such program: ");
        log_str(path);
        log_nl();
        return;
    }
    if (!ends_with(path, ".PY") && !ends_with(path, ".JCA")) {
        log_str("run: not an executable (expects .PY or .JCA).");
        log_nl();
        return;
    }
    if (ends_with(path, ".JCA")) {
        log_str("run: .JCA programs need the JCA compiler; use the JCA app.");
        log_nl();
        return;
    }

    static char src[4096];
    uint32_t got32 = 0;
    if (fat_read_file(path, src, sizeof(src) - 1, &got32) != 0 || got32 == 0) {
        log_str("run: cannot read ");
        log_str(path);
        log_nl();
        return;
    }
    src[got32] = 0;

    static char out[2048];
    out[0] = 0;
    py_init();
    py_exec(src, out, sizeof(out));
    log_str(out);
    if (out[0] && out[slen(out) - 1] != '\n') log_nl();
}

static void cmd_open(const char *name)
{
    char path[PATH_CAP];
    join_path(path, name);

    uint32_t size = 0;
    int isdir = 0;
    if (fat_stat_h(path, &size, &isdir) != 0) {
        log_str("Open: no such file or directory: ");
        log_str(path);
        log_nl();
        return;
    }
    if (isdir) { cmd_ls(path); return; }
    cmd_cat(name);
}

/* ---------------- partition / filesystem commands --------------------- */
static void cmd_fs(void)
{
    struct fsp_entry ents[PART_SCAN_MAX];
    int n = fsp_scan(ents, PART_SCAN_MAX);

    if (n < 0) {
        log_str("fs: no block device present (no disk backend).");
        log_nl();
        return;
    }
    if (n == 0) {
        log_str("fs: no partitions found on the disk.");
        log_nl();
        return;
    }

    log_str("#  kind       size(MiB)  start-LBA  notes");
    log_nl();
    for (int i = 0; i < n; i++) {
        log_u32((uint32_t)i);
        log_str("  ");
        log_str(ents[i].kind);
        /* pad the kind column so the table stays readable */
        for (int pad = slen(ents[i].kind); pad < 11; pad++)
            log_ch(' ');
        log_u32((ents[i].part.num_sectors / 2048));
        log_str("  ");
        log_u32(ents[i].part.start_lba);
        log_str("  ");
        if (ents[i].part.name[0]) {
            log_str(ents[i].part.name);
            log_str(" ");
        }
        log_str(ents[i].mountable ? "(mountable)" : "(no driver)");
        log_nl();
    }
    log_str("Use 'mount <#>' on a FAT partition, then ls / cd / cat / run / Open.");
    log_nl();
}

static void cmd_mount(const char *arg)
{
    struct fsp_entry ents[PART_SCAN_MAX];
    int n = fsp_scan(ents, PART_SCAN_MAX);
    if (n <= 0) {
        log_str("mount: no partitions to mount.");
        log_nl();
        return;
    }

    int idx = 0;
    for (const char *p = arg; *p; p++) {
        if (*p < '0' || *p > '9') { break; }
        idx = idx * 10 + (*p - '0');
    }
    if (idx < 0 || idx >= n) {
        log_str("mount: partition index out of range (see 'fs').");
        log_nl();
        return;
    }
    if (!ents[idx].mountable) {
        log_str("mount: no driver for ");
        log_str(ents[idx].kind);
        log_str(" yet - only FAT12/16/32 can be mounted.");
        log_nl();
        return;
    }

    fat_unmount();
    if (fat_mount_part(ents[idx].part.start_lba, ents[idx].part.num_sectors) != 0) {
        log_str("mount: FAT mount failed on partition ");
        log_u32((uint32_t)idx);
        log_nl();
        return;
    }
    cwd[0] = 0;
    log_str("mounted ");
    log_str(ents[idx].kind);
    log_str(" partition ");
    log_u32((uint32_t)idx);
    log_str(" at /");
    log_nl();
}

static void cmd_umount(void)
{
    if (!fat_mounted()) {
        log_str("umount: nothing is mounted.");
        log_nl();
        return;
    }
    fat_unmount();
    cwd[0] = 0;
    log_str("unmounted.");
    log_nl();
}

static void log_hex4(uint16_t v)
{
    const char *h = "0123456789abcdef";
    for (int i = 12; i >= 0; i -= 4)
        log_ch(h[(v >> i) & 0xF]);
}

static void cmd_wifi(void)
{
    int n = wlan_count();

    if (n == 0) {
        log_str("wifi: no wireless adapter on the bus (no USB WLAN dongle,"
                " no PCI 0x02/0x80 device).");
        log_nl();
        return;
    }
    log_str("wifi: ");
    log_u32((uint32_t)n);
    log_str(" wireless adapter(s)");
    log_nl();
    for (int i = 0; i < n; i++) {
        const struct wlan_dev *d = wlan_dev_get(i);
        if (!d)
            continue;
        log_str("  [");
        log_u32((uint32_t)i);
        log_str("] ");
        log_hex4(d->vid);
        log_str(":");
        log_hex4(d->pid);
        log_str("  ");
        log_str(d->product[0] ? d->product : "(no product string)");
        log_nl();
        log_str("      family=");
        log_str(d->family);
        log_str("  driver=");
        log_str(d->driver);
        log_nl();
        log_str("      ep in=");
        log_u32(d->ep_in);
        log_str(" out=");
        log_u32(d->ep_out);
        log_str(" irq=");
        log_u32(d->ep_irq);
        log_str("  ifclass=");
        log_u32(d->if_class);
        log_nl();
        if (d->mac_valid) {
            log_str("      mac=");
            for (int k = 0; k < 6; k++) {
                log_hex4(d->mac[k] & 0xFF);
                if (k < 5) log_ch(':');
            }
            log_nl();
        }
        if (d->reg_ok) {
            log_str("      reg0=0x");
            log_hex4((uint16_t)(d->reg0 >> 16));
            log_hex4((uint16_t)d->reg0);
            log_nl();
        }
        log_str("      state=");
        log_str(d->state);
        log_nl();
    }
}

/* ---------------- command dispatch ------------------------------------ */
static void run_line(void)
{    /* echo the prompt + the typed line back into the scrollback */
    log_str("JACK> ");
    log_str(line);
    log_nl();

    char cmd[LINE_CAP];
    copy_z(cmd, line, LINE_CAP);

    /* strip trailing spaces */
    int n = slen(cmd);
    while (n > 0 && (cmd[n - 1] == ' ' || cmd[n - 1] == '\t')) cmd[--n] = 0;

    /* split command / argument */
    char *arg = cmd;
    while (*arg && *arg != ' ') arg++;
    if (*arg == ' ') { *arg = 0; arg++; while (*arg == ' ') arg++; } else if (*arg == 0) {
        arg = cmd + slen(cmd);
    }

    if (!cmd[0]) { return; }

    if (eq(cmd, "help")) {
        log_str("Commands: help  ver  fs  mount <#>  umount  wifi  ls [dir]  cd <dir>");
        log_nl();
        log_str("          cat <file>  run <file.py>  Open <file|dir>  session [save]  clear  exit");
        log_nl();
    } else if (eq(cmd, "ver")) {
        log_str("JackBOT OS 1.0.9 (EastLake) - terminal");
        log_nl();
    } else if (eq(cmd, "fs") || eq(cmd, "partitions")) {
        cmd_fs();
    } else if (eq(cmd, "wifi") || eq(cmd, "wlan")) {
        cmd_wifi();
    } else if (eq(cmd, "session")) {
        if (eq(arg, "save")) {
            int r = session_save();
            if (r >= 0) {
                log_str("session: snapshot saved (");
                log_u32((uint32_t)r);
                log_str(" bytes)");
                log_nl();
            } else if (r == -1) {
                log_str("session: no writable volume, nothing saved");
                log_nl();
            } else {
                log_str("session: write failed, nothing saved");
                log_nl();
            }
        } else {
            char snap[256];
            int n = session_load(snap, sizeof(snap));
            if (n > 0) {
                log_str("session: last snapshot for the next boot is on disk");
                log_nl();
            } else {
                log_str("session: no snapshot on the boot volume");
                log_nl();
            }
        }
    } else if (eq(cmd, "mount")) {
        cmd_mount(arg);
    } else if (eq(cmd, "umount")) {
        cmd_umount();
    } else if (eq(cmd, "ls") || eq(cmd, "dir")) {
        cmd_ls(arg);
    } else if (eq(cmd, "cd")) {
        cmd_cd(arg);
    } else if (eq(cmd, "cat") || eq(cmd, "type")) {
        if (!arg[0]) { log_str("cat: needs a file name."); log_nl(); }
        else cmd_cat(arg);
    } else if (eq(cmd, "run")) {
        if (!arg[0]) { log_str("run: needs a program name."); log_nl(); }
        else cmd_run(arg);
    } else if (eq(cmd, "Open") || eq(cmd, "open")) {
        if (!arg[0]) { log_str("Open: needs a file or directory."); log_nl(); }
        else cmd_open(arg);
    } else if (eq(cmd, "clear") || eq(cmd, "cls")) {
        loglen = 0;
        logbuf[0] = 0;
    } else {
        log_str("Unknown command: ");
        log_str(cmd);
        log_str("  (try 'help')");
        log_nl();
    }
}

void term_open(void)
{
    ui_fit(820, 560, &W);
    loglen = 0;
    logbuf[0] = 0;
    linelen = 0;
    line[0] = 0;
    cwd[0] = 0;

    log_str("JackBOT OS terminal - type 'help' for commands.");
    log_nl();
    if (!fat_mounted()) {
        log_str("Note: no FAT32 volume is mounted, file commands will fail.");
        log_nl();
    }
    inited = 1;
}

int term_frame(int mx, int my, int clicked)
{
    if (!inited)
        term_open();
    int S = ui_scale();
    if (ui_window(&W, "Terminal", mx, my, clicked))
        return 1;

    int x0 = W.x + 12 * S;
    int x1 = W.x + W.w - 12 * S;
    int y0 = W.y + 42 * S;
    int input_h = 34 * S;
    int input_y = W.y + W.h - input_h - 14 * S;

    /* keyboard: line editing + Enter to execute */
    char c;
    while (ps2_read_key(&c)) {
        if (c == '\b' || c == 0x7f) {
            if (linelen > 0) line[--linelen] = 0;
        } else if (c == '\r' || c == '\n') {
            line[linelen] = 0;
            run_line();
            linelen = 0;
            line[0] = 0;
        } else if (c >= 32 && c < 127) {
            if (linelen < LINE_CAP - 1) { line[linelen++] = c; line[linelen] = 0; }
        }
    }

    /* scrollback */
    ui_rect(x0, y0, x1 - x0, input_y - y0 - 10 * S, fb_rgb(12, 16, 24));
    ui_border(x0, y0, x1 - x0, input_y - y0 - 10 * S, fb_rgb(70, 90, 120));
    {
        int rows = (input_y - y0 - 16 * S) / (16 * S);
        draw_block(logbuf, loglen, x0 + 8 * S, y0 + 4 * S, x1 - 8 * S,
                   rows, fb_rgb(198, 222, 240), fb_rgb(12, 16, 24));
    }

    /* input line */
    ui_rect(x0, input_y, x1 - x0, input_h, fb_rgb(22, 28, 40));
    ui_border(x0, input_y, x1 - x0, input_h, fb_rgb(90, 130, 170));
    gfx_draw_string(x0 + 8 * S, input_y + 9 * S, fb_rgb(150, 230, 120),
                    fb_rgb(22, 28, 40), "JACK>");
    gfx_draw_string(x0 + 60 * S, input_y + 9 * S, fb_rgb(230, 240, 255),
                    fb_rgb(22, 28, 40), line);

    /* a caret so the user can see where typing lands */
    {
        int cx = x0 + 60 * S + linelen * 8 * S;
        ui_rect(cx + 1 * S, input_y + 8 * S, 2 * S, 16 * S, fb_rgb(230, 240, 255));
    }

    (void)mx; (void)my; (void)clicked;
    return 0;
}
