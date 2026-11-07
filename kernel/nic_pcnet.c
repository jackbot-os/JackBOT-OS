#include <stdint.h>
#include <stddef.h>
#include <kernel/kstring.h>
#include <kernel/pci.h>
#include <kernel/pmm.h>
#include <kernel/nic.h>
#include <kernel/serial.h>
#include <kernel/pcnet.h>

/* AMD PCnet-PCI II / FAST III driver (Am79C970A / 79C973 / 79C975).
 *
 * Programming model follows the AMD datasheet and the well-known
 * U-Boot/Linux pcnet32 sequence:
 *   - WIO (16-bit) register access: RDP 0x10, RAP 0x12, RESET 0x14,
 *     BDP 0x16 in the I/O BAR.
 *   - The controller is switched to 32-bit init-block mode through
 *     BCR20 (SSIZE32) before initialisation, so init block and
 *     descriptor addresses are full 32-bit DMA addresses.
 *   - Transmit and receive use the classic 16-byte descriptors with
 *     negative buffer lengths.
 * No interrupts are used: the NIC is polled from the network stack.
 */

#define PCNET_RDP   0x10
#define PCNET_RAP   0x12
#define PCNET_RESET 0x14
#define PCNET_BDP   0x16

#define CSR0_INIT 0x0001
#define CSR0_STRT 0x0002
#define CSR0_STOP 0x0004
#define CSR0_TDMD 0x0008
#define CSR0_IDON 0x0100

#define RX_ENTRIES 16
#define TX_ENTRIES 4
#define RX_LOG 4
#define TX_LOG 2
#define PKT_BUF_SZ 1544
#define TX_STATUS_OWN_STP_ENP 0x8300
#define RX_STATUS_OWN 0x8000

#define DMA_LIMIT 0x40000000ull

struct pcnet_rx_desc {
    uint32_t base;
    int16_t buf_len;
    int16_t status;
    uint32_t msg_len;
    uint32_t reserved;
} __attribute__((packed));

struct pcnet_tx_desc {
    uint32_t base;
    int16_t length;
    int16_t status;
    uint32_t misc;
    uint32_t reserved;
} __attribute__((packed));

/* 32-bit mode initialisation block (after BCR20 = SSIZE32). */
struct pcnet_init_block {
    uint16_t mode;
    uint16_t tlen_rlen;
    uint8_t mac[6];
    uint16_t reserved;
    uint32_t filter[2];
    uint32_t rx_ring;
    uint32_t tx_ring;
    uint32_t reserved2;
} __attribute__((packed));

static uint16_t io_base;
static int have;
static uint8_t mac_addr[6];
static struct pcnet_rx_desc *rx_ring;
static struct pcnet_tx_desc *tx_ring;
static uint8_t *rx_buf[RX_ENTRIES];
static uint8_t *tx_buf[TX_ENTRIES];
static struct pcnet_init_block *init_blk;
static int rx_cur, tx_cur;

static inline void outw(uint16_t port, uint16_t v)
{
    __asm__ volatile("outw %0,%1" : : "a"(v), "Nd"(port));
}
static inline uint16_t inw(uint16_t port)
{
    uint16_t v;
    __asm__ volatile("inw %1,%0" : "=a"(v) : "Nd"(port));
    return v;
}

static uint16_t csr_rd(uint16_t idx)
{
    outw(io_base + PCNET_RAP, idx);
    return inw(io_base + PCNET_RDP);
}
static void csr_wr(uint16_t idx, uint16_t v)
{
    outw(io_base + PCNET_RAP, idx);
    outw(io_base + PCNET_RDP, v);
}
static uint16_t bcr_rd(uint16_t idx)
{
    outw(io_base + PCNET_RAP, idx);
    return inw(io_base + PCNET_BDP);
}
static void bcr_wr(uint16_t idx, uint16_t v)
{
    outw(io_base + PCNET_RAP, idx);
    outw(io_base + PCNET_BDP, v);
}
static void pcnet_reset(void)
{
    (void)inw(io_base + PCNET_RESET);
    for (volatile int i = 0; i < 100000; i++)
        ;
}

static void delay_10us(void)
{
    for (volatile int i = 0; i < 1000; i++)
        ;
}

static int pcnet_pci_init(struct pci_dev *d)
{
    uint16_t cmd = pci_cfg_read16(d->bus, d->dev, d->func, 4);
    cmd |= 0x0005; /* I/O space enable + bus master */
    pci_cfg_write16(d->bus, d->dev, d->func, 4, cmd);
    pci_cfg_write8(d->bus, d->dev, d->func, 0x0F, 0x20); /* latency */

    uint32_t bar = 0;
    if (d->bar[0] & 1u)
        bar = d->bar[0] & 0xFFFFFFFCu;
    else if (d->bar[1] & 1u)
        bar = d->bar[1] & 0xFFFFFFFCu;
    if (!bar) {
        serial_puts("[PCNET] no I/O BAR found\n");
        return -1;
    }
    io_base = (uint16_t)(bar & 0xFFFF);
    return 0;
}

int pcnet_init(void)
{
    struct pci_dev *d = pci_find(0x1022, 0x2000);
    if (!d)
        d = pci_find(0x1022, 0x2001);
    if (!d) {
        serial_puts("[PCNET] no PCnet NIC found\n");
        return 0;
    }
    if (pcnet_pci_init(d) != 0)
        return 0;

    pcnet_reset();
    uint16_t csr0 = csr_rd(0);
    outw(io_base + PCNET_RAP, 88);
    uint16_t rap_echo = inw(io_base + PCNET_RAP);
    if (csr0 != CSR0_STOP || rap_echo != 88) {
        serial_puts("[PCNET] register access check failed csr0=");
        serial_puthex(csr0);
        serial_puts(" rap=");
        serial_puthex(rap_echo);
        serial_putc('\n');
        return 0;
    }

    uint32_t chip = (uint32_t)csr_rd(88) | ((uint32_t)csr_rd(89) << 16);
    uint32_t version = (chip >> 12) & 0xFFFF;
    if ((chip & 0xFFF) != 0x003) {
        serial_puts("[PCNET] unexpected chip id=");
        serial_puthex(chip);
        serial_putc('\n');
        return 0;
    }

    for (int i = 0; i < 3; i++) {
        uint16_t v = csr_rd((uint16_t)(12 + i));
        mac_addr[2 * i] = (uint8_t)(v & 0xFF);
        mac_addr[2 * i + 1] = (uint8_t)((v >> 8) & 0xFF);
    }

    /* Switch to 32-bit init block addressing, auto-negotiate. */
    bcr_wr(20, 2);
    {
        uint16_t v = bcr_rd(2);
        v = (uint16_t)((v & ~(uint16_t)2) | 2);
        bcr_wr(2, v);
    }
    {
        uint16_t v = bcr_rd(32);
        v = (uint16_t)((v & ~(uint16_t)0x98) | 0x20);
        bcr_wr(32, v);
    }
    {
        uint16_t v = bcr_rd(18);
        v |= (uint16_t)(1 << 11);
        bcr_wr(18, v);
    }
    {
        uint16_t v = csr_rd(80);
        v |= (uint16_t)(0x3 << 10);
        csr_wr(80, v);
    }

    void *init_pg = pmm_alloc_page_below(DMA_LIMIT);
    void *rx_pg = pmm_alloc_page_below(DMA_LIMIT);
    void *tx_pg = pmm_alloc_page_below(DMA_LIMIT);
    if (!init_pg || !rx_pg || !tx_pg) {
        serial_puts("[PCNET] DMA allocation failed\n");
        return 0;
    }
    memset(init_pg, 0, 4096);
    memset(rx_pg, 0, 4096);
    memset(tx_pg, 0, 4096);
    init_blk = (struct pcnet_init_block *)init_pg;
    rx_ring = (struct pcnet_rx_desc *)rx_pg;
    tx_ring = (struct pcnet_tx_desc *)tx_pg;

    for (int i = 0; i < RX_ENTRIES; i++) {
        void *b = pmm_alloc_page_below(DMA_LIMIT);
        if (!b) {
            serial_puts("[PCNET] RX buffer allocation failed\n");
            return 0;
        }
        memset(b, 0, 4096);
        rx_buf[i] = b;
        rx_ring[i].base = (uint32_t)(uintptr_t)b;
        rx_ring[i].buf_len = (int16_t)(-PKT_BUF_SZ);
        rx_ring[i].status = (int16_t)RX_STATUS_OWN;
        rx_ring[i].msg_len = 0;
        rx_ring[i].reserved = 0;
    }
    for (int i = 0; i < TX_ENTRIES; i++) {
        void *b = pmm_alloc_page_below(DMA_LIMIT);
        if (!b) {
            serial_puts("[PCNET] TX buffer allocation failed\n");
            return 0;
        }
        memset(b, 0, 4096);
        tx_buf[i] = b;
        tx_ring[i].base = (uint32_t)(uintptr_t)b;
        tx_ring[i].length = 0;
        tx_ring[i].status = 0;
        tx_ring[i].misc = 0;
        tx_ring[i].reserved = 0;
    }

    init_blk->mode = 0x0000;
    init_blk->tlen_rlen =
        (uint16_t)((TX_LOG << 12) | (RX_LOG << 4));
    for (int i = 0; i < 6; i++)
        init_blk->mac[i] = mac_addr[i];
    init_blk->filter[0] = 0;
    init_blk->filter[1] = 0;
    init_blk->rx_ring = (uint32_t)(uintptr_t)rx_ring;
    init_blk->tx_ring = (uint32_t)(uintptr_t)tx_ring;

    uint32_t ib_phys = (uint32_t)(uintptr_t)init_blk;
    csr_wr(1, (uint16_t)(ib_phys & 0xFFFF));
    csr_wr(2, (uint16_t)((ib_phys >> 16) & 0xFFFF));
    csr_wr(4, 0x0915);
    csr_wr(0, CSR0_INIT);
    int ok = 0;
    for (int i = 0; i < 10000; i++) {
        if (csr_rd(0) & CSR0_IDON) {
            ok = 1;
            break;
        }
        delay_10us();
    }
    if (!ok) {
        serial_puts("[PCNET] init timeout csr0=");
        serial_puthex(csr_rd(0));
        serial_putc('\n');
        pcnet_reset();
        return 0;
    }
    csr_wr(0, CSR0_STRT);

    rx_cur = 0;
    tx_cur = 0;
    have = 1;
    serial_puts("[PCNET] init OK version=");
    serial_puthex(version);
    serial_puts(" mac=");
    for (int i = 0; i < 6; i++) {
        serial_puthex(mac_addr[i]);
        if (i < 5)
            serial_putc(':');
    }
    serial_putc('\n');
    return 1;
}

int pcnet_send(const void *data, uint16_t len)
{
    if (!have || !data || len == 0 || len > PKT_BUF_SZ)
        return -1;
    int i = tx_cur;
    for (int t = 0; t < 1000; t++) {
        if (!(tx_ring[i].status & (int16_t)RX_STATUS_OWN))
            break;
        delay_10us();
        if (t == 999) {
            serial_puts("[PCNET] tx stuck entry=");
            serial_puthex((uint32_t)i);
            serial_putc('\n');
            return -1;
        }
    }
    memcpy(tx_buf[i], data, len);
    tx_ring[i].length = (int16_t)(-((int16_t)len));
    tx_ring[i].misc = 0;
    tx_ring[i].base = (uint32_t)(uintptr_t)tx_buf[i];
    __asm__ volatile("" ::: "memory");
    tx_ring[i].status = (int16_t)TX_STATUS_OWN_STP_ENP;
    tx_cur = (i + 1) % TX_ENTRIES;
    csr_wr(0, CSR0_TDMD);
    return (int)len;
}

int pcnet_recv(void *buf, uint16_t cap)
{
    if (!have || !buf || cap == 0)
        return 0;
    struct pcnet_rx_desc *e = &rx_ring[rx_cur];
    if (e->status & (int16_t)RX_STATUS_OWN)
        return 0;
    uint16_t err = (uint16_t)((uint16_t)e->status >> 8);
    if (err != 0x03) {
        /* Drop bad frames and hand the buffer back to the controller. */
        e->status = (int16_t)RX_STATUS_OWN;
        rx_cur = (rx_cur + 1) % RX_ENTRIES;
        return 0;
    }
    int32_t len = (int32_t)(e->msg_len & 0xFFF) - 4;
    if (len < 60 || len > PKT_BUF_SZ) {
        e->status = (int16_t)RX_STATUS_OWN;
        rx_cur = (rx_cur + 1) % RX_ENTRIES;
        return 0;
    }
    uint16_t n = (uint16_t)len;
    if (n > cap)
        n = cap;
    memcpy(buf, rx_buf[rx_cur], n);
    e->status = (int16_t)RX_STATUS_OWN;
    rx_cur = (rx_cur + 1) % RX_ENTRIES;
    return (int)n;
}

void pcnet_get_mac(uint8_t out[6])
{
    for (int i = 0; i < 6; i++)
        out[i] = mac_addr[i];
}

int pcnet_present(void)
{
    return have;
}

static int pcnet_nic_init(struct pci_dev *d)
{
    (void)d;
    return pcnet_init() == 1 ? 0 : -1;
}

static const struct nic_ops pcnet_ops = {
    .name = "pcnet",
    .init = pcnet_nic_init,
    .send = pcnet_send,
    .recv = pcnet_recv,
    .get_mac = pcnet_get_mac,
    .present = pcnet_present,
};

void pcnet_register_nic(void)
{
    nic_register(&pcnet_ops);
}
