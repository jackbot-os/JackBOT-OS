#include <stdint.h>
#include <stddef.h>
#include <kernel/crash.h>
#include <kernel/serial.h>
#include <kernel/proc.h>
#include <kernel/vmm.h>

#define USER_LO 0x8000000000ull
#define USER_HI 0x10000000000ull
#define ADDR_MASK 0x000ffffffffff000ull

uint64_t timer_get_ticks(void);

static struct crash_record records[CRASH_LOG_ENTRIES];
static int rec_idx;

static uint64_t read_cr3(void)
{
    uint64_t v;
    __asm__ volatile("mov %%cr3, %0" : "=r"(v));
    return v;
}

void crash_log_frame(struct trap_frame *f, const char *note)
{
    struct crash_record *r = &records[rec_idx % CRASH_LOG_ENTRIES];
    rec_idx++;

    r->used = 1;
    r->pid = current_thread() ? current_thread()->id : 0;
    r->vector = f->vector;
    r->error = f->error;
    r->rip = f->rip;
    r->rsp = f->rsp;
    r->rflags = f->rflags;
    r->regs[0] = f->r15; r->regs[1] = f->r14; r->regs[2] = f->r13;
    r->regs[3] = f->r12; r->regs[4] = f->r11; r->regs[5] = f->r10;
    r->regs[6] = f->r9;  r->regs[7] = f->r8;  r->regs[8] = f->rbp;
    r->regs[9] = f->rdi; r->regs[10] = f->rsi; r->regs[11] = f->rdx;
    r->regs[12] = f->rcx; r->regs[13] = f->rbx; r->regs[14] = f->rax;

    if (f->rsp >= USER_LO && f->rsp < USER_HI) {
        for (int i = 0; i < 16; i++) {
            uint64_t a = f->rsp + i * 8;
            if (user_ptr_valid((void *)a, 8))
                r->stack_words[i] = *(volatile uint64_t *)a;
            else
                r->stack_words[i] = 0;
        }
    }

    serial_puts("\n[CRASH] ");
    serial_puts(note ? note : "?");
    serial_puts(" pid="); serial_puthex(r->pid);
    serial_puts(" vec="); serial_puthex(r->vector);
    serial_puts(" rip="); serial_puthex(r->rip);
    serial_putc('\n');
}

void crash_dump_records(void)
{
    serial_puts("crash records=");
    serial_puthex((uint64_t)(rec_idx > CRASH_LOG_ENTRIES ? CRASH_LOG_ENTRIES : rec_idx));
    serial_putc('\n');
}

int user_ptr_valid(const void *p, uint64_t len)
{
    uint64_t a = (uint64_t)(uintptr_t)p;
    if (len == 0)
        return 1;
    if (a < USER_LO || a >= USER_HI)
        return 0;
    if (a + len > USER_HI || a + len < a)
        return 0;

    uint64_t cr3 = read_cr3();
    uint64_t end = a + len - 1;
    for (uint64_t page = a & ~0xfffull;; page += 0x1000) {
        uint64_t i4 = (page >> 39) & 0x1ff;
        uint64_t i3 = (page >> 30) & 0x1ff;
        uint64_t i2 = (page >> 21) & 0x1ff;
        uint64_t i1 = (page >> 12) & 0x1ff;

        uint64_t *p4 = (uint64_t *)(cr3 & ADDR_MASK);
        if (!(p4[i4] & PTE_PRESENT) || !(p4[i4] & PTE_USER))
            return 0;
        uint64_t *p3 = (uint64_t *)(p4[i4] & ADDR_MASK);
        if (!(p3[i3] & PTE_PRESENT) || !(p3[i3] & PTE_USER))
            return 0;
        uint64_t *p2 = (uint64_t *)(p3[i3] & ADDR_MASK);
        if (!(p2[i2] & PTE_PRESENT) || !(p2[i2] & PTE_USER))
            return 0;
        uint64_t *p1 = (uint64_t *)(p2[i2] & ADDR_MASK);
        if (!(p1[i1] & PTE_PRESENT) || !(p1[i1] & PTE_USER))
            return 0;

        if (page >= (end & ~0xfffull))
            break;
    }
    return 1;
}

static void watchdog_loop(void *arg)
{
    (void)arg;
    uint64_t last = timer_get_ticks();
    for (;;) {
        uint64_t now = timer_get_ticks();
        if (now - last < 100) {
            thread_yield();
            continue;
        }
        last = now;

        for (uint64_t id = 1; ; id++) {
            thread_t *t = thread_by_id(id);
            if (!t)
                break;
            if (t->state == T_ZOMBIE || t->state == T_UNUSED)
                continue;
            if (t->quota_cpu_ticks && t->cpu_ticks > t->quota_cpu_ticks) {
                serial_puts("[watchdog] pid "); serial_puthex(id);
                serial_puts(" exceeded CPU quota\n");
                process_kill(t, "cpu quota exceeded");
            }
        }
    }
}

void watchdog_init(void)
{
    thread_create(watchdog_loop, 0);
}
