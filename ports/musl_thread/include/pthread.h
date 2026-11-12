#ifndef _PTHREAD_H
#define _PTHREAD_H

/* 移植层公开 pthread.h —— 类型布局与常量严格对齐 musl-1.2.5 include/pthread.h
 * （MIT），因为移植的 musl 源码直接通过 pthread_impl.h 的 _m_* / _c_* / _a_*
 * 宏访问这些 union 的槽位，布局不一致会直接破坏算法。
 *
 * 注意：musl 官方 pthread_t = 无符号长（struct pthread 指针，即 TCB 句柄），
 * 与早期“整数 tid”草案不同 —— join/detach 依赖解引用该句柄，
 * 改为整数 tid 会破坏 musl 算法，故保持官方 ABI。内核侧不受影响
 * （内核仍按 SysV 五参 clone 收 tid 地址）。
 */
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "time.h"

typedef unsigned long pthread_t;
#define PTHREAD_NULL ((pthread_t)0)

typedef struct { union { int __i[6]; volatile int __vi[6]; volatile void *volatile __p[6]; } __u; } pthread_mutex_t;
typedef struct { union { int __i[12]; volatile int __vi[12]; void *volatile __p[12*sizeof(int)/sizeof(void*)]; } __u; } pthread_cond_t;
typedef struct { union { int __i[12]; volatile int __vi[12]; void *volatile __p[12*sizeof(int)/sizeof(void*)]; } __u; } pthread_rwlock_t;
typedef struct { unsigned __attr; } pthread_mutexattr_t;
typedef struct { unsigned __attr; } pthread_condattr_t;
typedef struct { unsigned __attr[7]; } pthread_attr_t;

enum {
	PTHREAD_CREATE_JOINABLE = 0,
	PTHREAD_CREATE_DETACHED = 1,
};

enum {
	PTHREAD_MUTEX_NORMAL = 0,
	PTHREAD_MUTEX_DEFAULT = 0,
	PTHREAD_MUTEX_RECURSIVE = 1,
	PTHREAD_MUTEX_ERRORCHECK = 2,
	PTHREAD_MUTEX_STALLED = 0,
	PTHREAD_MUTEX_ROBUST = 1,
	PTHREAD_PRIO_NONE = 0,
	PTHREAD_PRIO_INHERIT = 1,
	PTHREAD_PRIO_PROTECT = 2,
	PTHREAD_PROCESS_PRIVATE = 0,
	PTHREAD_PROCESS_SHARED = 1,
};

#define PTHREAD_MUTEX_INITIALIZER {{{0}}}
#define PTHREAD_COND_INITIALIZER {{{0}}}
#define PTHREAD_RWLOCK_INITIALIZER {{{0}}}

enum {
	PTHREAD_CANCEL_ENABLE = 0,
	PTHREAD_CANCEL_DISABLE = 1,
	PTHREAD_CANCEL_MASKED = 2,
	PTHREAD_CANCEL_DEFERRED = 0,
	PTHREAD_CANCEL_ASYNCHRONOUS = 1,
};

struct __ptcb {
	void (*__f)(void *);
	void *__x;
	struct __ptcb *__next;
};

_Noreturn void pthread_exit(void *);
int pthread_create(pthread_t *__restrict, const pthread_attr_t *__restrict, void *(*)(void *), void *__restrict);
int pthread_detach(pthread_t);
int pthread_join(pthread_t, void **);
pthread_t pthread_self(void);
int pthread_equal(pthread_t, pthread_t);
int pthread_setcancelstate(int, int *);
void pthread_testcancel(void);

int pthread_mutex_init(pthread_mutex_t *__restrict, const pthread_mutexattr_t *__restrict);
int pthread_mutex_destroy(pthread_mutex_t *);
int pthread_mutex_lock(pthread_mutex_t *);
int pthread_mutex_trylock(pthread_mutex_t *);
int pthread_mutex_timedlock(pthread_mutex_t *__restrict, const struct timespec *__restrict);
int pthread_mutex_unlock(pthread_mutex_t *);

int pthread_cond_init(pthread_cond_t *__restrict, const pthread_condattr_t *__restrict);
int pthread_cond_destroy(pthread_cond_t *);
int pthread_cond_wait(pthread_cond_t *__restrict, pthread_mutex_t *__restrict);
int pthread_cond_timedwait(pthread_cond_t *__restrict, pthread_mutex_t *__restrict, const struct timespec *__restrict);
int pthread_cond_signal(pthread_cond_t *);
int pthread_cond_broadcast(pthread_cond_t *);

#ifdef __cplusplus
}
#endif
#endif
