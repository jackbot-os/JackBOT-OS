#include <stdint.h>
#include <kernel/persist.h>
#include <kernel/fat32.h>
#include <kernel/ata.h>
#include <kernel/serial.h>

uint64_t timer_get_ticks(void);

static char dirty[PERSIST_MAX_DIRTY][PERSIST_NAME_MAX];
static int count;

void persist_init(void)
{
    count = 0;
}

static int has_name(const char *name)
{
    for (int i = 0; i < count; i++) {
        int j = 0;
        while (name[j] && dirty[i][j] == name[j])
            j++;
        if (!name[j] && !dirty[i][j])
            return 1;
    }
    return 0;
}

void persist_mark_dirty(const char *name)
{
    if (!name || has_name(name) || count >= PERSIST_MAX_DIRTY)
        return;
    int j = 0;
    while (name[j] && j < PERSIST_NAME_MAX - 1) {
        dirty[count][j] = name[j];
        j++;
    }
    dirty[count][j] = 0;
    count++;
}

int persist_pending(void)
{
    return count;
}

int persist_flush(uint32_t timeout_ticks)
{
    uint64_t deadline = timer_get_ticks() + timeout_ticks;
    int found = 0;
    for (int i = 0; i < count; i++) {
        if (timer_get_ticks() >= deadline)
            return -1;
        if (fat_exists(dirty[i]))
            found++;
    }
    if (timer_get_ticks() >= deadline)
        return -1;
    if (ata_flush_cache() < 0)
        return -1;
    count = 0;
    return found;
}

void persist_self_test(void)
{
    static const char payload[] = "persist-data";
    persist_init();
    fat_write_file("P.TXT", payload, sizeof(payload) - 1);
    int marked = persist_pending() >= 1;
    int timed_out = persist_flush(0) == -1 && persist_pending() >= 1;
    int flushed = persist_flush(400) >= 0 && persist_pending() == 0;
    serial_puts((marked && timed_out && flushed)
                    ? "[PERSIST] test PASS\n"
                    : "[PERSIST] test FAIL\n");
}
