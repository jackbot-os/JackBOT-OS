#ifndef KERNEL_POSIX_H
#define KERNEL_POSIX_H

#include <stdint.h>

struct trap_frame;

/* POSIX-ish syscall numbers (int 0x80, rax = number). The legacy calls
   0..2 (putc / yield / exit) are handled by trap.c before this layer. */
#define SYS_WRITE   16
#define SYS_READ    17
#define SYS_OPEN    18
#define SYS_CLOSE   19
#define SYS_LSEEK   20
#define SYS_BRK     21
#define SYS_STAT    22
#define SYS_UNLINK  23
#define SYS_TIME    24
#define SYS_GETPID  25
#define SYS_FSTAT   26
#define SYS_MKDIR   27
#define SYS_RENAME  28
#define SYS_ACCESS  29
#define SYS_ISATTY  30
#define SYS_SLEEP   31   /* milliseconds */
#define SYS_UPTIME  32   /* seconds since boot */

/* Handle one POSIX call; the return value is written back into f->rax. */
void posix_syscall(struct trap_frame *f);

/* Exercise the syscall helpers once at boot and print [POSIX] lines. */
void posix_selftest(void);

#endif
