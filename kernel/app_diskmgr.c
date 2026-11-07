#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ui.h>
#include <kernel/part.h>
#include <kernel/i18n.h>
#include "i18n_data.h"

static struct ui_win W;
static struct part_info PI[PART_SCAN_MAX];
static int pcount;
static int loaded;

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

static void u2dec64(uint64_t v, char *out)
{
    char tmp[20];
    int n = 0;
    if (v == 0) { out[0] = '0'; out[1] = 0; return; }
    while (v) { tmp[n++] = '0' + (int)(v % 10); v /= 10; }
    int p = 0;
    while (n) out[p++] = tmp[--n];
    out[p] = 0;
}

static const char *type_cn(uint8_t t)
{
    if (part_type_is_fat(t)) return tr(STR_PT_FAT);
    if (t == PART_TYPE_NTFS) return tr(STR_PT_NTFS);
    if (part_type_is_extended(t)) return tr(STR_PT_EXTENDED);
    if (t == PART_TYPE_LINUX) return tr(STR_PT_LINUX);
    if (t == PART_TYPE_LINUXSWAP) return tr(STR_PT_SWAP);
    return tr(STR_PT_OTHER);
}

static void load_layout(void)
{
    loaded = 0;
    pcount = part_scan(PI, PART_SCAN_MAX);
    if (pcount > 0)
        loaded = 1;
}

void diskmgr_open(void)
{
    ui_fit(800, 500, &W);
    load_layout();
}

int diskmgr_frame(int mx, int my, int clicked)
{
    if (!loaded)
        diskmgr_open();
    int S = ui_scale();
    if (ui_window(&W, "This PC", mx, my, clicked))
        return 1;

    int bg = fb_rgb(30, 34, 46);
    int x0 = W.x + 26 * S;
    int y0 = W.y + 52 * S;

    gfx_draw_string_wide(x0, y0, fb_rgb(150, 168, 200), bg,
                         tr(STR_DISK_HEADER));
    ui_rect(x0, y0 + 22 * S, W.w - 52 * S, 2 * S, fb_rgb(70, 84, 110));

    /* Scheme of the table itself, plus how many entries were found. */
    {
        char tag[24];
        int q = 0;
        const char *sc = part_scheme_name(pcount > 0 ? PI[0].scheme : PART_SCHEME_MBR);
        for (int k = 0; sc[k] && q < 10; k++)
            tag[q++] = sc[k];
        tag[q++] = ' ';
        char num[8];
        u2dec((uint32_t)pcount, num);
        for (int k = 0; num[k] && q < 20; k++)
            tag[q++] = num[k];
        tag[q] = 0;
        gfx_draw_string_wide(x0 + W.w - 150 * S, y0, fb_rgb(120, 200, 230), bg, tag);
    }

    char letter = 'C';
    int row = 0;
    for (int i = 0; i < pcount; i++) {
        struct part_info *e = &PI[i];
        int isext = (e->scheme == PART_SCHEME_MBR) && part_type_is_extended(e->type);
        int isfat = e->is_fat;
        int y = y0 + 36 * S + row * 42 * S;

        char dl[8];
        if (isext) {
            dl[0] = '-'; dl[1] = '-'; dl[2] = 0;
        } else if (isfat) {
            dl[0] = letter++; dl[1] = ':'; dl[2] = 0;
        } else {
            dl[0] = '-'; dl[1] = '-'; dl[2] = 0;
        }
        gfx_draw_string_wide(x0, y, fb_rgb(255, 208, 110), bg, dl);

        const char *label = (e->scheme == PART_SCHEME_GPT)
                                ? (isfat ? tr(STR_PT_FAT) : tr(STR_PT_OTHER))
                                : type_cn(e->type);
        gfx_draw_string_wide(x0 + 95 * S, y, fb_rgb(222, 230, 242), bg, label);

        /* GPT entries carry a name; MBR has none, so show the type number. */
        if (e->scheme == PART_SCHEME_GPT && e->name[0])
            gfx_draw_string_wide(x0 + 330 * S, y, fb_rgb(170, 200, 226), bg, e->name);

        uint32_t mb = (uint32_t)(((uint64_t)e->num_sectors) / 2048);
        char num[16];
        u2dec(mb, num);
        char cap[24];
        int q = 0;
        for (int k = 0; num[k] && q < 16; k++) cap[q++] = num[k];
        cap[q++] = ' '; cap[q++] = 'M'; cap[q++] = 'B'; cap[q] = 0;
        gfx_draw_string_wide(x0 + 470 * S, y, fb_rgb(200, 210, 226), bg, cap);

        int active = e->boot == 0x80;
        gfx_draw_string_wide(x0 + 600 * S, y,
                             active ? fb_rgb(120, 222, 140) : fb_rgb(120, 130, 146),
                             bg, active ? tr(STR_DISK_YES) : tr(STR_DISK_NO));
        gfx_draw_string_wide(x0 + 660 * S, y,
                             isfat ? fb_rgb(120, 222, 140) : fb_rgb(232, 178, 104),
                             bg, isfat ? tr(STR_DISK_ACCESS) : tr(STR_DISK_VISIBLE));
        row++;
    }

    if (!pcount)
        gfx_draw_string_wide(x0, y0 + 60 * S, fb_rgb(232, 150, 150), bg,
                             tr(STR_DISK_NO_PART));

    char total[24];
    {
        char num[20];
        uint64_t sum = 0;
        for (int i = 0; i < pcount; i++)
            sum += PI[i].num_sectors;
        u2dec64(sum / 2048, num);
        int q = 0;
        for (int k = 0; num[k] && q < 16; k++) total[q++] = num[k];
        total[q++] = ' '; total[q++] = 'M'; total[q++] = 'B'; total[q] = 0;
    }
    gfx_draw_string_wide(x0 + W.w - 150 * S, W.y + W.h - 66 * S,
                         fb_rgb(150, 166, 192), bg, total);

    int fy = W.y + W.h - 66 * S;
    gfx_draw_string_wide(x0, fy, fb_rgb(150, 166, 192), bg,
                         tr(STR_DISK_NOTE1));
    gfx_draw_string_wide(x0, fy + 22 * S, fb_rgb(150, 166, 192), bg,
                         tr(STR_DISK_NOTE2));
    return 0;
}
