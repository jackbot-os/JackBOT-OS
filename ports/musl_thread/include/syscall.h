#ifndef _INTERNAL_SYSCALL_H
#define _INTERNAL_SYSCALL_H

/* 移植层 syscall 编号表（对应 musl src/internal/syscall.h）。
 *
 * !!! 编号是占位分配，从 33 起，按“内核侧 syscall 表”为准同步 !!!
 * 内核侧（JackBOT）确认最终编号后，只改本文件即可，musl 源码不用动。
 * 负数返回约定：-errno。
 */
#include <features.h>
#include <errno.h>
#include "syscall_arch.h"

#define SYS_clone            33  /* 5 参 SysV: flags, child_stack, ptid(rdx), tls(r10), ctid(r8) */
#define SYS_exit             34  /* 只退当前线程；退出路径写 *ctid=0 并 futex 唤醒（CLONE_CHILD_CLEARTID） */
#define SYS_exit_group       35  /* 退整个进程 */
#define SYS_futex            36  /* 需要 FUTEX_WAIT(0) / FUTEX_WAKE(1) / FUTEX_REQUEUE(3) */
#define SYS_set_tid_address  37  /* musl start() 调度路径用；当前默认路径不触发 */
#define SYS_mmap             38
#define SYS_munmap           39
#define SYS_mprotect         40
#define SYS_rt_sigprocmask   41  /* 内核无信号阶段返回 ENOSYS，musl 侧均忽略返回值 */
#define SYS_set_robust_list  42  /* 仅 robust mutex 触发；默认 mutex 不触发 */
#define SYS_arch_prctl       43  /* ARCH_SET_FS=0x1002；主线程自举用 */
#define SYS_tkill            44
#define SYS_sched_setscheduler 45
#define SYS_clock_gettime    46  /* 仅 timedwait 带超时时触发；join/cond 无限等待不触发 */
#define SYS_gettid           47
#define SYS_write            48  /* 验收测试打印 */

/* musl 的 __syscall_cp 是取消点包装（src/thread/syscall_cp.c + x86_64 汇编）。
 * 移植版内核尚无取消机制，直接转发 __syscall（语义差异见 README）。 */
#define __syscall_cp(n,a,b,c,d,e) __syscall(n,a,b,c,d,e)

#endif
