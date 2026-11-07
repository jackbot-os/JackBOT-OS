#include <stdint.h>
#include <stddef.h>
#include <kernel/acpi.h>
#include <kernel/vmm.h>
#include <kernel/hal.h>
#include <kernel/serial.h>

/*
 * 最小 ACPI 解析：RSDP -> RSDT/XSDT -> FADT -> DSDT。
 * 所有物理内存都经 vmm_map_page 临时映射到一个 scratch 窗口后读取。
 */

#define ACPI_SCRATCH_VBASE 0xffff800084000000ull
#define ACPI_SCRATCH_SIZE  0x100000u /* 1 MiB */

static struct acpi_info g;

static uint8_t rd8(const void *p) { return *(volatile const uint8_t *)p; }
static uint16_t rd16(const void *p)
{
    return (uint16_t)(rd8((const uint8_t *)p) |
                      (rd8((const uint8_t *)p + 1) << 8));
}
static uint32_t rd32(const void *p)
{
    return (uint32_t)rd16(p) | ((uint32_t)rd16((const uint8_t *)p + 2) << 16);
}
static uint64_t rd64(const void *p)
{
    return (uint64_t)rd32(p) | ((uint64_t)rd32((const uint8_t *)p + 4) << 32);
}

static int eq_sig(const uint8_t *p, const char *s)
{
    for (int i = 0; i < 4; i++)
        if (p[i] != (uint8_t)s[i])
            return 0;
    return 1;
}

/* 把任意物理区间映射到 scratch 窗口，返回可直接访问的虚拟指针。 */
static void *scratch_map(uint64_t phys, uint32_t size)
{
    uint64_t pstart = phys & ~0xFFFull;
    uint64_t off = phys & 0xFFFull;
    uint32_t bytes = (uint32_t)((off + size + 4095u) & ~4095u);
    if (bytes > ACPI_SCRATCH_SIZE)
        bytes = ACPI_SCRATCH_SIZE;
    for (uint32_t o = 0; o < bytes; o += 4096)
        vmm_map_page(ACPI_SCRATCH_VBASE + o, pstart + o,
                     PTE_PRESENT | PTE_PCD | PTE_PWT);
    return (void *)(ACPI_SCRATCH_VBASE + off);
}

static int checksum_ok(const uint8_t *p, uint32_t len)
{
    uint8_t s = 0;
    for (uint32_t i = 0; i < len; i++)
        s += p[i];
    return s == 0;
}

static int rsdp_valid(const uint8_t *p)
{
    static const char sig[8] = {'R', 'S', 'D', ' ', 'P', 'T', 'R', ' '};
    for (int i = 0; i < 8; i++)
        if (p[i] != (uint8_t)sig[i])
            return 0;
    if (!checksum_ok(p, 20))
        return 0;
    if (p[15] >= 2) {
        uint32_t len = rd32(p + 20);
        if (len < 36 || len > 0x1000)
            return 0;
        if (!checksum_ok(p, len))
            return 0;
    }
    return 1;
}

/* 在一段物理内存中以 16 字节步进寻找 RSDP，返回物理地址或 0。 */
static uint64_t scan_rsdp(uint64_t phys, uint32_t len)
{
    uint8_t *m = scratch_map(phys, len);
    for (uint32_t off = 0; off + 20 <= len; off += 16) {
        if (rsdp_valid(m + off))
            return phys + off;
    }
    return 0;
}

static int blob_find(const uint8_t *base, uint32_t len, const char *needle)
{
    int nl = 0;
    while (needle[nl])
        nl++;
    if (nl == 0 || (uint32_t)nl > len)
        return 0;
    for (uint32_t i = 0; i + nl <= len; i++) {
        int k = 0;
        while (k < nl && base[i + k] == (uint8_t)needle[k])
            k++;
        if (k == nl)
            return 1;
    }
    return 0;
}

/* 从 DSDT AML 里粗解析 \_S5 包，取 SLP_TYP_A / SLP_TYP_B。 */
static void parse_s5(const uint8_t *dsdt, uint32_t len)
{
    static const uint8_t name[4] = {0x5C, '_', 'S', '5'}; /* \_S5 */
    for (uint32_t i = 0; i + 8 < len; i++) {
        if (dsdt[i] != name[0] || dsdt[i + 1] != name[1] ||
            dsdt[i + 2] != name[2] || dsdt[i + 3] != name[3])
            continue;
        /* 随后 40 字节内找 PackageOp 0x12 */
        uint32_t j = i + 4;
        uint32_t limit = i + 44;
        if (limit > len)
            limit = len;
        while (j < limit && dsdt[j] != 0x12)
            j++;
        if (j >= limit)
            continue;
        uint8_t lead = dsdt[j + 1];
        int extra = (lead >> 6) & 0x3;
        uint32_t p = j + 1 + 1 + extra; /* 跳过 PkgLength，到元素计数 */
        if (p + 2 >= len)
            continue;
        p++; /* 跳过 NumElements */
        /* 前两个元素：SLP_TYP_A、SLP_TYP_B */
        uint8_t vals[2] = {0xFF, 0xFF};
        for (int e = 0; e < 2 && p + 1 < len; e++) {
            uint8_t op = dsdt[p++];
            uint32_t v = 0;
            if (op == 0x00) {
                v = 0;
            } else if (op == 0x0A) {
                v = dsdt[p++];
            } else if (op == 0x0B) {
                v = dsdt[p] | (dsdt[p + 1] << 8); p += 2;
            } else if (op == 0x0C) {
                v = rd32(dsdt + p); p += 4;
            } else if (op == 0x0D) {
                v = (uint32_t)rd64(dsdt + p); p += 8;
            } else {
                break;
            }
            vals[e] = (uint8_t)(v & 0x7);
        }
        if (vals[0] != 0xFF) {
            g.slp_typa = vals[0];
            g.slp_typb = vals[1];
            return;
        }
    }
}

static void scan_dsdt(uint64_t dsdt_phys)
{
    if (!dsdt_phys)
        return;
    uint8_t *h = scratch_map(dsdt_phys, 36);
    if (!eq_sig(h, "DSDT") || !checksum_ok(h, rd32(h + 4) < ACPI_SCRATCH_SIZE
                                             ? rd32(h + 4)
                                             : ACPI_SCRATCH_SIZE)) {
        /* 校验失败也继续，部分虚拟机表校验宽松；只做字节扫描 */
    }
    uint32_t len = rd32(h + 4);
    if (len < 36 || len > ACPI_SCRATCH_SIZE)
        len = ACPI_SCRATCH_SIZE;
    uint8_t *d = scratch_map(dsdt_phys, len);
    g.has_dsdt = 1;
    if (blob_find(d, len, "PNP0C0A"))
        g.has_battery = 1;
    if (blob_find(d, len, "PNP0C09"))
        g.has_ec = 1;
    if (blob_find(d, len, "ACPI0003"))
        g.has_ac_adapter = 1;
    parse_s5(d, len);
}

static void parse_fadt(uint64_t fadt_phys)
{
    uint8_t *f = scratch_map(fadt_phys, 200);
    if (!eq_sig(f, "FACP"))
        return;
    g.has_fadt = 1;
    g.sci_int = rd16(f + 46);
    g.pm1a_evt = rd32(f + 56);
    g.pm1b_evt = rd32(f + 60);
    g.pm1a_cnt = rd32(f + 64);
    g.pm1b_cnt = rd32(f + 68);

    uint32_t fadt_len = rd32(f + 4);
    uint32_t dsdt32 = rd32(f + 40);
    uint64_t dsdt = (uint64_t)dsdt32;
    if (fadt_len >= 148) {
        uint64_t dsdt64 = rd64(f + 140);
        if (dsdt64)
            dsdt = dsdt64;
    }
    if (dsdt)
        scan_dsdt(dsdt);
}

void acpi_init(void)
{
    for (uint8_t *q = (uint8_t *)&g; q < (uint8_t *)(&g + 1);)
        *q++ = 0;
    g.slp_typa = 0xFF;
    g.slp_typb = 0xFF;

    uint64_t rsdp = 0;

    /* 1) EBDA：BDA 0x40:0x0E 给出 EBDA 段地址 */
    uint8_t *bda = scratch_map(0x0, 0x1000);
    uint16_t ebda_seg = (uint16_t)(bda[0x40E] | (bda[0x40F] << 8));
    if (ebda_seg)
        rsdp = scan_rsdp((uint64_t)ebda_seg << 4, 1024);

    /* 2) BIOS ROM 区 0xE0000..0xFFFFF */
    if (!rsdp)
        rsdp = scan_rsdp(0xE0000, 0x20000);

    if (!rsdp) {
        serial_puts("[ACPI] RSDP not found\n");
        return;
    }
    uint8_t *r = scratch_map(rsdp, 40);
    g.found = 1;
    g.revision = r[15];
    serial_puts("[ACPI] RSDP rev=");
    serial_putc('0' + (char)(g.revision & 0xF));
    serial_putc('\n');

    uint64_t entries[128];
    int nt = 0;
    uint32_t tlen = 0;

    if (g.revision >= 2) {
        uint64_t xsdt = rd64(r + 24);
        if (xsdt) {
            uint8_t *x = scratch_map(xsdt, 4096);
            if (eq_sig(x, "XSDT")) {
                tlen = rd32(x + 4);
                int n = (int)((tlen - 36) / 8);
                if (n > 128) n = 128;
                for (int i = 0; i < n; i++)
                    entries[nt++] = rd64(x + 36 + i * 8);
            }
        }
    } else {
        uint32_t rsdt = rd32(r + 16);
        if (rsdt) {
            uint8_t *x = scratch_map(rsdt, 4096);
            if (eq_sig(x, "RSDT")) {
                tlen = rd32(x + 4);
                int n = (int)((tlen - 36) / 4);
                if (n > 128) n = 128;
                for (int i = 0; i < n; i++)
                    entries[nt++] = rd32(x + 36 + i * 4);
            }
        }
    }

    g.table_count = nt;
    uint64_t fadt = 0;
    for (int i = 0; i < nt; i++) {
        uint8_t *h = scratch_map(entries[i], 36);
        if (eq_sig(h, "FACP"))
            fadt = entries[i];
    }
    if (fadt)
        parse_fadt(fadt);

    serial_puts("[ACPI] tables=");
    serial_puthex((uint32_t)nt);
    serial_puts(" fadt=");
    serial_putc(g.has_fadt ? '1' : '0');
    serial_puts(" battery=");
    serial_putc(g.has_battery ? '1' : '0');
    serial_putc('\n');
}

const struct acpi_info *acpi_get(void) { return &g; }

static void pm1_write(uint32_t port, uint8_t typ)
{
    if (!port)
        return;
    uint16_t val = (uint16_t)(((uint16_t)typ << 10) | (1u << 13));
    hal_outw((uint16_t)port, val);
}

int acpi_poweroff(void)
{
    if (!g.found || !g.pm1a_cnt)
        return -1;

    __asm__ __volatile__("cli");

    if (g.slp_typa != 0xFF) {
        pm1_write(g.pm1a_cnt, g.slp_typa);
        if (g.pm1b_cnt)
            pm1_write(g.pm1b_cnt, g.slp_typb != 0xFF ? g.slp_typb : g.slp_typa);
        for (int i = 0; i < 100; i++)
            __asm__ __volatile__("hlt");
    }

    /* 未解析出 SLP_TYP 时，枚举 0..7 直到机器断电。 */
    for (uint8_t t = 0; t < 8; t++) {
        pm1_write(g.pm1a_cnt, t);
        if (g.pm1b_cnt)
            pm1_write(g.pm1b_cnt, t);
        for (int i = 0; i < 3; i++)
            __asm__ __volatile__("hlt");
    }
    return -1;
}

int acpi_reset(void)
{
    /* ACPI 复位寄存器在 FADT RESET_REG/RESET_VALUE，这里交给上层用 8042/89 回退。 */
    return -1;
}
