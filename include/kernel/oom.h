#ifndef KERNEL_OOM_H
#define KERNEL_OOM_H

#include <stdint.h>

/* Out-of-memory killer. When an allocation cannot be satisfied the kernel
   first reaps the largest user process (never the caller) and retries once;
   only if that fails does the allocation return NULL and the caller decide
   what to do. The policy is deliberately blunt: biggest memory footprint
   goes first, which is what a desktop wants when one runaway app is eating
   the machine. */

#define OOM_MIN_VICTIM_BYTES (2ull * 1024 * 1024)

/* Returns 1 if a victim was killed (memory should have been released),
   0 if there was nothing worth killing. why is logged to serial. */
int oom_reclaim(const char *why);

/* Counters for the monitor app / serial audit. */
uint64_t oom_kill_count(void);
uint64_t oom_last_victim_bytes(void);
const char *oom_last_victim_name(void);

#endif
