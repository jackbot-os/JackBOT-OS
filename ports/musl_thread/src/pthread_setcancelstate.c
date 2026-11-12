/* Origin: musl-1.2.5 src\thread\pthread_setcancelstate.c (MIT). Verbatim copy, no code change.
 * ABI adaptation lives in ports/musl_thread include/syscall_arch.h + src/clone.s.
 * See ports/musl_thread/README.md. */
#include "pthread_impl.h"

int __pthread_setcancelstate(int new, int *old)
{
	if (new > 2U) return EINVAL;
	struct pthread *self = __pthread_self();
	if (old) *old = self->canceldisable;
	self->canceldisable = new;
	return 0;
}

weak_alias(__pthread_setcancelstate, pthread_setcancelstate);
