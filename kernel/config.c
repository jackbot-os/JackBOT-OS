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
static int hp;
static int region;
static int volume;
static int mic_perm;
static int voice_perm;
static int cam_perm;
static int loc_perm;
static int net_perm;
static int glass = 1;

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

static int bit_after(const char *b, int i)
{
    return b[i + 2] == '1' ? 1 : 0;
}

static void parse(const char *b)
{
    for (int i = 0; b[i]; i++) {
        if (b[i] == 'L' && b[i + 1] == '=' && b[i + 2] >= '0')
            num_after(b, i, &lang);
        if (b[i] == 'F' && b[i + 1] == '=' && b[i + 2] >= '0')
            num_after(b, i, &font);
        if (b[i] == 'T' && b[i + 1] == '=' && b[i + 2] >= '0')
            num_after(b, i, &tz);
        if (b[i] == 'W' && b[i + 1] == '=' && b[i + 2] >= '0')
            fw = bit_after(b, i);
        if (b[i] == 'O' && b[i + 1] == '=' && b[i + 2] >= '0')
            oobe = bit_after(b, i);
        if (b[i] == 'K' && b[i + 1] == '=' && b[i + 2] >= '0')
            num_after(b, i, &kb);
        if (b[i] == 'H' && b[i + 1] == '=' && b[i + 2] >= '0')
            hp = bit_after(b, i);
        if (b[i] == 'R' && b[i + 1] == '=' && b[i + 2] >= '0')
            num_after(b, i, &region);
        if (b[i] == 'V' && b[i + 1] == '=' && b[i + 2] >= '0')
            num_after(b, i, &volume);
        if (b[i] == 'M' && b[i + 1] == '=' && b[i + 2] >= '0')
            mic_perm = bit_after(b, i);
        if (b[i] == 'P' && b[i + 1] == '=' && b[i + 2] >= '0')
            voice_perm = bit_after(b, i);
        if (b[i] == 'C' && b[i + 1] == '=' && b[i + 2] >= '0')
            cam_perm = bit_after(b, i);
        if (b[i] == 'A' && b[i + 1] == '=' && b[i + 2] >= '0')
            loc_perm = bit_after(b, i);
        if (b[i] == 'N' && b[i + 1] == '=' && b[i + 2] >= '0')
            net_perm = bit_after(b, i);
        if (b[i] == 'G' && b[i + 1] == '=' && b[i + 2] >= '0')
            glass = bit_after(b, i);
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
    hp = 0;
    region = 0;
    volume = 70;
    mic_perm = 0;
    voice_perm = 0;
    cam_perm = 0;
    loc_perm = 0;
    net_perm = 1;
    glass = 1;
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
int config_honeypot(void) { return hp; }
int config_region(void) { return region; }
int config_volume(void) { return volume; }
int config_mic_perm(void) { return mic_perm; }
int config_voice_perm(void) { return voice_perm; }
int config_cam_perm(void) { return cam_perm; }
int config_loc_perm(void) { return loc_perm; }
int config_net_perm(void) { return net_perm; }
int config_glass(void) { return glass; }

void config_set_language(int l) { lang = l; }
void config_set_font(int f) { font = f; }
void config_set_tz(int v) { tz = v; }
void config_set_fw(int v) { fw = v ? 1 : 0; }
void config_set_oobe_done(int v) { oobe = v ? 1 : 0; }
void config_set_keyboard(int v) { kb = v; }
void config_set_honeypot(int v) { hp = v ? 1 : 0; }
void config_set_region(int v) { region = v; }
void config_set_volume(int v)
{
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    volume = v;
}
void config_set_mic_perm(int v) { mic_perm = v ? 1 : 0; }
void config_set_voice_perm(int v) { voice_perm = v ? 1 : 0; }
void config_set_cam_perm(int v) { cam_perm = v ? 1 : 0; }
void config_set_loc_perm(int v) { loc_perm = v ? 1 : 0; }
void config_set_net_perm(int v) { net_perm = v ? 1 : 0; }
void config_set_glass(int v) { glass = v ? 1 : 0; }

static void put_line(char *b, int *p, char tag, int v)
{
    b[(*p)++] = tag;
    b[(*p)++] = '=';
    *p += u2s(v, b + *p);
    b[(*p)++] = '\n';
}

void config_save(void)
{
    char b[160];
    int p = 0;
    put_line(b, &p, 'L', lang);
    put_line(b, &p, 'F', font);
    put_line(b, &p, 'T', tz);
    put_line(b, &p, 'W', fw);
    put_line(b, &p, 'O', oobe);
    put_line(b, &p, 'K', kb);
    put_line(b, &p, 'H', hp);
    put_line(b, &p, 'R', region);
    put_line(b, &p, 'V', volume);
    put_line(b, &p, 'M', mic_perm);
    put_line(b, &p, 'P', voice_perm);
    put_line(b, &p, 'C', cam_perm);
    put_line(b, &p, 'A', loc_perm);
    put_line(b, &p, 'N', net_perm);
    put_line(b, &p, 'G', glass);
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
