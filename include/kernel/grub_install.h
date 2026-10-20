#ifndef KERNEL_GRUB_INSTALL_H
#define KERNEL_GRUB_INSTALL_H
#include <stdint.h>
#include <kernel/part.h>

#define GRUB_CORE_LBA 1u
#define GRUB_EMBED_SECTORS 2047u

int grub_write_core(uint32_t lba, const void *core, uint32_t bytes);
int grub_install_boot_disk(const uint8_t *bootimg,
                           const void *core, uint32_t core_bytes,
                           const struct part_layout *pl);

#endif
