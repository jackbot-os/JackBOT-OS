#include <stdint.h>
#include <stddef.h>
#include <kernel/proc.h>
#include <kernel/heap.h>

void context_switch(uint64_t *old_rsp, uint64_t *new_rsp);
void thread_trampoline(void);

static thread_t threads[MAX_THREADS];
static thread_t *current;

thread_t *current_thread(void)
{
    return current;
}

static thread_t *pick_next(void)
{
    int start = current ? (int)current->id : 0;
    for (int k = 1; k <= MAX_THREADS; k++) {
        int idx = (start + k) % MAX_THREADS;
        if (threads[idx].state == T_READY)
            return &threads[idx];
    }
    return 0;
}

void sched_init(void)
{
    for (int i = 0; i < MAX_THREADS; i++)
        threads[i].state = T_UNUSED;
    thread_t *m = &threads[0];
    m->id = 0;
    m->state = T_RUNNING;
    current = m;
}

thread_t *thread_create(void (*entry)(void *), void *arg)
{
    int i;
    for (i = 1; i < MAX_THREADS; i++)
        if (threads[i].state == T_UNUSED)
            break;
    if (i >= MAX_THREADS)
        return 0;

    thread_t *t = &threads[i];
    t->id = i;
    t->entry = (void *)entry;
    t->arg = arg;
    t->kstack = (uint64_t)kmalloc(KSTACK_SIZE);
    if (!t->kstack)
        return 0;
    t->kstack_top = t->kstack + KSTACK_SIZE;

    uint64_t *sp = (uint64_t *)t->kstack_top;
    *--sp = (uint64_t)thread_trampoline;
    *--sp = (uint64_t)arg;
    *--sp = (uint64_t)entry;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    t->rsp = (uint64_t)sp;
    t->state = T_READY;
    return t;
}

void thread_yield(void)
{
    uint64_t flags;
    __asm__ volatile("pushf; pop %0" : "=r"(flags));
    __asm__ volatile("cli");

    if (!current) {
        __asm__ volatile("push %0; popf" : : "r"(flags));
        return;
    }
    thread_t *prev = current;
    thread_t *next = pick_next();
    if (!next || next == prev) {
        __asm__ volatile("push %0; popf" : : "r"(flags));
        return;
    }
    prev->state = T_READY;
    next->state = T_RUNNING;
    current = next;
    context_switch(&prev->rsp, &next->rsp);
    __asm__ volatile("push %0; popf" : : "r"(flags));
}

void thread_exit(void)
{
    thread_t *prev = current;
    prev->state = T_ZOMBIE;
    thread_t *next = pick_next();
    if (!next) {
        __asm__ volatile("cli");
        for (;;)
            __asm__ volatile("hlt");
    }
    next->state = T_RUNNING;
    current = next;
    context_switch(&prev->rsp, &next->rsp);
    for (;;)
        __asm__ volatile("hlt");
}

void sched_tick(void)
{
    thread_yield();
}
