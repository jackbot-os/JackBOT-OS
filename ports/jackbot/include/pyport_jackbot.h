#ifndef PYPORT_JACKBOT_H
#define PYPORT_JACKBOT_H

/* Platform glue for building upstream CPython 3.12 on JackBOT.
   Upstream files are not edited; the build passes these through
   CFLAGS/CPPFLAGS where needed. */

#define JACKBOT_PORT_VERSION 1

/* No dynamic linking in stage 1: all modules are static. */
#define HAVE_DYNAMIC_LOADING 0

/* Stage 1 is single-threaded; threads are enabled again at M4. */
#ifndef JACKBOT_WITH_THREADS
#define JACKBOT_WITH_THREADS 0
#endif

/* One process, pid 1, like a single-task RTOS port. */
#define JACKBOT_PID 1

/* Standard library location on the FAT system volume. */
#define JACKBOT_PYTHONHOME "/SYS/LIB/PYTHON"

#endif
