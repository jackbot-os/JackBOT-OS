#include <stdint.h>
#include <stddef.h>
#include <kernel/ahci.h>
#include <kernel/pci.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/kstring.h>
#include <kernel/serial.h>

#define AHCI_VBASE 0xffff800083000000ull

#define HBA_CAP    0x00
#define HBA_GHC    0x04
#define HBA_IS     0x08
#define HBA_PI     0x0C
#define HBA_VS     0x10
#define HBA_CAP2   0x28

#define GHC_AHCI_EN 0x80000000u
#define GHC_RESET   0x00000004u

#define PORT_BASE(n) (0x100u + (uint32_t)(n) * 0x80u)
#define PxCLB   0x00
#define PxCLBU  0x04
#define PxFB    0x08
#define PxFBU   0x0C
#define PxIS    0x10
#define PxIE    0x14
#define PxCMD   0x18
#define PxTFD   0x20
#define PxSSTS  0x28
#define PxSCTL  0x2C
#define PxSACT  0x34
#define PxCI    0x38

#define PxCMD_ST    0x00000001u
#define PxCMD_SUD   0x00000002u
#define PxCMD_FRE   0x00000010u
#define PxCMD_FR    0x00000040u
#define PxCMD_ICC   0x10000000u

#define TFD_BSY 0x80u
#define TFD_DRDY 0x40u
#define TFD_DRQ 0x08u
#define TFD_ERR 0x01u

#define IS_TFES 0x00000080u
#define IS_HBF  0x40000000u
#define IS_HBDS 0x80000000u

#define SSTS_DE_MASK 0x0000000Fu
#define SSTS_DE_ONLINE 0x00000003u

#define CMD_IDENTIFY 0xEC

struct ahci_cmd_hdr {
    uint32_t dw0;
    uint32_t prdtl;
    uint32_t prdbc;
    uint32_t reserved;
    uint32_t ctba_l;
    uint32_t ctba_h;
    uint32_t reserved2[2];
} __attribute__((packed));

struct ahci_prd {
    uint32_t dba_l;
    uint32_t dba_h;
    uint32_t reserved;
    uint32_t dw3;
} __attribute__((packed));

struct ahci_cmd_table {
    uint8_t fis[64];
    uint8_t atapi[16];
    uint8_t reserved[48];
    struct ahci_prd prd[1];
} __attribute__((packed));

static volatile uint8_t *hba;
static int g_present;
static int g_online;
static uint32_t g_pi;
static int g_cap64;

static uint32_t rd(uint32_t o)
{
    return *(volatile uint32_t *)(hba + o);
}

static void wr(uint32_t o, uint32_t v)
{
    *(volatile uint32_t *)(hba + o) = v;
    __asm__ volatile("" ::: "memory");
}

static uint32_t port_rd(int n, uint32_t off)
{
    return *(volatile uint32_t *)(hba + PORT_BASE(n) + off);
}

static void port_wr(int n, uint32_t off, uint32_t v)
{
    *(volatile uint32_t *)(hba + PORT_BASE(n) + off) = v;
    __asm__ volatile("" ::: "memory");
}

static uint32_t alloc_page(void)
{
    void *p = pmm_alloc_page_below(0x40000000ull);
    return p ? (uint32_t)(uintptr_t)p : 0;
}

static int port_online(int n)
{
    return (port_rd(n, PxSSTS) & SSTS_DE_MASK) == SSTS_DE_ONLINE;
}

static int identify_port(int n)
{
    uint32_t clb = alloc_page();
    uint32_t ct  = alloc_page();
    uint32_t recv = alloc_page();
    if (!clb || !ct || !recv) {
        serial_puts("[AHCI] dma alloc failed\n");
        return -1;
    }

    memset((void *)(uintptr_t)clb, 0, 4096);
    memset((void *)(uintptr_t)ct, 0, 4096);
    memset((void *)(uintptr_t)recv, 0, 512);

    uint32_t cmd = port_rd(n, PxCMD);
    cmd &= ~PxCMD_ST;
    port_wr(n, PxCMD, cmd);
    for (uint32_t i = 0; i < 100000u; i++) {
        if ((port_rd(n, PxCMD) & PxCMD_ST) == 0)
            break;
    }

    int idle = 0;
    for (uint32_t i = 0; i < 100000u; i++) {
        uint8_t tfd = (uint8_t)(port_rd(n, PxTFD) & 0xFFu);
        if (!(tfd & TFD_BSY)) {
            idle = 1;
            break;
        }
    }
    if (!idle) {
        serial_puts("[AHCI] port "); serial_puthex((uint32_t)n);
        serial_puts(" busy\n");
        return -1;
    }

    port_wr(n, PxFB, recv);
    port_wr(n, PxFBU, 0);
    port_wr(n, PxCLB, clb);
    port_wr(n, PxCLBU, 0);
    port_wr(n, PxIS, 0xFFFFFFFFu);

    cmd = port_rd(n, PxCMD);
    cmd |= PxCMD_FRE;
    port_wr(n, PxCMD, cmd);
    for (uint32_t i = 0; i < 100000u; i++) {
        if (port_rd(n, PxCMD) & PxCMD_FR)
            break;
    }

    struct ahci_cmd_hdr *h = (struct ahci_cmd_hdr *)(uintptr_t)clb;
    struct ahci_cmd_table *t = (struct ahci_cmd_table *)(uintptr_t)ct;

    t->fis[0]  = 0x27;
    t->fis[1]  = 0x80;
    t->fis[2]  = 0x00;
    t->fis[3]  = 0x01;
    t->fis[4]  = 0x00;
    t->fis[5]  = 0x00;
    t->fis[6]  = 0x00;
    t->fis[7]  = 0xA0;
    t->fis[16] = CMD_IDENTIFY;

    h->dw0    = 5u;
    h->prdtl  = 1;
    h->prdbc  = 0;
    h->ctba_l = ct;
    h->ctba_h = 0;

    t->prd[0].dba_l = recv;
    t->prd[0].dba_h = 0;
    t->prd[0].reserved = 0;
    t->prd[0].dw3 = (512u - 1u) | 0x80000000u;

    cmd = port_rd(n, PxCMD);
    cmd |= PxCMD_ST | PxCMD_ICC;
    port_wr(n, PxCMD, cmd);

    port_wr(n, PxCI, 1u);

    int done = 0;
    for (uint32_t i = 0; i < 1000000u; i++) {
        if ((port_rd(n, PxCI) & 1u) == 0) {
            done = 1;
            break;
        }
    }

    uint32_t is = port_rd(n, PxIS);
    port_wr(n, PxIS, is);

    if (!done) {
        serial_puts("[AHCI] port "); serial_puthex((uint32_t)n);
        serial_puts(" identify timeout IS="); serial_puthex(is);
        serial_puts(" TFD="); serial_puthex(port_rd(n, PxTFD));
        serial_puts(" CMD="); serial_puthex(port_rd(n, PxCMD));
        serial_putc('\n');
        return -1;
    }
    if (is & (IS_TFES | IS_HBF | IS_HBDS)) {
        serial_puts("[AHCI] port "); serial_puthex((uint32_t)n);
        serial_puts(" identify error IS="); serial_puthex(is);
        serial_putc('\n');
        return -1;
    }

    serial_puts("[AHCI] port ");
    serial_puthex((uint32_t)n);
    serial_puts(" identify OK bytes=");
    serial_puthex(h->prdbc);
    serial_putc('\n');
    return 0;
}

int ahci_init(struct pci_dev *d)
{
    if (!d)
        return -1;

    serial_puts("[AHCI] BAR dump:");
    for (int b = 0; b < 6; b++) {
        serial_puts(" [");
        serial_puthex((uint32_t)b);
        serial_puts("]=");
        serial_puthex(d->bar[b]);
    }
    serial_putc('\n');

    int bar_idx = -1;
    uint32_t bar = 0;
    for (int b = 0; b < 6; b++) {
        uint32_t raw = d->bar[b];
        if (raw != 0 && !(raw & 1u)) {
            bar = raw & 0xFFFFFFF0u;
            bar_idx = b;
            if ((raw & 6u) == 4u)
                b++;
        }
    }
    if (bar_idx < 0) {
        serial_puts("[AHCI] no memory BAR found\n");
        return -1;
    }

    serial_puts("[AHCI] using BAR");
    serial_puthex((uint32_t)bar_idx);
    serial_puts(" addr=");
    serial_puthex(bar);
    serial_putc('\n');

    pci_enable_bus_master(d);

    for (int i = 0; i < 2; i++)
        vmm_map_page(AHCI_VBASE + (uint64_t)i * 4096,
                     (uint64_t)bar + (uint64_t)i * 4096,
                     PTE_PRESENT | PTE_WRITABLE | PTE_PCD | PTE_PWT);
    hba = (volatile uint8_t *)AHCI_VBASE;

    uint32_t cap = rd(HBA_CAP);
    g_cap64 = (cap >> 31) & 1u;
    uint32_t nports = (cap & 0x1Fu) + 1u;

    wr(HBA_GHC, GHC_RESET);
    int reset_ok = 0;
    for (uint32_t i = 0; i < 1000000u; i++) {
        if ((rd(HBA_GHC) & GHC_RESET) == 0) {
            reset_ok = 1;
            break;
        }
    }
    if (!reset_ok) {
        serial_puts("[AHCI] HBA reset timeout\n");
        return -1;
    }
    wr(HBA_GHC, GHC_AHCI_EN);

    g_pi = rd(HBA_PI);

    serial_puts("[AHCI] CAP="); serial_puthex(cap);
    serial_puts(" ports="); serial_puthex(nports);
    serial_puts(" PI="); serial_puthex(g_pi);
    serial_puts(" 64bit="); serial_puthex((uint32_t)g_cap64);
    serial_putc('\n');

    g_online = 0;
    int online_list[32];
    int n_online = 0;
    for (uint32_t n = 0; n < nports && n < 32u; n++) {
        if (!(g_pi & (1u << n)))
            continue;
        port_wr(n, PxIS, 0xFFFFFFFFu);
        port_wr(n, PxIE, 0);
        if (port_online((int)n)) {
            g_online++;
            online_list[n_online++] = (int)n;
        }
    }

    serial_puts("[AHCI] online="); serial_puthex((uint32_t)g_online); serial_putc('\n');

    for (int i = 0; i < n_online; i++)
        identify_port(online_list[i]);

    g_present = 1;
    return 0;
}

int ahci_present(void)
{
    return g_present;
}

int ahci_online_ports(void)
{
    return g_online;
}

int ahci_read_sector(int port, uint64_t lba, uint8_t count, void *buf)
{
    (void)port; (void)lba; (void)count; (void)buf;
    serial_puts("[AHCI] sector read not implemented yet\n");
    return -1;
}

int ahci_write_sector(int port, uint64_t lba, uint8_t count, const void *buf)
{
    (void)port; (void)lba; (void)count; (void)buf;
    serial_puts("[AHCI] sector write not implemented yet\n");
    return -1;
}
