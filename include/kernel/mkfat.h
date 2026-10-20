#ifndef KERNEL_MKFAT_H
#define KERNEL_MKFAT_H
#include <stdint.h>

int mkfat_format(uint32_t start_lba, uint32_t total_sectors,
                 const char *label);

#endif
