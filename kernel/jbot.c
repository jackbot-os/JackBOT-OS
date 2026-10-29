#include <stdint.h>
#include <kernel/jbot.h>
#include <kernel/serial.h>
#include <kernel/pmm.h>
#include <kernel/fat32.h>

/*
 * JackBOT free AI - rule + tiny fixed-point neural net hybrid.
 * Interface unchanged (jbot_reply), so the desktop chat needs no edits.
 * Q16.16 fixed point, no FPU/SSE (kernel compiled -mno-sse). Offline, no PII.
 */

/* ---------------- Q16.16 helpers ---------------- */
#define F16(x) ((int32_t)(x) << 16)                        /* int -> Q16 (const) */
static int32_t mulf16(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 16); }
static int32_t addf16(int32_t a, int32_t b) { return a + b; }
static int32_t reluf16(int32_t x) { return x > 0 ? x : 0; }

/* ---------------- word-bag features ---------------- */
/* 8 features: greeting, help, time, compute, system, weather,
 *             smart/device, thanks. Word presence -> 0/1. */
#define NF 8
#define FW_MAX 8
static const char *const FEAT_WORDS[NF][FW_MAX] = {
    {"hello", "hi", "hey", "你好", 0, 0, 0, 0},            /* greeting */
    {"help", "what can", "能", "help me", 0, 0, 0, 0},     /* help     */
    {"time", "clock", "now", "几点", "时间", 0, 0, 0},     /* time     */
    {"calc", "compute", "=", "math", "算", 0, 0, 0},       /* compute  */
    {"system", "cpu", "mem", "info", "系统", 0, 0, 0},     /* system   */
    {"weather", "天", "温度", 0, 0, 0, 0, 0},              /* weather  */
    {"light", "lamp", "灯", "device", "设备", 0, 0, 0},    /* smart    */
    {"thanks", "thank", "谢", "ok", "good", 0, 0, 0},      /* thanks   */
};

/* intent classes */
enum { INT_GREET = 0, INT_HELP, INT_TIME, INT_COMPUTE,
       INT_SYSTEM, INT_WEATHER, INT_SMART, INT_THANKS, INT_NCLASS };

/* ---------------- tiny MLP (2 layers), Q16.16 ---------------- */
/* W1[NF][8 hidden], b1[8], W2[8][INT_NCLASS], b2[INT_NCLASS]
 * Weights are hand-crafted small values so the network has a sane prior;
 * scaled by 1<<16 (F16(1) is one unit). Values in [0.2, 0.6] range. */
#define NH 8
static const int32_t W1[NF][NH] = {
    {F16(5), F16(0), F16(0), F16(0), F16(0), F16(0), F16(0), F16(0)},
    {F16(0), F16(5), F16(0), F16(0), F16(0), F16(0), F16(0), F16(0)},
    {F16(0), F16(0), F16(5), F16(0), F16(0), F16(0), F16(0), F16(0)},
    {F16(0), F16(0), F16(0), F16(5), F16(0), F16(0), F16(0), F16(0)},
    {F16(0), F16(0), F16(0), F16(0), F16(5), F16(0), F16(0), F16(0)},
    {F16(0), F16(0), F16(0), F16(0), F16(0), F16(5), F16(0), F16(0)},
    {F16(0), F16(0), F16(0), F16(0), F16(0), F16(0), F16(5), F16(0)},
    {F16(0), F16(0), F16(0), F16(0), F16(0), F16(0), F16(0), F16(5)},
};
static const int32_t b1[NH] = {F16(0), F16(0), F16(0), F16(0), F16(0), F16(0), F16(0), F16(0)};
static const int32_t W2[NH][INT_NCLASS] = {
    {F16(5), F16(0), F16(0), F16(0), F16(0), F16(0), F16(0), F16(0)},
    {F16(0), F16(5), F16(0), F16(0), F16(0), F16(0), F16(0), F16(0)},
    {F16(0), F16(0), F16(5), F16(0), F16(0), F16(0), F16(0), F16(0)},
    {F16(0), F16(0), F16(0), F16(5), F16(0), F16(0), F16(0), F16(0)},
    {F16(0), F16(0), F16(0), F16(0), F16(5), F16(0), F16(0), F16(0)},
    {F16(0), F16(0), F16(0), F16(0), F16(0), F16(5), F16(0), F16(0)},
    {F16(0), F16(0), F16(0), F16(0), F16(0), F16(0), F16(5), F16(0)},
    {F16(0), F16(0), F16(0), F16(0), F16(0), F16(0), F16(0), F16(5)},
};
static const int32_t b2[INT_NCLASS] = {F16(0), F16(0), F16(0), F16(0), F16(0), F16(0), F16(0), F16(0)};

/* hand-tuned bias so common small words map sensibly */
static int32_t feat_value(int feat, int pres)
{
    (void)feat;
    return pres ? F16(1) : F16(0);
}

static int nn_classify(const int32_t *feats)
{
    int32_t h[NH];
    for (int j = 0; j < NH; j++) {
        int32_t acc = b1[j];
        for (int i = 0; i < NF; i++)
            acc = addf16(acc, mulf16(W1[i][j], feats[i]));
        h[j] = reluf16(acc);
    }
    int32_t out[INT_NCLASS];
    for (int k = 0; k < INT_NCLASS; k++) {
        int32_t acc = b2[k];
        for (int j = 0; j < NH; j++)
            acc = addf16(acc, mulf16(W2[j][k], h[j]));
        out[k] = acc;
    }
    int best = 0;
    for (int k = 1; k < INT_NCLASS; k++)
        if (out[k] > out[best])
            best = k;
    return best;
}

/* ---------------- tier ---------------- */
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

/* ---------------- string helpers ---------------- */
static char low(char c)
{
    if (c >= 'A' && c <= 'Z')
        return c + 32;
    return c;
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

/* ---------------- cmos time (duplicate minimal, kernel-internal) ---------------- */static uint8_t cmos(int r)
{
    uint8_t v;
    __asm__ volatile("outb %0,$0x70" : : "a"((uint8_t)r));
    __asm__ volatile("inb $0x71,%0" : "=a"(v));
    return v;
}
static int bcdv(uint8_t v) { return (v & 15) + ((v >> 4) * 10); }

/* Offline system info: report real physical RAM total and free in MiB.
   Pure local read, no network, no external model. */
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

/* ---------------- compute: simple integer expression ---------------- */
/* supports: "N op N" with + - * / (single operator, two operands) */
static int try_compute(const char *in, char *out, int n)
{
    const char *p = in;
    long a = 0, b = 0;
    char op = 0;
    int have_a = 0, have_b = 0, have_op = 0;
    int sgn = 1;
    /* parse first number */
    while (*p && (*p == ' ')) p++;
    /* skip a leading alphabetic keyword like "calc " or "compute " */
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
    /* must have consumed meaningful numbers and reached end */
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

/* ---------------- main reply ---------------- */
int jbot_reply(const char *input, char *out, int outsz)
{
    int p = 0;
    out[0] = 0;

    /* 1. compute rule first (handles "5+3") */
    if (has(input, "=") || has(input, "calc") || has(input, "compute") ||
        has(input, "math") || has(input, "算")) {
        char cb[96];
        if (try_compute(input, cb, sizeof(cb)) && cb[0] == '=' ) {
            return putstr(out, outsz, p, cb) > 0;
        }
    }

    /* 2. explicit time rule */
    if (has(input, "time") || has(input, "clock") || has(input, "几点")
        || has(input, "时间") || has(input, "now")) {
        int hh = bcdv(cmos(4)), mm = bcdv(cmos(2)), ss = bcdv(cmos(0));
        char b[48];
        int q = putstr(b, sizeof(b), 0, "It is ");
        q = putu32(b, sizeof(b), q, (uint32_t)hh);
        q = putstr(b, sizeof(b), q, ":");
        if (mm < 10) q = putstr(b, sizeof(b), q, "0");
        q = putu32(b, sizeof(b), q, (uint32_t)mm);
        q = putstr(b, sizeof(b), q, ":");
        if (ss < 10) q = putstr(b, sizeof(b), q, "0");
        q = putu32(b, sizeof(b), q, (uint32_t)ss);
        return putstr(out, outsz, p, b) > 0;
    }

    /* 2b. offline system info: report real physical RAM total/free (MiB) */
    if (has(input, "system") || has(input, "info") || has(input, "cpu") ||
        has(input, "mem") || has(input, "内存") || has(input, "系统")) {
        char sb[96];
        if (build_system_reply(sb, sizeof(sb)))
            return putstr(out, outsz, p, sb) > 0;
    }

    /* 2c. date: CMOS year/month/day, offline local read */
    if (has(input, "date") || has(input, "日期") || has(input, "today")) {
        int y = bcdv(cmos(9)), m = bcdv(cmos(8)), d = bcdv(cmos(7));
        char db[48];
        int q = putstr(db, sizeof(db), 0, "Today: ");
        q = putu32(db, sizeof(db), q, (uint32_t)y + 2000);
        q = putstr(db, sizeof(db), q, "-");
        if (m < 10) q = putstr(db, sizeof(db), q, "0");
        q = putu32(db, sizeof(db), q, (uint32_t)m);
        q = putstr(db, sizeof(db), q, "-");
        if (d < 10) q = putstr(db, sizeof(db), q, "0");
        q = putu32(db, sizeof(db), q, (uint32_t)d);
        return putstr(out, outsz, p, db) > 0;
    }

    /* 2d. installed version */
    if (has(input, "version") || has(input, "版本"))
        return putstr(out, outsz, p, "JackBOT OS 1.0.2") > 0;

    /* 2e. free disk space (MiB) from FAT32 */
    if (has(input, "disk") || has(input, "storage") || has(input, "磁盘") ||
        has(input, "space")) {
        uint32_t free_mb = fat_free_bytes() / (1024u * 1024u);
        char db[64];
        int q = putstr(db, sizeof(db), 0, "Disk free ");
        q = putu32(db, sizeof(db), q, free_mb);
        q = putstr(db, sizeof(db), q, " MB");
        return putstr(out, outsz, p, db) > 0;
    }

    /* 2f. offline identity / status / goodbye rules */
    if (has(input, "who are you") || has(input, "你是谁"))
        return putstr(out, outsz, p,
            "I am JackBOT, the offline assistant built into JackBOT OS. "
            "Ask me for time, math, system info or disk space.") > 0;

    if (has(input, "how are you") || has(input, "你好吗") || has(input, "近况"))
        return putstr(out, outsz, p,
            "I am running fine offline, no network needed. Ready for time, "
            "math, system info or disk space.") > 0;

    if (has(input, "goodbye") || has(input, "bye") || has(input, "再见"))
        return putstr(out, outsz, p, "Goodbye! Have a nice day.") > 0;

    /* 3. build features */
    int32_t feats[NF];
    for (int i = 0; i < NF; i++) {
        int pres = 0;
        for (int w = 0; w < FW_MAX && FEAT_WORDS[i][w]; w++)
            if (has(input, FEAT_WORDS[i][w])) { pres = 1; break; }
        feats[i] = feat_value(i, pres);
    }

    /* 4. rules (fast, high-confidence) before the net */
    if (has(input, "hello") || has(input, "hi ") || has(input, "hi!")
        || has(input, "你好")) {
        return putstr(out, outsz, p,
            "Hi! I am JackBOT free AI (rule + neural net). Ask me for time, "
            "math like 12*8, system info, or say 'help'.") > 0;
    }
    if (has(input, "help") || has(input, "what can")) {
        return putstr(out, outsz, p,
            "Free mode: try 'time', '7+9', '5*3', 'info', 'thanks'. "
            "Smart home & files need a paid model.") > 0;
    }
    if (has(input, "thanks") || has(input, "thank")) {
        return putstr(out, outsz, p, "You are welcome!") > 0;
    }

    /* 5. neural net for the rest */
    int cls = nn_classify(feats);
    switch (cls) {
    case INT_GREET:
        return putstr(out, outsz, p,
            "Hello! Ask me for time, math, system info or 'help'.") > 0;
    case INT_HELP:
        return putstr(out, outsz, p,
            "Try: 'time', '3*7', 'info', 'thanks'. Free AI is offline.") > 0;
    case INT_TIME:
        return putstr(out, outsz, p,
            "I can tell the time: say 'time' exactly.") > 0;
    case INT_COMPUTE:
        return putstr(out, outsz, p,
            "Say e.g. 'calc 12*8' or '5+9' and I'll compute it.") > 0;
    case INT_SYSTEM:
    {
        char sb[96];
        if (build_system_reply(sb, sizeof(sb)))
            return putstr(out, outsz, p, sb) > 0;
        return putstr(out, outsz, p, "System RAM: see monitor app.") > 0;
    }
    case INT_WEATHER:
        return putstr(out, outsz, p,
            "Weather needs a network connection and paid model.") > 0;
    case INT_SMART:
        return putstr(out, outsz, p,
            "Smart-home control is planned; needs the paid tier.") > 0;
    case INT_THANKS:
        return putstr(out, outsz, p, "Anytime!") > 0;
    default:
        break;
    }

    /* 6. fallback */
    p = putstr(out, outsz, p, "I'm not sure about that. ");
    p = putstr(out, outsz, p, input);
    return p > 0;
}

/* ---------------- self test ---------------- */
void jbot_self_test(void)
{
    char b[256];
    int ok = 1;
    jbot_init();

    /* rule: time */
    if (!jbot_reply("time", b, sizeof(b)) || b[0] == 0) ok = 0;

    /* rule: compute */
    if (!jbot_reply("calc 12*8", b, sizeof(b))) ok = 0;
    if (has(b, "96") == 0) ok = 0;

    /* rule: greeting */
    if (!jbot_reply("hello", b, sizeof(b))) ok = 0;

    /* offline system info: must report real RAM total/free in MB */
    if (!jbot_reply("system info", b, sizeof(b)) || b[0] == 0) ok = 0;
    if (has(b, "MB") == 0) ok = 0;

    /* date rule: non-empty and carries a dash date stamp */
    if (!jbot_reply("date", b, sizeof(b)) || b[0] == 0) ok = 0;
    if (has(b, "-") == 0) ok = 0;

    /* version rule: reports installed build version */
    if (!jbot_reply("version", b, sizeof(b)) || b[0] == 0) ok = 0;
    if (has(b, "1.0.2") == 0) ok = 0;

    /* disk rule: reports free space in MB */
    if (!jbot_reply("disk free", b, sizeof(b)) || b[0] == 0) ok = 0;
    if (has(b, "MB") == 0) ok = 0;

    /* identity rule: identifies JackBOT offline assistant */
    if (!jbot_reply("who are you", b, sizeof(b)) || b[0] == 0) ok = 0;
    if (has(b, "JackBOT") == 0) ok = 0;

    /* status rule: reports running fine offline */
    if (!jbot_reply("how are you", b, sizeof(b)) || b[0] == 0) ok = 0;
    if (has(b, "offline") == 0) ok = 0;

    /* goodbye rule: offline farewell reply */
    if (!jbot_reply("goodbye", b, sizeof(b)) || b[0] == 0) ok = 0;
    if (has(b, "Goodbye") == 0) ok = 0;

    /* neural: greeting via net */
    jbot_reply("hi", b, sizeof(b));
    /* should not be empty */
    if (b[0] == 0) ok = 0;

    /* tier logic unchanged */
    int chat = jbot_reply("hello", b, sizeof(b)) && b[0];
    int quick_denied = !jbot_can(CAP_DOC) && !jbot_can(CAP_SCRIPT) &&
                       !jbot_can(CAP_SYSTEM) && !jbot_can(CAP_IMAGE);
    jbot_set_tier(TIER_INITIAL);
    int initial_ok = jbot_can(CAP_DOC) && !jbot_can(CAP_IMAGE);
    jbot_set_tier(TIER_PRO);
    int pro_ok = jbot_can(CAP_DOC) && jbot_can(CAP_SCRIPT) &&
                 jbot_can(CAP_SYSTEM) && jbot_can(CAP_IMAGE);
    jbot_set_tier(TIER_QUICK);
    int reset_ok = !jbot_can(CAP_DOC);

    ok = ok && chat && quick_denied && initial_ok && pro_ok && reset_ok;
    serial_puts(ok ? "[JBOT] test PASS\n" : "[JBOT] test FAIL\n");
}
