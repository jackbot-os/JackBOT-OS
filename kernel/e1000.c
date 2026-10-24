#include <stdint.h>
#include <stddef.h>
#include <kernel/kstring.h>
#include <kernel/pci.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/e1000.h>
#include <kernel/nic.h>
#include <kernel/serial.h>

#define MMIO_VBASE 0xffff800081000000ull
#define MMIO_PAGES 32
#define TXN 32
#define RXN 32
#define BUFSZ 2048

#define REG_CTRL   0x0000
#define REG_STATUS 0x0008
#define REG_EERD   0x0014
#define REG_ICR    0x00C0
#define REG_IMS    0x00D0
#define REG_IMC    0x00D8
#define REG_RCTL   0x0100
#define REG_TCTL   0x0400
#define REG_TIPG   0x0410
#define REG_RDBAL  0x2800
#define REG_RDBAH  0x2804
#define REG_RDLEN  0x2808
#define REG_RDH    0x2810
#define REG_RDT    0x2818
#define REG_TDBAL  0x3800
#define REG_TDBAH  0x3804
#define REG_TDLEN  0x3808
#define REG_TDH    0x3810
#define REG_TDT    0x3818
#define REG_MTA    0x5200
#define REG_RAL0   0x5400
#define REG_RAH0   0x5404

#define TCTL_EN   0x00000002
#define TCTL_PSP  0x00000008
#define TCTL_CT   0x00000100
#define TCTL_COLD 0x00400000
#define TIPG_VAL  0x0060200A

#define CTRL_ASDE 0x00000020
#define CTRL_SLU  0x00000040
#define CTRL_FRCSPD 0x00000800
#define CTRL_FRCDPLX 0x00001000

#define RCTL_EN    0x00000002
#define RCTL_BAM   0x00008000
#define RCTL_SECRC 0x00000020
#define RAH_AV     0x80000000

#define TX_CMD 0x0B
#define DD     0x01
#define EOP    0x02

struct rx_desc {
    uint64_t addr;
    uint16_t length;
    uint16_t csum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed));

struct tx_desc {
    uint64_t addr;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
} __attribute__((packed));

static volatile uint8_t *mmio;
static struct rx_desc *rxring;
static struct tx_desc *txring;
static uint32_t rx_phys[RXN];
static uint32_t tx_phys[TXN];
static int rx_cur, tx_cur;
static uint8_t mac[6];
static int have;

static uint32_t rd(uint32_t o)
{
    return *(volatile uint32_t *)(mmio + o);
}

static void wr(uint32_t o, uint32_t v)
{
    *(volatile uint32_t *)(mmio + o) = v;
    __asm__ volatile("" ::: "memory");
}

static uint16_t eeprom_read(uint8_t addr)
{
    wr(REG_EERD, ((uint32_t)addr << 8) | 1);
    uint32_t v = 0;
    int t = 0;
    while (!((v = rd(REG_EERD)) & 0x10) && t++ < 200000)
        ;
    return (uint16_t)((v >> 16) & 0xFFFF);
}

static uint32_t alloc_dma_page(void)
{
    void *p = pmm_alloc_page_below(0x40000000ull);
    return p ? (uint32_t)(uintptr_t)p : 0;
}

int e1000_init(void)
{
    struct pci_dev *d = pci_find(0x8086, 0x100E);
    if (!d) d = pci_find(0x8086, 0x1000);
    if (!d) d = pci_find(0x8086, 0x1001);
    if (!d) d = pci_find(0x8086, 0x1004);
    if (!d) d = pci_find(0x8086, 0x10D3);
    if (!d) d = pci_find(0x8086, 0x10EA);
    if (!d) d = pci_find(0x8086, 0x10DE);
    if (!d) {
        serial_puts("[E1K] no e1000 NIC found\n");
        return 0;
    }
    pci_enable_bus_master(d);

    uint32_t bar = pci_bar_mem(d, 0);
    for (int i = 0; i < MMIO_PAGES; i++)
        vmm_map_page(MMIO_VBASE + (uint64_t)i * 4096,
                     (uint64_t)bar + i * 4096,
                     PTE_PRESENT | PTE_WRITABLE | PTE_PCD | PTE_PWT);
    mmio = (volatile uint8_t *)MMIO_VBASE;

    {
        uint32_t ctrl = rd(REG_CTRL);
        ctrl &= ~(uint32_t)(CTRL_FRCSPD | CTRL_FRCDPLX);
        ctrl |= CTRL_ASDE | CTRL_SLU;
        wr(REG_CTRL, ctrl);
        for (volatile int z = 0; z < 10000; z++)
            ;
    }

    wr(REG_IMC, 0xFFFFFFFF);
    (void)rd(REG_ICR);

    uint32_t txrp = alloc_dma_page();
    uint32_t rxrp = alloc_dma_page();
    if (!txrp || !rxrp)
        return 0;
    txring = (struct tx_desc *)(uintptr_t)txrp;
    rxring = (struct rx_desc *)(uintptr_t)rxrp;
    memset(txring, 0, TXN * sizeof(struct tx_desc));
    memset(rxring, 0, RXN * sizeof(struct rx_desc));

    for (int i = 0; i < TXN; i++) {
        if ((i & 1) == 0) {
            uint32_t pg = alloc_dma_page();
            if (!pg)
                return 0;
            tx_phys[i] = pg;
            tx_phys[i + 1] = pg + BUFSZ;
        }
        txring[i].addr = tx_phys[i];
        txring[i].status = DD;
    }
    for (int i = 0; i < RXN; i++) {
        if ((i & 1) == 0) {
            uint32_t pg = alloc_dma_page();
            if (!pg)
                return 0;
            rx_phys[i] = pg;
            rx_phys[i + 1] = pg + BUFSZ;
        }
        rxring[i].addr = rx_phys[i];
        rxring[i].status = 0;
    }

    uint16_t m0 = eeprom_read(0), m1 = eeprom_read(1), m2 = eeprom_read(2);
    mac[0] = (uint8_t)m0; mac[1] = (uint8_t)(m0 >> 8);
    mac[2] = (uint8_t)m1; mac[3] = (uint8_t)(m1 >> 8);
    mac[4] = (uint8_t)m2; mac[5] = (uint8_t)(m2 >> 8);

    wr(REG_TDBAL, txrp);
    wr(REG_TDBAH, 0);
    wr(REG_TDLEN, TXN * sizeof(struct tx_desc));
    wr(REG_TDH, 0);
    wr(REG_TDT, 0);
    wr(REG_TCTL, TCTL_EN | TCTL_PSP | TCTL_CT | TCTL_COLD);
    wr(REG_TIPG, TIPG_VAL);

    wr(REG_RDBAL, rxrp);
    wr(REG_RDBAH, 0);
    wr(REG_RDLEN, RXN * sizeof(struct rx_desc));
    wr(REG_RDH, 0);
    wr(REG_RDT, RXN - 1);

    for (int i = 0; i < 128; i++)
        wr(REG_MTA + i * 4, 0);

    uint32_t ral = (uint32_t)mac[0] | ((uint32_t)mac[1] << 8)
                 | ((uint32_t)mac[2] << 16) | ((uint32_t)mac[3] << 24);
    uint32_t rah = (uint32_t)mac[4] | ((uint32_t)mac[5] << 8) | RAH_AV;
    wr(REG_RAL0, ral);
    wr(REG_RAH0, rah);

    wr(REG_RCTL, RCTL_EN | RCTL_BAM | RCTL_SECRC);

    rx_cur = 0;
    tx_cur = 0;
    have = 1;

    serial_puts("[E1K] init OK mac=");
    for (int i = 0; i < 6; i++) {
        serial_puthex((uint32_t)mac[i]);
        if (i < 5)
            serial_putc(':');
    }
    serial_puts(" STATUS="); serial_puthex(rd(REG_STATUS));
    serial_puts(" RCTL="); serial_puthex(rd(REG_RCTL));
    serial_puts(" RDH="); serial_puthex(rd(REG_RDH));
    serial_puts(" RDT="); serial_puthex(rd(REG_RDT));
    serial_putc('\n');
    return 1;
}

int e1000_send(const void *data, uint16_t len)
{
    if (!have)
        return -1;
    if (len > BUFSZ)
        len = BUFSZ;
    int i = tx_cur;
    if (!(txring[i].status & DD))
        return -1;
    memcpy((void *)(uintptr_t)tx_phys[i], data, len);
    txring[i].length = len;
    txring[i].cso = 0;
    txring[i].css = 0;
    txring[i].cmd = TX_CMD;
    txring[i].status = 0;
    tx_cur = (i + 1) % TXN;
    wr(REG_TDT, (uint32_t)tx_cur);
    return (int)len;
}

int e1000_recv(void *buf, uint16_t cap)
{
    if (!have)
        return 0;
    int i = rx_cur;
    struct rx_desc *d = &rxring[i];
    if (!(d->status & DD))
        return 0;
    uint16_t len = d->length;
    if (len > cap)
        len = cap;
    memcpy(buf, (void *)(uintptr_t)rx_phys[i], len);

    d->status = 0;
    d->addr = rx_phys[i];
    wr(REG_RDT, (uint32_t)i);
    rx_cur = (i + 1) % RXN;
    return (int)len;
}

void e1000_get_mac(uint8_t out[6])
{
    for (int i = 0; i < 6; i++)
        out[i] = mac[i];
}

int e1000_present(void)
{
    return have;
}

/* ---- NIC 抽象层适配 ---- */
static int e1000_nic_init(struct pci_dev *d)
{
    (void)d;
    /* e1000_init: 1=成功 0=无卡；nic 层约定 0=成功 */
    return e1000_init() == 1 ? 0 : -1;
}

static const struct nic_ops e1000_ops = {
    .name = "e1000",
    .init = e1000_nic_init,
    .send = e1000_send,
    .recv = e1000_recv,
    .get_mac = e1000_get_mac,
    .present = e1000_present,
};

void e1000_register_nic(void)
{
    nic_register(&e1000_ops);
}
