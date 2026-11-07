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

/* ---- 鐩綍鍒椾妇 / 鏂囦欢绉诲姩锛堟枃浠剁鐞嗗櫒 + 鍥炴敹绔欏熀纭€鑳藉姏锛?--- */

#define FAT_DENT_NAME_MAX 64

struct fat_dirent {
    char name[FAT_DENT_NAME_MAX]; /* 鏄剧ず鍚嶏細浼樺厛 LFN 闀垮悕锛屽惁鍒?8.3 鐭悕 */
    char short83[13];             /* 8.3 鐭悕锛岀敤浜庢墦寮€/鍒犻櫎/绉诲姩 */
    uint32_t size;                /* 鏂囦欢瀛楄妭鏁帮紙鐩綍涓?0锛?*/
    uint32_t cluster;             /* 棣栫皣鍙?*/
    uint8_t is_dir;               /* 1=鐩綍 0=鏂囦欢 */
};

/* 鍒椾妇鐩綍鍐呭銆俤ir83 浼?"" 鎴?"/" 琛ㄧず鏍圭洰褰曪紱杩斿洖鏉＄洰鏁帮紝<0 鍑洪敊銆?   鑷姩璺宠繃 "."/".." 涓庡嵎鏍囷紝鎸夌鐩橀『搴忚繑鍥烇紝瑙ｆ瀽 LFN 杩樺師闀垮悕銆?*/
int fat_list_dir(const char *dir83, struct fat_dirent *out, int max);

/* 绉诲姩/閲嶅懡鍚嶆枃浠讹紙璇诲叏閮ㄦ暟鎹?-> 鍐欏叆鐩爣 -> 鍒犻櫎婧愶級銆?   鍙?protect 鏉冮檺绾︽潫锛涘ぇ灏忎笂闄?8 MiB锛堝洖鏀剁珯闈㈠悜鐢ㄦ埛鏂囨。锛夈€?*/
int fat_move(const char *src83, const char *dst83);

/* 鍒ゆ柇鏌愯矾寰勬槸鍚︿负宸插瓨鍦ㄧ殑鐩綍銆?*/
int fat_dir_exists(const char *path83);

/* ---- POSIX-style file handles (used by the syscall layer) ---------- */
#define FAT_FH_MAX 8
#define FAT_SEEK_SET 0
#define FAT_SEEK_CUR 1
#define FAT_SEEK_END 2

/* Open a file by path/8.3 name. writable=0 read-only, 1 allows overwrite of
   existing bytes. Returns a handle >= 0, or -1. */
int fat_open_h(const char *name, int writable);
/* Read/write at the handle position; write never extends the file. */
int fat_read_h(int h, void *buf, uint32_t len);
int fat_write_h(int h, const void *buf, uint32_t len);
/* Reposition; returns the new absolute offset or -1. */
int fat_lseek_h(int h, int32_t off, int whence);
int fat_close_h(int h);
/* size/isdir for a path (no handle needed). */
int fat_stat_h(const char *name, uint32_t *size, int *is_dir);

#endif

