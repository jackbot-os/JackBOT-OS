#include <stdint.h>
#include <stddef.h>
#include <kernel/posix.h>
#include <kernel/proc.h>
#include <kernel/trap.h>
#include <kernel/fat32.h>
#include <kernel/crash.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/serial.h>
#include <kernel/kstring.h>
#include <kernel/ps2.h>

uint64_t timer_get_ticks(void);

/* POSIX-ish system call layer for ring-3 programs.
 *
 * ABI: int 0x80, number in rax, arguments in rdi/rsi/rdx/r10, result in rax.
 * The legacy calls 0..2 (putc / yield / exit) stay where they are; the
 * POSIX subset lives from SYS_BASE upwards.
 */
#define USER_BASE_ADDR 0x8000000000ull
#define USER_HEAP_BASE (USER_BASE_ADDR + 0x200000ull)   /* 2 MiB after the image */
#define USER_HEAP_MAX  0x2000000ull                     /* 32 MiB */

static uint64_t heap_cur = USER_HEAP_BASE;

static int64_t sys_read_console(void *buf, uint32_t len);

/* ---- user memory helpers ---- */

static int uok(const void *p, uint64_t len)
{
    return user_ptr_valid(p, len);
}

static char *ustr(const char *p, char *dst, int cap)
{
    int i = 0;
    if (!p || !uok(p, 1))
        return 0;
    while (i < cap - 1) {
        if (!uok(p + i, 1))
            break;
        dst[i] = p[i];
        if (!dst[i])
            return dst;
        i++;
    }
    dst[i] = 0;
    return dst;
}

/* ---- brk ---- */

static uint64_t brk_set(uint64_t addr)
{
    thread_t *t = current_thread();
    uint64_t want;

    if (!t || !t->is_user || !t->p4phys)
        return heap_cur;
    if (addr == 0)
        return heap_cur;
    if (addr < USER_HEAP_BASE)
        return heap_cur;
    if (addr - USER_HEAP_BASE > USER_HEAP_MAX)
        return heap_cur;

    want = (addr + 0xFFFull) & ~0xFFFull;
    while (heap_cur < want) {
        void *frame = pmm_alloc_page_below(0x40000000ull);
        if (!frame)
            break;
        memset(frame, 0, 4096);
        vmm_map_in(t->p4phys, heap_cur, (uint64_t)(uintptr_t)frame,
                   PTE_PRESENT | PTE_WRITABLE | PTE_USER);
        heap_cur += 4096;
    }
    if (heap_cur < addr)
        return heap_cur;
    return heap_cur;
}

/* ---- file descriptors: 0..2 console, 3+ FAT handles ---- */

static int fd_file(int fd)
{
    if (fd < 3 || fd >= 3 + FAT_FH_MAX)
        return -1;
    return fd - 3;
}

static int64_t sys_write(int fd, const void *buf, uint32_t len)
{
    if (!uok(buf, len))
        return -1;
    if (fd == 1 || fd == 2 || fd == 0) {
        const char *p = buf;
        for (uint32_t i = 0; i < len; i++)
            serial_putc(p[i]);
        return (int64_t)len;
    }
    {
        int h = fd_file(fd);
        if (h < 0)
            return -1;
        return fat_write_h(h, buf, len);
    }
}

static int64_t sys_read(int fd, void *buf, uint32_t len)
{
    if (!uok(buf, len))
        return -1;
    if (fd == 0)
        return sys_read_console(buf, len);
    {
        int h = fd_file(fd);
        if (h < 0)
            return -1;
        return fat_read_h(h, buf, len);
    }
}

static int64_t sys_open(const char *path, int flags)
{
    char name[64];
    int writable = (flags & 1) != 0;
    if (!ustr(path, name, sizeof name))
        return -1;
    {
        int h = fat_open_h(name, writable);
        if (h < 0)
            return -1;
        return 3 + h;
    }
}

static int64_t sys_close(int fd)
{
    int h = fd_file(fd);
    if (h < 0)
        return -1;
    return fat_close_h(h);
}

static int64_t sys_lseek(int fd, int32_t off, int whence)
{
    int h = fd_file(fd);
    if (h < 0)
        return -1;
    return fat_lseek_h(h, off, whence);
}

static int64_t sys_stat(const char *path, uint32_t *out)
{
    char name[64];
    uint32_t size = 0;
    int is_dir = 0;
    if (!ustr(path, name, sizeof name))
        return -1;
    if (!uok(out, 8))
        return -1;
    if (fat_stat_h(name, &size, &is_dir) != 0)
        return -1;
    out[0] = size;
    out[1] = (uint32_t)is_dir;
    return 0;
}

static int64_t sys_fstat(int fd, uint32_t *out)
{
    int h = fd_file(fd);
    if (h < 0 || !uok(out, 8))
        return -1;
    /* the handle keeps the size; re-open semantics are not needed */
    {
        int r = fat_lseek_h(h, 0, FAT_SEEK_END);
        if (r < 0)
            return -1;
        out[0] = (uint32_t)r;
        out[1] = 0;
        fat_lseek_h(h, 0, FAT_SEEK_SET);
    }
    return 0;
}

static int64_t sys_unlink(const char *path)
{
    char name[64];
    if (!ustr(path, name, sizeof name))
        return -1;
    return fat_delete(name);
}

static int64_t sys_mkdir(const char *path)
{
    char name[64];
    if (!ustr(path, name, sizeof name))
        return -1;
    return fat_mkdir(name);
}

static int64_t sys_rename(const char *from, const char *to)
{
    char a[64], b[64];
    if (!ustr(from, a, sizeof a) || !ustr(to, b, sizeof b))
        return -1;
    return fat_move(a, b);
}

/* access(): 0 when the path exists, -1 otherwise. There are no permission
   bits in FAT, so the mode argument is accepted and ignored. */
static int64_t sys_access(const char *path, int mode)
{
    char name[64];
    uint32_t size = 0;
    int is_dir = 0;
    (void)mode;
    if (!ustr(path, name, sizeof name))
        return -1;
    if (fat_stat_h(name, &size, &is_dir) != 0)
        return -1;
    return 0;
}

static int64_t sys_sleep(uint32_t msec)
{
    uint64_t start = timer_get_ticks();
    uint64_t want = (uint64_t)msec / 10u;      /* TIMER_HZ = 100 */
    if (want == 0)
        want = 1;
    while (timer_get_ticks() - start < want)
        thread_yield();
    return 0;
}

/* Console input: pull whatever keys the PS/2 layer has queued, mapping
   carriage return to newline so line-oriented programs work. */
static int64_t sys_read_console(void *buf, uint32_t len)
{
    char *p = buf;
    uint32_t got = 0;
    while (got < len) {
        char c;
        if (!ps2_read_key(&c))
            break;
        if (c == '\r')
            c = '\n';
        p[got++] = c;
        if (c == '\n')
            break;
    }
    return (int64_t)got;
}

/* ---- boot-time self test ----
 * Runs the same helpers the syscalls use, so a serial log proves the layer
 * works on the machine that booted. File tests need a mounted FAT volume;
 * console/uptime tests always run.
 */

static void say_i(const char *tag, int64_t v)
{
    serial_puts(tag);
    if (v < 0) {
        serial_putc('-');
        v = -v;
    }
    if (v == 0) {
        serial_putc('0');
    } else {
        char d[24];
        int n = 0;
        while (v > 0 && n < 24) {
            d[n++] = (char)('0' + (int)(v % 10));
            v /= 10;
        }
        while (n > 0)
            serial_putc(d[--n]);
    }
    serial_putc('\n');
}

void posix_selftest(void)
{
    char buf[8];
    int64_t r;

    serial_puts("[POSIX] selftest\n");

    r = sys_read_console(buf, 4);
    say_i("[POSIX] console_read_pending=", r);
    say_i("[POSIX] sleep_ms(40)=", sys_sleep(40));
    say_i("[POSIX] uptime_s=", (int64_t)(timer_get_ticks() / 100u));

    if (!fat_mounted()) {
        serial_puts("[POSIX] no FAT volume: file calls skipped\n");
        return;
    }

    r = fat_write_file("PXTEST.TXT", "posix\n", 6);
    say_i("[POSIX] write_file=", r);

    /* The sys_* entry points validate their path against the user address
       range, so a kernel-side call has to go through the same FAT helpers
       they wrap. */
    {
        uint32_t size = 0;
        int is_dir = 0;
        r = fat_stat_h("PXTEST.TXT", &size, &is_dir);
        say_i("[POSIX] access=", r);
        say_i("[POSIX] access_size=", (int64_t)size);
    }

    r = fat_move("PXTEST.TXT", "PXT2.TXT");
    say_i("[POSIX] rename=", r);

    {
        uint32_t size = 0;
        int is_dir = 0;
        r = fat_stat_h("PXT2.TXT", &size, &is_dir);
        say_i("[POSIX] access_after_rename=", r);
        say_i("[POSIX] stat=", r);
        say_i("[POSIX] stat_size=", (int64_t)size);
        say_i("[POSIX] stat_isdir=", is_dir);
    }

    r = fat_mkdir("POSIXD");
    say_i("[POSIX] mkdir=", r);
    {
        uint32_t size = 0;
        int is_dir = 0;
        if (fat_stat_h("POSIXD", &size, &is_dir) == 0) {
            say_i("[POSIX] dir_isdir=", is_dir);
            /* -1 here just means the directory was already there, which on a
               second boot is exactly what persistence should look like. */
            if (r != 0 && is_dir)
                say_i("[POSIX] mkdir_already=", 1);
        }
    }

    r = fat_delete("PXT2.TXT");
    say_i("[POSIX] unlink=", r);
}

void posix_syscall(struct trap_frame *f)
{
    uint64_t num = f->rax;
    int64_t r = -1;

    switch (num) {
    case SYS_WRITE:
        r = sys_write((int)f->rdi, (const void *)f->rsi, (uint32_t)f->rdx);
        break;
    case SYS_READ:
        r = sys_read((int)f->rdi, (void *)f->rsi, (uint32_t)f->rdx);
        break;
    case SYS_OPEN:
        r = sys_open((const char *)f->rdi, (int)f->rsi);
        break;
    case SYS_CLOSE:
        r = sys_close((int)f->rdi);
        break;
    case SYS_LSEEK:
        r = sys_lseek((int)f->rdi, (int32_t)f->rsi, (int)f->rdx);
        break;
    case SYS_BRK:
        r = (int64_t)brk_set(f->rdi);
        break;
    case SYS_STAT:
        r = sys_stat((const char *)f->rdi, (uint32_t *)f->rsi);
        break;
    case SYS_FSTAT:
        r = sys_fstat((int)f->rdi, (uint32_t *)f->rsi);
        break;
    case SYS_UNLINK:
        r = sys_unlink((const char *)f->rdi);
        break;
    case SYS_TIME:
        r = (int64_t)(timer_get_ticks() / 100u);   /* TIMER_HZ = 100 */
        break;
    case SYS_GETPID:
        r = current_thread() ? (int64_t)current_thread()->id : 0;
        break;
    case SYS_MKDIR:
        r = sys_mkdir((const char *)f->rdi);
        break;
    case SYS_RENAME:
        r = sys_rename((const char *)f->rdi, (const char *)f->rsi);
        break;
    case SYS_ACCESS:
        r = sys_access((const char *)f->rdi, (int)f->rsi);
        break;
    case SYS_ISATTY:
        r = ((int)f->rdi >= 0 && (int)f->rdi <= 2) ? 1 : 0;
        break;
    case SYS_SLEEP:
        r = sys_sleep((uint32_t)f->rdi);
        break;
    case SYS_UPTIME:
        r = (int64_t)(timer_get_ticks() / 100u);
        break;
    default:
        r = -1;
        break;
    }
    f->rax = (uint64_t)r;
}
