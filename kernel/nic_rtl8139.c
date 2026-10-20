/*
 * Realtek RTL8139 网卡驱动（兼容层：最常见 PCI 网卡之一）
 * 通过 NIC 抽象层接入网络栈，支持真实收发帧。
 */
#include <stdint.h>
#include <stddef.h>
#include <kernel/hal.h>
#include <kernel/kstring.h>
#include <kernel/pci.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/nic.h>
#include <kernel/serial.h>

#define RTL_VENDOR 0x10ec
#define RTL_DEVICE 0x8139

/* 寄存器（IO 空间） */
#define REG_IDR0    0x00  /* MAC 6 字节 */
#define REG_CMD     0x37  /* 命令 */
#define REG_CFG9346 0x50  /* 9346 EEPROM 配置 */
#define REG_CONFIG1 0x51
#define REG_IMR     0x3C  /* 中断屏蔽 */
#define REG_ISR     0x3E  /* 中断状态 */
#define REG_RBSTART 0x30  /* 接收缓冲起始地址（4 字节） */
#define REG_CAPR    0x38  /* 当前接收读指针（驱动写） */
#define REG_CBR     0x3A  /* 当前缓冲写指针（硬件写；QEMU/VBox 模拟于此，
                             真实 RTL8139 在 0x34） */
#define REG_CBR_REAL 0x34
#define REG_RCR     0x44  /* 接收配置 */
#define REG_TCR     0x40  /* 发送配置 */
#define REG_TSAD0   0x20  /* 发送描述 0 */
#define REG_TSD0    0x10  /* 发送状态 0 */
#define REG_CR9346  0x50

/* 命令位（与 Linux 驱动/QEMU 一致：bit2=TX_ENABLE bit3=RX_ENABLE） */
#define CMD_RX_EN  0x08
#define CMD_TX_EN  0x04
#define CMD_RESET  0x10

#define RCR_AB     0x00000008  /* 接收广播 */
#define RCR_AM     0x00000004  /* 接收组播 */
#define RCR_APM    0x00000002  /* 接收物理 */
#define RCR_AER    0x00000001  /* 接收错误 */
#define RCR_WRAP   0x00008000

#define TX_FIFO_EMPTY 0x2000

#define RX_BUFSZ (8192 + 16)
#define TX_BUFSZ 2048

static uint16_t iobase;
static uint8_t mac[6];
static int have;
static uint32_t rxbuf_phys;
static uint32_t txbuf_phys[4];
static int tx_cur;
static uint16_t capr;

static void w8(uint16_t off, uint8_t v) { hal_outb((uint16_t)(iobase + off), v); }
static void w16(uint16_t off, uint16_t v) { hal_outw((uint16_t)(iobase + off), v); }
static void w32(uint16_t off, uint32_t v) { hal_outl((uint16_t)(iobase + off), v); }
static uint8_t r8(uint16_t off) { return hal_inb((uint16_t)(iobase + off)); }
static uint16_t r16(uint16_t off) { return hal_inw((uint16_t)(iobase + off)); }
static uint32_t r32(uint16_t off) { return hal_inl((uint16_t)(iobase + off)); }

static uint32_t dma_page(void)
{
    void *p = pmm_alloc_page_below(0x40000000);
    return p ? (uint32_t)(uintptr_t)p : 0;
}

/* 分配连续 n 页（RTL8139 环形接收缓冲必须物理连续） */
static uint32_t dma_pages(int n)
{
    for (int pass = 0; pass < 8; pass++) {
        uint32_t base = dma_page();
        if (!base)
            return 0;
        int ok = 1;
        for (int i = 1; i < n; i++) {
            uint32_t p = dma_page();
            if (p != base + (uint32_t)i * PAGE_SIZE) {
                pmm_free_page((void *)(uintptr_t)base);
                if (p)
                    pmm_free_page((void *)(uintptr_t)p);
                base = 0;
                ok = 0;
                break;
            }
        }
        if (ok)
            return base;
    }
    return 0;
}

static int rtl_init(struct pci_dev *d)
{
    if (!d) {
        d = pci_find(RTL_VENDOR, RTL_DEVICE);
        if (!d)
            return -1;
    }
    pci_enable_bus_master(d);
    iobase = (uint16_t)pci_bar_io(d, 0);
    if (!iobase)
        return -1;

    /* 复位 */
    w8(REG_CMD, CMD_RESET);
    for (int i = 0; i < 100 && (r8(REG_CMD) & CMD_RESET); i++)
        hal_timer_sleep_ms(1);

    /* 解锁 EEPROM 写 */
    w8(REG_CFG9346, 0xC0);

    /* 读 MAC */
    for (int i = 0; i < 6; i++)
        mac[i] = r8((uint16_t)(REG_IDR0 + i));

    /* 配置：总线宽度等保持默认，锁回 */
    w8(REG_CFG9346, 0x00);

    /* 分配接收缓冲（8KB+16，需 3 页物理连续） */
    rxbuf_phys = dma_pages(3);
    if (!rxbuf_phys)
        return -1;
    memset((void *)(uintptr_t)rxbuf_phys, 0, RX_BUFSZ);
    w32(REG_RBSTART, rxbuf_phys);
    capr = 0;
    w16(REG_CAPR, 0);

    /* 分配 4 个发送缓冲 */
    for (int i = 0; i < 4; i++) {
        txbuf_phys[i] = dma_page();
        if (!txbuf_phys[i])
            return -1;
        memset((void *)(uintptr_t)txbuf_phys[i], 0, 4096);
        w32((uint16_t)(REG_TSAD0 + i * 4), txbuf_phys[i]);
    }
    tx_cur = 0;

    /* 开接收/发送（写入使能位 0x03；必须在写 TxConfig/RxConfig 之前，
       QEMU 在未使能时拒绝 TxConfig 写入，否则回环位无法清除） */
    w8(REG_CMD, CMD_RX_EN | CMD_TX_EN);

    /* 发送配置：使能发送 + 清回环位（bit0 = TXEN） */
    w32(REG_TCR, 0x01u);
    /* 接收配置：使能 + 物理 + 广播 + 组播 + 环形回绕 */
    w32(REG_RCR, 0x01u | RCR_AB | RCR_AM | RCR_APM | RCR_WRAP);

    have = 1;
    serial_puts("[RTL8139] init OK mac=");
    for (int i = 0; i < 6; i++) {
        serial_puthex((uint32_t)mac[i]);
        if (i < 5)
            serial_putc(':');
    }
    serial_putc('\n');
    return 0;
}

static int rtl_send(const void *data, uint16_t len)
{
    if (!have || len > TX_BUFSZ)
        return -1;
    /* 等待当前发送槽空闲（OWN 位被硬件清除） */
    int slot = tx_cur;
    for (int i = 0; i < 10000; i++) {
        if (!(r32((uint16_t)(REG_TSD0 + slot * 4)) & 0x80000000u))
            break;
        hal_timer_sleep_ms(1);
    }
    uint8_t *dst = (uint8_t *)(uintptr_t)txbuf_phys[slot];
    memcpy(dst, data, len);
    /* 写 TSD：OWN=1 + 长度，触发发送 */
    w32((uint16_t)(REG_TSD0 + slot * 4), (uint32_t)len | 0x80000000u);
    tx_cur = (slot + 1) & 3;
    return (int)len;
}

static int rtl_recv(void *buf, uint16_t cap)
{
    if (!have)
        return 0;
    uint8_t *rx = (uint8_t *)(uintptr_t)rxbuf_phys;
    /* 硬件写指针：QEMU/VBox 模拟在 0x3A，真实 RTL8139 在 0x34。
       取两者中与 capr 不同者；若一个恒 0（模拟器未实现）则用另一个。 */
    uint16_t cur = r16(REG_CBR);       /* 0x3A 模拟器 */
    uint16_t cur_real = r16(REG_CBR_REAL); /* 0x34 真实硬件 */
    if (cur_real != capr && (cur == capr || cur == 0))
        cur = cur_real;
    if (cur == capr)
        return 0;

    /* 环形缓冲：头部 4 字节描述符（2 字节 rx_status, 2 字节 rx_len） */
    while (cur != capr) {
        uint16_t off = capr;
        uint16_t status = (uint16_t)(rx[off] | (rx[off + 1] << 8));
        uint16_t len = (uint16_t)(rx[off + 2] | (rx[off + 3] << 8));
        (void)status;
        if (len == 0 || len > 4096)
            break;
        /* 数据在 off+4 */
        uint32_t p = off + 4;
        if (p + len > RX_BUFSZ) {
            /* 跨环回绕：分两段拷 */
            uint32_t first = RX_BUFSZ - p;
            uint32_t take = len;
            uint32_t got = 0;
            if (first > 0) {
                uint32_t n = first < take ? first : take;
                if (n > cap - got)
                    n = cap - got;
                memcpy((uint8_t *)buf + got, rx + p, n);
                got += n;
                take -= n;
            }
            if (take > 0 && got < cap) {
                uint32_t n = take < (cap - got) ? take : (cap - got);
                memcpy((uint8_t *)buf + got, rx, n);
                got += n;
            }
            capr = (uint16_t)((len + 4 + 3) & ~3);
            if (capr >= RX_BUFSZ)
                capr = 0;
            w16(REG_CAPR, capr);
            return (int)got;
        }
        uint32_t got = len;
        if (got > cap)
            got = cap;
        memcpy(buf, rx + p, got);
        capr = (uint16_t)((off + len + 4 + 3) & ~3);
        if (capr >= RX_BUFSZ)
            capr = 0;
        w16(REG_CAPR, capr);
        return (int)got;
    }
    /* 失步：重置读指针 */
    capr = cur;
    w16(REG_CAPR, capr);
    return 0;
}

static void rtl_get_mac(uint8_t out[6])
{
    for (int i = 0; i < 6; i++)
        out[i] = mac[i];
}

static int rtl_present(void) { return have; }

static const struct nic_ops rtl_ops = {
    .name = "rtl8139",
    .init = rtl_init,
    .send = rtl_send,
    .recv = rtl_recv,
    .get_mac = rtl_get_mac,
    .present = rtl_present,
};

void rtl8139_register(void)
{
    nic_register(&rtl_ops);
}
