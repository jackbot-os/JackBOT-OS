#include <stdint.h>
#include <kernel/gpu_nv.h>
#include <kernel/pci.h>
#include <kernel/vmm.h>
#include <kernel/serial.h>

/* JackBOT OS original work, written from public register descriptions
   (docs/gpu/). No GPL/Linux-derived code; facts only, no doc text copied. */

#define NV_VBASE      0xffff800084000000ull
#define NV_MAP_PAGES  258u   /* 0x102000 bytes: PMC (0x0..) up to PSTRAPS (0x101000..) */

#define PMC_ID        0x0000u
#define PMC_ENDIAN    0x0004u
#define PMC_BOOT_2    0x0008u
#define PMC_INTR_HOST 0x0100u
#define PMC_ENABLE    0x0200u
#define PMC_SPOON_EN  0x0204u
#define PMC_NEW_ID    0x0A00u
#define PMC_INTR_MASK 0x0640u

#define PSTRAPS_BASE       0x101000u
#define PS0_PRIMARY        0x00u
#define PS0_SELECT         0x04u
#define PS0_SECONDARY      0x08u
#define PS1_PRIMARY        0x0Cu
#define PS1_SELECT         0x10u
#define PS1_SECONDARY      0x14u
#define PS2_PRIMARY        0x34u
#define PS2_SELECT         0x38u
#define PS2_SECONDARY      0x3Cu

static const char *const nv_bar0_size_code[] = {
    "16MB", "32MB", "64MB", "128MB", "256MB", "512MB", "1GB", "2GB"
};

static const char *const nv_bar1_size_code[] = {
    "64MB", "128MB", "256MB", "512MB", "1GB", "2GB", "4GB", "8GB",
    "16GB", "32GB", "64GB", "?"
};

struct nv_chip {
    uint32_t id;
    const char *code;
    const char *fam;
};

static const struct nv_chip nv_chips[] = {
    { 0x040, "NV40",   "Curie" },
    { 0x041, "NV41",   "Curie" },
    { 0x042, "NV42",   "Curie" },
    { 0x043, "NV43",   "Curie" },
    { 0x044, "NV44",   "Curie" },
    { 0x045, "NV45",   "Curie" },
    { 0x046, "G72",    "Curie" },
    { 0x047, "G70",    "Curie" },
    { 0x049, "G71",    "Curie" },
    { 0x04A, "NV44A",  "Curie" },
    { 0x04B, "G73",    "Curie" },
    { 0x04C, "MCP61",  "Curie" },
    { 0x04D, "RSX",    "Curie" },
    { 0x04E, "C51",    "Curie" },
    { 0x050, "G80",    "Tesla" },
    { 0x063, "MCP73",  "Curie" },
    { 0x067, "MCP67",  "Curie" },
    { 0x068, "MCP68",  "Curie" },
    { 0x084, "G84",    "Tesla" },
    { 0x086, "G86",    "Tesla" },
    { 0x092, "G92",    "Tesla" },
    { 0x094, "G94",    "Tesla" },
    { 0x096, "G96",    "Tesla" },
    { 0x098, "G98",    "Tesla" },
    { 0x0A0, "G200",   "Tesla" },
    { 0x0A3, "GT215",  "Tesla" },
    { 0x0A5, "GT216",  "Tesla" },
    { 0x0A8, "GT218",  "Tesla" },
    { 0x0AA, "MCP77",  "Tesla" },
    { 0x0AC, "MCP79",  "Tesla" },
    { 0x0AF, "MCP89",  "Tesla" },
    { 0x0C0, "GF100",  "Fermi" },
    { 0x0C1, "GF108",  "Fermi" },
    { 0x0C3, "GF106",  "Fermi" },
    { 0x0C4, "GF104",  "Fermi" },
    { 0x0C8, "GF110",  "Fermi" },
    { 0x0CE, "GF114",  "Fermi" },
    { 0x0CF, "GF116",  "Fermi" },
    { 0x0D7, "GF117",  "Fermi" },
    { 0x0D9, "GF119",  "Fermi" },
    { 0x0E4, "GK104",  "Kepler" },
    { 0x0E6, "GK106",  "Kepler" },
    { 0x0E7, "GK107",  "Kepler" },
    { 0x0EA, "GK20A",  "Kepler" },
    { 0x0F0, "GK110",  "Kepler" },
    { 0x0F1, "GK110B", "Kepler" },
    { 0x106, "GK208B", "Kepler" },
    { 0x108, "GK208",  "Kepler" },
    { 0x117, "GM107",  "Maxwell" },
    { 0x118, "GM108",  "Maxwell" },
    { 0x120, "GM200",  "Maxwell" },
    { 0x124, "GM204",  "Maxwell" },
    { 0x126, "GM206",  "Maxwell" },
    { 0x12B, "GM20B",  "Maxwell" },
    { 0x130, "GP100",  "Pascal" },
    { 0x132, "GP102",  "Pascal" },
    { 0x134, "GP104",  "Pascal" },
    { 0x136, "GP106",  "Pascal" },
    { 0x137, "GP107",  "Pascal" },
    { 0x138, "GP108",  "Pascal" },
    { 0x13B, "GP10B",  "Pascal" },
    { 0x140, "GV100",  "Volta" },
    { 0x15B, "GV11B",  "Volta" },
    { 0x162, "TU102",  "Turing" },
    { 0x164, "TU104",  "Turing" },
    { 0x166, "TU106",  "Turing" },
    { 0x167, "TU117",  "Turing" },
    { 0x168, "TU116",  "Turing" },
};

struct nv_eng {
    uint8_t bit;
    const char *name;
};

static const struct nv_eng nv_engines_gf100[] = {
    { 1,  "PPPP" },
    { 2,  "PXBAR" },
    { 3,  "PMFB" },
    { 4,  "PMEDIA" },
    { 5,  "PRING" },
    { 6,  "PCOPY0" },
    { 7,  "PCOPY1" },
    { 8,  "PFIFO" },
    { 12, "PGRAPH" },
    { 13, "PDAEMON" },
    { 15, "PVLD" },
    { 16, "PTIMER" },
    { 17, "PPDEC" },
    { 18, "PVENC" },
    { 20, "PBFB" },
    { 21, "PCOPY2" },
    { 28, "PCOUNTER" },
    { 29, "PFFB" },
    { 30, "PDISPLAY" },
};

static const struct nv_eng nv_engines_g80[] = {
    { 1,  "PPPP" },
    { 4,  "PMEDIA" },
    { 8,  "PFIFO" },
    { 12, "PGRAPH" },
    { 13, "PCOPY" },
    { 14, "PCIPHER" },
    { 15, "PBSP" },
    { 16, "PTIMER" },
    { 17, "PVP2" },
    { 20, "PFB" },
    { 21, "PGRAPH_CHSW" },
    { 22, "PMPEG_CHSW" },
    { 23, "PCOPY_CHSW" },
    { 30, "PDISPLAY" },
};

static int g_present;
static uint32_t g_gpuid;
static const char *g_name = "";

static uint32_t nvrd(uint64_t vbase, uint32_t off);

/* PSTRAPS: board configuration sampled from the memory bus pins at reset.
   Read-only here; the override bit (31) is never touched. */
static void nv_pstraps(uint64_t vbase, uint32_t gpuid)
{
    uint32_t s0p, s0s, s0c, s1p, s1s, s1c;
    uint32_t eff0, eff1, v;

    if (gpuid < 0x050u) {
        serial_puts("[NV] PSTRAPS decode: pre-G80 set tables not in this build\n");
        return;
    }

    s0p = nvrd(vbase, PSTRAPS_BASE + PS0_PRIMARY);
    s0s = nvrd(vbase, PSTRAPS_BASE + PS0_SELECT);
    s0c = nvrd(vbase, PSTRAPS_BASE + PS0_SECONDARY);
    s1p = nvrd(vbase, PSTRAPS_BASE + PS1_PRIMARY);
    s1s = nvrd(vbase, PSTRAPS_BASE + PS1_SELECT);
    s1c = nvrd(vbase, PSTRAPS_BASE + PS1_SECONDARY);

    eff0 = (s0p & s0s) | (s0c & ~s0s);
    eff1 = (s1p & s1s) | (s1c & ~s1s);

    serial_puts("[NV] PSTRAPS set0=");
    gpu_puthex32(eff0);
    serial_puts(" set1=");
    gpu_puthex32(eff1);
    serial_putc('\n');

    serial_puts("[NV] straps devid=");
    gpu_puthex32((eff0 >> 10) & 0xFu);
    serial_puts(" crystal=");
    serial_puts(((eff0 >> 6) & 1u) ? "25MHz" : "27MHz");
    serial_puts(" standalone=");
    serial_puts(((eff0 >> 1) & 1u) ? "yes" : "no (motherboard, romless)");

    v = (eff0 >> 22) & 0x3u;
    serial_puts(" vbios=");
    serial_puts(v == 0 ? "parallel" : (v == 1 ? "spi" : "unknown"));
    serial_puts(" fp_cfg=");
    gpu_puthex32((eff0 >> 24) & 0xFu);
    serial_putc('\n');

    v = (eff1 >> 17) & 0x7u;
    serial_puts("[NV] straps bar0=");
    serial_puts(nv_bar0_size_code[v]);
    serial_puts(" bar1=");
    {
        uint32_t bar1code = ((eff1 >> 14) & 0x3u) + ((eff1 >> 20) & 0x7u);
        if (bar1code < 11u)
            serial_puts(nv_bar1_size_code[bar1code]);
        else
            serial_puts("?");
    }
    serial_puts(" bar5_en=");
    serial_puts(((eff1 >> 16) & 1u) ? "1" : "0");
    serial_puts(" class=");
    serial_puts(((eff1 >> 4) & 1u) ? "0x030000 vga" : "0x030200 3d");
    serial_putc('\n');
}

static uint32_t nvrd(uint64_t vbase, uint32_t off)
{
    return *(volatile uint32_t *)(vbase + off);
}

static const struct nv_chip *nv_lookup(uint32_t id)
{
    for (uint32_t i = 0; i < sizeof(nv_chips) / sizeof(nv_chips[0]); i++)
        if (nv_chips[i].id == id)
            return &nv_chips[i];
    return 0;
}

static void nv_print_engines(uint32_t en, uint32_t gpuid)
{
    const struct nv_eng *tab;
    uint32_t n, hits = 0;

    if (gpuid >= 0x0C0u) {
        tab = nv_engines_gf100;
        n = (uint32_t)(sizeof(nv_engines_gf100) / sizeof(nv_engines_gf100[0]));
    } else {
        tab = nv_engines_g80;
        n = (uint32_t)(sizeof(nv_engines_g80) / sizeof(nv_engines_g80[0]));
    }

    serial_puts("[NV] engines on:");
    for (uint32_t i = 0; i < n; i++) {
        if (en & (1u << tab[i].bit)) {
            serial_putc(' ');
            serial_puts(tab[i].name);
            hits++;
        }
    }
    if (!hits)
        serial_puts(" none");
    serial_putc('\n');
}

int gpu_nv_attach(struct gpu_dev *g)
{
    uint64_t vbase, bar;
    uint32_t id, nid, en, spo, devid, step, pages;
    int have_pstraps;
    const struct nv_chip *chip;

    bar = gpu_bar_addr(g->pci, 0);
    if (!bar) {
        serial_puts("[NV] BAR0 missing, cannot reach PMC\n");
        return 0;
    }

    g->bar0_phys = bar;
    g->bar0_size = gpu_bar_size(g->pci, 0);

    serial_puts("[NV] BAR0=");
    serial_puthex(bar);
    serial_puts(" size=");
    gpu_puthex32(g->bar0_size);
    serial_putc('\n');

    vbase = NV_VBASE;
    pages = NV_MAP_PAGES;
    if (g->bar0_size && g->bar0_size < NV_MAP_PAGES * 0x1000u)
        pages = g->bar0_size / 0x1000u;
    if (!gpu_map_mmio(vbase, bar, pages)) {
        serial_puts("[NV] MMIO map failed\n");
        return 0;
    }
    g->vbase = vbase;
    g->pages = pages;
    g->bar0_mapped = 1;
    have_pstraps = (pages * 0x1000u > PSTRAPS_BASE);

    pci_enable_bus_master(g->pci);

    id = nvrd(vbase, PMC_ID);
    nid = nvrd(vbase, PMC_NEW_ID);
    en = nvrd(vbase, PMC_ENABLE);
    spo = nvrd(vbase, PMC_SPOON_EN);

    if (nid) {
        devid = nid & 0xFFu;
        step = (nid >> 12) & 0xFFu;
        g_gpuid = (nid >> 20) & 0xFFu;
    } else {
        step = id & 0xFFu;
        devid = (id >> 12) & 0xFFu;
        g_gpuid = (id >> 20) & 0xFFu;
    }

    serial_puts("[NV] PMC ID=");
    gpu_puthex32(id);
    serial_puts(" NEW_ID=");
    gpu_puthex32(nid);
    serial_puts(" gpuid=");
    gpu_puthex32(g_gpuid);
    serial_puts(" step=");
    gpu_puthex32(step);
    serial_puts(" devid=");
    gpu_puthex32(devid);
    serial_putc('\n');

    serial_puts("[NV] PMC BOOT_2=");
    gpu_puthex32(nvrd(vbase, PMC_BOOT_2));
    serial_puts(" ENDIAN=");
    gpu_puthex32(nvrd(vbase, PMC_ENDIAN));
    serial_puts(" INTR_HOST=");
    gpu_puthex32(nvrd(vbase, PMC_INTR_HOST));
    serial_puts(" INTR_MASK=");
    gpu_puthex32(nvrd(vbase, PMC_INTR_MASK));
    serial_putc('\n');

    serial_puts("[NV] PMC.ENABLE=");
    gpu_puthex32(en);
    serial_puts(" SPOON_ENABLE=");
    gpu_puthex32(spo);
    serial_putc('\n');

    nv_print_engines(en, g_gpuid);

    if (have_pstraps)
        nv_pstraps(vbase, g_gpuid);
    else
        serial_puts("[NV] PSTRAPS area outside the mapped aperture, skipped\n");

    chip = nv_lookup(g_gpuid);
    if (chip) {
        g->chip_name = chip->code;
        serial_puts("[NV] chip=");
        serial_puts(chip->code);
        serial_puts(" family=");
        serial_puts(chip->fam);
        serial_putc('\n');
    } else {
        serial_puts("[NV] chip id not in table, raw gpuid kept\n");
    }

    if (g->bar0_size && g->bar0_size != 0x1000000u)
        serial_puts("[NV] note: BAR0 size differs from the documented 16MB "
                    "aperture, trusting the measured value\n");

    serial_puts("[NV] stage1 done (read-only), engine init values not guessed\n");

    g_present = 1;
    g_name = chip ? chip->code : "";
    return 1;
}

int gpu_nv_present(void)
{
    return g_present;
}

uint32_t gpu_nv_gpuid(void)
{
    return g_gpuid;
}

const char *gpu_nv_name(void)
{
    return g_name;
}
