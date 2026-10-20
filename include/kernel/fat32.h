#ifndef KERNEL_FAT32_H
#define KERNEL_FAT32_H
#include <stdint.h>

int fat_mount(void);
int fat_mount_part(uint32_t offset, uint32_t part_sectors);
void fat_unmount(void);
int fat_mounted(void);
uint32_t fat_part_offset(void);
int fat_exists(const char *name83);
int fat_file_size(const char *name83, uint32_t *size);
int fat_write_file(const char *name83, const void *data, uint32_t len);
int fat_write_file_lfn(const char *dir83, const char *base,
                       const void *data, uint32_t len);
int fat_read_file(const char *name83, void *buf, uint32_t max, uint32_t *outlen);
int fat_delete(const char *name83);
int fat_delete_many(const char **names83, int n);
int fat_mkdir(const char *name83);
uint32_t fat_free_bytes(void);
uint32_t fat_cluster_size(void);
uint32_t fat_total_bytes(void);
void fat_debug_dump_dir(const char *dir83);

#endif
