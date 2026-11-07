#ifndef JACKBOT_GPU_H
#define JACKBOT_GPU_H

#include <stdint.h>
#include <kernel/pci.h>

#define GPU_MAX_DEVS 8

struct gpu_dev {
    int pci_index;
    struct pci_dev *pci;
    uint16_t vendor;
    uint16_t device;
    uint8_t rev;
    uint64_t bar0_phys;
    uint32_t bar0_size;
    uint64_t vbase;
    uint32_t pages;
    const char *vendor_name;
    const char *chip_name;
    int bar0_mapped;
};

void gpu_probe_all(void);
int gpu_count(void);
struct gpu_dev *gpu_get(int idx);

uint64_t gpu_bar_addr(const struct pci_dev *d, int idx);
uint32_t gpu_bar_size(const struct pci_dev *d, int idx);
uint32_t gpu_mmio_read(uint64_t vbase, uint32_t off);
int gpu_map_mmio(uint64_t vbase, uint64_t phys, uint32_t pages);
void gpu_puthex32(uint32_t v);

int gpu_nv_attach(struct gpu_dev *g);
int gpu_intel_attach(struct gpu_dev *g);
int gpu_amd_attach(struct gpu_dev *g);

#endif
