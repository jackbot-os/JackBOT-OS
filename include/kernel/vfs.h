#ifndef KERNEL_VFS_H
#define KERNEL_VFS_H

#include <stdint.h>

/* A small, real VFS: mount table + per-mount operation vtable + a kernel
   file-descriptor table. The point is that every consumer (syscalls,
   file manager, loader) asks the VFS instead of calling a filesystem
   driver directly, so a second filesystem can be plugged in later without
   touching them.

   Today the only backend is FAT32 (kernel/vfs.c fat_ops), mounted at "/".
   exFAT/NTFS/JBFS become new vfs_ops structs; nothing above them changes. */

#define VFS_NAME_MAX 64
#define VFS_MAX_MOUNTS 8
#define VFS_MAX_OPEN 24
#define VFS_FD_BASE 3

struct vfs_dirent {
    char name[VFS_NAME_MAX];
    uint32_t size;
    uint8_t is_dir;
};

struct vfs_ops {
    const char *fs_name;
    /* source is the block-device description ("ata0p1" etc), ignored by FAT */
    int (*mount)(const char *source);
    int (*stat)(const char *rel, uint32_t *size, int *is_dir);
    int (*open)(const char *rel, int writable);
    int (*read)(int fh, void *buf, uint32_t len);
    int (*write)(int fh, const void *buf, uint32_t len);
    int (*lseek)(int fh, int32_t off, int whence);
    int (*close)(int fh);
    int (*unlink)(const char *rel);
    int (*mkdir)(const char *rel);
    int (*readdir)(const char *rel, struct vfs_dirent *out, int max);
};

int vfs_init(void);
/* Mount ops at mount point mp ("/" for the root). Returns 0 on success. */
int vfs_mount(const char *mp, const struct vfs_ops *ops);
int vfs_mount_count(void);
const char *vfs_mount_point(int index);
const char *vfs_mount_type(int index);

/* File-descriptor calls. fd values start at VFS_FD_BASE so they never
   collide with stdin/stdout/stderr. All return >=0 on success or -1. */
int vfs_open(const char *path, int writable);
int vfs_read(int fd, void *buf, uint32_t len);
int vfs_write(int fd, const void *buf, uint32_t len);
int vfs_lseek(int fd, int32_t off, int whence);
int vfs_close(int fd);

/* Path calls, no descriptor needed. */
int vfs_stat(const char *path, uint32_t *size, int *is_dir);
int vfs_list(const char *path, struct vfs_dirent *out, int max);
int vfs_mkdir(const char *path);
int vfs_unlink(const char *path);

/* Boot-time proof: list "/", open+read a tiny file, print [VFS] lines. */
void vfs_selftest(void);

#endif
