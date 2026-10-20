#ifndef JACKBOT_PCI_H
#define JACKBOT_PCI_H

#include <stdint.h>
#include <stddef.h>

#define PCI_MAX_DEVS 32

struct pci_dev {
    uint8_t bus, dev, func;
    uint16_t vendor, device;
    uint8_t class_code, subclass, prog_if, rev;
    uint8_t irq_line;
    uint32_t bar[6];
};

uint32_t pci_cfg_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off);
uint16_t pci_cfg_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off);
uint8_t pci_cfg_read8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off);
void pci_cfg_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off,
                     uint32_t v);
void pci_cfg_write16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off,
                     uint16_t v);
void pci_cfg_write8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off,
                    uint8_t v);
void pci_enumerate(void);
struct pci_dev *pci_find(uint16_t vendor, uint16_t device);
struct pci_dev *pci_find_class(uint8_t cls, uint8_t sub);
uint32_t pci_bar_mem(const struct pci_dev *d, int idx);
uint32_t pci_bar_io(const struct pci_dev *d, int idx);
void pci_enable_bus_master(struct pci_dev *d);
int pci_count(void);
struct pci_dev *pci_get(int idx);

#endif
