# CPython 3.12.7 → JackBOT OS porting gap analysis

Goal: run the **unmodified** upstream CPython interpreter on JackBOT.
The only allowed code is this port layer + the kernel libos underneath.

CPython is not a freestanding program: it expects a hosted C runtime.
The standard way to provide one on a bare-metal kernel is a cross libc
(newlib/picolibc) whose system calls are implemented by the kernel.
This table maps every CPython platform dependency to the current
JackBOT kernel state.

## 1. Interface gap table

| CPython dependency | Used for | JackBOT kernel today | Action |
|---|---|---|---|
| malloc/realloc/free | object allocator (pymalloc wraps it) | `kmalloc`/`kfree` heap exists | implement `_sbrk` for newlib over kernel heap |
| string/stdio/math/qsort | core libc | kernel has own `kstring`, no hosted libc | provide newlib (it brings these) |
| open/read/write/close/lseek | import, `open()`, compile cache | FAT32 VFS + ATA driver exist | add fd table; map `_open/_read/_write/_lseek/_close` |
| stat/fstat/access | imports, `os.stat`, startup | VFS metadata partially exists | implement `_fstat`/`stat` |
| getcwd/chdir/mkdir/rename/unlink | `os` module | directory read/write exists | wire to VFS |
| isatty / termios | REPL, interactive mode | PS/2 kbd + framebuffer console | `_isatty`→1, minimal `tcgetattr` |
| write(fd 1/2) | stdout/stderr | serial + console text | `_write` → console/serial |
| gettimeofday / clock_gettime / time | time module, GC, timeouts | PIT timer exists | map to timer ticks |
| getpid / getppid / kill | `os` module | scheduler exists | return fixed pid 1; no-op signals |
| exit / _exit | interpreter shutdown | shutdown path exists | `_exit` → kernel task exit |
| fork / exec / wait / spawn | multiprocessing, subprocess | user processes exist, no exec loader | **disable for stage 1**; stub ENOSYS |
| signal / sigaction | signal module | IDT/IRQ dispatch exists | minimal; deliver only SIGINT (Ctrl-C) |
| pthread / threads | threading, GIL | kernel threads exist | stage 2: build *with* threads; stage 1 uses `--without-threads` |
| dlopen / dlsym | C extension loading | no ELF loader | **disable**: static module list (`Setup.jackbot`) |
| mmap / mprotect | some stdlib | paging exists (`vmm`) | stage 2 |
| socket / select | `_socket`, `select` | TCP/IP stack + e1000 exist | stage 3 |
| getenv / environ | PYTHONHOME, PATH | config store exists | static environment block |
| getentropy / getrandom | `os.urandom`, hash seeding | no RNG driver | read CPU RDRAND, else fixed seed + warning |
| tmpfile / tmpnam | tempfile | FAT writable | point TMPDIR to a RAM/FAT dir |
| locale / mbstowcs | unicode | kernel has own UTF-8 code | use libc built-in tables |
| iconv | codec fallback | absent | omitted (CPython bundles codecs) |
| poll / select | event loops | NIC IRQ exists | stage 3 |
| symlink / chmod / uid/gid | POSIX corner cases | FAT has no POSIX perms | return success/ENOSYS as appropriate |
| setjmp / longjmp | C stack (ceval uses no setjmp) | available in libc | from newlib |
| getloadavg / sysconf / uname | platform info | kernel version exists | return fixed values |

## 2. Build configuration

Cross target triple (proposed): `x86_64-jackbot-elf`
(host builds: Linux/WSL cross toolchain; no change to CPython).

```
./configure \
  --host=x86_64-jackbot-elf \
  --disable-shared \
  --without-threads \
  --without-ensurepip \
  --with-build-python=python3 \
  CONFIG_SITE=ports/jackbot/config.site
```

- Stage 1: `--without-threads`, no `dlopen`, built-in modules limited
  to pure core + frozen modules, so the interpreter can evaluate normal
  Python code and import the pure-Python parts of the stdlib.
- `pybuilddir.txt` / bootstrap: use the host Python to regenerate
  `Lib` bytecode (the normal cross build already supports this via
  `--with-build-python`).
- The standard library is copied to the system volume; `PYTHONHOME`
  points at it.

## 3. Staged milestones

- **M0 — scaffold (this commit).** Source in tree unmodified; this
  document; syscall shim skeletons; static module list; About attribution.
- **M1 — libc cross build.** Build newlib/picolibc for the target;
  implement `_sbrk`, `_write`, `_read`, `_exit`, `_isatty`, `_gettimeofday`.
  Cross-compile `libpython3.12.a`; link a kernel test task that runs
  `Py_Initialize` + `PyRun_SimpleString("print(1+1)")` and emits the
  result over serial. No filesystem yet (frozen stdlib only).
- **M2 — filesystem.** fd table over the FAT32 VFS; `open/read/write/
  lseek/stat/close`; ship `Lib/`; enable normal imports.
- **M3 — interactive REPL.** Wire stdio to the desktop console /
  terminal app; SIGINT handling; history.
- **M4 — threads + more modules.** Enable threads against the kernel
  scheduler; `_socket`/`select` over the existing TCP/IP stack; mmap.
- **M5 — process/exec.** ELF loader + `posix_spawn` for multiprocessing.

## 4. Honest scope note

M1–M3 is the bulk of the work and is comparable to porting CPython to a
new small RTOS (weeks of focused work, not one evening). The in-tree
self-contained interpreter `kernel/py.c` remains until M3 lands.
Nothing here claims CPython runs until the M1 serial test prints `2`.
