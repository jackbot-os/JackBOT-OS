/* Origin: musl-1.2.5 src\thread\pthread_mutex_init.c (MIT). Verbatim copy, no code change.
 * ABI adaptation lives in ports/musl_thread include/syscall_arch.h + src/clone.s.
 * See ports/musl_thread/README.md. */
#include "pthread_impl.h"

int pthread_mutex_init(pthread_mutex_t *restrict m, const pthread_mutexattr_t *restrict a)
{
	*m = (pthread_mutex_t){0};
	if (a) m->_m_type = a->__attr;
	return 0;
}
