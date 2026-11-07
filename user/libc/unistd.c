#include "jb.h"

int open(const char *path, int flags)
{
    return (int)__jb_sys(SYS_OPEN, (long)path, flags, 0);
}

int close(int fd) { return (int)__jb_sys(SYS_CLOSE, fd, 0, 0); }

ssize_t read(int fd, void *buf, size_t n)
{
    return (ssize_t)__jb_sys(SYS_READ, fd, (long)buf, (long)n);
}

ssize_t write(int fd, const void *buf, size_t n)
{
    return (ssize_t)__jb_sys(SYS_WRITE, fd, (long)buf, (long)n);
}

off_t lseek(int fd, off_t off, int whence)
{
    return (off_t)__jb_sys(SYS_LSEEK, fd, (long)off, whence);
}

long time(long *t)
{
    long v = __jb_sys(SYS_TIME, 0, 0, 0);
    if (t) *t = v;
    return v;
}

int getpid(void) { return (int)__jb_sys(SYS_GETPID, 0, 0, 0); }

int unlink(const char *path) { return (int)__jb_sys(SYS_UNLINK, (long)path, 0, 0); }

int stat(const char *path, unsigned int out[2])
{
    return (int)__jb_sys(SYS_STAT, (long)path, (long)out, 0);
}

int mkdir(const char *path) { return (int)__jb_sys(SYS_MKDIR, (long)path, 0, 0); }

int rename(const char *from, const char *to)
{
    return (int)__jb_sys(SYS_RENAME, (long)from, (long)to, 0);
}

int access(const char *path, int mode)
{
    return (int)__jb_sys(SYS_ACCESS, (long)path, mode, 0);
}

int isatty(int fd) { return (int)__jb_sys(SYS_ISATTY, fd, 0, 0); }

int sleep_ms(unsigned int msec) { return (int)__jb_sys(SYS_SLEEP, (long)msec, 0, 0); }

unsigned long uptime(void) { return (unsigned long)__jb_sys(SYS_UPTIME, 0, 0, 0); }
