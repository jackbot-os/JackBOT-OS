/*
 * newlib/picolibc system call shims for CPython on JackBOT.
 *
 * This file is compiled with the cross toolchain at milestone M1 (see
 * PORTING.md); it is not part of the kernel Makefile yet. Every shim
 * maps a POSIX call onto an existing JackBOT kernel service. Functions
 * that have no kernel counterpart return ENOSYS and are marked STUB so
 * the gap is explicit.
 */

#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/times.h>
#include <unistd.h>

/* Provided by the kernel (real symbols confirmed in the tree). */
extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* Heap: kmalloc/kfree are the kernel allocator. The M1 libos will wrap
   these; for the first bring-up we reserve a static arena so _sbrk has
   no dependency on the kernel heap layout. */
#ifndef JACKBOT_LIBC_HEAP_BYTES
#define JACKBOT_LIBC_HEAP_BYTES (24u * 1024u * 1024u)
#endif
static unsigned char g_heap_arena[JACKBOT_LIBC_HEAP_BYTES]
    __attribute__((aligned(16)));
static size_t g_heap_used;

void *_sbrk(ptrdiff_t incr)
{
    if ((size_t)incr > JACKBOT_LIBC_HEAP_BYTES - g_heap_used) {
        errno = ENOMEM;
        return (void *)-1;
    }
    void *p = g_heap_arena + g_heap_used;
    g_heap_used += (size_t)incr;
    return p;
}

/* stdout/stderr over the serial port; the desktop console redirect is
   wired in at M3. */
static int write_serial(const char *buf, size_t n)
{
    for (size_t i = 0; i < n; i++)
        serial_putc(buf[i]);
    return (int)n;
}

int _write(int fd, const char *buf, size_t n)
{
    if (fd == 1 || fd == 2)
        return write_serial(buf, n);
    /* M2: route to FAT VFS fd table. */
    errno = EBADF;
    return -1;
}

int _read(int fd, char *buf, size_t n)
{
    (void)fd; (void)buf; (void)n;
    /* M3: read from the keyboard/terminal input ring. */
    errno = EAGAIN;
    return -1;
}

void _exit(int status)
{
    (void)status;
    serial_puts("[cpython] interpreter exit; libos halt (M1)\n");
    for (;;)
        __asm__ volatile("hlt");
}

int _isatty(int fd)
{
    return (fd == 0 || fd == 1 || fd == 2) ? 1 : 0;
}

int _close(int fd) { (void)fd; errno = ENOSYS; return -1; }
int _open(const char *p, int flags, int mode)
{
    (void)p; (void)flags; (void)mode;
    errno = ENOSYS;
    return -1;
}

off_t _lseek(int fd, off_t off, int whence)
{
    (void)fd; (void)off; (void)whence;
    errno = ENOSYS;
    return -1;
}

int _fstat(int fd, struct stat *st)
{
    (void)fd;
    if (st) {
        __builtin_memset(st, 0, sizeof(*st));
        st->st_mode = S_IFCHR | 0600;
    }
    return 0;
}

int _getpid(void) { return 1; }

int _kill(int pid, int sig)
{
    (void)pid; (void)sig;
    errno = EINVAL;
    return -1;
}

int _gettimeofday(struct timeval *tv, void *tz)
{
    (void)tz;
    /* TODO(M1): read the PIT tick counter (kernel timer). */
    if (tv) {
        tv->tv_sec = 0;
        tv->tv_usec = 0;
    }
    return 0;
}

int _stat(const char *path, struct stat *st)
{
    (void)path; (void)st;
    errno = ENOSYS;
    return -1;
}

char *_getenv(const char *name)
{
    (void)name;
    return NULL; /* M2: static environment block with PYTHONHOME */
}
