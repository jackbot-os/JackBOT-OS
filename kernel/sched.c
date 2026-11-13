#include <stdint.h>
#include <stddef.h>
#include <kernel/proc.h>
#include <kernel/heap.h>
#include <kernel/serial.h>
#include <kernel/crash.h>

void context_switch(uint64_t *old_rsp, uint64_t *new_rsp);
void thread_trampoline(void);
void user_enter(void);
void user_enter_clone(void);

extern uint64_t p4_table[512];
void tss_set_rsp0(uint64_t rsp0);
uint64_t timer_get_ticks(void);

/* IA32_FS_BASE. Threads carry their own TLS base; the MSR is restored on
   every switch so ring-3 pthread code sees the right thread pointer. */
#define MSR_FS_BASE 0xC0000100u

static void wrmsr(uint32_t msr, uint64_t val)
{
    __asm__ volatile("wrmsr"
                     : : "c"(msr), "a"((uint32_t)val), "d"((uint32_t)(val >> 32)));
}

static uint64_t rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static thread_t threads[MAX_THREADS];
static thread_t *current;

/* Per-thread address space: user threads run under their own page table,
   kernel threads under the kernel one. p4_table is linked/loaded low and
   identity mapped, so its address is also its physical address.
   TSS.rsp0 must follow the running user thread: the CPU loads it when a
   ring-3 task traps into the kernel, and using another task's stack there
   corrupts both stacks. */
static void load_cr3_for(thread_t *next)
{
    uint64_t cr3 = (next && next->is_user && next->p4phys)
                 ? next->p4phys
                 : (uint64_t)(uintptr_t)p4_table;
    if (next && next->is_user && next->kstack_top)
        tss_set_rsp0(next->kstack_top);
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
    wrmsr(MSR_FS_BASE, (next && next->has_tls) ? next->tls_base : 0);
}

void thread_set_fs(uint64_t base)
{
    thread_t *t = current;
    if (t) {
        t->tls_base = base;
        t->has_tls = 1;
    }
    wrmsr(MSR_FS_BASE, base);
}

uint64_t thread_get_fs(void)
{
    thread_t *t = current;
    if (t && t->has_tls)
        return t->tls_base;
    return rdmsr(MSR_FS_BASE);
}

thread_t *current_thread(void)
{
    return current;
}

static thread_t *pick_next(void)
{
    thread_t *idle = &threads[0];
    int start = current ? (int)current->id : 0;
    for (int k = 1; k <= MAX_THREADS; k++) {
        int idx = (start + k) % MAX_THREADS;
        if (threads[idx].state == T_READY)
            return &threads[idx];
    }
    if (current && current != idle)
        return idle;
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
    t->tid = i;
    t->is_user = 0;
    t->p4phys = 0;
    t->has_tls = 0;
    t->tls_base = 0;
    t->clear_tid = 0;
    t->wait_addr = 0;
    t->exit_code = 0;
    t->detached = 0;
    t->mmap_cur = 0;
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

thread_t *thread_create_user(uint64_t p4phys, uint64_t user_rip, uint64_t user_rsp)
{
    int i;
    for (i = 1; i < MAX_THREADS; i++)
        if (threads[i].state == T_UNUSED)
            break;
    if (i >= MAX_THREADS)
        return 0;

    thread_t *t = &threads[i];
    t->id = i;
    t->tid = i;
    t->is_user = 1;
    t->has_tls = 0;
    t->tls_base = 0;
    t->clear_tid = 0;
    t->wait_addr = 0;
    t->exit_code = 0;
    t->detached = 0;
    t->mmap_cur = 0;
    t->user_entry = user_rip;
    t->recover_count = 0;
    t->recover_tick = 0;
    t->p4phys = p4phys;
    t->cpu_ticks = 0;
    t->mem_bytes = 0;
    t->quota_mem_bytes = DEFAULT_QUOTA_MEM_BYTES;
    t->quota_cpu_ticks = DEFAULT_QUOTA_CPU_TICKS;
    t->last_alive_tick = 0;
    t->kill_reason = 0;
    t->kstack = (uint64_t)kmalloc(KSTACK_SIZE);
    if (!t->kstack)
        return 0;
    t->kstack_top = t->kstack + KSTACK_SIZE;

    uint64_t *sp = (uint64_t *)t->kstack_top;
    *--sp = 0x23;
    *--sp = user_rsp;
    *--sp = 0x202;
    *--sp = 0x1b;
    *--sp = user_rip;
    *--sp = (uint64_t)user_enter;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    t->rsp = (uint64_t)sp;
    t->state = T_READY;
    return t;
}

thread_t *thread_create_clone(thread_t *parent, uint64_t user_rip, uint64_t user_rsp,
                              uint64_t tls, uint64_t clear_tid, uint64_t start_r9)
{
    int i;
    for (i = 1; i < MAX_THREADS; i++)
        if (threads[i].state == T_UNUSED)
            break;
    if (i >= MAX_THREADS)
        return 0;

    thread_t *t = &threads[i];
    t->id = i;
    t->tid = i;
    t->is_user = 1;
    t->user_entry = user_rip;
    t->recover_count = 0;
    t->recover_tick = 0;
    t->cpu_ticks = 0;
    t->mem_bytes = 0;
    t->last_alive_tick = 0;
    t->kill_reason = 0;
    t->wait_addr = 0;
    t->exit_code = 0;
    t->detached = 0;
    /* pthread: the child lives in the parent's address space */
    t->p4phys = parent->p4phys;
    t->heap_cur = parent->heap_cur;
    t->mmap_cur = parent->mmap_cur;
    t->quota_mem_bytes = parent->quota_mem_bytes;
    t->quota_cpu_ticks = parent->quota_cpu_ticks;
    t->has_tls = (tls != 0);
    t->tls_base = tls;
    t->clear_tid = clear_tid;

    t->kstack = (uint64_t)kmalloc(KSTACK_SIZE);
    if (!t->kstack)
        return 0;
    t->kstack_top = t->kstack + KSTACK_SIZE;

    /* The child resumes at the parent's return-from-trap rip (the
       instruction after int 0x80) with rax = 0 and r9 = entry function,
       which is exactly what musl's __clone expects after the syscall. */
    t->boot_rax = 0;
    t->boot_r9 = start_r9;

    uint64_t *sp = (uint64_t *)t->kstack_top;
    *--sp = 0x23;                       /* ss */
    *--sp = user_rsp;
    *--sp = 0x202;                      /* rflags: IF set */
    *--sp = 0x1b;                       /* cs */
    *--sp = user_rip;
    *--sp = (uint64_t)&t->boot_rax;     /* popped into rdi by user_enter_clone */
    *--sp = (uint64_t)user_enter_clone; /* context_switch ret target */
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    t->rsp = (uint64_t)sp;
    t->state = T_READY;
    return t;
}

/* fork(): like thread_create_clone, but the child gets its own page table
   (already filled from the parent with copy-on-write pages) and resumes at
   the same user instruction with rax = 0. */
thread_t *thread_create_fork(thread_t *parent, uint64_t new_p4,
                             uint64_t user_rip, uint64_t user_rsp)
{
    int i;
    for (i = 1; i < MAX_THREADS; i++)
        if (threads[i].state == T_UNUSED)
            break;
    if (i >= MAX_THREADS)
        return 0;

    thread_t *t = &threads[i];
    t->id = i;
    t->tid = i;
    t->is_user = 1;
    t->user_entry = user_rip;
    t->recover_count = 0;
    t->recover_tick = 0;
    t->cpu_ticks = 0;
    t->mem_bytes = 0;
    t->last_alive_tick = 0;
    t->kill_reason = 0;
    t->wait_addr = 0;
    t->exit_code = 0;
    t->detached = 0;
    t->p4phys = new_p4;
    t->heap_cur = parent->heap_cur;
    t->mmap_cur = parent->mmap_cur;
    t->quota_mem_bytes = parent->quota_mem_bytes;
    t->quota_cpu_ticks = parent->quota_cpu_ticks;
    t->has_tls = parent->has_tls;
    t->tls_base = parent->tls_base;
    t->clear_tid = 0;

    t->kstack = (uint64_t)kmalloc(KSTACK_SIZE);
    if (!t->kstack)
        return 0;
    t->kstack_top = t->kstack + KSTACK_SIZE;

    t->boot_rax = 0;
    t->boot_r9 = 0;

    uint64_t *sp = (uint64_t *)t->kstack_top;
    *--sp = 0x23;
    *--sp = user_rsp;
    *--sp = 0x202;
    *--sp = 0x1b;
    *--sp = user_rip;
    *--sp = (uint64_t)&t->boot_rax;
    *--sp = (uint64_t)user_enter_clone;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    t->rsp = (uint64_t)sp;
    t->state = T_READY;
    return t;
}

/* Slots of finished threads are recycled here. Only call it from a context
   that may sleep: the timer interrupt path enters with IF clear. */
static void sched_reap(void)
{
    for (int i = 1; i < MAX_THREADS; i++) {
        thread_t *t = &threads[i];
        if (t->state != T_ZOMBIE || !t->kstack)
            continue;
        /* the stack of a zombie never runs again */
        kfree((void *)t->kstack);
        t->kstack = 0;
        t->kstack_top = 0;
        t->rsp = 0;
        t->state = T_UNUSED;
        t->is_user = 0;
        t->p4phys = 0;
        t->has_tls = 0;
        t->tls_base = 0;
        t->clear_tid = 0;
    }
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
    if (flags & 0x200)              /* only from a context that may sleep */
        sched_reap();
    thread_t *prev = current;
    thread_t *next = pick_next();
    if (!next || next == prev) {
        __asm__ volatile("push %0; popf" : : "r"(flags));
        return;
    }
    if (prev->state == T_RUNNING)
        prev->state = T_READY;      /* a blocked thread stays blocked */
    next->state = T_RUNNING;
    current = next;
    load_cr3_for(next);
    context_switch(&prev->rsp, &next->rsp);
    __asm__ volatile("push %0; popf" : : "r"(flags));
}

/* Shared tail of every exit path. Runs with interrupts off from the point
   where the slot becomes a zombie, so no other thread can reap the stack
   this one is still standing on. */
static void thread_switch_away_zombie(thread_t *prev)
{
    thread_t *next = pick_next();
    if (!next) {
        __asm__ volatile("cli");
        for (;;)
            __asm__ volatile("hlt");
    }
    next->state = T_RUNNING;
    current = next;
    load_cr3_for(next);
    context_switch(&prev->rsp, &next->rsp);
    for (;;)
        __asm__ volatile("hlt");
}

void thread_exit_status(uint64_t code)
{
    thread_t *prev = current;
    __asm__ volatile("cli");
    prev->exit_code = code;
    if (prev->is_user && prev->clear_tid && user_ptr_valid((void *)prev->clear_tid, 4)) {
        volatile int *ctid = (volatile int *)prev->clear_tid;
        /* musl parks the thread-list lock on this word: clearing it and
           waking the waiter is what lets pthread_create return */
        *ctid = 0;
        futex_wake_word(ctid, 0);
    }
    prev->wait_addr = 0;
    prev->state = T_ZOMBIE;
    thread_switch_away_zombie(prev);
}

void thread_exit_group(uint64_t code)
{
    thread_t *prev = current;
    __asm__ volatile("cli");
    for (int i = 1; i < MAX_THREADS; i++) {
        thread_t *t = &threads[i];
        if (t == prev || !t->is_user || t->p4phys != prev->p4phys)
            continue;
        if (t->state == T_UNUSED || t->state == T_ZOMBIE)
            continue;
        t->wait_addr = 0;
        t->state = T_ZOMBIE;
    }
    thread_exit_status(code);
}

void thread_exit(void)
{
    thread_exit_status(0);
}

int futex_wait_word(volatile int *uaddr, int expected, uint64_t timeout_ticks)
{
    thread_t *t = current;
    uint64_t deadline = 0;

    if (!t || !uaddr)
        return -22;                 /* EINVAL */
    if (*uaddr != expected)
        return -11;                 /* EAGAIN: value changed already */
    if (timeout_ticks)
        deadline = timer_get_ticks() + timeout_ticks;

    while (*uaddr == expected) {
        if (timeout_ticks && timer_get_ticks() >= deadline)
            return -110;            /* ETIMEDOUT */
        t->wait_addr = (uint64_t)uaddr;
        t->state = T_BLOCKED;
        thread_yield();
        t->wait_addr = 0;
        t->state = T_RUNNING;
    }
    return 0;
}

int futex_wake_word(volatile int *uaddr, int count)
{
    uint64_t flags;
    int woken = 0;

    if (!uaddr)
        return -22;
    __asm__ volatile("pushf; pop %0" : "=r"(flags));
    __asm__ volatile("cli");
    for (int i = 1; i < MAX_THREADS; i++) {
        thread_t *t = &threads[i];
        if (t->state != T_BLOCKED || t->wait_addr != (uint64_t)uaddr)
            continue;
        t->wait_addr = 0;
        t->state = T_READY;
        woken++;
        if (count > 0 && woken >= count)
            break;
    }
    __asm__ volatile("push %0; popf" : : "r"(flags));
    return woken;
}

thread_t *thread_by_id(uint64_t id)
{
    if (id < MAX_THREADS && threads[id].state != T_UNUSED)
        return &threads[id];
    return 0;
}

/* Read-only enumeration for the process manager: index 0..MAX_THREADS-1,
   returns the slot itself even when unused so callers can tell the id. */
thread_t *thread_enum(int index)
{
    if (index < 0 || index >= MAX_THREADS)
        return 0;
    return &threads[index];
}

int thread_count_user(void)
{
    int n = 0;
    for (int i = 1; i < MAX_THREADS; i++)
        if (threads[i].is_user && threads[i].state != T_UNUSED
            && threads[i].state != T_ZOMBIE)
            n++;
    return n;
}

/* Audit for address-space isolation: every live user thread must run on a
   real user page table, never on the kernel's, and a page table must not be
   shared between *unrelated* processes. Threads of one process (CLONE_VM)
   legitimately share a p4, so sharing alone is not a failure — running with
   no address space at all is. */
void sched_iso_report(void)
{
    int procs = 0, distinct = 0, threads_shared = 0, bad = 0;
    uint64_t seen[16];

    for (int i = 1; i < MAX_THREADS; i++) {
        if (!threads[i].is_user || threads[i].state == T_UNUSED ||
            threads[i].state == T_ZOMBIE)
            continue;
        if (!threads[i].p4phys || threads[i].p4phys == (uint64_t)(uintptr_t)p4_table) {
            bad++;
            continue;
        }
        procs++;
        int dup = 0;
        for (int k = 0; k < distinct; k++)
            if (seen[k] == threads[i].p4phys)
                dup = 1;
        if (dup)
            threads_shared++;
        else if (distinct < 16)
            seen[distinct++] = threads[i].p4phys;
    }

    serial_puts("[USERISO] procs=");
    serial_puthex((uint64_t)procs);
    serial_puts(" distinct_p4=");
    serial_puthex((uint64_t)distinct);
    serial_puts(" threads_shared=");
    serial_puthex((uint64_t)threads_shared);
    serial_puts(bad ? " FAIL\n" : " OK\n");
}

void process_kill(thread_t *t, const char *reason)
{
    uint64_t flags;
    __asm__ volatile("pushf; pop %0" : "=r"(flags));
    __asm__ volatile("cli");

    t->kill_reason = reason;
    t->state = T_ZOMBIE;

    if (t == current) {
        thread_t *next = pick_next();
        if (next) {
            next->state = T_RUNNING;
            current = next;
            load_cr3_for(next);
            context_switch(&t->rsp, &next->rsp);
        } else {
            __asm__ volatile("cli");
            for (;;)
                __asm__ volatile("hlt");
        }
    }
    __asm__ volatile("push %0; popf" : : "r"(flags));
}

void sched_tick(void)
{
    if (current)
        current->cpu_ticks++;
    thread_yield();
}

