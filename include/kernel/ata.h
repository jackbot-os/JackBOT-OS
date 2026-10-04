#ifndef KERNEL_ATA_H
#define KERNEL_ATA_H
#include <stdint.h>

int ata_init(void);
int ata_present(void);
uint64_t ata_sector_count(void);
int ata_read(uint64_t lba, uint8_t count, void *buf);
int ata_write(uint64_t lba, uint8_t count, const void *buf);
int ata_flush_cache(void);

#endif
