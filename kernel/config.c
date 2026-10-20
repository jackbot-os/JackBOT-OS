#include <stdint.h>
#include <kernel/config.h>
#include <kernel/fat32.h>
#include <kernel/serial.h>

static int lang;
static int font;
static int tz;
static int fw;
static int oobe;
static int kb;

static int contains(const char *h, const char *n)
{
    for (int i = 0; h[i]; i++) {
        int j = 0;
        while (n[j] && h[i + j] == n[j])
            j++;
        if (!n[j])
            return 1;
    }
    return 0;
}

static int u2s(int v, char *out)
{
    char tmp[8];
    int n = 0;
    if (v == 0)
        tmp[n++] = '0';
    while (v > 0) {
        tmp[n++] = '0' + v % 10;
        v /= 10;
    }
    for (int i = 0; i < n; i++)
        out[i] = tmp[n - 1 - i];
    return n;
}

static int num_after(const char *b, int i, int *out)
{
    int k = i + 2, v = 0, got = 0;
    while (b[k] >= '0' && b[k] <= '9') {
        v = v * 10 + b[k] - '0';
        k++;
        got = 1;
    }
    if (got)
        *out = v;
    return got;
}

static void parse(const char *b)
{
    for (int i = 0; b[i]; i++) {
        if (b[i] == 'L' && b[i + 1] == '=' && b[i + 2] >= '0')
            num_after(b, i, &lang);
        if (b[i] == 'F' && b[i + 1] == '=' && b[i + 2] >= '0')
            num_after(b, i, &font);
        if (b[i] == 'T' && b[i + 1] == '=') {
            int v = 0, k = i + 2, got = 0;
            while (b[k] >= '0' && b[k] <= '9') {
                v = v * 10 + b[k] - '0';
                k++;
                got = 1;
            }
            if (got)
                tz = v;
        }
        if (b[i] == 'W' && b[i + 1] == '=' && b[i + 2] >= '0')
            fw = b[i + 2] - '0';
        if (b[i] == 'O' && b[i + 1] == '=' && b[i + 2] >= '0')
            oobe = b[i + 2] - '0';
        if (b[i] == 'K' && b[i + 1] == '=' && b[i + 2] >= '0')
            num_after(b, i, &kb);
    }
}

void config_init(void)
{
    lang = 0;
    font = 0;
    tz = 20;
    fw = 1;
    oobe = 0;
    kb = 0;
    if (fat_exists("JBOT.CFG")) {
        char b[256];
        uint32_t n = 0;
        if (fat_read_file("JBOT.CFG", b, sizeof(b) - 1, &n) == 0) {
            b[n] = 0;
            parse(b);
        }
    } else {
        config_save();
    }
}

int config_language(void) { return lang; }
int config_font(void) { return font; }
int config_tz(void) { return tz; }
int config_fw(void) { return fw; }
int config_oobe_done(void) { return oobe; }
int config_keyboard(void) { return kb; }

void config_set_language(int l) { lang = l; }
void config_set_font(int f) { font = f; }
void config_set_tz(int v) { tz = v; }
void config_set_fw(int v) { fw = v; }
void config_set_oobe_done(int v) { oobe = v; }
void config_set_keyboard(int v) { kb = v; }

void config_save(void)
{
    char b[80];
    int p = 0;
    b[p++] = 'L'; b[p++] = '='; p += u2s(lang, b + p); b[p++] = '\n';
    b[p++] = 'F'; b[p++] = '='; p += u2s(font, b + p); b[p++] = '\n';
    b[p++] = 'T'; b[p++] = '='; p += u2s(tz, b + p); b[p++] = '\n';
    b[p++] = 'W'; b[p++] = '='; p += u2s(fw, b + p); b[p++] = '\n';
    b[p++] = 'O'; b[p++] = '='; b[p++] = oobe ? '1' : '0'; b[p++] = '\n';
    b[p++] = 'K'; b[p++] = '='; p += u2s(kb, b + p); b[p++] = '\n';
    fat_write_file("JBOT.CFG", b, p);
}

void config_self_test(void)
{
    /* preserve the user's real configuration across the write test so the
       language chosen at install is never reset back to English */
    int sl = lang, sf = font, st = tz, sw = fw;
    config_init();
    config_set_language(1);
    config_set_font(2);
    config_save();
    char b[256];
    uint32_t n = 0;
    int ok = 0;
    if (fat_read_file("JBOT.CFG", b, sizeof(b) - 1, &n) == 0) {
        b[n] = 0;
        ok = contains(b, "L=1") && contains(b, "F=2");
    }
    config_set_language(sl);
    config_set_font(sf);
    config_set_tz(st);
    config_set_fw(sw);
    config_save();
    serial_puts(ok ? "[CFG] test PASS\n" : "[CFG] test FAIL\n");
}
