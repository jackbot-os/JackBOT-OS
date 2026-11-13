#ifndef KERNEL_POSIX_H
#define KERNEL_POSIX_H

#include <stdint.h>

struct trap_frame;

/* POSIX-ish syscall numbers (int 0x80, rax = number). The legacy calls
   0..2 (putc / yield / exit) are handled by trap.c before this layer. */
#define SYS_WRITE   16
#define SYS_READ    17
#define SYS_OPEN    18
#define SYS_CLOSE   19
#define SYS_LSEEK   20
#define SYS_BRK     21
#define SYS_STAT    22
#define SYS_UNLINK  23
#define SYS_TIME    24
#define SYS_GETPID  25
#define SYS_FSTAT   26
#define SYS_MKDIR   27
#define SYS_RENAME  28
#define SYS_ACCESS  29
#define SYS_ISATTY  30
#define SYS_SLEEP   31   /* milliseconds */
#define SYS_UPTIME  32   /* seconds since boot */

/* ---- pthread / clone layer (musl 1.2.5 port in ports/musl_thread) ----
   These numbers are mirrored by ports/musl_thread/include/syscall.h; the
   kernel table is authoritative for JackBOT. */
#define SYS_CLONE        33  /* flags, stack, ptid, tls, ctid (SysV order) */
#define SYS_THREAD_EXIT  34  /* ends the calling thread only */
#define SYS_EXIT_GROUP   35  /* ends every thread of the address space */
#define SYS_FUTEX        36  /* WAIT(0)/WAKE(1); REQUEUE(3) reports ENOSYS */
#define SYS_SET_TID_ADDR 37  /* set_tid_address: park *addr for clear-on-exit */
#define SYS_MMAP         38  /* anonymous, MAP_FIXED is not honoured */
#define SYS_MUNMAP       39
#define SYS_MPROTECT     40  /* accepted, pages are always RW + NX */
#define SYS_RT_SIGPROCMASK 41  /* no signals yet: ENOSYS */
#define SYS_SET_ROBUST_LIST 42 /* ENOSYS */
#define SYS_ARCH_PRCTL   43  /* ARCH_SET_FS = 0x1002 */
#define SYS_TKILL        44  /* ENOSYS */
#define SYS_SCHED_SETSCHEDULER 45  /* accepted, one policy */
#define SYS_CLOCK_GETTIME 46
#define SYS_GETTID       47
#define SYS_WRITE_THREAD 48  /* musl's SYS_write; same console/file path */

/* ---- IPC layer (kernel/ipc.c) ---- */
#define SYS_PIPE        49  /* int fds[2] -> 0 / -1 */
#define SYS_SHM_CREATE  50  /* size -> handle */
#define SYS_SHM_MAP     51  /* handle, vaddr -> 0 / -1 */
#define SYS_SEM_CREATE  52  /* value -> id */
#define SYS_SEM_WAIT    53
#define SYS_SEM_POST    54
#define SYS_MQ_CREATE   55  /* -> id */
#define SYS_MQ_SEND     56  /* id, buf, len -> 0 / -1 */
#define SYS_MQ_RECV     57  /* id, buf, len -> len / -1 */
#define SYS_FORK        58  /* -> child pid in the parent, 0 in the child */

/* ---- readiness layer (kernel/eventpoll.c) ---- */
#define SYS_EPOLL_CREATE1 59 /* flags -> epfd */
#define SYS_EPOLL_CTL     60 /* epfd, op, fd, event* */
#define SYS_EPOLL_WAIT    61 /* epfd, events*, maxevents, timeout_ms */
#define SYS_POLL          62 /* pollfd*, nfds, timeout_ms */

/* Handle one POSIX call; the return value is written back into f->rax. */
void posix_syscall(struct trap_frame *f);

/* Exercise the syscall helpers once at boot and print [POSIX] lines. */
void posix_selftest(void);

#endif
