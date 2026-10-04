#include <stdint.h>
#include <stddef.h>
#include <kernel/serial.h>
#include <kernel/apic.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/heap.h>
#include <kernel/multiboot2.h>
#include <kernel/proc.h>
#include <kernel/crash.h>
#include <kernel/fb.h>

#define VGA_MEM ((volatile uint16_t *)0xb8000)
#define VGA_W 80
#define VGA_H 25
#define VGA_ATTR 0x1f
#define TIMER_PROOF_TICKS 500

void gdt_init(void);
void tss_set_rsp0(uint64_t rsp0);
thread_t *spawn_test_user(void);
thread_t *spawn_crash_user(void);
void pic_remap(void);
void pic_mask_all(void);
void pic_clear_mask(uint8_t irq);
void idt_init(void);
void timer_init(void);
uint64_t timer_get_ticks(void);

static void vga_clear(void)
{
    for (size_t i = 0; i < VGA_W * VGA_H; i++)
        VGA_MEM[i] = (uint16_t)' ' | ((uint16_t)VGA_ATTR << 8);
}

static void vga_puts(const char *s)
{
    static size_t row = 0;
    static size_t col = 0;

    while (*s) {
        char c = *s++;
        if (c == '\n') {
            col = 0;
            row++;
            continue;
        }
        if (col >= VGA_W) {
            col = 0;
            row++;
        }
        if (row >= VGA_H)
            break;
        VGA_MEM[row * VGA_W + col] =
            (uint16_t)(uint8_t)c | ((uint16_t)VGA_ATTR << 8);
        col++;
    }
}

static int check_pattern(void *p, size_t n, uint8_t seed)
{
    uint8_t *b = p;
    for (size_t i = 0; i < n; i++)
        b[i] = (uint8_t)(seed + i);
    for (size_t i = 0; i < n; i++)
        if (b[i] != (uint8_t)(seed + i))
            return 0;
    return 1;
}

static void mem_selftest(void)
{
    int ok = 1;
    size_t sizes[] = {32, 4096, 100000};
    void *blk[3];
    for (int i = 0; i < 3; i++) {
        blk[i] = kmalloc(sizes[i]);
        if (!blk[i] ||
            !check_pattern(blk[i], sizes[i], (uint8_t)(i * 7)))
            ok = 0;
    }

    kfree(blk[1]);
    void *again = kmalloc(4096);
    if (!again || !check_pattern(again, 4096, 0x5a))
        ok = 0;

    void *grown = krealloc(blk[0], 500);
    if (!grown || !check_pattern(grown, 500, 0x33))
        ok = 0;

    void *z = kcalloc(8, 64);
    uint8_t *zb = z;
    for (int i = 0; i < 512; i++)
        if (zb[i])
            ok = 0;

    serial_puts(ok ? "HEAP SELFTEST OK\n" : "HEAP SELFTEST FAIL\n");
    serial_puts("heap mapped=");
    serial_puthex(heap_bytes());
    serial_putc('\n');
}

void kmain(uint64_t magic, uint64_t mb_info)
{
    (void)magic;
    (void)mb_info;

    serial_init();
    vga_clear();

    vga_puts("JackBOT OS 0.5.0\n");
    serial_puts("JackBOT OS 0.5.0\n");

    gdt_init();
    pic_remap();
    idt_init();
    timer_init();
    pic_mask_all();
    pic_clear_mask(0);

    pmm_init(mb_info);
    vmm_init();
    heap_init();

    apic_init();
    __asm__ volatile("sti");

    serial_puts("GDT/IDT/PIT/PM initialized\n");
    serial_puts("mem total="); serial_puthex(pmm_total_bytes());
    serial_puts(" free="); serial_puthex(pmm_free_bytes());
    serial_putc('\n');

    mem_selftest();

    fb_bochs_init(1024, 768);
    if (fb_present()) {
        fb_clear(fb_rgb(18, 22, 34));
        fb_fill_rect(0, 0, (int)fb_width() / 3, (int)fb_height(),
                     fb_rgb(200, 60, 60));
        fb_fill_rect((int)fb_width() / 3, 0, (int)fb_width() / 3,
                     (int)fb_height(), fb_rgb(60, 170, 90));
        fb_fill_rect(2 * (int)fb_width() / 3, 0,
                     (int)fb_width() - 2 * (int)fb_width() / 3,
                     (int)fb_height(), fb_rgb(70, 110, 210));
    }

    sched_init();
    watchdog_init();
    thread_t *user = spawn_test_user();
    tss_set_rsp0(user->kstack_top);
    __asm__ volatile("mov %0, %%cr3" : : "r"(user->p4phys));

    int timer_reported = 0;
    uint64_t last_dot = 0;
    for (;;) {
        thread_yield();
        __asm__ volatile("hlt");
        uint64_t t = timer_get_ticks();
        if (t / 100 > last_dot) {
            serial_putc('.');
            last_dot = t / 100;
        }
        if (!timer_reported && t >= TIMER_PROOF_TICKS) {
            serial_puts("\ntimer IRQ alive at tick ");
            serial_puthex(t);
            serial_putc('\n');
            timer_reported = 1;
        }
    }
}
