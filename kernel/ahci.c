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
#define PxCMD_FRE   0x00000010u
#define PxCMD_ICC   0x10000000u

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

struct ahci_cmd_table {
    uint8_t fis[64];
    uint8_t atapi[16];
    uint8_t reserved[48];
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
    uint32_t fis = alloc_page();
    if (!clb || !ct || !fis) {
        serial_puts("[AHCI] dma alloc failed\n");
        return -1;
    }

    memset((void *)(uintptr_t)clb, 0, 4096);
    memset((void *)(uintptr_t)ct, 0, 4096);
    memset((void *)(uintptr_t)fis, 0, 256);

    port_wr(n, PxFB, fis);
    port_wr(n, PxFBU, 0);
    port_wr(n, PxCLB, clb);
    port_wr(n, PxCLBU, 0);

    struct ahci_cmd_hdr *h = (struct ahci_cmd_hdr *)(uintptr_t)clb;
    struct ahci_cmd_table *t = (struct ahci_cmd_table *)(uintptr_t)ct;

    t->fis[0]  = 0x27;
    t->fis[1]  = 0x80;
    t->fis[3]  = 0x01;
    t->fis[7]  = 0xA0;
    t->fis[16] = CMD_IDENTIFY;

    h->dw0    = (5u & 0x1F) | (1u << 15);
    h->prdtl  = 0;
    h->prdbc  = 0;
    h->ctba_l = ct;
    h->ctba_h = 0;

    port_wr(n, PxIS, 0xFFFFFFFFu);
    uint32_t cmd = port_rd(n, PxCMD);
    cmd |= PxCMD_FRE | PxCMD_ST | PxCMD_ICC;
    port_wr(n, PxCMD, cmd);

    port_wr(n, PxCI, 1u);

    int done = 0;
    for (uint32_t i = 0; i < 1000000u; i++) {
        if ((port_rd(n, PxCI) & 1u) == 0 && (port_rd(n, PxSACT) & 1u) == 0) {
            done = 1;
            break;
        }
    }

    uint32_t is = port_rd(n, PxIS);
    port_wr(n, PxIS, is);

    if (!done) {
        serial_puts("[AHCI] port identify timeout\n");
        return -1;
    }
    if (is & 0x00000100u) {
        serial_puts("[AHCI] port identify error\n");
        return -1;
    }

    serial_puts("[AHCI] port ");
    serial_puthex((uint32_t)n);
    serial_puts(" identify OK\n");
    return 0;
}

int ahci_init(struct pci_dev *d)
{
    if (!d)
        return -1;

    uint32_t bar = pci_bar_mem(d, 0);
    if (bar == 0 || (d->bar[0] & 1u)) {
        serial_puts("[AHCI] BAR0 not memory mapped\n");
        return -1;
    }

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
    int first = -1;
    for (uint32_t n = 0; n < nports && n < 32u; n++) {
        if (!(g_pi & (1u << n)))
            continue;
        port_wr(n, PxIS, 0xFFFFFFFFu);
        port_wr(n, PxIE, 0);
        if (port_online((int)n)) {
            g_online++;
            if (first < 0)
                first = (int)n;
        }
    }

    serial_puts("[AHCI] online="); serial_puthex((uint32_t)g_online); serial_putc('\n');

    if (first >= 0)
        identify_port(first);

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
