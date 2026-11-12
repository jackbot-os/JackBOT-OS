/* Origin: musl-1.2.5 src\thread\pthread_testcancel.c (MIT). Verbatim copy, no code change.
 * ABI adaptation lives in ports/musl_thread include/syscall_arch.h + src/clone.s.
 * See ports/musl_thread/README.md. */
#include "pthread_impl.h"

static void dummy()
{
}

weak_alias(dummy, __testcancel);

void __pthread_testcancel()
{
	__testcancel();
}

weak_alias(__pthread_testcancel, pthread_testcancel);
