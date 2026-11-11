#ifndef KERNEL_FSPROBE_H
#define KERNEL_FSPROBE_H
#include <stdint.h>
#include <kernel/part.h>

#define FSP_KIND_MAX 12

struct fsp_entry {
    struct part_info part;
    char kind[FSP_KIND_MAX];   /* detected filesystem name, "?" if unknown */
    int  mountable;            /* 1 = this kernel can actually mount it */
};

/* Scan the disk's partition table and identify the filesystem inside every
   partition by reading its boot sector. Returns the number of entries, or
   a negative value when no block device is present. */
int fsp_scan(struct fsp_entry *out, int max);

/* Identify one partition's filesystem without scanning the table again. */
int fsp_part_kind(uint32_t start_lba, char *kind, int cap);

#endif
