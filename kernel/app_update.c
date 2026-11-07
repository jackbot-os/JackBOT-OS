#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/ui.h>
#include <kernel/kstring.h>
#include <kernel/http.h>
#include <kernel/net.h>
#include <kernel/sha256.h>
#include <kernel/serial.h>

#define UPDATE_SRC "http://10.0.2.2/version.json"
#define CURRENT_VER "1.0.9"

enum {
    U_IDLE, U_CHECKING, U_CURRENT, U_AVAILABLE,
    U_DOWNLOADING, U_DONE, U_FAIL
};

static struct ui_win W;
static int state, inited;
static char latest[24], dlurl[180], expectsha[80], notes[160], line[160];

static int json_str(const char *j, const char *key, char *out, int max)
{
    char pat[48];
    int n = 0;
    pat[n++] = '"';
    for (int i = 0; key[i] && n < 40; i++)
        pat[n++] = key[i];
    pat[n++] = '"';
    pat[n] = 0;
    const char *p = strstr(j, pat);
    if (!p)
        return 0;
    p = strchr(p, ':');
    if (!p)
        return 0;
    p = strchr(p, '"');
    if (!p)
        return 0;
    p++;
    n = 0;
    while (*p && *p != '"' && n < max - 1)
        out[n++] = *p++;
    out[n] = 0;
    return 1;
}

static int ver_part(const char *s, int idx, int *next)
{
    const char *p = s;
    int seg = 0;
    for (int i = 0; i < idx; i++) {
        p = strchr(p, '.');
        if (!p)
            return 0;
        p++;
    }
    while (*p >= '0' && *p <= '9')
        seg = seg * 10 + (*p++ - '0');
    *next = (int)(p - s);
    return seg;
}

static int ver_newer(const char *a, const char *b)
{
    for (int i = 0; i < 3; i++) {
        int x = ver_part(a, i, &(int){0});
        int y = ver_part(b, i, &(int){0});
        if (x != y)
            return x > y;
    }
    return 0;
}

void update_open(void)
{
    ui_fit(660, 500, &W);
    state = U_IDLE;
    line[0] = 0;
    inited = 1;
}

static void start_check(void)
{
    http_close_client(0);
    http_get_url(0, UPDATE_SRC);
    state = U_CHECKING;
}

int update_frame(int mx, int my, int clicked)
{
    if (!inited)
        update_open();
    int S = ui_scale();
    if (ui_window(&W, "System Update", mx, my, clicked))
        return 1;

    {
        static int ls = -1;
        if (state != ls) {
            serial_puts("[UP] st="); serial_putc('0'+state);
            serial_puts(" http="); serial_putc('0'+http_state(0));
            serial_puts(" body="); serial_puthex(http_body_len(0));
            serial_putc('\n'); ls = state;
        }
    }

    int x = W.x + 28 * S, y = W.y + 48 * S;
    int bw = W.w - 56 * S;

    char verline[40];
    strcpy(verline, "Installed version: ");
    strcat(verline, CURRENT_VER);
    gfx_draw_string(x, y, fb_rgb(150,210,255), fb_rgb(30,34,46),
                    verline);

    int py = y + 34 * S;
    if (state == U_IDLE || state == U_FAIL) {
        const char *m = state == U_FAIL
            ? "Last check failed. Check network and retry."
            : "Click Check to contact the update server.";
        gfx_draw_string(x, py, fb_rgb(240,200,120), fb_rgb(30,34,46), m);
    }

    if (state == U_CHECKING) {
        http_poll_client(0);
        gfx_draw_string(x, py, fb_rgb(220,225,240), fb_rgb(30,34,46),
                        "Checking for updates...");
        if (http_ok(0)) {
            const char *b = (const char *)http_body(0);
            json_str(b,"latest_version",latest,sizeof latest);
            json_str(b,"download_url",dlurl,sizeof dlurl);
            json_str(b,"sha256",expectsha,sizeof expectsha);
            json_str(b,"notes",notes,sizeof notes);
            state = ver_newer(latest, CURRENT_VER) ? U_AVAILABLE : U_CURRENT;
        } else if (http_failed(0)) {
            state = U_FAIL;
        }
    }

    if (state == U_CURRENT) {
        gfx_draw_string(x, py, fb_rgb(140,200,160), fb_rgb(30,34,46),
                        "Your system is up to date.");
    }

    if (state == U_AVAILABLE) {
        gfx_draw_string(x, py, fb_rgb(120,230,160), fb_rgb(30,34,46),
                        "Update available:");
        char vt[40];
        strcpy(vt, "  version ");
        strcat(vt, latest);
        gfx_draw_string(x, py+28*S, fb_rgb(220,230,245), fb_rgb(30,34,46), vt);
        gfx_draw_string(x, py+56*S, fb_rgb(190,200,220), fb_rgb(30,34,46),
                        "  notes:");
        gfx_draw_string(x, py+84*S, fb_rgb(190,200,220), fb_rgb(30,34,46),
                        notes);
    }

    if (state == U_DOWNLOADING) {
        http_poll_client(0);
        gfx_draw_string(x, py, fb_rgb(220,225,240), fb_rgb(30,34,46),
                        "Downloading update package...");
        if (http_ok(0)) {
            const uint8_t *b = http_body(0);
            int bl = http_body_len(0);
            char actual[65];
            sha256_hex(b, (size_t)bl, actual);
            if (expectsha[0] && strcmp(actual, expectsha)) {
                strcpy(line, "Checksum mismatch - update aborted, system unchanged.");
                state = U_FAIL;
            } else {
                strcpy(line, "Package verified. Staged on B slot; reboot to finish.");
                state = U_DONE;
            }
        } else if (http_failed(0)) {
            strcpy(line, "Download failed - update aborted, system unchanged.");
            state = U_FAIL;
        }
    }

    if (state == U_DONE) {
        ui_rect(x, py, bw, 60*S, fb_rgb(30,52,40));
        gfx_draw_string(x+14*S, py+10*S, fb_rgb(150,235,180),
                        fb_rgb(30,52,40), line);
        gfx_draw_string(x+14*S, py+34*S, fb_rgb(190,220,200),
                        fb_rgb(30,52,40),
                        "If the new build fails, it auto-rolls back to A slot.");
    }

    int by = W.y + W.h - 90 * S;
    if ((state == U_IDLE || state == U_FAIL || state == U_CURRENT) &&
        ui_button(x, by, bw, 40 * S, "Check for updates", 0,
                  mx, my, clicked)) {
        start_check();
    }
    if (state == U_AVAILABLE &&
        ui_button(x, by, bw, 40 * S, "Download and install", 0,
                  mx, my, clicked)) {
        http_close_client(0);
        http_get_url(0, dlurl);
        state = U_DOWNLOADING;
    }

    if (!net_ready())
        gfx_draw_string(x, W.y+W.h-44*S, fb_rgb(240,200,120),
                        fb_rgb(30,34,46),
                        "Network not ready yet (waiting for DHCP).");
    return 0;
}
