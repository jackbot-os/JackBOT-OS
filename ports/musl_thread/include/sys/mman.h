#ifndef _SYS_MMAN_H
#define _SYS_MMAN_H

/* 移植层最小 sys/mman.h：值与 musl x86_64 一致（Linux 约定）。
 */
#include <stddef.h>

#define PROT_NONE  0
#define PROT_READ  1
#define PROT_WRITE 2

#define MAP_PRIVATE 2
#define MAP_ANON    0x20

#define MAP_FAILED ((void *)-1)

typedef long off_t;

#endif
