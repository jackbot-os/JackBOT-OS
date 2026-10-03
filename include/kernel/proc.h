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
    uint64_t p4phys;
    int is_user;
    tstate_t state;
    struct thread *next;
    void *entry;
    void *arg;
} thread_t;

void sched_init(void);
thread_t *thread_create(void (*entry)(void *), void *arg);
thread_t *thread_create_user(uint64_t p4phys, uint64_t user_rip, uint64_t user_rsp);
void thread_yield(void);
void thread_exit(void);
thread_t *current_thread(void);
void sched_tick(void);

#endif
