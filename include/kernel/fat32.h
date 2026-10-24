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

/* ---- 目录列举 / 文件移动（文件管理器 + 回收站基础能力）---- */

#define FAT_DENT_NAME_MAX 64

struct fat_dirent {
    char name[FAT_DENT_NAME_MAX]; /* 显示名：优先 LFN 长名，否则 8.3 短名 */
    char short83[13];             /* 8.3 短名，用于打开/删除/移动 */
    uint32_t size;                /* 文件字节数（目录为 0） */
    uint32_t cluster;             /* 首簇号 */
    uint8_t is_dir;               /* 1=目录 0=文件 */
};

/* 列举目录内容。dir83 传 "" 或 "/" 表示根目录；返回条目数，<0 出错。
   自动跳过 "."/".." 与卷标，按磁盘顺序返回，解析 LFN 还原长名。 */
int fat_list_dir(const char *dir83, struct fat_dirent *out, int max);

/* 移动/重命名文件（读全部数据 -> 写入目标 -> 删除源）。
   受 protect 权限约束；大小上限 8 MiB（回收站面向用户文档）。 */
int fat_move(const char *src83, const char *dst83);

/* 判断某路径是否为已存在的目录。 */
int fat_dir_exists(const char *path83);

#endif
