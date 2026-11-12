/* Origin: musl-1.2.5 src\thread\pthread_equal.c (MIT). Verbatim copy, no code change.
 * ABI adaptation lives in ports/musl_thread include/syscall_arch.h + src/clone.s.
 * See ports/musl_thread/README.md. */
#include <pthread.h>
#include <threads.h>

static int __pthread_equal(pthread_t a, pthread_t b)
{
	return a==b;
}

weak_alias(__pthread_equal, pthread_equal);
weak_alias(__pthread_equal, thrd_equal);
