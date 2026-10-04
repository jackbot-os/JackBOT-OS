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
#include <kernel/ata.h>
#include <kernel/fat32.h>
#include <kernel/installer.h>
#include <kernel/protect.h>
#include <kernel/i18n.h>
#include "i18n_data.h"
#include <kernel/ps2.h>
#include <kernel/gfxtext.h>
#include <kernel/desktop.h>

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

    ata_init();
    static uint8_t ab[512];
    if (ata_present() && ata_read(0, 1, ab) == 0) {
        serial_puts("[ATA] oem=");
        for (int i = 3; i < 11; i++) {
            char c = (char)ab[i];
            serial_putc((c >= 32 && c < 127) ? c : '.');
        }
        serial_putc('\n');

        static uint8_t wb[512], rb2[512];
        uint64_t tlba = ata_sector_count() - 2;
        for (int i = 0; i < 512; i++)
            wb[i] = (uint8_t)(i * 31 + 7);
        int wok = 1;
        if (ata_write(tlba, 1, wb) != 0)
            wok = 0;
        if (ata_read(tlba, 1, rb2) != 0)
            wok = 0;
        for (int i = 0; i < 512; i++)
            if (rb2[i] != wb[i])
                wok = 0;
        serial_puts(wok ? "[ATA] write PASS\n" : "[ATA] write FAIL\n");
    }

    if (fat_mount() == 0) {
        const char *msg = "JackBOT FAT32 test payload 0123456789";
        uint32_t slen = 0;
        while (msg[slen])
            slen++;
        if (fat_write_file("TEST.TXT", msg, slen) == 0) {
            static uint8_t fbuf[256];
            uint32_t rl = 0;
            int ok = 0;
            if (fat_read_file("TEST.TXT", fbuf, sizeof fbuf, &rl) == 0 &&
                rl == slen) {
                ok = 1;
                for (uint32_t i = 0; i < slen; i++)
                    if (fbuf[i] != (uint8_t)msg[i])
                        ok = 0;
            }
            serial_puts(ok ? "[FAT] readback PASS\n"
                           : "[FAT] readback FAIL\n");
            if (fat_delete("TEST.TXT") == 0)
                serial_puts("[FAT] delete ok\n");
            serial_puts(fat_exists("TEST.TXT") ? "[FAT] still exists\n"
                                               : "[FAT] gone\n");
            if (installer_run() == 0) {
                int had_stg = fat_exists("INSTALL.STG");
                postinstall_cleanup();
                int stg_gone = !fat_exists("INSTALL.STG");
                int marker_gone = !fat_exists("FIRSTBOOT");
                int sys_kept = fat_exists("SYSTEM.IMG");
                int ok = had_stg && stg_gone && marker_gone && sys_kept;
                serial_puts(ok ? "[INST] cleanup test PASS\n"
                               : "[INST] cleanup test FAIL\n");
            }

            protect_set_actor(ACTOR_USER);
            int user_del_blocked =
                (fat_delete("SYSTEM.IMG") != 0) && fat_exists("SYSTEM.IMG");
            int user_write_blocked =
                fat_write_file("SYSTEM.IMG", "x", 1) != 0;

            protect_set_actor(ACTOR_JBOT);
            int jbot_del_blocked =
                (fat_delete("SYSTEM.IMG") != 0) && fat_exists("SYSTEM.IMG");
            static uint8_t jrb[128];
            uint32_t jrl = 0;
            int jbot_read_ok =
                fat_read_file("SYSTEM.IMG", jrb, sizeof jrb, &jrl) == 0 &&
                jrl > 0;

            protect_set_actor(ACTOR_USER);
            int noncore_ok =
                fat_write_file("USER.TXT", "hi", 2) == 0 &&
                fat_delete("USER.TXT") == 0;

            int pok = user_del_blocked && user_write_blocked &&
                      jbot_del_blocked && jbot_read_ok && noncore_ok;
            serial_puts(pok ? "[PROT] protection test PASS\n"
                            : "[PROT] protection test FAIL\n");
        }
    }

    fb_bochs_init(1920, 1080);
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

    ps2_init();
    pic_clear_mask(1);
    pic_clear_mask(2);
    pic_clear_mask(12);

    sched_init();
    watchdog_init();
    thread_t *user = spawn_test_user();
    tss_set_rsp0(user->kstack_top);

    desktop_init();

    {
        i18n_init();
        const char *en = tr(STR_CLOSE);
        i18n_set_code("zh-CN");
        const char *zh = tr(STR_CLOSE);
        int changed = (en != zh) && ((unsigned char)zh[0] >= 0x80);
        i18n_set_lang(0);
        int langok = i18n_count() == 7 && changed;
        int fontok = gfx_font_count() == 3;
        int f0 = gfx_get_font();
        gfx_set_font(2);
        fontok = fontok && gfx_get_font() == 2;
        gfx_set_font(f0);
        serial_puts((langok && fontok) ? "[I18N] test PASS\n"
                                      : "[I18N] test FAIL\n");
    }

    __asm__ volatile("mov %0, %%cr3" : : "r"(user->p4phys));

    for (;;) {
        desktop_run();
        thread_yield();
        __asm__ volatile("hlt");
    }
}
