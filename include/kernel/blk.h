#ifndef KERNEL_BLK_H
#define KERNEL_BLK_H

#include <stdint.h>

/* Block device abstraction for the single system disk.
   Picks the backend once: ATA (legacy PIO) if present, else AHCI. */

int blk_init(void);
int blk_present(void);
uint64_t blk_sectors(void);
const char *blk_backend_name(void);
int blk_port(void);

int blk_read(uint64_t lba, uint32_t count, void *buf);
int blk_write(uint64_t lba, uint32_t count, const void *buf);
int blk_flush(void);

/* Reader shaped for part_scan_reader(). */
int blk_read_one(uint64_t lba, uint8_t *out512);

#endif
