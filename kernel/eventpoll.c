#include <stdint.h>
#include <stddef.h>
#include <kernel/eventpoll.h>
#include <kernel/ipc.h>
#include <kernel/proc.h>
#include <kernel/serial.h>

/* The tick counter lives in kernel/timer.c; this tree declares it per user
   rather than through a header. */
uint64_t timer_get_ticks(void);

#define EPOLL_FD_BASE 1000
#define EPOLL_TIMEOUT_SPINS 200000
#define FAT_FD_LIMIT IPC_FD_BASE          /* 3..63 are FAT handles */

struct epoll_watch {
    int fd;
    struct epoll_event ev;
};

struct epoll_inst {
    int used;
    int count;
    struct epoll_watch w[EPOLL_MAX_WATCH];
};

static struct epoll_inst g_eps[EPOLL_MAX_INSTANCES];

static void zero(void *p, uint64_t n)
{
    uint8_t *b = p;
    for (uint64_t i = 0; i < n; i++)
        b[i] = 0;
}

void eventpoll_init(void)
{
    zero(g_eps, sizeof g_eps);
}

/* What a descriptor can do right now, expressed as epoll bits. */
static uint32_t ready_bits(int fd)
{
    uint32_t bits = 0;
    if (fd < 0)
        return 0;
    if (ipc_fd_is_pipe(fd)) {
        /* A live pipe only reports the directions it can actually move data
           in. HUP is for a pipe whose object is gone, not for "this end is
           read-only" — that mistake once made an empty pipe look readable. */
        if (!ipc_pipe_alive(fd)) {
            bits |= EPOLLHUP;
            return bits;
        }
        if (ipc_pipe_readable(fd))
            bits |= EPOLLIN;
        if (ipc_pipe_writable(fd))
            bits |= EPOLLOUT;
        return bits;
    }
    if (fd <= 2) {
        /* console: readable and writable, the reader just returns 0 when
           nothing has been typed yet */
        bits |= EPOLLIN | EPOLLOUT;
        return bits;
    }
    if (fd < FAT_FD_LIMIT) {
        /* regular file handle: always ready, position never blocks */
        bits |= EPOLLIN | EPOLLOUT;
        return bits;
    }
    return 0;
}

int eventpoll_create(void)
{
    for (int i = 0; i < EPOLL_MAX_INSTANCES; i++) {
        if (g_eps[i].used)
            continue;
        zero(&g_eps[i], sizeof g_eps[i]);
        g_eps[i].used = 1;
        return EPOLL_FD_BASE + i;
    }
    return -1;
}

static struct epoll_inst *inst_of(int epfd)
{
    int i = epfd - EPOLL_FD_BASE;
    if (i < 0 || i >= EPOLL_MAX_INSTANCES || !g_eps[i].used)
        return 0;
    return &g_eps[i];
}

int eventpoll_ctl(int epfd, int op, int fd, const struct epoll_event *ev)
{
    struct epoll_inst *e = inst_of(epfd);
    if (!e || !ev)
        return -1;

    int slot = -1;
    for (int i = 0; i < e->count; i++)
        if (e->w[i].fd == fd) {
            slot = i;
            break;
        }

    if (op == 2) {                       /* DEL */
        if (slot < 0)
            return -1;
        e->w[slot] = e->w[e->count - 1];
        e->count--;
        return 0;
    }
    if (op == 1) {                       /* ADD */
        if (slot >= 0 || e->count >= EPOLL_MAX_WATCH)
            return -1;
        e->w[e->count].fd = fd;
        e->w[e->count].ev = *ev;
        e->count++;
        return 0;
    }
    if (op == 3) {                       /* MOD */
        if (slot < 0)
            return -1;
        e->w[slot].ev = *ev;
        return 0;
    }
    return -1;
}

/* Collects the ready subset into out[]; returns the count. */
static int scan(struct epoll_inst *e, struct epoll_event *out, int maxevents)
{
    int n = 0;
    for (int i = 0; i < e->count && n < maxevents; i++) {
        uint32_t now = ready_bits(e->w[i].fd);
        uint32_t hit = now & (e->w[i].ev.events | EPOLLERR | EPOLLHUP);
        if (!hit)
            continue;
        out[n] = e->w[i].ev;
        out[n].events = hit;
        n++;
    }
    return n;
}

int eventpoll_wait(int epfd, struct epoll_event *out, int maxevents, int timeout_ms)
{
    struct epoll_inst *e = inst_of(epfd);
    if (!e || !out || maxevents <= 0)
        return -1;

    uint64_t start = timer_get_ticks();
    uint64_t budget = timeout_ms > 0 ? (uint64_t)timeout_ms / 10u : 0;
    int spins = EPOLL_TIMEOUT_SPINS;

    for (;;) {
        int n = scan(e, out, maxevents);
        if (n > 0)
            return n;
        if (timeout_ms < 0) {
            thread_yield();              /* block until someone writes */
            if (--spins <= 0)
                return 0;
            continue;
        }
        if (timeout_ms == 0)
            return 0;
        if (timer_get_ticks() - start >= budget)
            return 0;
        thread_yield();
    }
}

int eventpoll_poll(struct pollfd_k *fds, int nfds, int timeout_ms)
{
    if (!fds || nfds <= 0)
        return -1;
    uint64_t start = timer_get_ticks();
    uint64_t budget = timeout_ms > 0 ? (uint64_t)timeout_ms / 10u : 0;
    int spins = EPOLL_TIMEOUT_SPINS;

    for (;;) {
        int ready = 0;
        for (int i = 0; i < nfds; i++) {
            uint32_t now = ready_bits(fds[i].fd);
            fds[i].revents = (short)(now & (uint32_t)fds[i].events);
            if (fds[i].revents)
                ready++;
        }
        if (ready > 0)
            return ready;
        if (timeout_ms == 0)
            return 0;
        if (timeout_ms > 0 && timer_get_ticks() - start >= budget)
            return 0;
        thread_yield();
        if (--spins <= 0)
            return 0;
    }
}

/* ---- boot selftest ---- */

static void put(const char *s)
{
    while (*s)
        serial_putc(*s++);
}

static void put_num(uint64_t v)
{
    char b[20];
    int n = 0;
    if (!v) {
        serial_putc('0');
        return;
    }
    while (v && n < 19) {
        b[n++] = (char)('0' + v % 10);
        v /= 10;
    }
    while (n-- > 0)
        serial_putc(b[n]);
}

void eventpoll_selftest(void)
{
    int fds[2] = { 0, 0 };
    int epfd = eventpoll_create();
    int ok = 0;
    int before = -1, after = -1;

    if (epfd >= 0 && ipc_pipe_create(fds) == 0) {
        struct epoll_event ev;
        struct epoll_event out[2];
        zero(&ev, sizeof ev);
        ev.events = EPOLLIN;
        ev.data = 0xCAFEull;

        if (eventpoll_ctl(epfd, 1, fds[0], &ev) == 0) {
            before = eventpoll_wait(epfd, out, 2, 0);
            ipc_pipe_write(fds[1], "ping", 4);
            after = eventpoll_wait(epfd, out, 2, 50);
            ok = (before == 0 && after == 1 && (out[0].events & EPOLLIN) &&
                  out[0].data == 0xCAFEull);
            eventpoll_ctl(epfd, 2, fds[0], &ev);
        }
        ipc_pipe_close(fds[0]);
        ipc_pipe_close(fds[1]);
    }

    put("[EPOLL] epfd=");
    put_num((uint64_t)(epfd >= 0 ? epfd : 0));
    put(" idle=");
    put_num((uint64_t)(before < 0 ? 0 : before));
    put(" ready=");
    put_num((uint64_t)(after < 0 ? 0 : after));
    put(ok ? " edge OK\n" : " FAIL\n");
}
