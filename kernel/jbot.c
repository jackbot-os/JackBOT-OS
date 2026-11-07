#include <stdint.h>
#include <kernel/jbot.h>
#include <kernel/jbot_trained_weights.h>
#include <kernel/serial.h>
#include <kernel/pmm.h>
#include <kernel/fat32.h>

/*
 * JackBOT free AI - rule + self-trained fixed-point MLP hybrid.
 * Q16.16 fixed point, no FPU/SSE (kernel compiled -mno-sse). Offline, no PII.
 */

#define F16(x) ((int32_t)(x) << 16)
static int32_t mulf16(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 16); }
static int32_t reluf16(int32_t x) { return x > 0 ? x : 0; }

static char low(char c)
{
    if (c >= 'A' && c <= 'Z')
        return c + 32;
    return c;
}

static int is_alpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int has(const char *h, const char *w)
{
    for (int i = 0; h[i]; i++) {
        int j = 0;
        while (w[j] && low(h[i + j]) == low(w[j]))
            j++;
        if (!w[j])
            return 1;
    }
    return 0;
}

static int has_word(const char *text, const char *trig)
{
    for (int i = 0; text[i]; i++) {
        int j = 0;
        while (trig[j] && low(text[i + j]) == low(trig[j]))
            j++;
        if (!trig[j]) {
            int before_ok = (i == 0) || !is_alpha(text[i - 1]);
            int after = i + j;
            int after_ok = (text[after] == 0) || !is_alpha(text[after]);
            if (before_ok && after_ok)
                return 1;
        }
    }
    return 0;
}

#define TRIG_MAX 10
static const char *const TRIGGERS[JBOT_NF][TRIG_MAX] = {
    {"hello", "hi", "hey", "greetings", "howdy", "hiya",
     "good morning", "good evening", "good afternoon", 0},
    {"help", "assist", "support", "what can", "options", "skills",
     "what now", 0, 0, 0},
    {"time", "clock", "o'clock", 0, 0, 0, 0, 0, 0, 0},
    {"calc", "compute", "math", "plus", "multiply", "add",
     "calculate", "times", "minus", "sum"},
    {"system", "cpu", "spec", "processor", "specs", "computer",
     "machine", 0, 0, 0},
    {"ram", "memory", "mem", "info", 0, 0, 0, 0, 0, 0},
    {"weather", "temperature", "rain", "sunny", "forecast", "raining",
     0, 0, 0, 0},
    {"light", "lamp", "device", "appliance", "smart", "lights",
     0, 0, 0, 0},
    {"thanks", "thank", "appreciate", "thx", "cheers",
     0, 0, 0, 0, 0},
    {"date", "today", "calendar", "day", 0, 0, 0, 0, 0, 0},
    {"disk", "storage", "space", "drive", "hard disk",
     0, 0, 0, 0, 0},
    {"version", "build", "release", 0, 0, 0, 0, 0, 0, 0},
    {"who", "your name", "what are you", "introduce", "yourself", "name",
     0, 0, 0, 0},
    {"goodbye", "bye", "later", "farewell", "see you", "see ya",
     0, 0, 0, 0},
    {"network", "internet", "wifi", "online", "connection",
     0, 0, 0, 0, 0},
    {"file", "folder", "document", 0, 0, 0, 0, 0, 0, 0},
};

enum { I_GREET = 0, I_HELP, I_TIME, I_COMPUTE, I_SYSTEM, I_DATE,
       I_VERSION, I_DISK, I_IDENTITY, I_THANKS, I_GOODBYE,
       I_WEATHER, I_SMART };

static void extract_features(const char *input, int32_t *feats)
{
    for (int i = 0; i < JBOT_NF; i++) {
        int found = 0;
        for (int w = 0; w < TRIG_MAX && TRIGGERS[i][w]; w++) {
            if (has_word(input, TRIGGERS[i][w])) {
                found = 1;
                break;
            }
        }
        feats[i] = found ? F16(1) : 0;
    }
}

static int nn_classify(const int32_t *feats)
{
    int32_t h[JBOT_NH];
    for (int j = 0; j < JBOT_NH; j++) {
        int32_t acc = JBOT_b1[j];
        for (int i = 0; i < JBOT_NF; i++)
            acc += mulf16(JBOT_W1[i][j], feats[i]);
        h[j] = reluf16(acc);
    }
    int32_t out[JBOT_NO];
    for (int k = 0; k < JBOT_NO; k++) {
        int32_t acc = JBOT_b2[k];
        for (int j = 0; j < JBOT_NH; j++)
            acc += mulf16(JBOT_W2[j][k], h[j]);
        out[k] = acc;
    }
    int best = 0;
    for (int k = 1; k < JBOT_NO; k++)
        if (out[k] > out[best])
            best = k;
    return best;
}

static int tier;

void jbot_init(void) { tier = TIER_QUICK; }
int jbot_tier(void) { return tier; }
void jbot_set_tier(int t) { tier = t; }

int jbot_can(int cap)
{
    switch (cap) {
    case CAP_DOC:
        return tier >= TIER_INITIAL;
    case CAP_SCRIPT:
        return tier >= TIER_DAILY;
    case CAP_SYSTEM:
        return tier >= TIER_DAILY;
    case CAP_IMAGE:
        return tier >= TIER_PRO;
    }
    return 0;
}

static int putstr(char *out, int n, int p, const char *s)
{
    while (*s && p < n - 1)
        out[p++] = *s++;
    out[p] = 0;
    return p;
}

static int putu32(char *out, int n, int p, uint32_t v)
{
    (void)n;
    char tmp[16];
    int t = 0;
    if (v == 0) {
        tmp[t++] = '0';
    } else {
        while (v && t < 15) {
            tmp[t++] = (char)('0' + (v % 10));
            v /= 10;
        }
    }
    while (t > 0)
        out[p++] = tmp[--t];
    out[p] = 0;
    return p;
}

static int putu64(char *out, int n, int p, uint64_t v)
{
    (void)n;
    char tmp[24];
    int t = 0;
    if (v == 0) {
        tmp[t++] = '0';
    } else {
        while (v && t < 23) {
            tmp[t++] = (char)('0' + (v % 10));
            v /= 10;
        }
    }
    while (t > 0)
        out[p++] = tmp[--t];
    out[p] = 0;
    return p;
}

static uint8_t cmos(int r)
{
    uint8_t v;
    __asm__ volatile("outb %0,$0x70" : : "a"((uint8_t)r));
    __asm__ volatile("inb $0x71,%0" : "=a"(v));
    return v;
}
static int bcdv(uint8_t v) { return (v & 15) + ((v >> 4) * 10); }

static int build_system_reply(char *out, int outsz)
{
    uint64_t total_mb = pmm_total_bytes() / (1024u * 1024u);
    uint64_t free_mb = pmm_free_bytes() / (1024u * 1024u);
    int p = 0;
    p = putstr(out, outsz, p, "RAM total ");
    p = putu64(out, outsz, p, total_mb);
    p = putstr(out, outsz, p, " MB, free ");
    p = putu64(out, outsz, p, free_mb);
    p = putstr(out, outsz, p, " MB");
    return p > 0;
}

static int time_reply(char *out, int n)
{
    int hh = bcdv(cmos(4)), mm = bcdv(cmos(2)), ss = bcdv(cmos(0));
    int q = putstr(out, n, 0, "It is ");
    q = putu32(out, n, q, (uint32_t)hh);
    q = putstr(out, n, q, ":");
    if (mm < 10) q = putstr(out, n, q, "0");
    q = putu32(out, n, q, (uint32_t)mm);
    q = putstr(out, n, q, ":");
    if (ss < 10) q = putstr(out, n, q, "0");
    q = putu32(out, n, q, (uint32_t)ss);
    return q > 0;
}

static int date_reply(char *out, int n)
{
    int y = bcdv(cmos(9)), m = bcdv(cmos(8)), d = bcdv(cmos(7));
    int q = putstr(out, n, 0, "Today: ");
    q = putu32(out, n, q, (uint32_t)y + 2000);
    q = putstr(out, n, q, "-");
    if (m < 10) q = putstr(out, n, q, "0");
    q = putu32(out, n, q, (uint32_t)m);
    q = putstr(out, n, q, "-");
    if (d < 10) q = putstr(out, n, q, "0");
    q = putu32(out, n, q, (uint32_t)d);
    return q > 0;
}

static int disk_reply(char *out, int n)
{
    uint32_t free_mb = fat_free_bytes() / (1024u * 1024u);
    int q = putstr(out, n, 0, "Disk free ");
    q = putu32(out, n, q, free_mb);
    q = putstr(out, n, q, " MB");
    return q > 0;
}

static int try_compute(const char *in, char *out, int n)
{
    const char *p = in;
    long a = 0, b = 0;
    char op = 0;
    int have_a = 0, have_b = 0, have_op = 0;
    int sgn = 1;
    while (*p && (*p == ' ')) p++;
    while (*p && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')
                  || *p == '='))
        p++;
    while (*p && (*p == ' ')) p++;
    if (*p == '-') { sgn = -1; p++; }
    while (*p >= '0' && *p <= '9') { a = a * 10 + (*p - '0'); p++; have_a = 1; }
    a *= sgn;
    while (*p && (*p == ' ')) p++;
    if (*p == '+' || *p == '-' || *p == '*' || *p == '/') { op = *p; have_op = 1; p++; }
    if (!have_op) return 0;
    while (*p && (*p == ' ')) p++;
    sgn = 1;
    if (*p == '-') { sgn = -1; p++; }
    while (*p >= '0' && *p <= '9') { b = b * 10 + (*p - '0'); p++; have_b = 1; }
    b *= sgn;
    while (*p && (*p == ' ')) p++;
    if (!have_a || !have_b || *p != 0) return 0;

    long r = 0;
    if (op == '+') r = a + b;
    else if (op == '-') r = a - b;
    else if (op == '*') r = a * b;
    else if (op == '/') {
        if (b == 0) {
            putstr(out, n, 0, "Division by zero is undefined.");
            return 1;
        }
        r = a / b;
    }
    int o = putstr(out, n, 0, "= ");
    if (r < 0) { out[o++] = '-'; r = -r; }
    return putu32(out, n, o, (uint32_t)r);
}

static int reply_for_intent(int cls, char *out, int n)
{
    switch (cls) {
    case I_GREET:
        return putstr(out, n, 0,
            "Hi! I am JackBOT free AI. Ask me for time, math like 12*8, "
            "system info, or say 'help'.") > 0;
    case I_HELP:
        return putstr(out, n, 0,
            "Free mode: try 'time', '7+9', '5*3', 'info', 'thanks'. "
            "Smart home and weather need a paid model.") > 0;
    case I_TIME:
        return time_reply(out, n);
    case I_COMPUTE:
        return putstr(out, n, 0,
            "Say e.g. 'calc 12*8' or '5+9' and I'll compute it.") > 0;
    case I_SYSTEM:
        if (build_system_reply(out, n))
            return 1;
        return putstr(out, n, 0, "System RAM: see monitor app.") > 0;
    case I_DATE:
        return date_reply(out, n);
    case I_VERSION:
        return putstr(out, n, 0, "JackBOT OS 1.0.9") > 0;
    case I_DISK:
        return disk_reply(out, n);
    case I_IDENTITY:
        return putstr(out, n, 0,
            "I am JackBOT, the offline assistant built into JackBOT OS. "
            "Ask me for time, math, system info or disk space.") > 0;
    case I_THANKS:
        return putstr(out, n, 0, "You are welcome!") > 0;
    case I_GOODBYE:
        return putstr(out, n, 0, "Goodbye! Have a nice day.") > 0;
    case I_WEATHER:
        return putstr(out, n, 0,
            "Weather needs a network connection and a paid model.") > 0;
    case I_SMART:
        return putstr(out, n, 0,
            "Smart-home control is planned; it needs the paid tier.") > 0;
    }
    return 0;
}

int jbot_reply(const char *input, char *out, int outsz)
{
    out[0] = 0;

    if (has(input, "=") || has_word(input, "calc") ||
        has_word(input, "compute") || has_word(input, "math")) {
        char cb[96];
        if (try_compute(input, cb, sizeof(cb)) && cb[0] == '=')
            return putstr(out, outsz, 0, cb) > 0;
    }

    int32_t feats[JBOT_NF];
    extract_features(input, feats);
    int cls = nn_classify(feats);
    if (reply_for_intent(cls, out, outsz))
        return 1;

    int p = putstr(out, outsz, 0, "I'm not sure about that. ");
    p = putstr(out, outsz, p, input);
    return p > 0;
}

void jbot_self_test(void)
{
    char b[256];
    int ok = 1;
    jbot_init();

    if (!jbot_reply("time", b, sizeof(b)) || b[0] == 0) ok = 0;
    if (!jbot_reply("calc 12*8", b, sizeof(b))) ok = 0;
    if (!has(b, "96")) ok = 0;
    if (!jbot_reply("hello", b, sizeof(b)) || b[0] == 0) ok = 0;

    if (!jbot_reply("system info", b, sizeof(b)) || b[0] == 0) ok = 0;
    if (!has(b, "MB")) ok = 0;

    if (!jbot_reply("date", b, sizeof(b)) || b[0] == 0) ok = 0;
    if (!has(b, "-")) ok = 0;

    if (!jbot_reply("version", b, sizeof(b)) || b[0] == 0) ok = 0;
    if (!has(b, "1.0.9")) ok = 0;

    if (!jbot_reply("disk free", b, sizeof(b)) || b[0] == 0) ok = 0;
    if (!has(b, "MB")) ok = 0;

    if (!jbot_reply("who are you", b, sizeof(b)) || b[0] == 0) ok = 0;
    if (!has(b, "JackBOT")) ok = 0;

    if (!jbot_reply("goodbye", b, sizeof(b)) || b[0] == 0) ok = 0;
    if (!has(b, "Goodbye")) ok = 0;

    if (!jbot_reply("thanks", b, sizeof(b)) || b[0] == 0) ok = 0;
    if (!jbot_reply("help", b, sizeof(b)) || b[0] == 0) ok = 0;

    if (!jbot_reply("what is the weather", b, sizeof(b)) || b[0] == 0) ok = 0;
    if (!jbot_reply("turn on the light", b, sizeof(b)) || b[0] == 0) ok = 0;

    if (jbot_reply("hi", b, sizeof(b)) && b[0] == 0) ok = 0;

    int quick_denied = !jbot_can(CAP_DOC) && !jbot_can(CAP_SCRIPT) &&
                       !jbot_can(CAP_SYSTEM) && !jbot_can(CAP_IMAGE);
    jbot_set_tier(TIER_INITIAL);
    int initial_ok = jbot_can(CAP_DOC) && !jbot_can(CAP_IMAGE);
    jbot_set_tier(TIER_PRO);
    int pro_ok = jbot_can(CAP_DOC) && jbot_can(CAP_SCRIPT) &&
                 jbot_can(CAP_SYSTEM) && jbot_can(CAP_IMAGE);
    jbot_set_tier(TIER_QUICK);
    int reset_ok = !jbot_can(CAP_DOC);

    ok = ok && quick_denied && initial_ok && pro_ok && reset_ok;
    serial_puts(ok ? "[JBOT] test PASS\n" : "[JBOT] test FAIL\n");
}
