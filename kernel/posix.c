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
#include <kernel/ipc.h>
#include <kernel/fork.h>

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

/* heap break now lives on each thread (see proc.h) */

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
        return 0;
    if (t->heap_cur == 0)
        t->heap_cur = USER_HEAP_BASE;
    if (addr == 0)
        return t->heap_cur;
    if (addr < USER_HEAP_BASE)
        return t->heap_cur;
    if (addr - USER_HEAP_BASE > USER_HEAP_MAX)
        return t->heap_cur;

    want = (addr + 0xFFFull) & ~0xFFFull;
    while (t->heap_cur < want) {
        void *frame = pmm_alloc_page_below(0x40000000ull);
        if (!frame)
            break;
        memset(frame, 0, 4096);
        /* heap is data, never code: NX keeps a ring-3 process from
           turning its buffer overflow into executable shellcode */
        vmm_map_in(t->p4phys, t->heap_cur, (uint64_t)(uintptr_t)frame,
                   PTE_PRESENT | PTE_WRITABLE | PTE_USER | PTE_NX);
        t->heap_cur += 4096;
    }
    return t->heap_cur;
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
    if (ipc_fd_is_pipe(fd))
        return ipc_pipe_write(fd, buf, len);
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
    if (ipc_fd_is_pipe(fd))
        return ipc_pipe_read(fd, buf, len);
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

/* ---- pthread / clone layer (musl 1.2.5 ABI, see ports/musl_thread) ---- */

#define CLONE_VM             0x00000100
#define CLONE_SETTLS         0x00080000
#define CLONE_PARENT_SETTID  0x00100000
#define CLONE_CHILD_CLEARTID 0x00200000

#define ARCH_SET_FS 0x1002
#define ARCH_GET_FS 0x1001

#define E_FAULT   (-14)
#define E_INVAL   (-22)
#define E_NOMEM   (-12)
#define E_NOSYS   (-38)

/* Anonymous mappings live above the heap. The cursor only moves forward so
   a freed range is never handed out twice: a stale pointer then faults
   instead of quietly aliasing someone else's new stack. */
#define MMAP_BASE (USER_HEAP_BASE + USER_HEAP_MAX + 0x10000000ull)
#define MMAP_TOP  (USER_HEAP_BASE + 0x180000000ull)
static uint64_t mmap_next = MMAP_BASE;

static int64_t sys_mmap_anon(uint64_t addr, uint64_t len)
{
    thread_t *t = current_thread();
    uint64_t pages, base, p;

    if (!t || !t->is_user || !t->p4phys)
        return E_INVAL;
    if (len == 0 || len > 0x400000ull)          /* 4 MiB per call */
        return E_INVAL;
    pages = (len + 0xfffull) >> 12;

    base = addr ? (addr & ~0xfffull) : ((mmap_next + 0xfffull) & ~0xfffull);
    if (base < MMAP_BASE || base + pages * 4096 > MMAP_TOP)
        return E_NOMEM;
    if (!addr)
        mmap_next = base + pages * 4096;

    for (p = 0; p < pages; p++)
        if (vmm_phys_of_in(t->p4phys, base + p * 4096))
            return E_INVAL;                     /* range already in use */

    for (p = 0; p < pages; p++) {
        void *frame = pmm_alloc_page_below(0x40000000ull);
        if (!frame) {
            for (uint64_t k = 0; k < p; k++) {
                uint64_t phys = vmm_unmap_in(t->p4phys, base + k * 4096);
                if (phys)
                    pmm_free_page((void *)(uintptr_t)phys);
            }
            return E_NOMEM;
        }
        memset(frame, 0, 4096);
        /* thread stacks and mmap'd blobs are data, not code */
        vmm_map_in(t->p4phys, base + p * 4096, (uint64_t)(uintptr_t)frame,
                   PTE_PRESENT | PTE_WRITABLE | PTE_USER | PTE_NX);
    }
    return (int64_t)base;
}

static int64_t sys_munmap(uint64_t addr, uint64_t len)
{
    thread_t *t = current_thread();
    uint64_t pages;

    if (!t || !t->is_user || !t->p4phys || (addr & 0xfffull) || len == 0)
        return E_INVAL;
    pages = (len + 0xfffull) >> 12;
    for (uint64_t p = 0; p < pages; p++) {
        uint64_t phys = vmm_unmap_in(t->p4phys, addr + p * 4096);
        if (phys)
            pmm_free_page((void *)(uintptr_t)phys);
    }
    return 0;
}

static int64_t sys_futex(uint64_t uaddr, int op, int val, uint64_t timeout_ptr)
{
    int cmd = op & 0x7f;                        /* drop PRIVATE/CLOCK flags */

    if (!uok((void *)uaddr, 4))
        return E_FAULT;
    if (cmd == 0) {                             /* FUTEX_WAIT */
        uint64_t tmo = 0;
        if (timeout_ptr) {
            uint64_t *ts = (uint64_t *)(uintptr_t)timeout_ptr;
            if (!uok(ts, 16))
                return E_FAULT;
            tmo = ts[0] * 100ull + ts[1] / 10000000ull;   /* TIMER_HZ = 100 */
            if (tmo == 0)
                tmo = 1;
        }
        return futex_wait_word((volatile int *)(uintptr_t)uaddr, val, tmo);
    }
    if (cmd == 1)                               /* FUTEX_WAKE */
        return futex_wake_word((volatile int *)(uintptr_t)uaddr, val);
    /* REQUEUE and the robust-list ops: musl's condvar falls back to a
       plain wake when this reports ENOSYS, so none of them are required. */
    return E_NOSYS;
}

static int64_t sys_clone(struct trap_frame *f)
{
    thread_t *parent = current_thread();
    uint64_t flags = f->rdi, stack = f->rsi, ptid = f->rdx;
    uint64_t tls = f->r10, ctid = f->r8;
    thread_t *child;

    if (!parent || !parent->is_user || !parent->p4phys)
        return E_INVAL;
    if (!(flags & CLONE_VM))
        return E_INVAL;                 /* only shared address spaces exist */
    if (stack && !uok((void *)stack, 8))
        return E_FAULT;
    if (!uok((void *)f->rip, 1))
        return E_FAULT;                 /* the resume address must be mapped */

    child = thread_create_clone(parent, f->rip, stack,
                                (flags & CLONE_SETTLS) ? tls : parent->tls_base,
                                (flags & CLONE_CHILD_CLEARTID) ? ctid : 0,
                                f->r9);
    if (!child)
        return E_NOMEM;
    if ((flags & CLONE_PARENT_SETTID) && uok((void *)ptid, 4))
        *(uint32_t *)(uintptr_t)ptid = (uint32_t)child->tid;
    return (int64_t)child->tid;
}

static int64_t sys_arch_prctl(int code, uint64_t addr)
{
    if (code == ARCH_SET_FS) {
        thread_set_fs(addr);
        return 0;
    }
    if (code == ARCH_GET_FS) {
        if (!uok((void *)addr, 8))
            return E_FAULT;
        *(uint64_t *)(uintptr_t)addr = thread_get_fs();
        return 0;
    }
    return E_INVAL;
}

static int64_t sys_clock_gettime(int clk, void *ts)
{
    uint64_t t = timer_get_ticks();
    uint64_t *p = ts;

    (void)clk;
    if (!uok(ts, 16))
        return E_FAULT;
    p[0] = t / 100ull;
    p[1] = (t % 100ull) * 10000000ull;
    return 0;
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
    case SYS_CLONE:
        r = sys_clone(f);
        break;
    case SYS_THREAD_EXIT:
        thread_exit_status(f->rdi);     /* does not return */
        r = 0;
        break;
    case SYS_EXIT_GROUP:
        thread_exit_group(f->rdi);      /* does not return */
        r = 0;
        break;
    case SYS_FUTEX:
        r = sys_futex(f->rdi, (int)f->rsi, (int)f->rdx, f->r10);
        break;
    case SYS_SET_TID_ADDR: {
        thread_t *t = current_thread();
        if (t) {
            if (uok((void *)f->rdi, 4))
                t->clear_tid = f->rdi;
            r = (int64_t)t->tid;
        }
        break;
    }
    case SYS_MMAP:
        r = sys_mmap_anon(f->rdi, f->rsi);
        break;
    case SYS_MUNMAP:
        r = sys_munmap(f->rdi, f->rsi);
        break;
    case SYS_MPROTECT:
        r = 0;                          /* pages are already RW and NX */
        break;
    case SYS_RT_SIGPROCMASK:
    case SYS_SET_ROBUST_LIST:
    case SYS_TKILL:
        r = E_NOSYS;
        break;
    case SYS_SCHED_SETSCHEDULER:
        r = 0;                          /* one scheduling policy for now */
        break;
    case SYS_ARCH_PRCTL:
        r = sys_arch_prctl((int)f->rdi, f->rsi);
        break;
    case SYS_CLOCK_GETTIME:
        r = sys_clock_gettime((int)f->rdi, (void *)f->rsi);
        break;
    case SYS_GETTID: {
        thread_t *t = current_thread();
        r = t ? (int64_t)t->tid : 0;
        break;
    }
    case SYS_WRITE_THREAD:
        r = sys_write((int)f->rdi, (const void *)f->rsi, (uint32_t)f->rdx);
        break;
    case SYS_FORK:
        r = sys_fork(f);
        break;
    case SYS_PIPE: {
        int fds[2];
        if (!uok((void *)f->rdi, 8)) {
            r = -1;
            break;
        }
        r = (ipc_pipe_create(fds) == 0) ? 0 : -1;
        if (r == 0) {
            int *out = (int *)f->rdi;
            out[0] = fds[0];
            out[1] = fds[1];
        }
        break;
    }
    case SYS_SHM_CREATE:
        r = ipc_shm_create((uint32_t)f->rdi);
        break;
    case SYS_SHM_MAP: {
        thread_t *t = current_thread();
        r = t ? ipc_shm_map((int)f->rdi, t->p4phys, f->rsi) : -1;
        break;
    }
    case SYS_SEM_CREATE:
        r = ipc_sem_create((int)f->rdi);
        break;
    case SYS_SEM_WAIT:
        r = ipc_sem_wait((int)f->rdi);
        break;
    case SYS_SEM_POST:
        r = ipc_sem_post((int)f->rdi);
        break;
    case SYS_MQ_CREATE:
        r = ipc_mq_create();
        break;
    case SYS_MQ_SEND:
        r = uok((void *)f->rsi, (uint32_t)f->rdx)
                ? ipc_mq_send((int)f->rdi, (const void *)f->rsi, (uint32_t)f->rdx)
                : -1;
        break;
    case SYS_MQ_RECV:
        r = uok((void *)f->rsi, (uint32_t)f->rdx)
                ? ipc_mq_recv((int)f->rdi, (void *)f->rsi, (uint32_t)f->rdx)
                : -1;
        break;
    default:
        r = -1;
        break;
    }
    f->rax = (uint64_t)r;
}
