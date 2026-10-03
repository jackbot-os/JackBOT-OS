#ifndef JACKBOT_PROC_H
#define JACKBOT_PROC_H

#include <stdint.h>
#include <stddef.h>

#define MAX_THREADS 64
#define KSTACK_SIZE 16384

typedef enum {
    T_UNUSED = 0,
    T_READY,
    T_RUNNING,
    T_BLOCKED,
    T_ZOMBIE
} tstate_t;

typedef struct thread {
    uint64_t id;
    uint64_t rsp;
    uint64_t kstack;
    uint64_t kstack_top;
    tstate_t state;
    struct thread *next;
    void *entry;
    void *arg;
} thread_t;

void sched_init(void);
thread_t *thread_create(void (*entry)(void *), void *arg);
void thread_yield(void);
void thread_exit(void);
thread_t *current_thread(void);
void sched_tick(void);

#endif
