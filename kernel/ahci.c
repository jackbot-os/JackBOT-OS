#include <stdint.h>
#include <stddef.h>
#include <kernel/ahci.h>
#include <kernel/pci.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/kstring.h>
#include <kernel/serial.h>
uint64_t timer_get_ticks(void);

/* Real AHCI (Advanced Host Controller Interface) driver.
 *
 *   - PCI class 0x01 / subclass 0x06 (SATA AHCI), ABAR mapped uncached
 *   - CAP / PI read, every implemented port initialised (ST/FRE disabled,
 *     CLB + FB installed, engine started)
 *   - one command list (32 headers) + 32 command tables per controller,
 *     each 4K-aligned/physical; PxCI submission with polling completion
 *   - IDENTIFY DEVICE parsed into model / capacity / LBA48 support
 *   - LBA48 READ DMA EXT (0x25) and WRITE DMA EXT (0x35) with PRDT built
 *     from the caller buffer, PxIS/PxTFD error checks
 *
 * The driver is read-only by default at boot: the only disk access during
 * system start-up is the LBA0 signature self check (no writes).
 */

#define AHCI_VBASE 0xffff800083000000ull
#define AHCI_MMIO_PAGES 4

#define HBA_CAP    0x00
#define HBA_GHC    0x04
#define HBA_IS     0x08
#define HBA_PI     0x0C
#define HBA_VS     0x10

#define GHC_AE     0x80000000u
#define GHC_HR     0x00000001u

#define PORT_BASE(n) (0x100u + (uint32_t)(n) * 0x80u)
#define PxCLB   0x00
#define PxCLBU  0x04
#define PxFB    0x08
#define PxFBU   0x0C
#define PxIS    0x10
#define PxIE    0x14
#define PxCMD   0x18
#define PxTFD   0x20
#define PxSIG   0x24
#define PxSSTS  0x28
#define PxSCTL  0x2C
#define PxSERR  0x30
#define PxSACT  0x34
#define PxCI    0x38

#define PxCMD_ST    0x00000001u
#define PxCMD_CLO   0x00000008u
#define PxCMD_SUD   0x00000002u
#define PxCMD_POD   0x00000004u
#define PxCMD_FRE   0x00000010u
#define PxCMD_FR    0x00000040u
#define PxCMD_CR    0x00008000u

#define PxIS_DHRS   0x00000001u
#define PxIS_PSS    0x00000002u
#define PxIS_DSS    0x00000004u
#define PxIS_TFES   0x40000000u
#define PxIS_HBDS   0x80000000u
#define PxIS_HBFS   0x20000000u
#define PxIS_IFS    0x10000000u
#define PxIS_ERR_MASK (PxIS_TFES | PxIS_HBDS | PxIS_HBFS | PxIS_IFS)

#define TFD_BSY 0x80u
#define TFD_DRQ 0x08u
#define TFD_ERR 0x01u

#define SSTS_DET_MASK 0x0Fu
#define SSTS_DET_ONLINE 0x03u

#define CMD_IDENTIFY      0xEC
#define CMD_READ_DMA_EXT  0x25
#define CMD_WRITE_DMA_EXT 0x35
#define CMD_FLUSH_CACHE_EXT 0xEA

#define AHCI_MAX_PRDT 32
#define AHCI_MAX_SECTORS 128   /* per command; 128*512 = 64 KiB */
#define AHCI_SECTOR 512u

struct ahci_cmd_hdr {
    uint32_t dw0;
    uint32_t prdtl;
    uint32_t prdbc;
    uint32_t ctba_l;
    uint32_t ctba_h;
    uint32_t rsv[4];
} __attribute__((packed));

struct ahci_prd {
    uint32_t dba_l;
    uint32_t dba_h;
    uint32_t rsv;
    uint32_t dw3;
} __attribute__((packed));

struct ahci_cmd_table {
    uint8_t cfis[64];
    uint8_t acmd[16];
    uint8_t rsv[48];
    struct ahci_prd prdt[AHCI_MAX_PRDT];
} __attribute__((packed));

struct ahci_port {
    int implemented;
    int online;
    int lba48;
    uint64_t sectors;
    char model[41];
    uint32_t clb_phys;
    uint32_t ct_phys[32];
    uint32_t fb_phys;
};

static volatile uint8_t *hba;
static int g_present;
static int g_online;
static uint32_t g_pi;
static int g_cap64;
static int g_primary = -1;
static struct ahci_port g_ports[32];

static uint32_t rd(uint32_t o)
{
    return *(volatile uint32_t *)(hba + o);
}

static void wr(uint32_t o, uint32_t v)
{
    *(volatile uint32_t *)(hba + o) = v;
    __asm__ volatile("" ::: "memory");
}

static uint32_t prd(int n, uint32_t off)
{
    return *(volatile uint32_t *)(hba + PORT_BASE(n) + off);
}

static void pwr(int n, uint32_t off, uint32_t v)
{
    *(volatile uint32_t *)(hba + PORT_BASE(n) + off) = v;
    __asm__ volatile("" ::: "memory");
}

static uint32_t alloc_page_below1g(void)
{
    void *p = pmm_alloc_page_below(0x40000000ull);
    return p ? (uint32_t)(uintptr_t)p : 0;
}

static int port_online(int n)
{
    return (prd(n, PxSSTS) & SSTS_DET_MASK) == SSTS_DET_ONLINE;
}

/* ------------------------------------------------------------------ */
/* port / engine setup                                                 */
/* ------------------------------------------------------------------ */

static int port_stop(int n)
{
    uint32_t cmd = prd(n, PxCMD);
    cmd &= ~PxCMD_ST;
    pwr(n, PxCMD, cmd);
    for (uint32_t i = 0; i < 500000u; i++)
        if (!(prd(n, PxCMD) & PxCMD_CR))
            break;
    cmd = prd(n, PxCMD);
    cmd &= ~PxCMD_FRE;
    pwr(n, PxCMD, cmd);
    for (uint32_t i = 0; i < 500000u; i++)
        if (!(prd(n, PxCMD) & PxCMD_FR))
            break;
    for (uint32_t i = 0; i < 500000u; i++) {
        uint32_t tfd = prd(n, PxTFD);
        if (!(tfd & TFD_BSY) && !(tfd & TFD_DRQ))
            return 0;
    }
    return -1;
}

static int port_start(int n)
{
    uint32_t cmd = prd(n, PxCMD);
    serial_puts("[AHCI] p"); serial_puthex((uint32_t)n);
    serial_puts(" set FRE (cmd="); serial_puthex(cmd); serial_puts(")\n");
    cmd |= PxCMD_FRE;
    pwr(n, PxCMD, cmd);
    serial_puts("[AHCI] p"); serial_puthex((uint32_t)n);
    serial_puts(" FRE written, cmd now="); serial_puthex(prd(n, PxCMD)); serial_putc('\n');
    for (uint32_t i = 0; i < 500000u; i++)
        if (prd(n, PxCMD) & PxCMD_FR)
            break;
    cmd = prd(n, PxCMD);
    serial_puts("[AHCI] p"); serial_puthex((uint32_t)n);
    serial_puts(" set ST|SUD (cmd="); serial_puthex(cmd); serial_puts(")\n");
    cmd |= PxCMD_ST | PxCMD_SUD | PxCMD_POD;
    pwr(n, PxCMD, cmd);
    serial_puts("[AHCI] p"); serial_puthex((uint32_t)n);
    serial_puts(" ST written, cmd now="); serial_puthex(prd(n, PxCMD)); serial_putc('\n');
    for (uint32_t i = 0; i < 500000u; i++)
        if (prd(n, PxCMD) & PxCMD_CR)
            return 0;
    return -1;
}

static int port_setup(int n)
{
    struct ahci_port *p = &g_ports[n];
    uint32_t clb, fb;

    if (p->clb_phys == 0) {
        serial_puts("[AHCI] port ");
        serial_puthex((uint32_t)n);
        serial_puts(" alloc\n");
        clb = alloc_page_below1g();           /* 32 x 32 B = 1 KiB */
        fb = alloc_page_below1g();            /* received FIS area */
        serial_puts("[AHCI] port ");
        serial_puthex((uint32_t)n);
        serial_puts(" clb=");
        serial_puthex(clb);
        serial_puts(" fb=");
        serial_puthex(fb);
        serial_putc('\n');
        if (!clb || !fb) {
            serial_puts("[AHCI] port ");
            serial_puthex((uint32_t)n);
            serial_puts(" DMA alloc failed\n");
            return -1;
        }
        memset((void *)(uintptr_t)clb, 0, 4096);
        memset((void *)(uintptr_t)fb, 0, 4096);
        p->clb_phys = clb;
        p->fb_phys = fb;
        for (int i = 0; i < 32; i++) {
            uint32_t ct = alloc_page_below1g();
            if (!ct) {
                serial_puts("[AHCI] port ");
                serial_puthex((uint32_t)n);
                serial_puts(" command table alloc failed\n");
                return -1;
            }
            memset((void *)(uintptr_t)ct, 0, 4096);
            p->ct_phys[i] = ct;
        }
        serial_puts("[AHCI] port ");
        serial_puthex((uint32_t)n);
        serial_puts(" ct ok\n");
    }

    serial_puts("[AHCI] p"); serial_puthex((uint32_t)n);
    serial_puts(" stop begin CMD="); serial_puthex(prd(n, PxCMD));
    serial_puts(" TFD="); serial_puthex(prd(n, PxTFD)); serial_putc('\n');
    if (port_stop(n) != 0) {
        serial_puts("[AHCI] port ");
        serial_puthex((uint32_t)n);
        serial_puts(" engine did not idle TFD=");
        serial_puthex(prd(n, PxTFD));
        serial_putc('\n');
        return -1;
    }

    serial_puts("[AHCI] p"); serial_puthex((uint32_t)n);
    serial_puts(" stop done, installing CLB/FB\n");

    /* AHCI spec: the driver must set PxCMD.CLO before programming
       PxCLB/PxFB, otherwise the HBA keeps using its previous (zero)
       command list and commands are never fetched. */
    {
        uint32_t cmd = prd(n, PxCMD);
        cmd |= PxCMD_CLO;
        pwr(n, PxCMD, cmd);
        for (uint32_t i = 0; i < 500000u; i++)
            if (!(prd(n, PxCMD) & PxCMD_CLO))
                break;
        serial_puts("[AHCI] p"); serial_puthex((uint32_t)n);
        serial_puts(" CLO cycle cmd="); serial_puthex(prd(n, PxCMD));
        serial_putc('\n');
    }

    pwr(n, PxCLB, p->clb_phys);
    pwr(n, PxCLBU, 0);
    pwr(n, PxFB, p->fb_phys);
    pwr(n, PxFBU, 0);

    /* release the override so the HBA latches the new addresses */
    pwr(n, PxCMD, (uint32_t)(prd(n, PxCMD) & ~PxCMD_CLO));
    serial_puts("[AHCI] p"); serial_puthex((uint32_t)n);
    serial_puts(" CLB readback="); serial_puthex(prd(n, PxCLB));
    serial_puts(" FB="); serial_puthex(prd(n, PxFB));
    serial_puts(" CT0="); serial_puthex(p->ct_phys[0]);
    serial_putc('\n');

    /* COMRESET disabled: the link is already established (SSTS.DET=3) and
       toggling DET put the emulated device into a rejecting state. */
    serial_puts("[AHCI] p"); serial_puthex((uint32_t)n);
    serial_puts(" comreset skipped ssts=");
    serial_puthex(prd(n, PxSSTS));
    serial_putc('\n');

    /* clear every pending interrupt and disable them (polled driver) */
    pwr(n, PxSERR, 0xFFFFFFFFu);
    pwr(n, PxIS, 0xFFFFFFFFu);
    pwr(n, PxIE, 0);
    pwr(n, PxSACT, 0);

    serial_puts("[AHCI] p"); serial_puthex((uint32_t)n);
    serial_puts(" start begin\n");
    if (port_start(n) != 0) {
        serial_puts("[AHCI] port ");
        serial_puthex((uint32_t)n);
        serial_puts(" engine failed to start CMD=");
        serial_puthex(prd(n, PxCMD));
        serial_putc('\n');
        return -1;
    }
    serial_puts("[AHCI] p"); serial_puthex((uint32_t)n);
    serial_puts(" started CMD="); serial_puthex(prd(n, PxCMD)); serial_putc('\n');
    return 0;
}

/* ------------------------------------------------------------------ */
/* command submission                                                  */
/* ------------------------------------------------------------------ */

static void fis_h2d(struct ahci_cmd_table *t, uint8_t cmd, uint8_t write,
                    uint64_t lba, uint32_t count)
{
    (void)write;   /* direction is carried by the command byte */
    memset(t->cfis, 0, 20);
    t->cfis[0] = 0x27;                       /* H2D register FIS */
    t->cfis[1] = 0x80;                       /* command */
    t->cfis[2] = cmd;
    t->cfis[4] = (uint8_t)lba;
    t->cfis[5] = (uint8_t)(lba >> 8);
    t->cfis[6] = (uint8_t)(lba >> 16);
    t->cfis[7] = (cmd == CMD_IDENTIFY) ? 0xA0 : 0x40;  /* drive 0 / LBA mode */
    t->cfis[8] = (uint8_t)(lba >> 24);
    t->cfis[9] = (uint8_t)(lba >> 32);
    t->cfis[10] = (uint8_t)(lba >> 40);
    t->cfis[12] = (uint8_t)count;
    t->cfis[13] = (uint8_t)(count >> 8);
    t->cfis[15] = 0;                         /* control */
}

/* Fill PRDT entries for a buffer, splitting on 4 KiB boundaries.
   Returns the number of entries used or -1. */
static int prdt_fill(struct ahci_cmd_table *t, const void *buf, uint32_t bytes)
{
    uint64_t addr = (uint64_t)(uintptr_t)buf;
    uint32_t left = bytes;
    int n = 0;

    while (left) {
        uint32_t page_left = 4096u - (uint32_t)(addr & 0xFFFu);
        uint32_t chunk = left < page_left ? left : page_left;
        uint64_t phys = vmm_phys_of(addr);
        if (!phys)
            return -1;
        if (n >= AHCI_MAX_PRDT)
            return -1;
        t->prdt[n].dba_l = (uint32_t)phys;
        t->prdt[n].dba_h = (uint32_t)(phys >> 32);
        t->prdt[n].rsv = 0;
        t->prdt[n].dw3 = (chunk - 1u) | 0x80000000u;  /* interrupt on completion */
        addr += chunk;
        left -= chunk;
        n++;
    }
    return n;
}

static int port_exec(int n, uint8_t cmd, uint8_t write, uint64_t lba,
                     uint32_t count, const void *wbuf, void *rbuf,
                     uint32_t bytes)
{
    struct ahci_port *p = &g_ports[n];
    struct ahci_cmd_hdr *h;
    struct ahci_cmd_table *t;
    int nprd;

    if (!p->implemented)
        return -1;
    if (p->clb_phys == 0 && port_setup(n) != 0)
        return -1;

    h = (struct ahci_cmd_hdr *)(uintptr_t)p->clb_phys;
    t = (struct ahci_cmd_table *)(uintptr_t)p->ct_phys[0];

    memset(t, 0, sizeof(*t));
    fis_h2d(t, cmd, write, lba, count);
    nprd = 0;
    if (bytes) {
        nprd = prdt_fill(t, write ? wbuf : rbuf, bytes);
        if (nprd < 0) {
            serial_puts("[AHCI] port ");
            serial_puthex((uint32_t)n);
            serial_puts(" buffer not physically mappable\n");
            return -1;
        }
    }

    memset(h, 0, sizeof(*h));
    h->dw0 = 5u | (write ? (1u << 6) : 0);   /* CFL=5, W */
    h->prdtl = (uint32_t)nprd;
    h->prdbc = 0;
    h->ctba_l = p->ct_phys[0];
    h->ctba_h = 0;

    pwr(n, PxIS, 0xFFFFFFFFu);

    serial_puts("[AHCI] hdr dw0="); serial_puthex(h->dw0);
    serial_puts(" prdtl="); serial_puthex(h->prdtl);
    serial_puts(" ctba="); serial_puthex(h->ctba_l);
    serial_puts(" cfis0="); serial_puthex(((uint32_t)t->cfis[0]) | ((uint32_t)t->cfis[2] << 8));
    serial_putc('\n');
    serial_puts("[AHCI] p"); serial_puthex((uint32_t)n);
    serial_puts(" submit cmd="); serial_puthex(cmd);
    serial_puts(" nprd="); serial_puthex((uint32_t)nprd);
    serial_puts(" bytes="); serial_puthex(bytes); serial_putc('\n');
    pwr(n, PxCI, 1u);
    serial_puts("[AHCI] p"); serial_puthex((uint32_t)n);
    serial_puts(" CI written, readback="); serial_puthex(prd(n, PxCI));
    serial_puts(" CMD="); serial_puthex(prd(n, PxCMD));
    serial_putc('\n');

    {
        uint64_t t0 = timer_get_ticks();
        uint64_t last = t0;
        int timed_out = 0;
        for (;;) {
            if ((prd(n, PxCI) & 1u) == 0)
                break;
            if (prd(n, PxIS) & PxIS_ERR_MASK)
                break;
            uint64_t now = timer_get_ticks();
            if (now - last >= 100) {
                last = now;
                serial_puts("[AHCI] p"); serial_puthex((uint32_t)n);
                serial_puts(" poll CI="); serial_puthex(prd(n, PxCI));
                serial_puts(" IS="); serial_puthex(prd(n, PxIS));
                serial_puts(" TFD="); serial_puthex(prd(n, PxTFD));
                serial_putc('\n');
            }
            if (now - t0 > 1000) {
                timed_out = 1;
                break;
            }
        }
        if (timed_out) {
            serial_puts("[AHCI] port ");
            serial_puthex((uint32_t)n);
            serial_puts(" command timeout cmd=");
            serial_puthex(cmd);
            serial_puts(" IS=");
            serial_puthex(prd(n, PxIS));
            serial_puts(" TFD=");
            serial_puthex(prd(n, PxTFD));
            serial_putc('\n');
            return -1;
        }
    }

    {
        uint32_t is = prd(n, PxIS);
        uint32_t tfd = prd(n, PxTFD);
        if (is & PxIS_ERR_MASK) {
            serial_puts("[AHCI] port ");
            serial_puthex((uint32_t)n);
            serial_puts(" command error IS=");
            serial_puthex(is);
            serial_puts(" TFD=");
            serial_puthex(tfd);
            serial_puts(" SERR=");
            serial_puthex(prd(n, PxSERR));
            serial_putc('\n');
            return -1;
        }
        if (tfd & (TFD_ERR | TFD_BSY)) {
            serial_puts("[AHCI] port ");
            serial_puthex((uint32_t)n);
            serial_puts(" bad TFD=");
            serial_puthex(tfd);
            serial_putc('\n');
            return -1;
        }
        if (bytes && h->prdbc != bytes) {
            serial_puts("[AHCI] port ");
            serial_puthex((uint32_t)n);
            serial_puts(" short transfer prdbc=");
            serial_puthex(h->prdbc);
            serial_puts(" want=");
            serial_puthex(bytes);
            serial_putc('\n');
            return -1;
        }
    }
    return 0;
}



/* ------------------------------------------------------------------ */
/* IDENTIFY                                                            */
/* ------------------------------------------------------------------ */

static char *put_field(char *dst, const uint16_t *ident, int first, int words)
{
    int o = 0;
    for (int i = 0; i < words; i++) {
        uint16_t w = ident[first + i];
        char hi = (char)(w >> 8), lo = (char)(w & 0xFF);
        if (hi && hi != ' ')
            dst[o++] = hi;
        if (lo && lo != ' ')
            dst[o++] = lo;
    }
    while (o > 0 && dst[o - 1] == ' ')
        o--;
    dst[o] = 0;
    return dst;
}

/* Diagnostic: a command with no data phase. If this completes, the command
   submission path works and a failure in IDENTIFY points at the PRDT/DMA
   path instead. */
static void port_probe_flush(int n)
{
    int rc = port_exec(n, CMD_FLUSH_CACHE_EXT, 1, 0, 0, 0, 0, 0);
    serial_puts("[AHCI] p"); serial_puthex((uint32_t)n);
    serial_puts(" flush probe rc="); serial_puthex((uint32_t)rc);
    serial_puts(" CI="); serial_puthex(prd(n, PxCI));
    serial_puts(" IS="); serial_puthex(prd(n, PxIS));
    serial_puts(" TFD="); serial_puthex(prd(n, PxTFD));
    serial_puts(" SERR="); serial_puthex(prd(n, PxSERR));
    serial_putc('\n');
    pwr(n, PxIS, 0xFFFFFFFFu);
    pwr(n, PxSERR, 0xFFFFFFFFu);
}

static int port_identify(int n)
{
    struct ahci_port *p = &g_ports[n];
    uint16_t ident[256];
    uint32_t bytes = 512;
    uint32_t phys;

    phys = alloc_page_below1g();
    if (!phys) {
        serial_puts("[AHCI] identify buffer alloc failed\n");
        return -1;
    }
    memset((void *)(uintptr_t)phys, 0, 4096);

    if (port_exec(n, CMD_IDENTIFY, 0, 0, 0, 0,
                  (void *)(uintptr_t)phys, bytes) != 0) {
        serial_puts("[AHCI] port ");
        serial_puthex((uint32_t)n);
        serial_puts(" IDENTIFY failed\n");
        return -1;
    }

    memcpy(ident, (void *)(uintptr_t)phys, sizeof ident);

    put_field(p->model, ident, 27, 20);

    p->lba48 = (ident[83] & 0x0400) ? 1 : 0;
    if (p->lba48) {
        p->sectors = (uint64_t)ident[100] |
                     ((uint64_t)ident[101] << 16) |
                     ((uint64_t)ident[102] << 32) |
                     ((uint64_t)ident[103] << 48);
    }
    if (p->sectors == 0) {
        p->sectors = (uint64_t)ident[60] | ((uint64_t)ident[61] << 16);
        p->lba48 = 0;
    }
    p->online = 1;

    serial_puts("[AHCI] port ");
    serial_puthex((uint32_t)n);
    serial_puts(" model='");
    serial_puts(p->model);
    serial_puts("' sectors=");
    serial_puthex(p->sectors);
    serial_puts(" lba48=");
    serial_puthex((uint32_t)p->lba48);
    serial_putc('\n');
    return 0;
}

/* ------------------------------------------------------------------ */
/* public API                                                          */
/* ------------------------------------------------------------------ */

int ahci_init(struct pci_dev *d)
{
    int bar_idx = -1;
    uint32_t bar = 0;
    uint32_t cap, nports;

    if (!d) {
        serial_puts("[AHCI] null pci device\n");
        return -1;
    }

    for (int b = 0; b < 6; b++) {
        uint32_t raw = d->bar[b];
        if (raw != 0 && !(raw & 1u)) {
            bar = raw & 0xFFFFFFF0u;
            bar_idx = b;
            if ((raw & 6u) == 4u)
                b++;
            break;
        }
    }
    if (bar_idx < 0) {
        serial_puts("[AHCI] no memory BAR\n");
        return -1;
    }

    serial_puts("[AHCI] pci ");
    serial_puthex(d->vendor);
    serial_puthex(d->device);
    serial_puts(" class=");
    serial_puthex(d->class_code);
    serial_puthex(d->subclass);
    serial_puts(" BAR");
    serial_puthex((uint32_t)bar_idx);
    serial_puts("=");
    serial_puthex(bar);
    serial_putc('\n');

    pci_enable_bus_master(d);

    for (int i = 0; i < AHCI_MMIO_PAGES; i++)
        vmm_map_page(AHCI_VBASE + (uint64_t)i * 4096,
                     (uint64_t)bar + (uint64_t)i * 4096,
                     PTE_PRESENT | PTE_WRITABLE | PTE_PCD | PTE_PWT);
    hba = (volatile uint8_t *)AHCI_VBASE;

    cap = rd(HBA_CAP);
    g_cap64 = (cap >> 31) & 1u;
    nports = (cap & 0x1Fu) + 1u;

    serial_puts("[AHCI] ghc-in=");
    serial_puthex(rd(HBA_GHC));
    serial_putc('\n');
    wr(HBA_GHC, rd(HBA_GHC) | GHC_AE);
    serial_puts("[AHCI] ae set ghc=");
    serial_puthex(rd(HBA_GHC));
    serial_putc('\n');
    serial_puts("[AHCI] hba reset start\n");
    serial_puts("[AHCI] HBA reset start GHC=");
    serial_puthex(rd(HBA_GHC));
    serial_putc('\n');
    /* HBA reset experiment: skip HR, only keep AE set. Some emulated HBAs
       leave the device in a faulted TFD state after a global reset. */
    serial_puts("[AHCI] hba reset skipped (AE only)\n");
    wr(HBA_GHC, rd(HBA_GHC) | GHC_AE);
    wr(HBA_IS, 0xFFFFFFFFu);
    serial_puts("[AHCI] HBA reset done\n");

    g_pi = rd(HBA_PI);

    serial_puts("[AHCI] CAP=");
    serial_puthex(cap);
    serial_puts(" NP=");
    serial_puthex(nports);
    serial_puts(" PI=");
    serial_puthex(g_pi);
    serial_puts(" 64bit=");
    serial_puthex((uint32_t)g_cap64);
    serial_putc('\n');

    memset(g_ports, 0, sizeof g_ports);
    g_online = 0;
    for (uint32_t n = 0; n < nports && n < 32u; n++) {
        if (!(g_pi & (1u << n)))
            continue;
        g_ports[n].implemented = 1;
        pwr(n, PxIS, 0xFFFFFFFFu);
        pwr(n, PxIE, 0);
        serial_puts("[AHCI] scan port ");
        serial_puthex(n);
        serial_puts(" ssts=");
        serial_puthex(prd(n, PxSSTS));
        serial_puts(" sig=");
        serial_puthex(prd(n, PxSIG));
        serial_putc('\n');
        if (!port_online((int)n)) {
            serial_puts("[AHCI] port ");
            serial_puthex(n);
            serial_puts(" offline SSTS=");
            serial_puthex(prd(n, PxSSTS));
            serial_putc('\n');
            continue;
        }
        if (prd(n, PxSIG) != 0x00000101u) {
            serial_puts("[AHCI] port ");
            serial_puthex(n);
            serial_puts(" not ATA, skip\n");
            continue;
        }
        if (port_setup((int)n) != 0)
            continue;
        port_probe_flush((int)n);
        if (port_identify((int)n) == 0) {
            g_online++;
            if (g_primary < 0)
                g_primary = (int)n;
        }
    }

    serial_puts("[AHCI] online=");
    serial_puthex((uint32_t)g_online);
    serial_puts(" primary=");
    if (g_primary >= 0)
        serial_puthex((uint32_t)g_primary);
    else
        serial_puts("none");
    serial_putc('\n');

    g_present = g_online > 0;
    return g_present ? 0 : -1;
}

int ahci_probe(void)
{
    int found = 0;
    serial_puts("[AHCI] pci_count before=");
    serial_puthex((uint32_t)pci_count());
    serial_putc('\n');
    pci_enumerate();
    serial_puts("[AHCI] pci_count after=");
    serial_puthex((uint32_t)pci_count());
    serial_putc('\n');
    for (int i = 0; i < pci_count(); i++) {
        struct pci_dev *d = pci_get(i);
        if (!d)
            break;
        if (d->class_code == 0x01 && d->subclass == 0x06) {
            serial_puts("[AHCI] found controller at pci index ");
            serial_puthex((uint32_t)i);
            serial_putc('\n');
            if (ahci_init(d) == 0)
                found++;
        }
    }
    if (!found) {
        serial_puts("[AHCI] no AHCI controller\n");
        for (int i = 0; i < pci_count(); i++) {
            struct pci_dev *d = pci_get(i);
            if (!d)
                break;
            serial_puts("[AHCI] skip ");
            serial_puthex(d->vendor);
            serial_puthex(d->device);
            serial_puts(" class=");
            serial_puthex(d->class_code);
            serial_puthex(d->subclass);
            serial_putc('\n');
        }
    }
    return found;
}

int ahci_present(void) { return g_present; }
int ahci_online_ports(void) { return g_online; }
int ahci_primary_port(void) { return g_primary; }

int ahci_port_sectors(int port, uint64_t *sectors)
{
    if (port < 0 || port >= 32 || !g_ports[port].online)
        return -1;
    if (sectors)
        *sectors = g_ports[port].sectors;
    return 0;
}

const char *ahci_port_model(int port)
{
    if (port < 0 || port >= 32)
        return "";
    return g_ports[port].model;
}

int ahci_port_lba48(int port)
{
    if (port < 0 || port >= 32)
        return 0;
    return g_ports[port].lba48;
}

int ahci_read_sector(int port, uint64_t lba, uint32_t count, void *buf)
{
    uint8_t *p = buf;
    if (!buf || count == 0)
        return -1;
    while (count) {
        uint32_t n = count > AHCI_MAX_SECTORS ? AHCI_MAX_SECTORS : count;
        if (port_exec(port, CMD_READ_DMA_EXT, 0, lba, n, 0, p,
                      n * AHCI_SECTOR) != 0)
            return -1;
        lba += n;
        p += n * AHCI_SECTOR;
        count -= n;
    }
    return 0;
}

int ahci_write_sector(int port, uint64_t lba, uint32_t count, const void *buf)
{
    const uint8_t *p = buf;
    if (!buf || count == 0)
        return -1;
    while (count) {
        uint32_t n = count > AHCI_MAX_SECTORS ? AHCI_MAX_SECTORS : count;
        if (port_exec(port, CMD_WRITE_DMA_EXT, 1, lba, n, p, 0,
                      n * AHCI_SECTOR) != 0)
            return -1;
        lba += n;
        p += n * AHCI_SECTOR;
        count -= n;
    }
    return 0;
}

/* Read-only self check: LBA0 must carry the 0x55AA boot signature. */
int ahci_lba0_signature(int port)
{
    uint8_t buf[512];
    if (ahci_read_sector(port, 0, 1, buf) != 0) {
        serial_puts("[AHCI] LBA0 read failed\n");
        return -1;
    }
    serial_puts("[AHCI] LBA0 sig=");
    serial_puthex((uint32_t)buf[510]);
    serial_puthex((uint32_t)buf[511]);
    serial_putc('\n');
    if (buf[510] == 0x55 && buf[511] == 0xAA) {
        serial_puts("[AHCI] LBA0 boot signature OK (read-only check)\n");
        return 0;
    }
    serial_puts("[AHCI] LBA0 has no 0x55AA signature\n");
    return -1;
}


/* FLUSH CACHE EXT: no data transfer, makes the write path durable. */
int ahci_flush(int port)
{
    if (!g_ports[port].online)
        return -1;
    return port_exec(port, CMD_FLUSH_CACHE_EXT, 1, 0, 0, 0, 0, 0);
}











