#ifndef _THREADS_H
#define _THREADS_H

/* 移植层最小 threads.h（C11）：musl pthread_self.c / pthread_detach.c 用
 * weak_alias 给 thrd_* 起别名，这里给出与 musl 一致的声明。
 */
#include "pthread.h"

typedef pthread_t thrd_t;

int thrd_current(void);
int thrd_equal(thrd_t, thrd_t);
int thrd_detach(thrd_t);

#endif
