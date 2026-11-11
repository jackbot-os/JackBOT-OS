#include <kernel/trap.h>
#include <kernel/serial.h>
#include <kernel/apic.h>
#include <kernel/proc.h>
#include <kernel/crash.h>
#include <kernel/ps2.h>
#include <kernel/posix.h>
#include <kernel/panic.h>

void timer_irq(void);
void pic_send_eoi(uint8_t irq);
uint64_t timer_get_ticks(void);

#define RECOVER_LIMIT 5
#define RECOVER_GAP_TICKS 100

#define IRQ_SHARED 4

static struct {
    irq_handler_fn fn;
    void *ctx;
} irq_table[16][IRQ_SHARED];

void irq_register(uint8_t irq, irq_handler_fn fn, void *ctx)
{
    if (irq < 16)
        for (int i = 0; i < IRQ_SHARED; i++)
            if (!irq_table[irq][i].fn) {
                irq_table[irq][i].fn = fn;
                irq_table[irq][i].ctx = ctx;
                break;
            }
}

static const char *exception_names[32] = {
    "divide error",
    "debug",
    "nmi",
    "breakpoint",
    "overflow",
    "bound range",
    "invalid opcode",
    "device not available",
    "double fault",
    "coprocessor segment overrun",
    "invalid tss",
    "segment not present",
    "stack segment fault",
    "general protection fault",
    "page fault",
    "reserved",
    "x87 floating point",
    "alignment check",
    "machine check",
    "simd floating point",
    "virtualization",
    "control protection",
    "reserved",
    "reserved",
    "reserved",
    "reserved",
    "reserved",
    "reserved",
    "hypervisor injection",
    "vmm communication",
    "security exception",
    "reserved",
};

static void dump_frame(struct trap_frame *f)
{
    serial_puts("\n*** KERNEL TRAP: ");
    if (f->vector < 32)
        serial_puts(exception_names[f->vector]);
    serial_puts(" vector=");
    serial_puthex(f->vector);
    serial_puts(" error=");
    serial_puthex(f->error);
    serial_putc('\n');
    serial_puts("rip="); serial_puthex(f->rip);
    serial_puts(" cs="); serial_puthex(f->cs);
    serial_puts(" rflags="); serial_puthex(f->rflags);
    serial_putc('\n');
    serial_puts("rax="); serial_puthex(f->rax);
    serial_puts(" rbx="); serial_puthex(f->rbx);
    serial_puts(" rcx="); serial_puthex(f->rcx);
    serial_puts(" rdx="); serial_puthex(f->rdx);
    serial_putc('\n');
    serial_puts("rsi="); serial_puthex(f->rsi);
    serial_puts(" rdi="); serial_puthex(f->rdi);
    serial_puts(" rbp="); serial_puthex(f->rbp);
    serial_puts(" rsp="); serial_puthex(f->rsp);
    serial_putc('\n');
    serial_puts("r8="); serial_puthex(f->r8);
    serial_puts(" r9="); serial_puthex(f->r9);
    serial_puts(" r10="); serial_puthex(f->r10);
    serial_puts(" r11="); serial_puthex(f->r11);
    serial_putc('\n');
    serial_puts("r12="); serial_puthex(f->r12);
    serial_puts(" r13="); serial_puthex(f->r13);
    serial_puts(" r14="); serial_puthex(f->r14);
    serial_puts(" r15="); serial_puthex(f->r15);
    serial_puts("\n");
}

static void syscall_dispatch(struct trap_frame *f)
{
    uint64_t num = f->rax;
    switch (num) {
    case 0:
        serial_putc((char)f->rdi);
        break;
    case 1:
        thread_yield();
        break;
    case 2:
        thread_exit();
        break;
    default:
        posix_syscall(f);       /* POSIX subset (>= SYS_WRITE) */
        break;
    }
}

void trap_dispatch(struct trap_frame *f)
{
    if (f->vector == 128) {
        syscall_dispatch(f);
        return;
    }

    if (f->vector == 32) {
        timer_irq();
        pic_send_eoi(0);
        apic_eoi();
        sched_tick();
        return;
    }

    if (f->vector >= 33 && f->vector < 48) {
        uint8_t irq = (uint8_t)(f->vector - 32);
        if (irq == 1)
            ps2_kbd_irq();
        else if (irq == 12)
            ps2_mouse_irq();
        for (int i = 0; i < IRQ_SHARED; i++)
            if (irq_table[irq][i].fn)
                irq_table[irq][i].fn(irq_table[irq][i].ctx);
        pic_send_eoi(irq);
        apic_eoi();
        return;
    }

    if (f->vector == 255)
        return;

    if (f->vector < 32 && (f->cs & 3) == 3) {
        thread_t *t = current_thread();
        if (t && t->user_entry) {
            uint64_t now = timer_get_ticks();
            if (now - t->recover_tick >= RECOVER_GAP_TICKS)
                t->recover_count = 1;
            else
                t->recover_count++;
            t->recover_tick = now;
            if (t->recover_count > RECOVER_LIMIT) {
                crash_log_frame(f, "repeated user exception, giving up");
                process_kill(t, "repeated user exception");
                return;
            }
            crash_log_frame(f, "user exception recovered");
            /* The fault came from user code, so the kernel is intact. Rewind the
               task to its entry point and let it run again; a task that keeps
               faulting immediately is killed after RECOVER_LIMIT attempts. */
            f->rip = t->user_entry;
        } else {
            crash_log_frame(f, "user exception unrecoverable");
            process_kill(t, "user exception unrecoverable");
        }
        return;
    }

    dump_frame(f);
    crash_log_frame(f, "kernel fault");

    /* Not a halt loop any more: show the stop screen with every register,
       the faulting address and the stage the kernel was in, dump the same
       data on the serial port, then restart instead of hanging here. */
    panic_screen(f, "kernel fault inside the OS");
}
