/* M1 smoke test: compile (not link) against the bare-metal picolibc sysroot.
 * Proves the cross clang can find the picolibc headers and that the libc
 * prototypes used by CPython (string/memory/stdlib) are available for
 * x86_64-unknown-none. Linking a runnable binary is M2 (kernel task, linker
 * script, crt, syscalls). See PORTING.md. */
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

int m1_smoke(void)
{
    char b[32];
    memset(b, 0, sizeof(b));
    memcpy(b, "JackBOT", 7);
    long n = (long)strlen(b);
    void *p = malloc(64);
    if (p) {
        free(p);
    }
    return (int)n + 1;
}
