#include <stdint.h>
#include <stddef.h>
#include <kernel/pci.h>
#include <kernel/serial.h>

#define CONFIG_ADDR 0xCF8u
#define CONFIG_DATA 0xCFCu

static struct pci_dev g_devs[PCI_MAX_DEVS];
static int g_count;

static uint32_t make_addr(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off)
{
    return ((uint32_t)1 << 31) | ((uint32_t)bus << 16)
         | ((uint32_t)dev << 11) | ((uint32_t)func << 8)
         | ((uint32_t)off & 0xFCu);
}

static void outl(uint16_t port, uint32_t val)
{
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static uint32_t inl(uint16_t port)
{
    uint32_t val;
    __asm__ volatile("inl %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

uint32_t pci_cfg_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off)
{
    outl(CONFIG_ADDR, make_addr(bus, dev, func, off));
    return inl(CONFIG_DATA);
}

uint16_t pci_cfg_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off)
{
    uint32_t v = pci_cfg_read32(bus, dev, func, off);
    return (uint16_t)((v >> ((off & 2) * 8)) & 0xFFFF);
}

uint8_t pci_cfg_read8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off)
{
    uint32_t v = pci_cfg_read32(bus, dev, func, off);
    return (uint8_t)((v >> ((off & 3) * 8)) & 0xFF);
}

void pci_cfg_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off,
                     uint32_t val)
{
    outl(CONFIG_ADDR, make_addr(bus, dev, func, off));
    outl(CONFIG_DATA, val);
}

void pci_cfg_write16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off,
                     uint16_t val)
{
    uint32_t v = pci_cfg_read32(bus, dev, func, off);
    int sh = (off & 2) * 8;
    v = (v & ~(0xFFFFu << sh)) | ((uint32_t)val << sh);
    pci_cfg_write32(bus, dev, func, off, v);
}

void pci_cfg_write8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off,
                    uint8_t val)
{
    uint32_t v = pci_cfg_read32(bus, dev, func, off);
    int sh = (off & 3) * 8;
    v = (v & ~(0xFFu << sh)) | ((uint32_t)val << sh);
    pci_cfg_write32(bus, dev, func, off, v);
}

static void enable_device(uint8_t bus, uint8_t dev, uint8_t func)
{
    uint16_t cmd = pci_cfg_read16(bus, dev, func, 4);
    cmd |= 0x7;
    pci_cfg_write16(bus, dev, func, 4, cmd);
}

void pci_enumerate(void)
{
    g_count = 0;
    for (int dev = 0; dev < 32 && g_count < PCI_MAX_DEVS; dev++) {
        if ((uint16_t)pci_cfg_read32(0, dev, 0, 0) == 0xFFFF)
            continue;
        uint8_t hdr = pci_cfg_read8(0, dev, 0, 0x0E);
        int funcs = (hdr & 0x80) ? 8 : 1;
        for (int func = 0; func < funcs && g_count < PCI_MAX_DEVS; func++) {
            uint32_t id = pci_cfg_read32(0, dev, func, 0);
            if ((uint16_t)id == 0xFFFF)
                continue;
            struct pci_dev *d = &g_devs[g_count];
            d->bus = 0; d->dev = (uint8_t)dev; d->func = (uint8_t)func;
            d->vendor = (uint16_t)id;
            d->device = (uint16_t)(id >> 16);
            uint32_t ccrev = pci_cfg_read32(0, dev, func, 8);
            d->rev = (uint8_t)ccrev;
            d->prog_if = (uint8_t)(ccrev >> 8);
            d->subclass = (uint8_t)(ccrev >> 16);
            d->class_code = (uint8_t)(ccrev >> 24);
            for (int b = 0; b < 6; b++)
                d->bar[b] = pci_cfg_read32(0, dev, func, 0x10 + b * 4);
            d->irq_line = pci_cfg_read8(0, dev, func, 0x3C);
            enable_device(0, dev, func);
            g_count++;
        }
    }

    serial_puts("[PCI] devices=");
    serial_puthex((uint32_t)g_count);
    serial_putc('\n');
    for (int i = 0; i < g_count; i++) {
        struct pci_dev *d = &g_devs[i];
        serial_puts("  ");
        serial_puthex(d->vendor); serial_putc(':'); serial_puthex(d->device);
        serial_puts(" class="); serial_puthex(d->class_code);
        serial_putc('/'); serial_puthex(d->subclass);
        serial_puts(" irq="); serial_puthex(d->irq_line);
        serial_putc('\n');
    }
}

struct pci_dev *pci_find(uint16_t vendor, uint16_t device)
{
    for (int i = 0; i < g_count; i++)
        if (g_devs[i].vendor == vendor && g_devs[i].device == device)
            return &g_devs[i];
    return 0;
}

struct pci_dev *pci_find_class(uint8_t cls, uint8_t sub)
{
    for (int i = 0; i < g_count; i++)
        if (g_devs[i].class_code == cls && g_devs[i].subclass == sub)
            return &g_devs[i];
    return 0;
}

uint32_t pci_bar_mem(const struct pci_dev *d, int idx)
{
    return d->bar[idx] & 0xFFFFFFF0u;
}

uint32_t pci_bar_io(const struct pci_dev *d, int idx)
{
    return d->bar[idx] & 0xFFFFFFFCu;
}

void pci_enable_bus_master(struct pci_dev *d)
{
    uint16_t cmd = pci_cfg_read16(d->bus, d->dev, d->func, 4);
    cmd |= 0x4;
    pci_cfg_write16(d->bus, d->dev, d->func, 4, cmd);
}

int pci_count(void) { return g_count; }
struct pci_dev *pci_get(int idx)
{
    return (idx >= 0 && idx < g_count) ? &g_devs[idx] : 0;
}
