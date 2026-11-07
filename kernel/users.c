#include <stdint.h>
#include <stddef.h>
#include <kernel/users.h>
#include <kernel/protect.h>
#include <kernel/fat32.h>
#include <kernel/serial.h>

static char names[USERS_MAX][USER_NAME_MAX];
static uint32_t hashes[USERS_MAX];
static int count;
static int cur;

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
    for (int i = 0; i < count; i++) {
        buf[p++] = 'U'; buf[p++] = '=';
        for (int j = 0; names[i][j] && p < (int)sizeof(buf) - 20; j++)
            buf[p++] = names[i][j];
        buf[p++] = ':';
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
        } else if (b[i] == 'U' && b[i + 1] == '=' && count < USERS_MAX) {
            char nm[USER_NAME_MAX];
            int n = 0, k = i + 2;
            while (b[k] && b[k] != ':' && b[k] != '\n' && n < USER_NAME_MAX - 1)
                nm[n++] = b[k++];
            nm[n] = 0;
            uint32_t h = 0;
            if (b[k] == ':')
                h = s2u(b + k + 1);
            if (n > 0) {
                set_name(count, nm);
                hashes[count] = h;
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
    return hashes[i] == phash(pass);
}

int users_switch(int i)
{
    if (i < 0 || i >= count)
        return -1;
    cur = i;
    users_save();
    users_ensure_folders();
    return cur;
}

int users_add(const char *name, const char *pass)
{
    if (count >= USERS_MAX || !name || !name[0])
        return -1;
    if (find(name) >= 0)
        return -1;
    int i = count++;
    set_name(i, name);
    hashes[i] = phash(pass);
    users_save();
    users_ensure_folders();
    return i;
}

int users_set_pass(int i, const char *pass)
{
    if (i < 0 || i >= count)
        return -1;
    hashes[i] = phash(pass);
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
