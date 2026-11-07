#include <stdint.h>
#include <kernel/gpu_intel.h>
#include <kernel/pci.h>
#include <kernel/serial.h>

/* JackBOT OS original work. Aperture layout knowledge comes from public
   PCI/Intel platform documentation; no GPL/Linux-derived code. */

#define INTEL_VBASE 0xffff800085000000ull

int gpu_intel_attach(struct gpu_dev *g)
{
    uint64_t gttbar, mmiobar;
    uint32_t gttsize, mmiosize;
    uint16_t subsys;

    /* BAR0 is the GTT/MMIO aperture on Intel integrated graphics, BAR2 the
       register MMIO window. Sizes are measured from the BAR itself, not
       assumed. */
    gttbar = gpu_bar_addr(g->pci, 0);
    gttsize = gpu_bar_size(g->pci, 0);
    mmiobar = gpu_bar_addr(g->pci, 2);
    mmiosize = gpu_bar_size(g->pci, 2);
    subsys = pci_cfg_read16(g->pci->bus, g->pci->dev, g->pci->func, 0x2C);

    g->bar0_phys = gttbar;
    g->bar0_size = gttsize;

    serial_puts("[IGPU] GTTMMADR=");
    serial_puthex(gttbar);
    serial_puts(" size=");
    gpu_puthex32(gttsize);
    serial_putc('\n');
    serial_puts("[IGPU] MMIO BAR2=");
    serial_puthex(mmiobar);
    serial_puts(" size=");
    gpu_puthex32(mmiosize);
    serial_puts(" subsystem=");
    gpu_puthex32(subsys);
    serial_putc('\n');

    if (!gttbar || !gttsize) {
        serial_puts("[IGPU] no usable MMIO aperture, stop here\n");
        return 0;
    }

    serial_puts("[IGPU] register-level init needs the Intel PRM; the public "
                "PRM links are offline, so no register offsets are used "
                "here rather than guessed ones\n");
    return 0;
}
