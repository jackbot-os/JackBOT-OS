#include <stdint.h>
#include <kernel/gpu.h>
#include <kernel/pci.h>
#include <kernel/vmm.h>
#include <kernel/serial.h>

/* JackBOT OS original work. Register knowledge comes from public
   documentation (docs/gpu/source-map.md); no GPL/Linux-derived code. */

#define GPU_VENDOR_NV    0x10DE
#define GPU_VENDOR_INTEL 0x8086
#define GPU_VENDOR_AMD   0x1002
#define GPU_VENDOR_VMW   0x15AD

static struct gpu_dev g_devs[GPU_MAX_DEVS];
static int g_count;

static const char *gpu_vendor_name(uint16_t v)
{
    switch (v) {
    case GPU_VENDOR_NV:    return "NVIDIA";
    case GPU_VENDOR_INTEL: return "Intel";
    case GPU_VENDOR_AMD:   return "AMD";
    case GPU_VENDOR_VMW:   return "VMware";
    default:               return "unknown";
    }
}

static uint32_t cfg32(const struct pci_dev *d, uint8_t off)
{
    return pci_cfg_read32(d->bus, d->dev, d->func, off);
}

static void cfg16w(const struct pci_dev *d, uint8_t off, uint16_t v)
{
    pci_cfg_write16(d->bus, d->dev, d->func, off, v);
}

uint64_t gpu_bar_addr(const struct pci_dev *d, int idx)
{
    uint32_t lo = cfg32(d, (uint8_t)(0x10 + idx * 4));
    uint64_t hi = 0;

    if ((lo & 0x6u) == 0x4u)
        hi = cfg32(d, (uint8_t)(0x10 + (idx + 1) * 4));
    return (uint64_t)(lo & 0xFFFFFFF0u) | (hi << 32);
}

uint32_t gpu_bar_size(const struct pci_dev *d, int idx)
{
    uint8_t off = (uint8_t)(0x10 + idx * 4);
    uint16_t cmd = pci_cfg_read16(d->bus, d->dev, d->func, 0x04);
    uint32_t orig, mask, size;
    int decode = (cmd & 0x2) != 0;

    if (decode)
        cfg16w(d, 0x04, (uint16_t)(cmd & ~0x2u));

    orig = cfg32(d, off);
    pci_cfg_write32(d->bus, d->dev, d->func, off, 0xFFFFFFFFu);
    mask = cfg32(d, off);
    pci_cfg_write32(d->bus, d->dev, d->func, off, orig);

    if (decode)
        cfg16w(d, 0x04, cmd);

    if (!(orig & 0x1u) || (mask & 0x1u))
        return 0;

    mask &= ~0xFu;
    if (!mask)
        return 0;
    size = ~mask + 1u;
    return size;
}

int gpu_map_mmio(uint64_t vbase, uint64_t phys, uint32_t pages)
{
    if (!phys || !pages)
        return 0;
    for (uint32_t i = 0; i < pages; i++)
        vmm_map_page(vbase + (uint64_t)i * 4096ull,
                     phys + (uint64_t)i * 4096ull,
                     PTE_PRESENT | PTE_WRITABLE | PTE_PCD | PTE_PWT);
    return 1;
}

uint32_t gpu_mmio_read(uint64_t vbase, uint32_t off)
{
    return *(volatile uint32_t *)(vbase + off);
}

void gpu_puthex32(uint32_t v)
{
    static const char digits[] = "0123456789abcdef";
    int started = 0;

    serial_puts("0x");
    for (int i = 28; i >= 0; i -= 4) {
        uint32_t nib = (v >> i) & 0xFu;
        if (nib || started || i == 0) {
            serial_putc(digits[nib]);
            started = 1;
        }
    }
}

void gpu_probe_all(void)
{
    g_count = 0;
    if (pci_count() <= 0)
        pci_enumerate();

    for (int i = 0; i < pci_count() && g_count < GPU_MAX_DEVS; i++) {
        struct pci_dev *d = pci_get(i);
        struct gpu_dev *g;

        if (!d || d->class_code != 0x03)
            continue;

        g = &g_devs[g_count];
        g->pci_index = i;
        g->pci = d;
        g->vendor = d->vendor;
        g->device = d->device;
        g->rev = d->rev;
        g->bar0_phys = 0;
        g->bar0_size = 0;
        g->vbase = 0;
        g->pages = 0;
        g->bar0_mapped = 0;
        g->vendor_name = gpu_vendor_name(d->vendor);
        g->chip_name = "";

        serial_puts("[GPU] dev ");
        gpu_puthex32((uint32_t)i);
        serial_puts(" ");
        gpu_puthex32(d->vendor);
        serial_putc(':');
        gpu_puthex32(d->device);
        serial_puts(" class=0x03 sub=");
        gpu_puthex32(d->subclass);
        serial_puts(" rev=");
        gpu_puthex32(d->rev);
        serial_puts(" (");
        serial_puts(g->vendor_name);
        serial_puts(")\n");

        switch (d->vendor) {
        case GPU_VENDOR_NV:
            gpu_nv_attach(g);
            break;
        case GPU_VENDOR_INTEL:
            gpu_intel_attach(g);
            break;
        case GPU_VENDOR_AMD:
            gpu_amd_attach(g);
            break;
        default:
            serial_puts("[GPU] no driver for this vendor, identification only\n");
            break;
        }
        g_count++;
    }

    if (!g_count)
        serial_puts("[GPU] no display-class device found\n");
}

int gpu_count(void)
{
    return g_count;
}

struct gpu_dev *gpu_get(int idx)
{
    if (idx < 0 || idx >= g_count)
        return 0;
    return &g_devs[idx];
}
