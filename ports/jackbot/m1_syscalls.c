#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/times.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

extern void serial_putc(char c);

static int ser_write(const char *buf, size_t n)
{
    for (size_t i = 0; i < n; i++)
        serial_putc(buf[i]);
    return (int)n;
}

ssize_t read(int fd, void *buf, size_t n)
{
    (void)fd; (void)buf; (void)n;
    errno = EAGAIN;
    return -1;
}

ssize_t write(int fd, const void *buf, size_t n)
{
    if (fd == 1 || fd == 2)
        return ser_write((const char *)buf, n);
    errno = EBADF;
    return -1;
}

int open(const char *path, int flags, ...)
{
    (void)path; (void)flags;
    errno = ENOSYS;
    return -1;
}

int close(int fd) { (void)fd; errno = ENOSYS; return -1; }

off_t lseek(int fd, off_t off, int whence)
{
    (void)fd; (void)off; (void)whence;
    errno = ENOSYS;
    return -1;
}

int isatty(int fd) { return (fd >= 0 && fd <= 2) ? 1 : 0; }

int fstat(int fd, struct stat *st)
{
    (void)fd;
    if (st) {
        __builtin_memset(st, 0, sizeof(*st));
        st->st_mode = S_IFCHR | 0600;
    }
    return 0;
}

pid_t getpid(void) { return 1; }
uid_t getuid(void) { return 0; }
uid_t geteuid(void) { return 0; }
gid_t getgid(void) { return 0; }
gid_t getegid(void) { return 0; }

pid_t fork(void) { errno = ENOSYS; return (pid_t)-1; }

int execve(const char *p, char *const a[], char *const e[])
{
    (void)p; (void)a; (void)e;
    errno = ENOSYS;
    return -1;
}

pid_t waitpid(pid_t pid, int *status, int opts)
{
    (void)pid; (void)status; (void)opts;
    errno = ENOSYS;
    return (pid_t)-1;
}

int gettimeofday(struct timeval *tv, void *tz)
{
    (void)tz;
    if (tv) { tv->tv_sec = 0; tv->tv_usec = 0; }
    return 0;
}

int clock_gettime(clockid_t clk, struct timespec *ts)
{
    (void)clk;
    if (ts) { ts->tv_sec = 0; ts->tv_nsec = 0; }
    return 0;
}

int clock_getres(clockid_t clk, struct timespec *ts)
{
    (void)clk;
    if (ts) { ts->tv_sec = 0; ts->tv_nsec = 1; }
    return 0;
}

clock_t times(struct tms *t)
{
    if (t)
        __builtin_memset(t, 0, sizeof(*t));
    return (clock_t)0;
}

int kill(pid_t pid, int sig)
{
    (void)pid; (void)sig;
    errno = EINVAL;
    return -1;
}

int stat(const char *path, struct stat *st)
{
    (void)path; (void)st;
    errno = ENOENT;
    return -1;
}

char *getcwd(char *buf, size_t size)
{
    if (buf && size >= 2) {
        buf[0] = '/';
        buf[1] = 0;
        return buf;
    }
    errno = ERANGE;
    return NULL;
}

char *realpath(const char *path, char *resolved)
{
    (void)path; (void)resolved;
    errno = ENOENT;
    return NULL;
}

ssize_t readlink(const char *path, char *buf, size_t bufsiz)
{
    (void)path; (void)buf; (void)bufsiz;
    errno = ENOENT;
    return -1;
}

int fcntl(int fd, int cmd, ...)
{
    (void)fd; (void)cmd;
    errno = ENOSYS;
    return -1;
}

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
    (void)addr; (void)len; (void)prot; (void)flags; (void)fd; (void)off;
    errno = ENOSYS;
    return MAP_FAILED;
}

int munmap(void *addr, size_t len)
{
    (void)addr; (void)len;
    return 0;
}

int getentropy(void *buf, size_t buflen)
{
    __builtin_memset(buf, 0, buflen);
    return 0;
}

int getrusage(int who, struct rusage *usage)
{
    (void)who;
    if (usage)
        __builtin_memset(usage, 0, sizeof(*usage));
    return 0;
}

int getitimer(int which, struct itimerval *value)
{
    (void)which; (void)value;
    errno = ENOSYS;
    return -1;
}

int setitimer(int which, const struct itimerval *newv, struct itimerval *oldv)
{
    (void)which; (void)newv; (void)oldv;
    errno = ENOSYS;
    return -1;
}

unsigned int alarm(unsigned int seconds)
{
    (void)seconds;
    return 0;
}

int pause(void)
{
    errno = ENOSYS;
    return -1;
}

int sigaction(int sig, const struct sigaction *act, struct sigaction *oact)
{
    (void)sig; (void)act; (void)oact;
    return 0;
}

int clock_settime(clockid_t clk, const struct timespec *ts)
{
    (void)clk; (void)ts;
    errno = EPERM;
    return -1;
}

int clock_nanosleep(clockid_t clk, int flags, const struct timespec *req,
                    struct timespec *rem)
{
    (void)clk; (void)flags; (void)req; (void)rem;
    return ENOSYS;
}

void _exit(int status)
{
    (void)status;
    serial_putc('\n');
    for (;;)
        __asm__ volatile("hlt");
}

void exit(int status) { _exit(status); }
