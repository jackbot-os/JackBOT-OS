# CPython for JackBOT OS — port layer

This directory is the **only** place that adapts upstream CPython to
JackBOT OS. The upstream source tree lives in `third_party/cpython` and is
kept **byte-for-byte unmodified** (Python 3.12.7 release tarball,
PSF License / Python-2.0).

## Layout

```
third_party/cpython/      upstream CPython 3.12.7, do NOT edit
ports/jackbot/
  README.md               this file
  PORTING.md              interface gap analysis and roadmap
  include/pyport_jackbot.h platform macros for the JackBOT kernel
  jackbot_syscalls.c      newlib/picolibc system call shims (libos)
  jackbot_main.c          embedded interpreter entry point
  Setup.jackbot           static module list (no dynamic loading)
```

## Legal / attribution

CPython is:

    Copyright (c) 2001-2024 Python Software Foundation; All Rights Reserved

    Licensed under the Python License (PSF License v2 / Python-2.0).

JackBOT OS only links and redistributes it under that license and keeps
the original `LICENSE` and copyright headers intact. The same notice is
shown in the desktop "About" dialog.

## Status (honest)

This is a **port scaffold**, not a working interpreter yet. Running the
upstream CPython on a bare-metal kernel requires a hosted C environment
(POSIX-ish libc + file descriptors + process/thread primitives). See
`PORTING.md` for the exact gap list and the staged plan. The legacy
self-contained interpreter in `kernel/py.c` is kept as a fallback and is
unrelated to CPython.

## Build intent (once the libos layer is ready)

1. Build a cross newlib (or picolibc) for the JackBOT target.
2. Configure CPython with `--disable-shared`, `--without-pymalloc`
   initially, static module list from `Setup.jackbot`.
3. Cross-compile `libpython3.12.a` against the shims.
4. Link `jackbot_main.c` + the libos into the kernel payload; the
   standard library (`Lib/`) is shipped on the FAT system volume and
   located through `PYTHONHOME`.
