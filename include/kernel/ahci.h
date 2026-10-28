#ifndef KERNEL_AHCI_H
#define KERNEL_AHCI_H

#include <stdint.h>

struct pci_dev;

int ahci_init(struct pci_dev *d);
int ahci_present(void);
int ahci_online_ports(void);

int ahci_read_sector(int port, uint64_t lba, uint8_t count, void *buf);
int ahci_write_sector(int port, uint64_t lba, uint8_t count, const void *buf);

#endif
