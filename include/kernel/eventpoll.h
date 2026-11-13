#ifndef KERNEL_EVENTPOLL_H
#define KERNEL_EVENTPOLL_H

#include <stdint.h>

/* epoll/poll layer: the readiness engine every future library (Skia, V8,
   Chromium) blocks on. Only plain descriptors exist today (console, files,
   pipe ends), so readiness is decided by the descriptor type, not by a
   driver callback table. */

#define EPOLL_MAX_INSTANCES 8
#define EPOLL_MAX_WATCH     32

#define EPOLLIN   0x001
#define EPOLLOUT  0x004
#define EPOLLERR  0x008
#define EPOLLHUP  0x010

struct epoll_event {
    uint32_t events;
    uint32_t pad;
    uint64_t data;
} __attribute__((packed));

void eventpoll_init(void);

/* epoll_create1(flags) -> epfd (>= 1000) or -1 */
int eventpoll_create(void);
/* epoll_ctl(epfd, op, fd, event): op 1=ADD 2=DEL 3=MOD */
int eventpoll_ctl(int epfd, int op, int fd, const struct epoll_event *ev);
/* epoll_wait(epfd, out, maxevents, timeout_ms). Returns the ready count,
   0 on timeout, -1 on a bad epfd. */
int eventpoll_wait(int epfd, struct epoll_event *out, int maxevents, int timeout_ms);
/* poll(fds, nfds, timeout_ms) built on the same readiness probe. */
struct pollfd_k {
    int fd;
    short events;
    short revents;
};
int eventpoll_poll(struct pollfd_k *fds, int nfds, int timeout_ms);

/* One [EPOLL] line after exercising a pipe pair end to end. */
void eventpoll_selftest(void);

#endif
