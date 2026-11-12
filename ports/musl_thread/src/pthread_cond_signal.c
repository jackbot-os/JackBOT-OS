/* Origin: musl-1.2.5 src\thread\pthread_cond_signal.c (MIT). Verbatim copy, no code change.
 * ABI adaptation lives in ports/musl_thread include/syscall_arch.h + src/clone.s.
 * See ports/musl_thread/README.md. */
#include "pthread_impl.h"

int pthread_cond_signal(pthread_cond_t *c)
{
	if (!c->_c_shared) return __private_cond_signal(c, 1);
	if (!c->_c_waiters) return 0;
	a_inc(&c->_c_seq);
	__wake(&c->_c_seq, 1, 0);
	return 0;
}
