#!/usr/bin/env bash
# JackBOT bare-metal pyconfig overrides + make (idempotent).
#
# Configure has no bare-metal entry, so after it generates pyconfig.h we
# (re)write an override block at the end of pyconfig.h:
#  - enable CPython's OWN single-thread pthread stubs (the same ones WASI
#    uses); supplies pthread_mutex_t/cond_t/key and the no-op TSS layer
#  - disable dynamic loading: static module list only
#  - supply POSIX macros that picolibc's freestanding headers do not expose
#
# Uses `make -k` so one pass surfaces as many missing-symbol errors as
# possible. Run after run_pyconfigure.sh.
set -uo pipefail
cd /root/Python-3.12.7 || exit 2

# Drop any previous override block, then append a fresh one.
sed -i '/JACKBOT_BAREMETAL_OVERRIDES/,$d' pyconfig.h
cat >> pyconfig.h <<'PYEOF'

/* ---- JACKBOT_BAREMETAL_OVERRIDES ---- */
/* CPython official single-thread pthread stubs (as used by WASI). */
#define HAVE_PTHREAD_STUBS 1
/* Static module build only; no dlopen on bare metal. */
#undef HAVE_DLOPEN
#undef HAVE_DYNAMIC_LOADING
/* Linux-only syscalls absent from picolibc. */
#undef HAVE_CLOSE_RANGE
/* Stage 1 has no fork/exec/wait, scheduler control, PTYs, uname, ctermid,
   chflags, renameat, getgrouplist or pthread_sigmask. Disabling these removes
   the matching posixmodule/faulthandler code (and the struct rusage fields
   picolibc does not define). */
#undef HAVE_SCHED_H
#undef HAVE_SCHED_GET_PRIORITY_MAX
#undef HAVE_SCHED_GET_PRIORITY_MIN
#undef HAVE_SCHED_GETSCHEDULER
#undef HAVE_SCHED_SETSCHEDULER
#undef HAVE_SCHED_GETPARAM
#undef HAVE_SCHED_SETPARAM
#undef HAVE_SCHED_RR_GET_INTERVAL
#undef HAVE_SCHED_SETAFFINITY
#undef HAVE_OPENPTY
#undef HAVE_FORKPTY
#undef HAVE_LOGIN_TTY
#undef HAVE_DEV_PTMX
#undef HAVE__GETPTY
#undef HAVE_CTERMID
#undef HAVE_CTERMID_R
#undef HAVE_CHFLAGS
#undef HAVE_RENAMEAT
#undef HAVE_UNAME
#undef HAVE_GETGROUPLIST
#undef HAVE_WAIT3
#undef HAVE_WAIT4
#undef HAVE_WAITID
#undef HAVE_PTHREAD_SIGMASK
#undef HAVE_PTHREAD_KILL
#undef HAVE_PTHREAD_GETCPUCLOCKID
/* scatter/gather and Linux-only file IO absent from picolibc. */
#undef HAVE_READV
#undef HAVE_WRITEV
#undef HAVE_PREADV
#undef HAVE_PREADV2
#undef HAVE_PWRITEV
#undef HAVE_PWRITEV2
#undef HAVE_SENDFILE
#undef HAVE_SPLICE
#undef HAVE_COPY_FILE_RANGE
#undef HAVE_POSIX_FALLOCATE
#undef HAVE_POSIX_FADVISE
#undef HAVE_GETLOADAVG
#undef HAVE_SETNS
#undef HAVE_UNSHARE
/* No process creation on stage 1 bare metal: posix_spawn (and the struct
   sched_param it pulls in) and the setres*id/getres*id syscalls. */
#undef HAVE_POSIX_SPAWN
#undef HAVE_POSIX_SPAWNP
#undef POSIX_SPAWN_SETSCHEDULER
#undef POSIX_SPAWN_SETSCHEDPARAM
#undef HAVE_SETRESUID
#undef HAVE_SETRESGID
#undef HAVE_GETRESUID
#undef HAVE_GETRESGID
/* picolibc freestanding headers do not expose these POSIX limits. */
#ifndef SSIZE_MAX
#define SSIZE_MAX 0x7fffffffffffffffL
#endif
/* inttypes format macros are absent from picolibc's freestanding inttypes.h
   (x86_64 LP64: int64_t is long, int32_t is int). */
#ifndef PRId64
#define PRId64 "ld"
#define PRIu64 "lu"
#define PRIx64 "lx"
#define PRIi64 "li"
#define PRId32 "d"
#define PRIu32 "u"
#define PRIx32 "x"
#define PRIi32 "i"
#endif
PYEOF

echo "--- relevant macros after patch ---"
grep -nE "HAVE_PTHREAD_STUBS|SSIZE_MAX" pyconfig.h | tail -5

# Use CPython's no-dynamic-loading backend (dynload_stub.c) instead of the
# dlopen() backend; the static build loads only built-in/frozen modules.
sed -i -E 's/DYNLOADFILE=[[:space:]]*dynload_shlib\.o/DYNLOADFILE= dynload_stub.o/' Makefile
echo "--- DYNLOADFILE ---"
grep -nE "^DYNLOADFILE" Makefile

export PATH=/root/bin:$PATH
# M1 target is the STATIC library only; do not build the .so shared
# extensions (which would need a bare-metal linker spec and are not used by
# the static module build).
make -j3 -k libpython3.12.a > /root/pylib.log 2>&1
echo "LIBMAKE=$?"
echo "=== libpython3.12.a ==="
ls -la libpython3.12.a 2>/dev/null || echo "NOT YET BUILT"
echo "=== .o compiled so far ==="
find . -name '*.o' | wc -l
echo "=== distinct error messages ==="
grep -h -i "error:" /root/pylib.log | sed -E 's/^[^:]*:[0-9]+:[0-9]+: //' | sort | uniq -c | sort -rn | head -30
