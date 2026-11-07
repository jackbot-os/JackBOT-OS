#include <stdint.h>
#include <stddef.h>
#include <kernel/hal.h>
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
#include <kernel/ahci.h>
#include <kernel/fat32.h>
#include <kernel/part.h>
#include <kernel/posix.h>
#include <kernel/blk.h>
#include <kernel/mkfat.h>
#include <kernel/payload.h>
#include <kernel/installer.h>
#include <kernel/installer_gui.h>
#include <kernel/oobe.h>
#include <kernel/protect.h>
#include <kernel/persist.h>
#include <kernel/config.h>
#include <kernel/jbot.h>
#include <kernel/py.h>
#include <kernel/jca.h>
#include <kernel/jcac.h>
#include <kernel/hwcompat.h>
#include <kernel/hwd.h>
#include <kernel/honeypot.h>
#include <kernel/i18n.h>
#include "i18n_data.h"
#include <kernel/ps2.h>
#include <kernel/pci.h>
#include <kernel/ac97.h>
#include <kernel/dev.h>
#include <kernel/compat.h>
#include <kernel/gfxtext.h>
#include <kernel/desktop.h>
#include <kernel/splash.h>
#include <kernel/net.h>
#include <kernel/selftest.h>
#include <kernel/svga.h>
#include <kernel/gpu.h>
#include <kernel/gpu_nv.h>
#include <kernel/acpi.h>
#include <kernel/usb.h>
#include <kernel/recycle.h>
#include <kernel/bluetooth.h>
#include <kernel/voice.h>
#include <kernel/hmac_sha256.h>
#include "native_demo.h"
#include "native_demo_cxx.h"
#include "native_libc.h"

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

static int boot_mount_system(void)
{
    struct part_info pi[PART_SCAN_MAX];
    int n = part_scan(pi, PART_SCAN_MAX);
    if (n <= 0)
        return -1;

    serial_puts("[PART] scheme=");
    serial_puts(part_scheme_name(pi[0].scheme));
    serial_puts(" count=");
    serial_puthex((uint32_t)n);
    serial_putc('\n');
    for (int i = 0; i < n; i++) {
        serial_puts("[PART] ");
        serial_puthex((uint32_t)i);
        serial_puts(" start=");
        serial_puthex(pi[i].start_lba);
        serial_puts(" sectors=");
        serial_puthex(pi[i].num_sectors);
        serial_puts(" fat=");
        serial_puthex(pi[i].is_fat ? 1u : 0u);
        serial_putc('\n');
    }

    /* Mount the first FAT partition; that is the system volume. */
    int sys_idx = -1;
    for (int i = 0; i < n; i++) {
        if (!pi[i].is_fat)
            continue;
        if (fat_mount_part(pi[i].start_lba, pi[i].num_sectors) == 0) {
            sys_idx = i;
            serial_puts("[PART] mounted partition ");
            serial_puthex((uint32_t)i);
            serial_putc('\n');
            break;
        }
    }
    if (sys_idx < 0)
        return -1;

    /* Every FAT volume on the disk is reachable: mount each in turn and
       count its root entries, then put the system volume back so the rest
       of the boot keeps running on it. */
    for (int i = 0; i < n; i++) {
        if (i == sys_idx || !pi[i].is_fat)
            continue;
        static struct fat_dirent de[8];
        if (fat_mount_part(pi[i].start_lba, pi[i].num_sectors) != 0)
            continue;
        int cnt = fat_list_dir("", de, 8);
        serial_puts("[PART] volume ");
        serial_puthex((uint32_t)i);
        serial_puts(" entries=");
        serial_puthex((uint32_t)(cnt < 0 ? 0 : cnt));
        serial_putc('\n');
    }
    fat_mount_part(pi[sys_idx].start_lba, pi[sys_idx].num_sectors);
    return 0;
}

/* Headless real-disk FAT32 regression: partitions, formats, mounts the
   attached ATA disk, then exercises write/readback/overwrite/delete/permissions.
   Driven only by the jb.fattest=1 command-line token; does not touch desktop. */
static void fat_disk_selftest(void)
{
    int pass = 1;
    if (!ata_present()) {
        serial_puts("[FAT-TEST] FAIL: no ATA disk\n");
        return;
    }

    uint64_t total = ata_sector_count();
    serial_puts("[FAT-TEST] sectors="); serial_puthex((uint32_t)total); serial_putc('\n');

    struct part_layout pl;
    if (part_plan(total, &pl) != 0) {
        serial_puts("[FAT-TEST] FAIL: part_plan\n");
        return;
    }
    if (part_write_mbr(0, &pl) != 0) {
        serial_puts("[FAT-TEST] FAIL: write MBR\n");
        return;
    }
    serial_puts("[FAT-TEST] MBR written, partitions=");
    serial_puthex((uint32_t)pl.count); serial_putc('\n');

    uint32_t s = pl.e[PART_SYS].start_lba;
    uint32_t n = pl.e[PART_SYS].num_sectors;
    if (mkfat_format(s, n, "JBOTTEST") != 0) {
        serial_puts("[FAT-TEST] FAIL: mkfat_format\n");
        return;
    }
    if (fat_mount_part(s, n) != 0) {
        serial_puts("[FAT-TEST] FAIL: mount\n");
        return;
    }
    serial_puts("[FAT-TEST] mounted; free=");
    serial_puthex(fat_free_bytes()); serial_putc('\n');

    static uint8_t buf[512];
    uint32_t rl = 0;

    const char *m1 = "fat32 real disk payload alpha 0123456789";
    uint32_t l1 = 0; while (m1[l1]) l1++;
    int rb = 0;
    if (fat_write_file("TEST.TXT", m1, l1) == 0 &&
        fat_read_file("TEST.TXT", buf, sizeof buf, &rl) == 0 && rl == l1) {
        rb = 1;
        for (uint32_t i = 0; i < l1; i++)
            if (buf[i] != (uint8_t)m1[i]) rb = 0;
    }
    serial_puts(rb ? "[FAT-TEST] write/readback PASS\n"
                   : "[FAT-TEST] write/readback FAIL\n");
    if (!rb) pass = 0;

    const char *m2 = "overwrite beta content longer than first";
    uint32_t l2 = 0; while (m2[l2]) l2++;
    int ov = 0;
    if (fat_write_file("TEST.TXT", m2, l2) == 0 &&
        fat_read_file("TEST.TXT", buf, sizeof buf, &rl) == 0 && rl == l2) {
        ov = 1;
        for (uint32_t i = 0; i < l2; i++)
            if (buf[i] != (uint8_t)m2[i]) ov = 0;
    }
    serial_puts(ov ? "[FAT-TEST] overwrite reuse PASS\n"
                   : "[FAT-TEST] overwrite reuse FAIL\n");
    if (!ov) pass = 0;

    int multi = fat_write_file("OVR.TXT", "second file", 11) == 0 &&
                fat_exists("TEST.TXT") && fat_exists("OVR.TXT");
    serial_puts(multi ? "[FAT-TEST] multi-file PASS\n"
                      : "[FAT-TEST] multi-file FAIL\n");
    if (!multi) pass = 0;

    int del = fat_delete("TEST.TXT") == 0 && fat_delete("OVR.TXT") == 0 &&
              !fat_exists("TEST.TXT") && !fat_exists("OVR.TXT");
    serial_puts(del ? "[FAT-TEST] delete PASS\n" : "[FAT-TEST] delete FAIL\n");
    if (!del) pass = 0;

    int a0 = protect_set_actor(ACTOR_ADMIN);
    if (!fat_exists("SYSTEM.IMG"))
        fat_write_file("SYSTEM.IMG", "system image body", 17);
    protect_set_actor(a0);

    protect_set_actor(ACTOR_USER);
    int u_del = (fat_delete("SYSTEM.IMG") != 0) && fat_exists("SYSTEM.IMG");
    int u_wr  = fat_write_file("SYSTEM.IMG", "x", 1) != 0;
    protect_set_actor(ACTOR_JBOT);
    int j_del = (fat_delete("SYSTEM.IMG") != 0) && fat_exists("SYSTEM.IMG");
    int j_rd  = fat_read_file("SYSTEM.IMG", buf, sizeof buf, &rl) == 0 && rl > 0;
    protect_set_actor(ACTOR_USER);
    int ncr = fat_write_file("USER.TXT", "hi", 2) == 0 &&
              fat_delete("USER.TXT") == 0;
    int prot = u_del && u_wr && j_del && j_rd && ncr;
    serial_puts(prot ? "[FAT-TEST] permissions PASS\n"
                     : "[FAT-TEST] permissions FAIL\n");
    if (!prot) pass = 0;

    serial_puts(pass ? "[FAT-TEST] RESULT: ALL PASS\n"
                     : "[FAT-TEST] RESULT: FAIL\n");
}

void kmain(uint64_t magic, uint64_t mb_info)
{
    (void)magic;
    (void)mb_info;

    serial_init();
    hal_early_init();
    vga_clear();

    vga_puts("JackBOT OS 1.0.9\n");
    serial_puts("JackBOT OS 1.0.9\n");

    hal_selftest();

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

    payload_collect(mb_info);
    int live_install = payload_live_mode();
    int live_desktop = payload_live_desktop();

    /* AHCI/SATA must be brought up before the block layer is initialised,
       so ata_read/ata_write can route through it. */
    serial_puts("[STEP] ahci_probe\n");
    {
        int ctrls = ahci_probe();
        serial_puts("[AHCI] controllers=");
        serial_puthex((uint32_t)ctrls);
        serial_puts(" ports_online=");
        serial_puthex((uint32_t)ahci_online_ports());
        serial_putc('\n');
        if (ahci_present()) {
            int p = ahci_primary_port();
            uint64_t secs = 0;
            ahci_port_sectors(p, &secs);
            serial_puts("[AHCI] primary port=");
            serial_puthex((uint32_t)p);
            serial_puts(" model='");
            serial_puts(ahci_port_model(p));
            serial_puts("' sectors=");
            serial_puthex(secs);
            serial_puts(" lba48=");
            serial_puthex((uint32_t)ahci_port_lba48(p));
            serial_putc('\n');
            /* read-only verification: no writes are performed */
            ahci_lba0_signature(p);
        }
    }

    serial_puts("[STEP] ata_init\n");
    ata_init();
    serial_puts("[STEP] ata_present=");
    serial_puthex(ata_present() ? 1 : 0);
    serial_putc('\n');

    /* One block backend for FAT32 and the partition scanner: ATA when the
       legacy controller owns the disk, AHCI otherwise. */
    static uint8_t ab[512];
    blk_init();
    serial_puts("[BLK] read0=");
    serial_puthex(blk_read(0, 1, ab) == 0 ? 1 : 0);
    serial_putc('\n');

    if (blk_present() && blk_read(0, 1, ab) == 0) {
        serial_puts("[ATA] oem=");
        for (int i = 3; i < 11; i++) {
            char c = (char)ab[i];
            serial_putc((c >= 32 && c < 127) ? c : '.');
        }
        serial_putc('\n');

        /* Raw last-sector write test is destructive by nature, so it only
           runs in the explicit headless regression mode, or on a disk with
           no MBR signature at all (nothing on it to damage). */
        if (payload_fattest() || ab[510] != 0x55 || ab[511] != 0xAA) {
            static uint8_t wb[512], rb2[512];
            uint64_t tlba = blk_sectors() - 2;
            for (int i = 0; i < 512; i++)
                wb[i] = (uint8_t)(i * 31 + 7);
            int wok = 1;
            if (blk_write(tlba, 1, wb) != 0)
                wok = 0;
            if (blk_read(tlba, 1, rb2) != 0)
                wok = 0;
            for (int i = 0; i < 512; i++)
                if (rb2[i] != wb[i])
                    wok = 0;
            serial_puts(wok ? "[ATA] write PASS\n" : "[ATA] write FAIL\n");
        }
    }

    /* Enumerate the partitions and mount the first FAT volume even in live
       mode: the disk manager lists them, and the POSIX file helpers need a
       real volume to read and write. */
    if (blk_present() && !fat_mounted())
        boot_mount_system();

    if (payload_fattest()) {
        serial_puts("[B] FAT disk headless regression\n");        fat_disk_selftest();
        serial_puts("[FAT-TEST] halt\n");
        for (;;)
            __asm__ volatile("hlt");
    }

    if (!live_install && !live_desktop && boot_mount_system() == 0) {
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
            {
                int a0 = protect_set_actor(ACTOR_ADMIN);
                if (!fat_exists("SYSTEM.IMG")) {
                    const char *simg = "JackBOT OS system image";
                    uint32_t sl2 = 0;
                    while (simg[sl2])
                        sl2++;
                    fat_write_file("SYSTEM.IMG", simg, sl2);
                }
                protect_set_actor(a0);
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

    serial_puts("[STEP] ata block done\n");
    serial_puts("[STEP] i18n_init\n");
    i18n_init();
    serial_puts("[STEP] i18n done\n");
    if (!live_install && !live_desktop) {
        persist_self_test();
        config_init();
        i18n_set_lang(config_language());
        config_self_test();
        honeypot_init();
        honeypot_self_test();
        jbot_init();
        jbot_self_test();
        py_init();
        py_self_test();
        jca_self_test();
        jcac_self_test();
    }
    recycle_init();

    serial_puts("[STEP] fb scan\n");
    {
        int fb_ok = 0;
        struct mb2_info *mi = (struct mb2_info *)(uintptr_t)mb_info;
        struct mb2_tag *t = mb2_first_tag(mi);
        while (t->type != MB2_TAG_END) {
            if (t->type == MB2_TAG_FRAMEBUFFER) {
                struct mb2_tag_framebuffer *f =
                    (struct mb2_tag_framebuffer *)t;
                serial_puts("[FB] tag bpp=");
                serial_puthex(f->framebuffer_bpp);
                serial_puts(" type=");
                serial_puthex(f->framebuffer_type);
                serial_putc('\n');
                if (f->framebuffer_type == 1 &&
                    (f->framebuffer_bpp == 32 || f->framebuffer_bpp == 24 ||
                     f->framebuffer_bpp == 16)) {
                    fb_ok = fb_linear_init(
                        f->framebuffer_addr,
                        f->framebuffer_width, f->framebuffer_height,
                        f->framebuffer_pitch, f->framebuffer_bpp,
                        f->red_field_position, f->red_mask_size,
                        f->green_field_position, f->green_mask_size,
                        f->blue_field_position, f->blue_mask_size);
                }
            }
            t = mb2_next_tag(t);
        }
        if (!fb_ok) {
            serial_puts("[FB] no multiboot framebuffer, fallback Bochs VBE\n");
            fb_bochs_init(1920, 1080);
        }
    }
    serial_puts("[STEP] fb_present=");
    serial_puthex(fb_present() ? 1 : 0);
    serial_putc('\n');
    if (fb_present()) {
        /* GPU scanout takeover: switch the VMSVGA device from VBE legacy
           into native SVGA II mode so the GPU owns the display. If the
           device rejects the hand-off we stay on the working VBE path. */
        serial_puts("[STEP] gpu takeover\n");
        pci_enumerate();
        gpu_probe_all();
        {
            uint64_t gphys = 0;
            uint32_t gpitch = 0, gsize = 0;
            if (svga_gpu_takeover(fb_width(), fb_height(),
                                  &gphys, &gpitch, &gsize))
                fb_rebind_gpu(gphys, fb_width(), fb_height(), gpitch);
        }
        serial_puts("[STEP] splash\n");
        fb_clear(fb_rgb(0, 0, 0));
        splash_show();
    } else {
        serial_puts("[FB] NO FRAMEBUFFER AVAILABLE\n");
    }

    serial_puts("[B] ps2\n");
    ps2_init();
    pic_clear_mask(1);
    pic_clear_mask(2);
    pic_clear_mask(12);

    if (live_install) {
        serial_puts("[B] pci (live)\n");
        pci_enumerate();
        svga_gpu_init();
        serial_puts("[B] ac97 (live)\n");
        if (ac97_init())
            ac97_play_boot();
        serial_puts("[B] launch installer GUI\n");
        installer_gui_run();
        for (;;)
            __asm__ volatile("hlt");
    }

    serial_puts("[B] pci\n");
    pci_enumerate();
    svga_gpu_init();
    serial_puts("[B] acpi\n");
    acpi_init();
    serial_puts("[B] usb\n");
    usb_scan();
    serial_puts("[B] ac97\n");
    ac97_init();

    serial_puts("[B] bluetooth\n");
    bt_init();
    bt_self_test();

    hmac_sha256_selftest();

    serial_puts("[B] voice\n");
    voice_init();
    voice_self_test();

    serial_puts("[B] compat drivers\n");
    compat_register_drivers();
    {
        int n = dev_probe_all();
        serial_puts("[COMPAT] matched+ok drivers=");
        serial_puthex((uint32_t)n);
        serial_putc('\n');
    }
    hwcompat_self_test();
    hwd_self_test();

    serial_puts("[B] net\n");
    compat_register_nics();
    net_init();
#ifdef NET_SELFTEST
    net_selftest();
#endif

    serial_puts("[B] sched\n");
    sched_init();
    serial_puts("[B] watchdog\n");
    watchdog_init();
    serial_puts("[B] spawn\n");
    thread_t *user = spawn_test_user();
    if (!user) {
        serial_puts("[FATAL] user self-test process spawn failed\n");
        __asm__ volatile("cli");
        for (;;)
            __asm__ volatile("hlt");
    }
    tss_set_rsp0(user->kstack_top);
    /* The scheduler now loads CR3 per thread (kernel threads run on the
       kernel page table, user threads on their own), so the main loop no
       longer needs a manual CR3 switch here. */

    /* Native payload demos: hello-world images built by tools/jcacc.py.
       The C one goes through chibicc, the C++ one through g++ in
       freestanding mode; both run as ring-3 processes and use the JackBOT
       syscall ABI (0 = print a character, 2 = exit). */
    serial_puts("[B] native C demo\n");
    if (jca_run_native(native_demo, (int)native_demo_len) == 0)
        thread_yield();
    else
        serial_puts("[NAT] native C payload rejected\n");

    serial_puts("[B] native C++ demo\n");
    if (jca_run_native(native_demo_cxx, (int)native_demo_cxx_len) == 0)
        thread_yield();
    else
        serial_puts("[NAT] native C++ payload rejected\n");

    /* libc + POSIX syscall self-test: the payload below is linked against
       user/libc (printf, malloc, fopen/fread) and talks to kernel/posix.c
       through int 0x80. It needs a mounted FAT32 volume for the file test,
       so live mode mounts it here when a disk is present. */
    serial_puts("[B] libc demo\n");
    if (!fat_mounted() && boot_mount_system() == 0)
        serial_puts("[LIBC] disk mounted for the libc self-test\n");
    if (fat_mounted() && !fat_exists("LC.TXT"))
        fat_write_file("LC.TXT", "libc file io ok\n", 16);
    if (jca_run_native(native_libc, (int)native_libc_len) == 0)
        thread_yield();
    else
        serial_puts("[NAT] libc payload rejected\n");

    /* The POSIX surface the libc payload talks to: prove mkdir/rename/
       access/unlink/stat and console input really work on this machine. */
    posix_selftest();


    serial_puts("[B] desktop\n");
    desktop_init();
    serial_puts("[B] desktop done\n");

    if (!live_desktop && !config_oobe_done()) {
        serial_puts("[B] first-boot OOBE\n");
        oobe_run();
    }

    if (ac97_present())
        ac97_play_boot();

    {
        int saved_lang = config_language();
        const char *en = tr(STR_CLOSE);
        i18n_set_code("zh-CN");
        const char *zh = tr(STR_CLOSE);
        int changed = (en != zh) && ((unsigned char)zh[0] >= 0x80);
        int ar = i18n_set_code("ar");
        int ar_rtl = i18n_rtl_idx(ar);
        int en_rtl = i18n_rtl_idx(0);
        int langok = i18n_count() >= 20 && changed && ar_rtl && !en_rtl;
        int fontok = gfx_font_count() == 3;
        int f0 = gfx_get_font();
        gfx_set_font(2);
        fontok = fontok && gfx_get_font() == 2;
        gfx_set_font(f0);
        i18n_set_lang(saved_lang);
        serial_puts((langok && fontok) ? "[I18N] test PASS\n"
                                      : "[I18N] test FAIL\n");
    }

    for (;;) {
        desktop_run();
        thread_yield();
        __asm__ volatile("hlt");
    }
}




