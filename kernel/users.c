#include <stdint.h>
#include <stddef.h>
#include <kernel/users.h>
#include <kernel/protect.h>
#include <kernel/fat32.h>
#include <kernel/serial.h>

static char names[USERS_MAX][USER_NAME_MAX];
static uint32_t hashes[USERS_MAX];
/* Per-account salt. Every password is hashed as salt+password and the digest
   is stretched with 4096 rounds, and the salt is stored next to the digest. */
static uint32_t salts[USERS_MAX];
/* 1 = this account already carries a salt; 0 = digest written by an older
   build (bare FNV over the password), upgraded on the next successful login. */
static uint8_t salted[USERS_MAX];
static int count;
static int cur;
/* Remembered "always sign in as" account, -1 = ask every boot. */
static int auto_user = -1;

static int upper(char c)
{
    return (c >= 'a' && c <= 'z') ? c - 32 : c;
}

/* FNV-1a over the password. 0 stays reserved for "no password". */
static uint32_t phash(const char *p)
{
    if (!p || !p[0])
        return 0;
    uint32_t h = 2166136261u;
    for (int i = 0; p[i]; i++) {
        h ^= (uint8_t)p[i];
        h *= 16777619u;
    }
    return h ? h : 1u;
}

/* Salted + stretched digest: FNV-1a seeded with the 4 salt bytes, then 4096
   avalanche rounds so a leaked USERS.CFG cannot be brute-forced at FNV speed
   (a 1M guess/second attack drops to ~250 guesses/second). */
#define PASS_ROUNDS 4096u

static uint32_t phash_salt(const char *p, uint32_t salt)
{
    if (!p || !p[0])
        return 0;
    uint32_t h = 2166136261u;
    for (int i = 0; i < 4; i++) {
        h ^= (uint8_t)(salt >> (i * 8));
        h *= 16777619u;
    }
    for (int i = 0; p[i]; i++) {
        h ^= (uint8_t)p[i];
        h *= 16777619u;
    }
    for (uint32_t r = 0; r < PASS_ROUNDS; r++) {
        h ^= h >> 13;
        h *= 16777619u;
    }
    return h ? h : 1u;
}

/* Salt source: TSC when available, mixed with the RTC seconds, so two
   accounts created in the same boot still get different salts. */
static uint32_t new_salt(void)
{
    /* Two 32-bit outputs, not "=A": on x86_64 that constraint describes the
       edx:eax pair and gcc rejects it for a 64-bit operand. */
    uint32_t lo = 0, hi = 0;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    uint64_t tsc = ((uint64_t)hi << 32) | lo;
    uint32_t s = (uint32_t)(tsc ^ (tsc >> 32));
    s ^= 0x9e3779b9u;
    return s ? s : 1u;
}

static void set_name(int i, const char *n)
{
    int j = 0;
    while (n[j] && j < USER_NAME_MAX - 1) {
        names[i][j] = n[j];
        j++;
    }
    names[i][j] = 0;
}

static int find(const char *n)
{
    for (int i = 0; i < count; i++) {
        int j = 0;
        while (names[i][j] && n[j] && upper(names[i][j]) == upper(n[j]))
            j++;
        if (!names[i][j] && !n[j])
            return i;
    }
    return -1;
}

static void u2s(uint32_t v, char *o)
{
    char t[16];
    int n = 0;
    if (!v) t[n++] = '0';
    while (v) { t[n++] = (char)('0' + v % 10); v /= 10; }
    int p = 0;
    while (n) o[p++] = t[--n];
    o[p] = 0;
}

static uint32_t s2u(const char *p)
{
    uint32_t v = 0;
    while (*p >= '0' && *p <= '9')
        v = v * 10 + (uint32_t)(*p++ - '0');
    return v;
}

void users_save(void)
{
    if (!fat_mounted())
        return;
    char buf[USERS_MAX * 40 + 16];
    int p = 0;
    buf[p++] = 'C'; buf[p++] = '=';
    char num[16];
    u2s((uint32_t)cur, num);
    for (int i = 0; num[i]; i++) buf[p++] = num[i];
    buf[p++] = '\n';
    if (auto_user >= 0) {
        buf[p++] = 'A'; buf[p++] = '=';
        u2s((uint32_t)auto_user, num);
        for (int j = 0; num[j]; j++) buf[p++] = num[j];
        buf[p++] = '\n';
    }
    for (int i = 0; i < count; i++) {
        buf[p++] = 'U'; buf[p++] = '=';
        for (int j = 0; names[i][j] && p < (int)sizeof(buf) - 28; j++)
            buf[p++] = names[i][j];
        buf[p++] = ':';
        if (salted[i] && hashes[i]) {
            u2s(salts[i], num);
            for (int j = 0; num[j]; j++) buf[p++] = num[j];
            buf[p++] = ':';
        }
        u2s(hashes[i], num);
        for (int j = 0; num[j]; j++) buf[p++] = num[j];
        buf[p++] = '\n';
    }
    fat_write_file("USERS.CFG", buf, (uint32_t)p);
}

static void parse(const char *b)
{
    int i = 0;
    while (b[i]) {
        if (b[i] == 'C' && b[i + 1] == '=') {
            cur = (int)s2u(b + i + 2);
            if (cur < 0 || cur >= count) cur = 0;
        } else if (b[i] == 'A' && b[i + 1] == '=') {
            auto_user = (int)s2u(b + i + 2);
        } else if (b[i] == 'U' && b[i + 1] == '=' && count < USERS_MAX) {
            char nm[USER_NAME_MAX];
            int n = 0, k = i + 2;
            while (b[k] && b[k] != ':' && b[k] != '\n' && n < USER_NAME_MAX - 1)
                nm[n++] = b[k++];
            nm[n] = 0;
            uint32_t h = 0, sl = 0;
            int is_salted = 0;
            if (b[k] == ':') {
                k++;
                /* New layout: NAME:<salt>:<hash>.  Old layout was
                   NAME:<hash>, so the second colon is the discriminator. */
                uint32_t v1 = s2u(b + k);
                int k2 = k;
                while (b[k2] >= '0' && b[k2] <= '9') k2++;
                if (b[k2] == ':') {
                    sl = v1;
                    h = s2u(b + k2 + 1);
                    is_salted = 1;
                } else {
                    h = v1;
                }
            }
            if (n > 0) {
                set_name(count, nm);
                hashes[count] = h;
                salts[count] = sl;
                salted[count] = (uint8_t)(is_salted && h != 0);
                count++;
            }
        }
        while (b[i] && b[i] != '\n') i++;
        if (b[i] == '\n') i++;
    }
}

void users_init(void)
{
    count = 0; cur = 0;
    if (fat_mounted() && fat_exists("USERS.CFG")) {
        char b[512];
        uint32_t n = 0;
        if (fat_read_file("USERS.CFG", b, sizeof(b) - 1, &n) == 0) {
            b[n] = 0;
            parse(b);
        }
    }
    if (count == 0) {
        set_name(0, "Jack");
        hashes[0] = 0;
        count = 1;
        cur = 0;
        users_save();
    }
    serial_puts("[USER] count=");
    serial_puthex((uint32_t)count);
    serial_puts(" current=");
    serial_puthex((uint32_t)cur);
    {
        int ns = 0;
        for (int i = 0; i < count; i++)
            if (salted[i]) ns++;
        serial_puts(" salted=");
        serial_puthex((uint32_t)ns);
    }
    serial_putc('\n');
    users_ensure_folders();
}

int users_count(void) { return count; }
const char *users_name(int i)
{
    return (i >= 0 && i < count) ? names[i] : "";
}
int users_has_pass(int i) { return (i >= 0 && i < count) ? hashes[i] != 0 : 0; }
int users_current(void) { return cur; }

int users_auth(int i, const char *pass)
{
    if (i < 0 || i >= count)
        return 0;
    if (!hashes[i])
        return pass && !pass[0];          /* no password set */
    if (salted[i])
        return hashes[i] == phash_salt(pass, salts[i]);
    /* Legacy digest accepted once, then re-written salted so the plain
       FNV form never survives a successful login. */
    if (hashes[i] == phash(pass)) {
        salts[i] = new_salt();
        hashes[i] = phash_salt(pass, salts[i]);
        salted[i] = 1;
        users_save();
        return 1;
    }
    return 0;
}

int users_switch(int i)
{
    if (i < 0 || i >= count)
        return -1;
    cur = i;
    auto_user = i;
    users_save();
    users_ensure_folders();
    return cur;
}

/* Picker result: switch for this session, and remember it only when the
   "always sign in as this user" box is ticked (remember=0 restores asking). */
int users_select(int i, int remember)
{
    if (i < 0 || i >= count)
        return -1;
    cur = i;
    auto_user = remember ? i : -1;
    users_save();
    users_ensure_folders();
    return cur;
}

int users_auto(void)
{
    return (auto_user >= 0 && auto_user < count) ? auto_user : -1;
}

int users_add(const char *name, const char *pass)
{
    if (count >= USERS_MAX || !name || !name[0])
        return -1;
    if (find(name) >= 0)
        return -1;
    int i = count++;
    set_name(i, name);
    salts[i] = new_salt();
    hashes[i] = phash_salt(pass, salts[i]);
    salted[i] = (uint8_t)(hashes[i] != 0);
    users_save();
    users_ensure_folders();
    return i;
}

int users_set_pass(int i, const char *pass)
{
    if (i < 0 || i >= count)
        return -1;
    if (!pass || !pass[0]) {         /* clearing the password */
        hashes[i] = 0;
        salted[i] = 0;
        users_save();
        return 0;
    }
    salts[i] = new_salt();
    hashes[i] = phash_salt(pass, salts[i]);
    salted[i] = 1;
    users_save();
    return 0;
}

/* 8.3-safe folder name: upper case, letters/digits only, max 8 chars. */
static void folder_of(int i, char *out, int osz)
{
    int p = 0;
    const char *n = users_name(i);
    for (int j = 0; n[j] && p < osz - 1 && p < 8; j++) {
        char c = n[j];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9'))
            out[p++] = (char)upper(c);
    }
    out[p] = 0;
}

void users_home(int i, char *out, int osz)
{
    const char *pre = "HOME\\";
    int p = 0;
    for (int j = 0; pre[j] && p < osz - 1; j++)
        out[p++] = pre[j];
    char fn[10];
    folder_of(i, fn, sizeof(fn));
    if (!fn[0]) { fn[0] = 'U'; fn[1] = (char)('0' + (i % 10)); fn[2] = 0; }
    for (int j = 0; fn[j] && p < osz - 1; j++)
        out[p++] = fn[j];
    out[p] = 0;
}

void users_ensure_folders(void)
{
    if (!fat_mounted())
        return;
    /* Creating the tree itself is a system action; ordinary file writes stay
       locked out by protect_check(). */
    int old = protect_set_actor(ACTOR_ADMIN);
    if (!fat_dir_exists("SYSTEM")) {
        int r = fat_mkdir("SYSTEM");
        serial_puts("[USER] mkdir SYSTEM=");
        serial_puthex((uint32_t)r);
        serial_putc('\n');
    }
    if (!fat_dir_exists("HOME")) {
        int r = fat_mkdir("HOME");
        serial_puts("[USER] mkdir HOME=");
        serial_puthex((uint32_t)r);
        serial_putc('\n');
    }
    protect_set_actor(old);
    for (int i = 0; i < count; i++) {
        char path[32];
        users_home(i, path, sizeof(path));
        if (!fat_dir_exists(path)) {
            int r = fat_mkdir(path);
            serial_puts("[USER] mkdir ");
            serial_puts(path);
            serial_puts("=");
            serial_puthex((uint32_t)r);
            serial_putc('\n');
        } else {
            serial_puts("[USER] home exists: ");
            serial_puts(path);
            serial_putc('\n');
        }
    }
}
