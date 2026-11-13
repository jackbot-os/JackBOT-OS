#include <stdint.h>
#include <stddef.h>
#include <kernel/vfs.h>
#include <kernel/fat32.h>
#include <kernel/serial.h>

struct vfs_mount {
    char point[16];
    const struct vfs_ops *ops;
    int used;
};

struct vfs_fd {
    int backend;
    int used;
    const struct vfs_ops *ops;
};

static struct vfs_mount mounts[VFS_MAX_MOUNTS];
static struct vfs_fd fds[VFS_MAX_OPEN];

/* ---- helpers ------------------------------------------------------- */

static int str_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b)
            return 0;
        a++;
        b++;
    }
    return *a == *b;
}

static int str_prefix(const char *s, const char *p)
{
    while (*p) {
        if (*s != *p)
            return 0;
        s++;
        p++;
    }
    return 1;
}

static void copy_str(char *dst, const char *src, int max)
{
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

/* Strip the mount point prefix from an absolute path. The FAT backend wants
   paths without a leading slash ("HOME" rather than "/HOME"), so "/" maps to
   the empty string and "/HOME/A.TXT" maps to "HOME/A.TXT". */
static const char *rel_of(const char *path)
{
    while (*path == '/')
        path++;
    return path;
}

/* Longest-prefix match over the mount table. */
static const struct vfs_ops *resolve(const char *path, const char **rel)
{
    const struct vfs_ops *best = 0;
    int best_len = -1;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!mounts[i].used)
            continue;
        const char *mp = mounts[i].point;
        int len = 0;
        while (mp[len])
            len++;
        if (str_prefix(path, mp) && len >= best_len) {
            /* "/home" must not match "/homepage": the next char has to be
               a separator or the end of the path. */
            char next = path[len];
            if (mp[len - 1] != '/' && next != '/' && next != 0)
                continue;
            best = mounts[i].ops;
            best_len = len;
        }
    }
    if (!best)
        return 0;
    *rel = rel_of(path + best_len);
    return best;
}

/* ---- FAT32 backend ------------------------------------------------- */

static int fat_be_mount(const char *source)
{
    (void)source;
    if (fat_mounted())
        return 0;
    return fat_mount() == 0 ? 0 : -1;
}

static int fat_be_stat(const char *rel, uint32_t *size, int *is_dir)
{
    return fat_stat_h(rel, size, is_dir);
}

static int fat_be_open(const char *rel, int writable)
{
    return fat_open_h(rel, writable);
}

static int fat_be_read(int fh, void *buf, uint32_t len)
{
    return fat_read_h(fh, buf, len);
}

static int fat_be_write(int fh, const void *buf, uint32_t len)
{
    return fat_write_h(fh, buf, len);
}

static int fat_be_lseek(int fh, int32_t off, int whence)
{
    return fat_lseek_h(fh, off, whence);
}

static int fat_be_close(int fh)
{
    return fat_close_h(fh);
}

static int fat_be_unlink(const char *rel)
{
    return fat_delete(rel);
}

static int fat_be_mkdir(const char *rel)
{
    return fat_mkdir(rel);
}

static int fat_be_readdir(const char *rel, struct vfs_dirent *out, int max)
{
    struct fat_dirent raw[16];
    if (max > 16)
        max = 16;
    int n = fat_list_dir(rel, raw, max);
    if (n < 0)
        return -1;
    for (int i = 0; i < n; i++) {
        copy_str(out[i].name, raw[i].name, VFS_NAME_MAX);
        out[i].size = raw[i].size;
        out[i].is_dir = raw[i].is_dir;
    }
    return n;
}

static const struct vfs_ops fat_ops = {
    .fs_name = "fat32",
    .mount = fat_be_mount,
    .stat = fat_be_stat,
    .open = fat_be_open,
    .read = fat_be_read,
    .write = fat_be_write,
    .lseek = fat_be_lseek,
    .close = fat_be_close,
    .unlink = fat_be_unlink,
    .mkdir = fat_be_mkdir,
    .readdir = fat_be_readdir,
};

/* ---- mount table --------------------------------------------------- */

int vfs_mount(const char *mp, const struct vfs_ops *ops)
{
    if (!mp || !ops)
        return -1;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (mounts[i].used && str_eq(mounts[i].point, mp)) {
            mounts[i].ops = ops;
            return 0;
        }
    }
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!mounts[i].used) {
            copy_str(mounts[i].point, mp, 16);
            mounts[i].ops = ops;
            mounts[i].used = 1;
            return 0;
        }
    }
    return -1;
}

int vfs_mount_count(void)
{
    int n = 0;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++)
        if (mounts[i].used)
            n++;
    return n;
}

const char *vfs_mount_point(int index)
{
    int seen = 0;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!mounts[i].used)
            continue;
        if (seen++ == index)
            return mounts[i].point;
    }
    return 0;
}

const char *vfs_mount_type(int index)
{
    int seen = 0;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!mounts[i].used)
            continue;
        if (seen++ == index)
            return mounts[i].ops->fs_name;
    }
    return 0;
}

int vfs_init(void)
{
    for (int i = 0; i < VFS_MAX_MOUNTS; i++)
        mounts[i].used = 0;
    for (int i = 0; i < VFS_MAX_OPEN; i++)
        fds[i].used = 0;
    return vfs_mount("/", &fat_ops);
}

/* ---- descriptor table ---------------------------------------------- */

int vfs_open(const char *path, int writable)
{
    const char *rel = "";
    const struct vfs_ops *ops = resolve(path, &rel);
    if (!ops || !ops->open)
        return -1;
    int fh = ops->open(rel, writable);
    if (fh < 0)
        return -1;
    for (int i = 0; i < VFS_MAX_OPEN; i++) {
        if (!fds[i].used) {
            fds[i].used = 1;
            fds[i].backend = fh;
            fds[i].ops = ops;
            return i + VFS_FD_BASE;
        }
    }
    ops->close(fh);
    return -1;
}

static struct vfs_fd *fd_of(int fd)
{
    int i = fd - VFS_FD_BASE;
    if (i < 0 || i >= VFS_MAX_OPEN || !fds[i].used)
        return 0;
    return &fds[i];
}

int vfs_read(int fd, void *buf, uint32_t len)
{
    struct vfs_fd *f = fd_of(fd);
    if (!f)
        return -1;
    return f->ops->read(f->backend, buf, len);
}

int vfs_write(int fd, const void *buf, uint32_t len)
{
    struct vfs_fd *f = fd_of(fd);
    if (!f)
        return -1;
    return f->ops->write(f->backend, buf, len);
}

int vfs_lseek(int fd, int32_t off, int whence)
{
    struct vfs_fd *f = fd_of(fd);
    if (!f || !f->ops->lseek)
        return -1;
    return f->ops->lseek(f->backend, off, whence);
}

int vfs_close(int fd)
{
    struct vfs_fd *f = fd_of(fd);
    if (!f)
        return -1;
    f->ops->close(f->backend);
    f->used = 0;
    return 0;
}

/* ---- path calls ---------------------------------------------------- */

int vfs_stat(const char *path, uint32_t *size, int *is_dir)
{
    const char *rel = "";
    const struct vfs_ops *ops = resolve(path, &rel);
    if (!ops)
        return -1;
    if (!rel[0] && str_eq(path, "/")) {
        /* The root is a directory by definition, even though the backend has
           no 8.3 name for it. */
        if (size)
            *size = 0;
        if (is_dir)
            *is_dir = 1;
        return 0;
    }
    return ops->stat(rel, size, is_dir);
}

int vfs_list(const char *path, struct vfs_dirent *out, int max)
{
    const char *rel = "";
    const struct vfs_ops *ops = resolve(path, &rel);
    if (!ops || !ops->readdir)
        return -1;
    return ops->readdir(rel, out, max);
}

int vfs_mkdir(const char *path)
{
    const char *rel = "";
    const struct vfs_ops *ops = resolve(path, &rel);
    if (!ops || !ops->mkdir)
        return -1;
    return ops->mkdir(rel);
}

int vfs_unlink(const char *path)
{
    const char *rel = "";
    const struct vfs_ops *ops = resolve(path, &rel);
    if (!ops || !ops->unlink)
        return -1;
    return ops->unlink(rel);
}

/* ---- boot proof ---------------------------------------------------- */

void vfs_selftest(void)
{
    if (vfs_mount_count() == 0) {
        serial_puts("[VFS] FAIL: no mounts\n");
        return;
    }
    serial_puts("[VFS] mount ");
    serial_puts(vfs_mount_point(0));
    serial_puts(" type=");
    serial_puts(vfs_mount_type(0));

    struct vfs_dirent d[12];
    int n = vfs_list("/", d, 12);
    serial_puts(" root=");
    if (n < 0) {
        serial_puts("ERR\n");
        return;
    }
    serial_puthex((uint64_t)n);
    serial_puts(" entries");

    /* One real open/read through the VFS, not a direct FAT call. */
    int fd = vfs_open("/LC.TXT", 0);
    if (fd >= 0) {
        char buf[32];
        int got = vfs_read(fd, buf, sizeof(buf));
        vfs_close(fd);
        serial_puts(" read(LC.TXT)=");
        serial_puthex((uint64_t)(got < 0 ? 0 : got));
    } else {
        serial_puts(" read(LC.TXT)=none");
    }
    serial_puts(n > 0 ? " OK\n" : " EMPTY\n");
}
