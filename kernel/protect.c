#include <stdint.h>
#include <stddef.h>
#include <kernel/protect.h>
#include <kernel/serial.h>

static int actor = ACTOR_USER;

static void fmt_name(const char *in, uint8_t out[11])
{
    for (int i = 0; i < 11; i++)
        out[i] = ' ';
    int bi = 0, ei = 0, dot = 0;
    for (const char *p = in; *p; p++) {
        char ch = *p;
        if (ch == '.') {
            dot = 1;
            continue;
        }
        if (ch >= 'a' && ch <= 'z')
            ch -= 32;
        if (!dot) {
            if (bi < 8)
                out[bi++] = (uint8_t)ch;
        } else {
            if (ei < 3)
                out[8 + ei++] = (uint8_t)ch;
        }
    }
}

static const char *core_names[] = {
    "SYSTEM.IMG",
    "KERNEL.BIN",
    "JBOTCORE.LIB",
    "SYS",
};

int protect_is_core(const char *name83)
{
    uint8_t want[11], have[11];
    fmt_name(name83, want);
    for (uint32_t i = 0; i < sizeof core_names / sizeof core_names[0]; i++) {
        fmt_name(core_names[i], have);
        int same = 1;
        for (int j = 0; j < 11; j++)
            if (want[j] != have[j])
                same = 0;
        if (same)
            return 1;
    }
    return 0;
}

int protect_check(const char *name83, int action)
{
    if (!protect_is_core(name83))
        return 0;

    if (action == ACT_READ)
        return 0;
    if (action == ACT_DELETE) {
        serial_puts("[PROT] blocked DELETE of core ");
        serial_puts(name83);
        serial_putc('\n');
        return -1;
    }
    if (action == ACT_WRITE) {
        if (actor == ACTOR_ADMIN)
            return 0;
        serial_puts("[PROT] blocked WRITE of core ");
        serial_puts(name83);
        serial_putc('\n');
        return -1;
    }
    return -1;
}

int protect_set_actor(int a)
{
    int old = actor;
    actor = a;
    return old;
}

int protect_get_actor(void)
{
    return actor;
}
