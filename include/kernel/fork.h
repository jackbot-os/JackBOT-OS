#ifndef KERNEL_FORK_H
#define KERNEL_FORK_H

#include <stdint.h>

struct trap_frame;

/* SYS_fork: returns the child pid to the parent, and leaves the child
   resuming at the caller's next instruction with rax = 0. -1 on failure. */
int64_t sys_fork(struct trap_frame *f);

/* Prints one [COW] line after exercising the copy-on-write machinery. */
void cow_selftest(void);

#endif
