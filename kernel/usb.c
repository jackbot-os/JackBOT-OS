#include <stdint.h>
#include <stddef.h>
#include <kernel/usb.h>
#include <kernel/pci.h>
#include <kernel/hal.h>
#include <kernel/vmm.h>
#include <kernel/serial.h>

/*
 * USB 主控制器发现 + 根集线器端口状态读取。
 * 寄存器布局遵循各控制器公开规范（Intel UHCI / OHCI / EHCI-2.0 / xHCI-1.x）。
 */

/* 给 USB 控制器 MMIO 寄存器预留的内核虚拟窗口（每个控制器 1 MiB）。 */
#define USB_MMIO_VBASE 0xffff800083000000ull
#define USB_MMIO_WINDOW 0x100000u
#define USB_MMIO_MAP    0x40000u /* 实际映射 256 KiB，足够所有寄存器 */

static struct usb_hc hcs[USB_MAX_HC];
static int hc_n;
static int mmio_slot;

static volatile uint8_t *map_bar(uint32_t phys)
{
    if (phys == 0 || phys == 0xFFFFFFFFu)
        return 0;
    if (mmio_slot >= USB_MAX_HC)
        return 0;
    uint64_t vbase = USB_MMIO_VBASE + (uint64_t)mmio_slot * USB_MMIO_WINDOW;
    mmio_slot++;
    for (uint32_t off = 0; off < USB_MMIO_MAP; off += 4096)
        vmm_map_page(vbase + off, (uint64_t)phys + off,
                     PTE_PRESENT | PTE_WRITABLE | PTE_PCD | PTE_PWT);
    return (volatile uint8_t *)vbase;
}

static void set_speed(struct usb_hc *h, int p, int spd)
{
    if (p >= 0 && p < USB_MAX_PORTS)
        h->speed[p] = (uint8_t)spd;
}

/* UHCI：I/O 寄存器；根集线器端口状态从 IO+0x10 起，每个 2 字节，ICH 为 2 口 */
static void read_uhci(struct usb_hc *h, const struct pci_dev *d)
{
    h->io_base = pci_bar_io(d, 4);
    if (!h->io_base || h->io_base == 0xFFFCu)
        return;
    h->nports = 2;
    for (int i = 0; i < h->nports; i++) {
        uint16_t sc = hal_inw(h->io_base + 0x10 + (uint16_t)(i * 2));
        h->conn[i] = sc & 0x0001 ? 1 : 0;
        h->pwr[i] = sc & 0x1000 ? 1 : 0;   /* PPS Port Power */
        h->oc[i] = sc & 0x0020 ? 1 : 0;    /* OCA Over-Current Active */
        if (h->conn[i])
            set_speed(h, i, sc & 0x0100 ? USB_SPD_LOW : USB_SPD_FULL);
        else
            set_speed(h, i, USB_SPD_NONE);
    }
}

/* OHCI：MMIO；HcRhDescriptorA@0x48 给端口数，HcRhPortStatus@0x54+4i */
static void read_ohci(struct usb_hc *h, const struct pci_dev *d)
{
    h->mem_base = pci_bar_mem(d, 0);
    volatile uint8_t *b = map_bar(h->mem_base);
    if (!b)
        return;
    volatile uint32_t *r = (volatile uint32_t *)b;
    uint32_t rhda = r[0x48 / 4];
    int n = (int)((rhda >> 17) & 0x7F);
    if (n < 1) n = 1;
    if (n > USB_MAX_PORTS) n = USB_MAX_PORTS;
    h->nports = (uint8_t)n;
    for (int i = 0; i < n; i++) {
        uint32_t ps = r[(0x54 + i * 4) / 4];
        h->conn[i] = ps & (1u << 0) ? 1 : 0;
        h->pwr[i] = ps & (1u << 8) ? 1 : 0;   /* PPS */
        h->oc[i] = ps & (1u << 5) ? 1 : 0;
        if (h->conn[i])
            set_speed(h, i, ps & (1u << 17) ? USB_SPD_LOW : USB_SPD_FULL);
        else
            set_speed(h, i, USB_SPD_NONE);
    }
}

/* EHCI：MMIO；CAPLENGTH@0，HCCPARAMS@4 给端口数，PORTSC@op+0x44+4i */
static void read_ehci(struct usb_hc *h, const struct pci_dev *d)
{
    h->mem_base = pci_bar_mem(d, 0);
    volatile uint8_t *b = map_bar(h->mem_base);
    if (!b)
        return;
    uint8_t cap = hal_read8(b);
    uint32_t hcc = hal_read32(b + 4);
    volatile uint32_t *op = (volatile uint32_t *)(b + cap);
    int n = (int)(hcc & 0x0F);
    if (n < 1) n = 1;
    if (n > USB_MAX_PORTS) n = USB_MAX_PORTS;
    h->nports = (uint8_t)n;
    for (int i = 0; i < n; i++) {
        uint32_t ps = op[(0x44 + i * 4) / 4];
        h->conn[i] = ps & (1u << 0) ? 1 : 0;
        h->pwr[i] = ps & (1u << 12) ? 1 : 0;  /* PP */
        h->oc[i] = ps & (1u << 4) ? 1 : 0;
        if (h->conn[i]) {
            uint32_t line = (ps >> 10) & 0x3; /* LineStatus: 01 low 10 full 00 high */
            int spd = USB_SPD_HIGH;
            if (line == 1) spd = USB_SPD_LOW;
            else if (line == 2) spd = USB_SPD_FULL;
            set_speed(h, i, spd);
        } else {
            set_speed(h, i, USB_SPD_NONE);
        }
    }
}

/* xHCI：MMIO；CAPLENGTH@0，HCSPARAMS1@4 给 MaxPorts，PORTSC@op+0x400+0x10i */
static void read_xhci(struct usb_hc *h, const struct pci_dev *d)
{
    h->mem_base = pci_bar_mem(d, 0);
    volatile uint8_t *b = map_bar(h->mem_base);
    if (!b)
        return;
    uint8_t cap = hal_read8(b);
    uint32_t hcs1 = hal_read32(b + 4);
    volatile uint8_t *op = b + cap;
    int n = (int)((hcs1 >> 24) & 0xFF);
    if (n < 1) n = 1;
    if (n > USB_MAX_PORTS) n = USB_MAX_PORTS;
    h->nports = (uint8_t)n;
    for (int i = 0; i < n; i++) {
        volatile uint32_t *psc =
            (volatile uint32_t *)(op + 0x400 + i * 0x10);
        uint32_t ps = hal_read32((volatile const void *)psc);
        h->conn[i] = ps & (1u << 0) ? 1 : 0;
        h->pwr[i] = ps & (1u << 9) ? 1 : 0;   /* PP */
        h->oc[i] = ps & (1u << 3) ? 1 : 0;    /* OCA */
        if (h->conn[i]) {
            uint32_t sid = (ps >> 10) & 0xF; /* Port Speed ID */
            int spd = USB_SPD_FULL;
            switch (sid) {
            case 1: spd = USB_SPD_FULL; break;  /* Full Speed ID */
            case 2: spd = USB_SPD_LOW; break;   /* Low Speed ID */
            case 3: spd = USB_SPD_HIGH; break;  /* High Speed ID */
            case 4: case 5: spd = USB_SPD_SUPER; break; /* SS / SS+ */
            default: spd = USB_SPD_FULL; break;
            }
            set_speed(h, i, spd);
        } else {
            set_speed(h, i, USB_SPD_NONE);
        }
    }
}

static int kind_of(const struct pci_dev *d)
{
    if (d->class_code == 0x0C && d->subclass == 0x03) {
        switch (d->prog_if) {
        case 0x00: return USB_KIND_UHCI;
        case 0x10: return USB_KIND_OHCI;
        case 0x20: return USB_KIND_EHCI;
        case 0x30: return USB_KIND_XHCI;
        default: return USB_KIND_UNKNOWN;
        }
    }
    return -1;
}

int usb_scan(void)
{
    hc_n = 0;
    mmio_slot = 0;
    int nd = pci_count();
    for (int i = 0; i < nd && hc_n < USB_MAX_HC; i++) {
        struct pci_dev *d = pci_get(i);
        if (!d)
            continue;
        int kind = kind_of(d);
        if (kind < 0)
            continue;
        struct usb_hc *h = &hcs[hc_n];
        for (uint8_t *q = (uint8_t *)h; q < (uint8_t *)(h + 1);)
            *q++ = 0;
        h->present = 1;
        h->bus = d->bus; h->dev = d->dev; h->func = d->func;
        h->kind = (uint8_t)kind;
        h->vendor = d->vendor; h->device = d->device;
        h->irq = d->irq_line;
        switch (kind) {
        case USB_KIND_UHCI: read_uhci(h, d); break;
        case USB_KIND_OHCI: read_ohci(h, d); break;
        case USB_KIND_EHCI: read_ehci(h, d); break;
        case USB_KIND_XHCI: read_xhci(h, d); break;
        default: break;
        }
        serial_puts("[USB] hc");
        serial_putc('0' + (char)hc_n);
        serial_puts(" kind=");
        serial_puts(usb_kind_name(kind));
        serial_puts(" ports=");
        serial_puthex((uint32_t)h->nports);
        serial_putc('\n');
        hc_n++;
    }
    return hc_n;
}

int usb_hc_count(void) { return hc_n; }

const struct usb_hc *usb_hc_get(int idx)
{
    if (idx < 0 || idx >= hc_n)
        return 0;
    return &hcs[idx];
}

int usb_total_connected(void)
{
    int n = 0;
    for (int i = 0; i < hc_n; i++)
        for (int p = 0; p < hcs[i].nports; p++)
            if (hcs[i].conn[p])
                n++;
    return n;
}

const char *usb_kind_name(int kind)
{
    switch (kind) {
    case USB_KIND_UHCI: return "UHCI";
    case USB_KIND_OHCI: return "OHCI";
    case USB_KIND_EHCI: return "EHCI";
    case USB_KIND_XHCI: return "xHCI";
    default: return "USB";
    }
}

const char *usb_kind_full_name(int kind)
{
    switch (kind) {
    case USB_KIND_UHCI: return "UHCI  USB 1.1 主控";
    case USB_KIND_OHCI: return "OHCI  USB 1.1 主控";
    case USB_KIND_EHCI: return "EHCI  USB 2.0 主控";
    case USB_KIND_XHCI: return "xHCI  USB 3.x 主控";
    default: return "USB 主控（接口未知）";
    }
}

const char *usb_speed_name(int s)
{
    switch (s) {
    case USB_SPD_LOW: return "低速 1.5M";
    case USB_SPD_FULL: return "全速 12M";
    case USB_SPD_HIGH: return "高速 480M";
    case USB_SPD_SUPER: return "超高速 5G";
    default: return "空";
    }
}
