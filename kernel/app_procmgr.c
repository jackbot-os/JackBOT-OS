#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ui.h>
#include <kernel/proc.h>

static struct ui_win W;
static int inited;

static void u64dec(uint64_t v, char *out)
{
    char tmp[24];
    int n = 0;
    if (v == 0) { out[0] = '0'; out[1] = 0; return; }
    while (v) { tmp[n++] = '0' + (v % 10); v /= 10; }
    int p = 0;
    while (n) out[p++] = tmp[--n];
    out[p] = 0;
}

static const char *state_name(tstate_t s)
{
    switch (s) {
    case T_READY:   return "ready";
    case T_RUNNING: return "running";
    case T_BLOCKED: return "blocked";
    case T_ZOMBIE:  return "zombie";
    default:        return "-";
    }
}

static void append(char *s, const char *a)
{
    int p = 0;
    while (s[p]) p++;
    while (*a) s[p++] = *a++;
    s[p] = 0;
}

void procmgr_open(void)
{
    ui_fit(720, 460, &W);
    inited = 1;
}

int procmgr_frame(int mx, int my, int clicked)
{
    if (!inited)
        procmgr_open();
    int S = ui_scale();
    if (ui_window(&W, "Process Manager", mx, my, clicked))
        return 1;

    int bg = fb_rgb(30, 34, 46);
    int x0 = W.x + 30 * S;
    int y = W.y + 56 * S;
    int rowh = 24 * S;

    char line[96];
    char num[24];

    /* header */
    line[0] = 0;
    append(line, "ID   STATE    KIND   CPU      MEM");
    gfx_draw_string_wide(x0, y, fb_rgb(140, 152, 176), bg, line);
    y += rowh + 4 * S;

    int shown = 0;
    for (int i = 0; i < MAX_THREADS && shown < 13; i++) {
        thread_t *t = thread_enum(i);
        if (!t || t->state == T_UNUSED)
            continue;

        line[0] = 0;
        u64dec(t->id, num);
        append(line, num);
        append(line, (t->id < 10) ? "    " : "   ");
        append(line, state_name(t->state));
        append(line, "  ");
        append(line, t->is_user ? "user   " : "kernel ");
        u64dec(t->cpu_ticks, num);
        append(line, num);
        append(line, "  ");
        u64dec(t->mem_bytes / 1024, num);
        append(line, num);
        append(line, " KB");

        int col = t->state == T_RUNNING ? fb_rgb(120, 222, 140)
                : t->state == T_ZOMBIE  ? fb_rgb(230, 120, 110)
                : fb_rgb(205, 214, 232);
        gfx_draw_string_wide(x0, y, col, bg, line);
        y += rowh;
        shown++;
    }

    y += 8 * S;
    line[0] = 0;
    append(line, "ku: ");
    u64dec((uint64_t)thread_count_user(), num);
    append(line, num);
    append(line, "   slots in use shown above");
    gfx_draw_string_wide(x0, y, fb_rgb(140, 152, 176), bg, line);
    return 0;
}
