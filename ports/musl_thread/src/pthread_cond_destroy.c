/* Origin: musl-1.2.5 src\thread\pthread_cond_destroy.c (MIT). Verbatim copy, no code change.
 * ABI adaptation lives in ports/musl_thread include/syscall_arch.h + src/clone.s.
 * See ports/musl_thread/README.md. */
#include "pthread_impl.h"

int pthread_cond_destroy(pthread_cond_t *c)
{
	if (c->_c_shared && c->_c_waiters) {
		int cnt;
		a_or(&c->_c_waiters, 0x80000000);
		a_inc(&c->_c_seq);
		__wake(&c->_c_seq, -1, 0);
		while ((cnt = c->_c_waiters) & 0x7fffffff)
			__wait(&c->_c_waiters, 0, cnt, 0);
	}
	return 0;
}
