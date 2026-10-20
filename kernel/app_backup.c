#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ui.h>
#include <kernel/fat32.h>
#include <kernel/config.h>

static struct ui_win W;
static int baks[99];
static int nb;
static char msg[80];
static int inited;

static void bak_name(int n, char *nm)
{
    const char *d = "0123456789";
    nm[0] = 'B'; nm[1] = 'A'; nm[2] = 'K';
    nm[3] = d[n / 10 % 10]; nm[4] = d[n % 10];
    nm[5] = '.'; nm[6] = 'C'; nm[7] = 'F'; nm[8] = 'G'; nm[9] = 0;
}

static void scan(void)
{
    nb = 0;
    for (int n = 1; n <= 99; n++) {
        char nm[13];
        bak_name(n, nm);
        if (fat_exists(nm))
            baks[nb++] = n;
    }
}

static int do_backup(void)
{
    if (!fat_exists("JBOT.CFG"))
        return -1;
    char data[256];
    uint32_t n = 0;
    if (fat_read_file("JBOT.CFG", data, sizeof(data) - 1, &n))
        return -2;
    int target = -1;
    for (int i = 1; i <= 99; i++) {
        char nm[13];
        bak_name(i, nm);
        if (!fat_exists(nm)) {
            target = i;
            break;
        }
    }
    if (target < 0)
        return -3;
    char nm[13];
    bak_name(target, nm);
    if (fat_write_file(nm, data, n))
        return -4;
    return target;
}

static int do_restore(int n)
{
    char nm[13];
    bak_name(n, nm);
    char data[256];
    uint32_t len = 0;
    if (fat_read_file(nm, data, sizeof(data) - 1, &len))
        return -1;
    if (fat_write_file("JBOT.CFG", data, len))
        return -2;
    config_init();
    return 0;
}

void backup_open(void)
{
    ui_fit(600, 470, &W);
    msg[0] = 0;
    scan();
    inited = 1;
}

int backup_frame(int mx, int my, int clicked)
{
    if (!inited)
        backup_open();
    int S = ui_scale();
    if (ui_window(&W, "Backup & Restore", mx, my, clicked))
        return 1;

    if (ui_button(W.x + 30 * S, W.y + 36 * S, 200 * S, 38 * S,
                  "Backup now", 0, mx, my, clicked)) {
        int rc = do_backup();
        if (rc > 0) {
            char nm[13];
            bak_name(rc, nm);
            const char *pre = "Created ";
            int p = 0;
            while (pre[p]) {
                msg[p] = pre[p];
                p++;
            }
            for (int q = 0; nm[q] && p < 70; q++)
                msg[p++] = nm[q];
            msg[p] = 0;
            scan();
        } else {
            const char *m = "Backup failed (config missing or disk full).";
            int p = 0;
            while (m[p] && p < 70) {
                msg[p] = m[p];
                p++;
            }
            msg[p] = 0;
        }
    }

    int ly = W.y + 96 * S;
    gfx_draw_string(W.x + 30 * S, ly, fb_rgb(150, 210, 255),
                    fb_rgb(30, 34, 46), "Saved backups:");
    ly += 28 * S;
    if (nb == 0)
        gfx_draw_string(W.x + 40 * S, ly, fb_rgb(180, 190, 210),
                        fb_rgb(30, 34, 46), "(none yet)");
    for (int i = 0; i < nb; i++) {
        char nm[13];
        bak_name(baks[i], nm);
        int rowy = ly + i * 40 * S;
        gfx_draw_string(W.x + 40 * S, rowy + 8 * S,
                        fb_rgb(220, 225, 235), fb_rgb(30, 34, 46), nm);
        if (ui_button(W.x + W.w - 180 * S, rowy, 140 * S, 32 * S,
                      "Restore", 0, mx, my, clicked)) {
            if (do_restore(baks[i])) {
                const char *m = "Restore failed.";
                int p = 0;
                while (m[p] && p < 70) {
                    msg[p] = m[p];
                    p++;
                }
                msg[p] = 0;
            } else {
                const char *pre = "Restored ";
                int p = 0;
                while (pre[p]) {
                    msg[p] = pre[p];
                    p++;
                }
                for (int q = 0; nm[q] && p < 70; q++)
                    msg[p++] = nm[q];
                msg[p] = 0;
            }
        }
    }
    if (msg[0])
        gfx_draw_string(W.x + 30 * S, W.y + W.h - 40 * S,
                        fb_rgb(150, 220, 160), fb_rgb(30, 34, 46), msg);
    return 0;
}
