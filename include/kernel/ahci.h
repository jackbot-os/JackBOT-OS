#ifndef KERNEL_AHCI_H
#define KERNEL_AHCI_H

#include <stdint.h>

struct pci_dev;

/* Initialise one AHCI controller (PCI class 0x01 / subclass 0x06). */
int ahci_init(struct pci_dev *d);

/* Scan PCI for AHCI controllers and initialise every one found.
   Returns the number of controllers brought up. */
int ahci_probe(void);

int ahci_present(void);
int ahci_online_ports(void);
int ahci_primary_port(void);

/* Per-port information gathered from IDENTIFY DEVICE. */
int ahci_port_sectors(int port, uint64_t *sectors);
const char *ahci_port_model(int port);
int ahci_port_lba48(int port);

/* LBA48 transfers through READ/WRITE DMA EXT (512-byte sectors). */
int ahci_read_sector(int port, uint64_t lba, uint32_t count, void *buf);
int ahci_write_sector(int port, uint64_t lba, uint32_t count, const void *buf);
int ahci_flush(int port);

/* Read-only boot check: verify the 0x55AA signature at LBA0. */
int ahci_lba0_signature(int port);

#endif

