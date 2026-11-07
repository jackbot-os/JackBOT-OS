#include <stdint.h>
#include <kernel/gpu_amd.h>
#include <kernel/pci.h>
#include <kernel/serial.h>

/* JackBOT OS original work. Aperture layout knowledge comes from the public
   AMD GPU architecture documentation; no GPL/Linux-derived code. */

#define AMD_VBASE 0xffff800086000000ull

int gpu_amd_attach(struct gpu_dev *g)
{
    uint64_t bar0, bar2, bar5;
    uint32_t s0, s2, s5;
    uint16_t subsys;

    bar0 = gpu_bar_addr(g->pci, 0);
    s0 = gpu_bar_size(g->pci, 0);
    bar2 = gpu_bar_addr(g->pci, 2);
    s2 = gpu_bar_size(g->pci, 2);
    bar5 = gpu_bar_addr(g->pci, 5);
    s5 = gpu_bar_size(g->pci, 5);
    subsys = pci_cfg_read16(g->pci->bus, g->pci->dev, g->pci->func, 0x2C);

    g->bar0_phys = bar0;
    g->bar0_size = s0;

    serial_puts("[AMD] BAR0=");
    serial_puthex(bar0);
    serial_puts(" size=");
    gpu_puthex32(s0);
    serial_puts(" BAR2=");
    serial_puthex(bar2);
    serial_puts(" size=");
    gpu_puthex32(s2);
    serial_puts(" BAR5=");
    serial_puthex(bar5);
    serial_puts(" size=");
    gpu_puthex32(s5);
    serial_puts(" subsystem=");
    gpu_puthex32(subsys);
    serial_putc('\n');

    if (!bar0 || !s0) {
        serial_puts("[AMD] no register aperture, stop here\n");
        return 0;
    }

    serial_puts("[AMD] RDNA ISA documents are public, register-level "
                "programming is not; aperture measured, no guessed offsets\n");
    return 0;
}
