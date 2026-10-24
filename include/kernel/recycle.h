#ifndef JACKBOT_RECYCLE_H
#define JACKBOT_RECYCLE_H
/*
 * JackBOT OS 系统回收站
 * 基于 FAT32 的 \RECYCLED 目录：删除文件 = 移动到 \RECYCLED\Rxxxx.EXT，
 * 并在索引文件 \RECYCLED\INFO.JBT 记录“存储短名|原始路径|大小”；
 * 支持还原（移回原位置）、清空、列举。不做物理粉碎，可恢复。
 * 受底层移动能力限制，单文件上限 8 MiB，且核心受保护文件拒绝删除。
 */
#include <stdint.h>
#include <kernel/fat32.h>

#define RECYCLE_MAX 64
#define RECYCLE_DIR   "RECYCLED"
#define RECYCLE_INDEX "RECYCLED\\INFO.JBT"

struct recycle_item {
    int used;
    char store[20];                    /* 回收站里的数据短名 Rxxxx.EXT */
    char orig[FAT_DENT_NAME_MAX];      /* 原始路径（短名） */
    uint32_t size;
};

/* 挂载后调用，确保回收站目录存在。返回 0 成功。 */
int recycle_init(void);
/* 把文件送入回收站（路径为 8.3 短名）。返回 0 成功。 */
int recycle_delete(const char *path83);
/* 列举回收站内容，返回条目数。 */
int recycle_list(struct recycle_item *out, int max);
/* 还原第 idx 项（idx 来自 recycle_list 的下标）。 */
int recycle_restore(int idx);
/* 清空回收站。 */
int recycle_empty(void);
/* 回收站条目数。 */
int recycle_count(void);

#endif
