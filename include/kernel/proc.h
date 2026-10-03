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
    uint64_t cpu_ticks;
    uint64_t mem_bytes;
    uint64_t quota_mem_bytes;
    uint64_t quota_cpu_ticks;
    uint64_t last_alive_tick;
    const char *kill_reason;
    tstate_t state;
    struct thread *next;
    void *entry;
    void *arg;
} thread_t;

#define DEFAULT_QUOTA_MEM_BYTES (256ull * 1024 * 1024)
#define DEFAULT_QUOTA_CPU_TICKS 0

void sched_init(void);
thread_t *thread_create(void (*entry)(void *), void *arg);
thread_t *thread_create_user(uint64_t p4phys, uint64_t user_rip, uint64_t user_rsp);
void thread_yield(void);
void thread_exit(void);
void process_kill(thread_t *t, const char *reason);
thread_t *current_thread(void);
thread_t *thread_by_id(uint64_t id);
int thread_count_user(void);
void sched_tick(void);

#endif
