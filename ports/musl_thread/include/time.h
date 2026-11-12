#ifndef _TIME_H
#define _TIME_H

/* 移植层最小 time.h：musl 线程源码只用到 struct timespec / clockid_t / CLOCK_REALTIME。
 * x86_64 下 time_t 为 64 位 long（与 musl 一致）。
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef long time_t;
typedef int clockid_t;

struct timespec {
	time_t tv_sec;
	long tv_nsec;
};

#define CLOCK_REALTIME 0

#ifdef __cplusplus
}
#endif
#endif
