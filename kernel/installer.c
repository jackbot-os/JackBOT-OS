#include <stdint.h>
#include <stddef.h>
#include <kernel/installer.h>
#include <kernel/fat32.h>
#include <kernel/part.h>
#include <kernel/mkfat.h>
#include <kernel/grub_install.h>
#include <kernel/payload.h>
#include <kernel/ata.h>
#include <kernel/protect.h>
#include <kernel/serial.h>
#include <kernel/i18n.h>

static install_progress_fn g_cb;
static int g_lang_idx = 0;
static uint32_t g_sys_mb = 0;
static int g_tz_idx = 20;

void installer_set_language(int idx)
{
    g_lang_idx = idx;
}
int installer_language(void)
{
    return g_lang_idx;
}
void installer_set_sys_mb(uint32_t mb)
{
    g_sys_mb = mb;
}
uint32_t installer_sys_mb(void)
{
    return g_sys_mb;
}
void installer_set_tz(int idx)
{
    if (idx < 0) idx = 0;
    if (idx > 24) idx = 24;
    g_tz_idx = idx;
}
int installer_tz(void)
{
    return g_tz_idx;
}

static void report(const char *step, int pct)
{
    serial_puts("[INST] ");
    serial_puts(step);
    serial_puts(" pct=");
    serial_puthex((uint32_t)pct);
    serial_putc('\n');
    if (g_cb)
        g_cb(step, pct);
}

static uint32_t slen(const char *s)
{
    uint32_t n = 0;
    while (s[n])
        n++;
    return n;
}

static uint32_t rd32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int magic_eq(const uint8_t *p, const char *m)
{
    for (int i = 0; m[i]; i++)
        if (p[i] != (uint8_t)m[i])
            return 0;
    return 1;
}

/* 解析 GRUBMODS blob，按真实文件名（含长名）写入 i386-pc 目录 */
static int install_grub_modules(const struct payload_mod *blob)
{
    const uint8_t *p = blob->data;
    const uint8_t *end = p + blob->size;
    if (blob->size < 13 || !magic_eq(p, "GRUBMODS1")) {
        serial_puts("[INST] bad grubmods magic\n");
        return -1;
    }
    p += 9;
    uint32_t count = rd32le(p);
    p += 4;
    serial_puts("[INST] grub modules count=");
    serial_puthex(count);
    serial_putc('\n');
    for (uint32_t i = 0; i < count; i++) {
        if (p + 4 > end)
            return -1;
        uint32_t nlen = rd32le(p);
        p += 4;
        if (p + nlen + 4 > end || nlen >= 32)
            return -1;
        char name[32];
        for (uint32_t k = 0; k < nlen; k++)
            name[k] = (char)p[k];
        name[nlen] = 0;
        p += nlen;
        uint32_t dlen = rd32le(p);
        p += 4;
        if (p + dlen > end)
            return -1;
        if (fat_write_file_lfn("BOOT/GRUB/I386-PC", name, p, dlen) != 0) {
            serial_puts("[INST] write module FAIL: ");
            serial_puts(name);
            serial_putc('\n');
            return -1;
        }
        p += dlen;
    }
    return 0;
}

int installer_have_payload(void)
{
    return payload_find(PAYLOAD_BOOTIMG) && payload_find(PAYLOAD_COREIMG) &&
           payload_find(PAYLOAD_GRUBMODS) && payload_find(PAYLOAD_GRUBCFG) &&
           payload_find(PAYLOAD_FONT) && payload_find(PAYLOAD_SYSKERNEL);
}

int installer_probe(struct disk_info *di)
{
    di->state = INST_STATE_NO_DISK;
    di->layout.count = 0;
    di->sys_start = 0;
    di->sys_sectors = 0;
    di->total_sectors = ata_sector_count();
    if (!ata_present() || di->total_sectors == 0)
        return 0;

    static uint8_t mbr[512];
    if (part_read_mbr(mbr) != 0) {
        di->state = INST_STATE_NO_DISK;
        return 0;
    }
    if (part_parse(mbr, &di->layout) != 0 || di->layout.count == 0) {
        di->state = INST_STATE_EMPTY;
        return 0;
    }

    uint32_t s, n;
    if (part_find_system(&di->layout, &s, &n) == 0) {
        di->sys_start = s;
        di->sys_sectors = n;
        if (fat_mount_part(s, n) == 0) {
            di->state = fat_exists(INSTALL_MARKER) ? INST_STATE_INSTALLED
                                                  : INST_STATE_FOREIGN;
            fat_unmount();
        } else {
            di->state = INST_STATE_FOREIGN;
        }
    } else {
        di->state = INST_STATE_FOREIGN;
    }
    return 0;
}

int installer_install(int mode, install_progress_fn progress)
{
    g_cb = progress;

    const struct payload_mod *bootimg = payload_find(PAYLOAD_BOOTIMG);
    const struct payload_mod *core = payload_find(PAYLOAD_COREIMG);
    const struct payload_mod *grubmods = payload_find(PAYLOAD_GRUBMODS);
    const struct payload_mod *grubcfg = payload_find(PAYLOAD_GRUBCFG);
    const struct payload_mod *font = payload_find(PAYLOAD_FONT);
    const struct payload_mod *kernelp = payload_find(PAYLOAD_SYSKERNEL);
    const struct payload_mod *fsans = payload_find(PAYLOAD_FSANS);
    const struct payload_mod *fmono = payload_find(PAYLOAD_FMONO);
    const struct payload_mod *fmonob = payload_find(PAYLOAD_FMONOB);
    const struct payload_mod *fcjk = payload_find(PAYLOAD_FCJK);
    const struct payload_mod *fkr = payload_find(PAYLOAD_FKR);
    const struct payload_mod *far = payload_find(PAYLOAD_FAR);
    const struct payload_mod *fth = payload_find(PAYLOAD_FTH);
    const struct payload_mod *fhi = payload_find(PAYLOAD_FHI);
    const struct payload_mod *bootpcm = payload_find(PAYLOAD_BOOTPCM);
    const struct payload_mod *warnpcm = payload_find(PAYLOAD_WARNPCM);
    if (!bootimg || !core || !grubmods || !grubcfg || !font || !kernelp) {
        serial_puts("[INST] missing payload modules\n");
        return -1;
    }

    struct disk_info di;
    if (installer_probe(&di) != 0 || di.state == INST_STATE_NO_DISK) {
        serial_puts("[INST] no target disk\n");
        return -1;
    }

    struct part_layout pl;
    int fresh = (mode == INST_MODE_FRESH) || di.state == INST_STATE_EMPTY;
    if (mode == INST_MODE_UPGRADE && di.state == INST_STATE_INSTALLED) {
        pl = di.layout;
        report("Preserving existing partitions", 12);
    } else {
        if (part_plan_custom(di.total_sectors, g_sys_mb, &pl) != 0) {
            serial_puts("[INST] partition plan FAIL\n");
            return -1;
        }
        report("Partitioning disk", 18);
    }

    if (grub_install_boot_disk(bootimg->data, core->data, core->size, &pl) !=
        0) {
        serial_puts("[INST] boot loader install FAIL\n");
        return -1;
    }
    report("Installing boot loader", 38);

    uint32_t sys_start, sys_sec;
    if (part_find_system(&pl, &sys_start, &sys_sec) != 0) {
        serial_puts("[INST] no system partition found\n");
        return -1;
    }

    if (mkfat_format(sys_start, sys_sec, "JACKBOT SYS") != 0) {
        serial_puts("[INST] system format FAIL\n");
        return -1;
    }
    report("Formatting system partition", 52);

    if (fresh && pl.count >= 2) {
        mkfat_format(pl.e[1].start_lba, pl.e[1].num_sectors,
                     "JACKBOT DATA");
        report("Preparing data partition", 60);
    }

    if (fat_mount_part(sys_start, sys_sec) != 0) {
        serial_puts("[INST] system mount FAIL\n");
        return -1;
    }

    int old = protect_set_actor(ACTOR_ADMIN);
    int rc = 0;

    if (fat_write_file("BOOT/KERNEL", kernelp->data, kernelp->size) != 0) {
        serial_puts("[INST] write kernel FAIL\n");
        rc = -1;
    }
    report("Copying system kernel", 70);

    if (fat_write_file("BOOT/GRUB/GRUB.CFG", grubcfg->data,
                       grubcfg->size) != 0) {
        serial_puts("[INST] write grub.cfg FAIL\n");
        rc = -1;
    }
    report("Writing boot configuration", 76);

    if (install_grub_modules(grubmods) != 0) {
        serial_puts("[INST] write grub modules FAIL\n");
        rc = -1;
    }
    report("Installing GRUB modules", 84);

    if (fat_write_file("BOOT/GRUB/FONTS/UNICODE.PF2", font->data,
                       font->size) != 0) {
        serial_puts("[INST] write font FAIL\n");
        rc = -1;
    }
    report("Installing fonts", 90);

    const struct { const char *path; const struct payload_mod *m; } ttfs[] = {
        {"SANS.TTF", fsans}, {"MONO.TTF", fmono},
        {"MONOB.TTF", fmonob}, {"CJK.TTF", fcjk},
        {"KR.TTF", fkr}, {"AR.TTF", far},
        {"TH.TTF", fth}, {"HI.TTF", fhi}
    };
    for (unsigned i = 0; i < sizeof(ttfs) / sizeof(ttfs[0]); i++) {
        if (ttfs[i].m && ttfs[i].m->data &&
            fat_write_file(ttfs[i].path, ttfs[i].m->data,
                           ttfs[i].m->size) != 0) {
            serial_puts("[INST] write TTF FAIL\n");
            rc = -1;
        }
    }

    /* All 205 per-language TTFs are installed on the user disk, not just
       the 8 fallback faces: the bundle lands as /BOOT/LGFONT.BIN (hd.cfg
       declares it as a multiboot module so the kernel indexes it), and
       every face is unpacked to /BOOT/FONTS/<CODE>.TTF so anything that
       opens a plain file has the real font there. Disk cost is ~32 MB
       total; installers are expected to take a while. */
    {
        const struct payload_mod *lg = payload_find(PAYLOAD_LGFONT);
        if (!lg || !lg->data || lg->size < 8) {
            serial_puts("[INST] LGFONT payload missing\n");
            rc = -1;
        } else {
            const unsigned char *lb = (const unsigned char *)lg->data;
            if (lb[0] != 'L' || lb[1] != 'G' || lb[2] != 'F' ||
                lb[3] != '1') {
                serial_puts("[INST] LGFONT bad magic\n");
                rc = -1;
            } else if (fat_write_file("BOOT/LGFONT.BIN", lg->data,
                                      lg->size) != 0) {
                serial_puts("[INST] write LGFONT FAIL\n");
                rc = -1;
            } else {
                uint32_t cnt = 0;
                cnt = *(const uint32_t *)(lb + 4);
                fat_mkdir("BOOT/FONTS");
                int wrote = 0;
                for (uint32_t i = 0; i < cnt && i < 512; i++) {
                    const unsigned char *e = lb + 8 + (size_t)i * 20;
                    uint32_t off = *(const uint32_t *)(e + 12);
                    uint32_t sz = *(const uint32_t *)(e + 16);
                    if (off + sz > lg->size || sz < 512)
                        continue;
                    char path[24];
                    int q = 0;
                    const char *pre = "BOOT/FONTS/";
                    while (pre[q]) { path[q] = pre[q]; q++; }
                    for (int k = 0; k < 12 && e[k]; k++)
                        path[q++] = (char)e[k];
                    path[q++] = '.';
                    path[q++] = 'T';
                    path[q++] = 'T';
                    path[q++] = 'F';
                    path[q] = 0;
                    if (fat_write_file(path, (const void *)(lb + off),
                                       sz) != 0) {
                        serial_puts("[INST] write lang TTF FAIL\n");
                        rc = -1;
                        break;
                    }
                    wrote++;
                }
                serial_puts("[INST] fonts installed=");
                serial_puthex((uint64_t)wrote);
                serial_puts(" of ");
                serial_puthex((uint64_t)cnt);
                serial_putc('\n');
            }
        }
    }

    const struct { const char *path; const struct payload_mod *m; } sounds[] = {
        {"BOOT.PCM", bootpcm}, {"WARN.PCM", warnpcm}
    };
    for (unsigned i = 0; i < sizeof(sounds) / sizeof(sounds[0]); i++) {
        if (sounds[i].m && sounds[i].m->data &&
            fat_write_file(sounds[i].path, sounds[i].m->data,
                           sounds[i].m->size) != 0) {
            serial_puts("[INST] write PCM FAIL\n");
            rc = -1;
        }
    }

    const char *mark = "JackBOT OS 1.0.9\n";
    if (fat_write_file(INSTALL_MARKER, mark, slen(mark)) != 0)
        rc = -1;

    /* Persist the language chosen in the installer as the first-boot
       system language (JBOT.CFG, same format as config.c). */
    {
        char cfg[48];
        int p = 0;
        const char *lp = "L=";
        while (*lp) cfg[p++] = *lp++;
        int lv = g_lang_idx;
        char num[8];
        int nn = 0;
        if (lv == 0)
            num[nn++] = '0';
        while (lv > 0) { num[nn++] = (char)('0' + lv % 10); lv /= 10; }
        while (nn) cfg[p++] = num[--nn];
        /* Q=<code>: store the language by code so a later change to the
           shipped language table cannot silently switch the system back */
        {
            const char *code = i18n_code(g_lang_idx);
            if (code && code[0]) {
                cfg[p++] = '\n';
                cfg[p++] = 'Q';
                cfg[p++] = '=';
                for (int i = 0; code[i] && p < 40; i++)
                    cfg[p++] = code[i];
            }
        }
        const char *pre = "\nF=0\nT=";
        while (*pre) cfg[p++] = *pre++;
        int tv = g_tz_idx;
        char tnum[8];
        int tn = 0;
        if (tv == 0)
            tnum[tn++] = '0';
        while (tv > 0) { tnum[tn++] = (char)('0' + tv % 10); tv /= 10; }
        while (tn) cfg[p++] = tnum[--tn];
        const char *tail = "\nW=1\n";
        while (*tail) cfg[p++] = *tail++;
        if (fat_write_file("JBOT.CFG", cfg, (uint32_t)p) != 0)
            rc = -1;
        serial_puts("[INST] wrote JBOT.CFG lang=");
        serial_puthex((uint32_t)g_lang_idx);
        serial_putc('\n');
    }

    protect_set_actor(old);
    ata_flush_cache();

    if (rc != 0)
        return -1;
    report("Installation complete", 100);
    serial_puts("[INST] install finished successfully\n");
    return 0;
}

int postinstall_cleanup(void)
{
    if (!fat_mounted())
        return -1;
    const char *legacy[2] = {"INSTALL.STG", "FIRSTBOOT"};
    fat_delete_many(legacy, 2);
    return 0;
}
