#include <stdint.h>
#include <kernel/oom.h>
#include <kernel/proc.h>
#include <kernel/pmm.h>
#include <kernel/serial.h>

static uint64_t killed;
static uint64_t last_victim_bytes;
static char last_name[24];
static int in_killer;

uint64_t oom_kill_count(void)
{
    return killed;
}

uint64_t oom_last_victim_bytes(void)
{
    return last_victim_bytes;
}

const char *oom_last_victim_name(void)
{
    return last_name;
}

int oom_reclaim(const char *why)
{
    /* Reentrancy guard: killing a process frees pages, and the free path
       must never call back into the killer. */
    if (in_killer)
        return 0;
    in_killer = 1;

    thread_t *cur = current_thread();
    thread_t *victim = 0;
    int idx = 0;
    thread_t *t;
    while ((t = thread_enum(idx++)) != 0) {
        if (!t->is_user || t == cur)
            continue;
        if (t->state == T_ZOMBIE)
            continue;
        if (t->mem_bytes < OOM_MIN_VICTIM_BYTES)
            continue;
        if (!victim || t->mem_bytes > victim->mem_bytes)
            victim = t;
    }

    int result = 0;
    if (victim) {
        killed++;
        last_victim_bytes = victim->mem_bytes;
        /* No process table names yet: report the victim by id and note it as
           a user image in last_name so the monitor app has something to show. */
        last_name[0] = 'u';
        last_name[1] = 's';
        last_name[2] = 'e';
        last_name[3] = 'r';
        last_name[4] = 0;
        serial_puts("[OOM] kill pid=");
        serial_puthex(victim->id);
        serial_puts(" mem=");
        serial_puthex(victim->mem_bytes);
        serial_puts(" why=");
        serial_puts(why ? why : "alloc");
        serial_puts("\n");
        process_kill(victim, "out of memory");
        result = 1;
    } else {
        serial_puts("[OOM] nothing to reclaim (");
        serial_puts(why ? why : "alloc");
        serial_puts(") free_pages=");
        serial_puthex(pmm_free_bytes() / PAGE_SIZE);
        serial_puts("\n");
    }

    in_killer = 0;
    return result;
}
