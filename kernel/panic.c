#include <stdint.h>
#include <kernel/panic.h>
#include <kernel/trap.h>
#include <kernel/fb.h>
#include <kernel/gfxtext.h>
#include <kernel/serial.h>

extern uint64_t timer_get_ticks(void);

#define STALL_TICKS 2000ull      /* ~20 s without a heartbeat */
#define REBOOT_TICKS 1200ull     /* ~12 s on the stop screen */

static volatile uint64_t g_beat_tick;
static volatile uint64_t g_beat_count;
static const char *volatile g_stage = "boot";

/* ---- small local formatters (kstring.h has no printf) ---- */

static void hex64(char *out, uint64_t v, int digits)
{
    static const char *d = "0123456789abcdef";
    for (int i = digits - 1; i >= 0; i--) {
        out[i] = d[v & 0xf];
        v >>= 4;
    }
    out[digits] = 0;
}

static void dec64(char *out, uint64_t v)
{
    char buf[24];
    int n = 0;
    if (!v) {
        out[0] = '0';
        out[1] = 0;
        return;
    }
    while (v && n < 23) {
        buf[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    int i = 0;
    while (n)
        out[i++] = buf[--n];
    out[i] = 0;
}

/* ---- helpers ---- */

static uint64_t read_cr2(void)
{
    uint64_t v;
    __asm__ volatile("mov %%cr2, %0" : "=r"(v));
    return v;
}

static uint64_t read_cr3(void)
{
    uint64_t v;
    __asm__ volatile("mov %%cr3, %0" : "=r"(v));
    return v;
}

static void outb_port(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static uint8_t inb_port(uint16_t port)
{
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static void cold_reboot(void) __attribute__((noreturn));
static void cold_reboot(void)
{
    /* 8042 pulse reset line, then fall back to a triple fault. */
    for (int i = 0; i < 200000; i++) {
        if (!(inb_port(0x64) & 0x02))
            break;
    }
    outb_port(0x64, 0xFE);

    struct {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed)) idtr = {0, 0};
    __asm__ volatile("lidt %0" : : "m"(idtr));
    __asm__ volatile("int3");
    for (;;)
        __asm__ volatile("hlt");
}

static const char *vector_name(uint64_t v)
{
    static const char *const names[32] = {
        "divide error", "debug", "NMI", "breakpoint",
        "overflow", "bound range", "invalid opcode", "device not available",
        "double fault", "coprocessor overrun", "invalid TSS", "segment not present",
        "stack fault", "general protection", "page fault", "reserved",
        "x87 FP", "alignment check", "machine check", "SIMD FP",
        "virtualisation", "control protection", "22", "23",
        "24", "25", "26", "27", "hypervisor injection", "VMM comms",
        "security", "31"
    };
    if (v < 32)
        return names[v];
    return "external interrupt";
}

static void serial_report(const struct trap_frame *f, const char *why,
                          uint64_t cr2, uint64_t cr3)
{
    serial_puts("\n==== JACKBOT STOP ====\n");
    serial_puts("reason: "); serial_puts(why); serial_putc('\n');
    if (f) {
        serial_puts("vec="); serial_puthex(f->vector);
        serial_puts(" ("); serial_puts(vector_name(f->vector)); serial_puts(")");
        serial_puts(" err="); serial_puthex(f->error);
        serial_puts(" rip="); serial_puthex(f->rip);
        serial_puts(" cs="); serial_puthex(f->cs);
        serial_puts(" rflags="); serial_puthex(f->rflags);
        serial_puts(" rsp="); serial_puthex(f->rsp);
        serial_putc('\n');
        serial_puts("rax="); serial_puthex(f->rax);
        serial_puts(" rbx="); serial_puthex(f->rbx);
        serial_puts(" rcx="); serial_puthex(f->rcx);
        serial_puts(" rdx="); serial_puthex(f->rdx); serial_putc('\n');
        serial_puts("rsi="); serial_puthex(f->rsi);
        serial_puts(" rdi="); serial_puthex(f->rdi);
        serial_puts(" rbp="); serial_puthex(f->rbp);
        serial_puts(" rsp="); serial_puthex(f->rsp); serial_putc('\n');
        serial_puts("r8="); serial_puthex(f->r8);
        serial_puts(" r9="); serial_puthex(f->r9);
        serial_puts(" r10="); serial_puthex(f->r10);
        serial_puts(" r11="); serial_puthex(f->r11); serial_putc('\n');
        serial_puts("r12="); serial_puthex(f->r12);
        serial_puts(" r13="); serial_puthex(f->r13);
        serial_puts(" r14="); serial_puthex(f->r14);
        serial_puts(" r15="); serial_puthex(f->r15); serial_putc('\n');
    }
    serial_puts("cr2="); serial_puthex(cr2);
    serial_puts(" cr3="); serial_puthex(cr3);
    serial_puts(" stage=\""); serial_puts((const char *)g_stage);
    serial_puts("\" beats="); serial_puthex(g_beat_count);
    serial_puts(" ticks="); serial_puthex(timer_get_ticks());
    serial_puts("\n======================\n");
}

/* ---- on-screen report ---- */

static void screen_report(const struct trap_frame *f, const char *why,
                          uint64_t cr2, uint64_t cr3)
{
    uint32_t w = fb_width();
    uint32_t h = fb_height();
    int scale = (w >= 1600) ? 2 : 1;
    int pad = 24 * scale;
    int lh = (FONT_H + 8) * scale;
    int y = pad;

    gfx_set_scale(scale);

    fb_clear(fb_rgb(0x12, 0x10, 0x1e));
    fb_fill_rect(0, 0, (int)w, 6 * scale, fb_rgb(0xe0, 0x3a, 0x3a));

    char line[96];
    uint32_t dim = fb_rgb(0x9a, 0x96, 0xb0);
    uint32_t hi = fb_rgb(0xf2, 0xf0, 0xff);
    uint32_t red = fb_rgb(0xff, 0x6b, 0x6b);

    gfx_draw_string(pad, y, hi, 0, "JackBOT OS stopped");
    y += lh * 2;
    gfx_draw_string(pad, y, red, 0, why ? why : "unknown fault");
    y += lh;

    if (f) {
        static char vbuf[64];
        char *p = vbuf;
        const char *n = "vector ";
        while (*n)
            *p++ = *n++;
        char num[8];
        dec64(num, f->vector);
        for (int i = 0; num[i]; i++)
            *p++ = num[i];
        *p++ = ' ';
        *p++ = '(';
        const char *vn = vector_name(f->vector);
        while (*vn)
            *p++ = *vn++;
        *p++ = ')';
        *p = 0;
        gfx_draw_string(pad, y, hi, 0, vbuf);
        y += lh;

        hex64(line, f->error, 4);
        gfx_draw_string(pad, y, dim, 0, "error");
        gfx_draw_string(pad + 7 * 8 * scale, y, hi, 0, line);
        y += lh;

        hex64(line, f->rip, 16);
        gfx_draw_string(pad, y, dim, 0, "rip  ");
        gfx_draw_string(pad + 5 * 8 * scale, y, hi, 0, line);
        y += lh;

        hex64(line, f->rsp, 16);
        gfx_draw_string(pad, y, dim, 0, "rsp  ");
        gfx_draw_string(pad + 5 * 8 * scale, y, hi, 0, line);
        y += lh;

        hex64(line, f->rflags, 16);
        gfx_draw_string(pad, y, dim, 0, "flags ");
        gfx_draw_string(pad + 6 * 8 * scale, y, hi, 0, line);
        y += lh;
    }

    hex64(line, cr2, 16);
    gfx_draw_string(pad, y, dim, 0, "cr2  ");
    gfx_draw_string(pad + 5 * 8 * scale, y, hi, 0, line);
    y += lh;

    hex64(line, cr3, 16);
    gfx_draw_string(pad, y, dim, 0, "cr3  ");
    gfx_draw_string(pad + 5 * 8 * scale, y, hi, 0, line);
    y += lh;

    gfx_draw_string(pad, y, dim, 0, "stage:");
    gfx_draw_string(pad + 6 * 8 * scale, y, hi, 0, (const char *)g_stage);
    y += lh * 2;

    if (f) {
        struct {
            const char *name;
            uint64_t val;
        } regs[8] = {
            {"rax", f->rax}, {"rbx", f->rbx}, {"rcx", f->rcx}, {"rdx", f->rdx},
            {"rsi", f->rsi}, {"rdi", f->rdi}, {"rbp", f->rbp}, {"r8 ", f->r8},
        };
        for (int i = 0; i < 8; i++) {
            int col = (i < 4) ? 0 : 1;
            int cx = pad + col * (24 * 8 * scale + 16 * scale);
            int cy = y + (i % 4) * lh;
            hex64(line, regs[i].val, 16);
            gfx_draw_string(cx, cy, dim, 0, regs[i].name);
            gfx_draw_string(cx + 3 * 8 * scale, cy, hi, 0, line);
        }
        y += lh * 4;
    }

    gfx_draw_string(pad, (int)h - 3 * lh, dim, 0,
                    "full register dump is on the serial port (COM1)");
    gfx_draw_string(pad, (int)h - 2 * lh, hi, 0,
                    "Esc = restart now, otherwise this PC restarts in 12 s");
    fb_mark_dirty(0, 0, (int)w, (int)h);
    fb_present_all();
}

/* The stop path runs with interrupts off, so the 18 Hz tick counter never
   advances here and must not be used as a clock. Read the PIT channel 0
   counter instead (latch command, 1.193182 MHz, counts down) and accumulate
   the decrease modulo 65536 - that keeps running whatever the IRQ state is. */
static uint16_t pit_latch(void)
{
    outb_port(0x43, 0x00);              /* latch channel 0 */
    uint8_t lo = inb_port(0x40);
    uint8_t hi = inb_port(0x40);
    return (uint16_t)(((uint16_t)hi << 8) | lo);
}

static void wait_and_restart(void) __attribute__((noreturn));
static void wait_and_restart(void)
{
    const uint64_t target = 1193182ull * 12ull;   /* 12 s of PIT counts */
    uint16_t prev = pit_latch();
    uint64_t elapsed = 0;

    for (;;) {
        while (inb_port(0x64) & 0x01) {
            uint8_t sc = inb_port(0x60);
            if (sc == 0x01)             /* Esc make code */
                cold_reboot();
        }
        uint16_t now = pit_latch();
        elapsed += (uint64_t)((uint16_t)(prev - now));
        prev = now;
        if (elapsed >= target)
            cold_reboot();
        __asm__ volatile("pause");
    }
}

void panic_screen(struct trap_frame *f, const char *why)
{
    __asm__ volatile("cli");

    uint64_t cr2 = read_cr2();
    uint64_t cr3 = read_cr3();

    serial_report(f, why, cr2, cr3);
    screen_report(f, why, cr2, cr3);
    wait_and_restart();
}

void panic_stop(const char *why)
{
    panic_screen(0, why);
}

void panic_stage(const char *s)
{
    g_stage = s;
}

void panic_beat(void)
{
    g_beat_tick = timer_get_ticks();
    g_beat_count++;
}

void panic_check_stall(void)
{
    uint64_t now = timer_get_ticks();
    if (!g_beat_tick)
        return;
    if (now - g_beat_tick < STALL_TICKS)
        return;

    char why[64];
    char *p = why;
    const char *a = "main loop stalled at \"";
    while (*a)
        *p++ = *a++;
    const char *s = (const char *)g_stage;
    while (*s)
        *p++ = *s++;
    *p++ = '"';
    *p = 0;

    panic_screen(0, why);
}
