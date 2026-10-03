#include <kernel/trap.h>
#include <kernel/serial.h>
#include <kernel/apic.h>
#include <kernel/proc.h>

void timer_irq(void);
void pic_send_eoi(uint8_t irq);

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
        pic_send_eoi((uint8_t)(f->vector - 32));
        apic_eoi();
        return;
    }

    if (f->vector == 255)
        return;

    dump_frame(f);
    serial_puts("halting kernel\n");

    __asm__ volatile("cli");
    for (;;)
        __asm__ volatile("hlt");
}
