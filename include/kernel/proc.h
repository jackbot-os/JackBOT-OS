#ifndef JACKBOT_PROC_H
#define JACKBOT_PROC_H

#include <stdint.h>
#include <stddef.h>

#define MAX_THREADS 64
#define KSTACK_SIZE 262144

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
    /* Per-process user heap break. Keeping it on the thread instead of a
       global means two user processes cannot walk on each other's heap. */
    uint64_t heap_cur;
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
    uint64_t user_entry;
    int recover_count;
    uint64_t recover_tick;
    /* ---- pthread / clone state (musl 1.2.5 ABI) ---- */
    uint64_t tid;          /* id reported by gettid and CLONE_PARENT_SETTID */
    uint64_t tls_base;     /* fs base restored on context switch */
    int has_tls;           /* 1 when tls_base is meaningful */
    uint64_t clear_tid;    /* CLONE_CHILD_CLEARTID / set_tid_address target */
    uint64_t wait_addr;    /* futex word this thread sleeps on, 0 = none */
    uint64_t exit_code;    /* value passed to SYS_exit */
    int detached;          /* no thread will ever read this slot again */
    uint64_t mmap_cur;     /* per-address-space anonymous map cursor */
    uint64_t boot_rax;     /* clone child's start registers: rax=0, r9=entry fn */
    uint64_t boot_r9;
} thread_t;

#define DEFAULT_QUOTA_MEM_BYTES (256ull * 1024 * 1024)
#define DEFAULT_QUOTA_CPU_TICKS 0

void sched_init(void);
thread_t *thread_create(void (*entry)(void *), void *arg);
thread_t *thread_create_user(uint64_t p4phys, uint64_t user_rip, uint64_t user_rsp);
/* CLONE_* child: shares the parent's page table, resumes at user_rip with
   user_rsp, rax = 0 and r9 = start_r9 (musl's clone calling convention). */
thread_t *thread_create_clone(thread_t *parent, uint64_t user_rip, uint64_t user_rsp,
                              uint64_t tls, uint64_t clear_tid, uint64_t start_r9);
void thread_yield(void);
/* fork(): child address space with copy-on-write pages. */
thread_t *thread_create_fork(thread_t *parent, uint64_t new_p4,
                             uint64_t user_rip, uint64_t user_rsp);
void thread_exit(void);
/* Exit the current thread with a status: honours CLONE_CHILD_CLEARTID
   (writes 0 to *clear_tid and wakes the futex) before switching away. */
void thread_exit_status(uint64_t code);
/* SYS_exit_group: tear down every thread sharing the current address space. */
void thread_exit_group(uint64_t code);
void thread_set_fs(uint64_t base);
uint64_t thread_get_fs(void);
/* futex services; timeout_ticks 0 means wait forever. */
int futex_wait_word(volatile int *uaddr, int expected, uint64_t timeout_ticks);
int futex_wake_word(volatile int *uaddr, int count);
void process_kill(thread_t *t, const char *reason);
thread_t *current_thread(void);
thread_t *thread_by_id(uint64_t id);
thread_t *thread_enum(int index);
int thread_count_user(void);
void sched_tick(void);

#endif
