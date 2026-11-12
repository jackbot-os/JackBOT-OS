#ifndef _ERRNO_H
#define _ERRNO_H

/* 移植层 errno.h：错误码值来自 musl-1.2.5 arch/generic/bits/errno.h（verbatim），
 * errno 宏按 musl 语义指向当前线程的 errno_val（struct pthread 字段）。
 */
#include "bits/errno.h"

int *__errno_location(void);
#define errno (*__errno_location())

#endif
