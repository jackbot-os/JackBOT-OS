# Python subset offline harness (task 335)

Local-only offline test for the real kernel `kernel/py.c` (the built-in minimal
Python subset interpreter, **not CPython**). libc stands in for the kernel heap,
stdout stands in for the serial port; the real `py.c` is compiled unchanged.
No real machine boot, no network, no exploit payload.

## Scope

- Subject under test: `kernel/py.c` — lexer, expression evaluator, statement
  executor, built-ins, functions, recursion, lists, dicts, strings, control flow.
- Stubs: `stub.c` provides `kmalloc` / `kfree` backed by libc and
  `serial_putc` / `serial_puts` / `serial_puthex` to stdout.
- `harness.c`: per-case positive expectations plus three must-error cases, then
  the built-in `py_self_test()`.

## Build and run (Debian build VM, userland gcc)

    gcc -Iinclude -Wall -Wextra -g \
        kernel/py.c tests/py_harness/stub.c tests/py_harness/harness.c \
        -o /tmp/py_harness
    /tmp/py_harness

Wrapper: `new-chat/py_harness_run.py` uploads `kernel/py.c` and this directory,
compiles, runs, and writes `new-chat/py_harness_out.txt`.

## Result (2026-10-29, Debian VM gcc 14.2.0)

- Compile: rc=0, no warnings on `py.c`.
- Per-case: 52/52 PASS (41 original self-test expressions, 3 must-error cases,
  8 added boundary cases), built-in `[PY] self-test OK`, harness rc=0.
- Full freestanding kernel rebuild on the same VM: `py.o` compiled and linked
  clean; only the pre-existing `ld: LOAD segment RWX permissions` warning remains.

## Hardening applied in this pass (defensive, no semantic change)

1. `vlist` / `vdict`: when a heap allocation fails the container now returns a
   safe empty `T_NONE` value instead of leaving a non-null length with a null
   pointer that later code would dereference. `vdict` now checks both allocations
   and releases the partial one on failure.
2. `range`: list size capped at `MAX_LISTE` (256) to match every other list in
   the interpreter; the previous 100 000 cap could request a multi-megabyte
   single heap block. After allocation failure the range case returns instead of
   writing through a null array.
3. `split`: the per-token stack buffer now stops writing at `MAX_STR - 1`, so a
   single over-long token cannot overflow the fixed buffer.
4. `str.replace`: the working buffer allocation failure now reports
   `out of memory` and returns after freeing arguments, instead of continuing on
   a null pointer.

## What this harness does NOT prove

- `py.c` is a hand-written subset. It does not implement full Python syntax, the
  standard library, file I/O, imports, classes, exceptions, or third-party
  packages.
- The harness runs on a libc heap with an 8 MB user stack; it cannot reproduce a
  real-kernel heap-fragmentation or specific GUI click/dispatch timing issue.
- The desktop open path (`apps.c` registration, `desktop.c` menu enumeration,
  `app_pyconsole.c` window frame) was reviewed statically and found wired up, but
  was **not** exercised on a booted VM in this pass: VM power operations were
  blocked by the local safety policy, so the live "window does not open" symptom
  could not be reproduced on real hardware here. Any remaining GUI-level cause
  still needs a booted VirtualBox session to confirm against the serial log.

## Honest open items for task 335

- Confirm on a booted VM whether the Python Console window opens from the Start
  menu and whether `py_self_test` prints `[PY] self-test OK` on the serial port.
- If the window still fails to open there, inspect the `desktop.c` click hit-test
  and `ui_window` close/active state next; the interpreter core itself already
  passes the offline cases above.
