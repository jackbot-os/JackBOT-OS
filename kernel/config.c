#include <stdint.h>
#include <kernel/config.h>
#include <kernel/fat32.h>
#include <kernel/serial.h>

static int lang;
static int font;

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

static void parse(const char *b)
{
    for (int i = 0; b[i]; i++) {
        if (b[i] == 'L' && b[i + 1] == '=' && b[i + 2] >= '0')
            lang = b[i + 2] - '0';
        if (b[i] == 'F' && b[i + 1] == '=' && b[i + 2] >= '0')
            font = b[i + 2] - '0';
    }
}

void config_init(void)
{
    lang = 0;
    font = 0;
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

void config_set_language(int l) { lang = l; }
void config_set_font(int f) { font = f; }

void config_save(void)
{
    char b[48];
    int p = 0;
    b[p++] = 'L'; b[p++] = '='; p += u2s(lang, b + p); b[p++] = '\n';
    b[p++] = 'F'; b[p++] = '='; p += u2s(font, b + p); b[p++] = '\n';
    fat_write_file("JBOT.CFG", b, p);
}

void config_self_test(void)
{
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
    config_set_language(0);
    config_set_font(0);
    config_save();
    serial_puts(ok ? "[CFG] test PASS\n" : "[CFG] test FAIL\n");
}
