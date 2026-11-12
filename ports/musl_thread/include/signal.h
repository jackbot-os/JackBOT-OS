#ifndef _SIGNAL_H
#define _SIGNAL_H

/* 移植层最小 signal.h：只提供 musl 线程源码用到的部分
 * （sigset_t 布局对齐 musl x86_64：128 位，2 个 unsigned long）。
 * 内核无信号阶段：rt_sigprocmask 返回 ENOSYS，调用方均忽略返回值。
 */
#include <limits.h>

#define _NSIG 65

typedef struct {
	unsigned long __bits[128 / sizeof(long)];
} sigset_t;

#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

#endif
