/*
 * Embedded CPython entry point for JackBOT OS.
 *
 * M1 target: this task is linked into the kernel payload with
 * libpython3.12.a and runs Py_Initialize + a trivial expression, with
 * output over serial. It does not run until PORTING.md milestone M1.
 */

#include "Python.h"
#include <kernel/serial.h>

static int run_bootstrap(void)
{
    PyStatus st;
    PyConfig cfg;
    PyConfig_InitIsolatedConfig(&cfg);
    cfg.parse_argv = 0;
    cfg.install_signal_handlers = 0;
    cfg.site_import = 0;          /* M2 enables site once the FS exists */
    /* frozen stdlib only at M1 */
    st = Py_InitializeFromConfig(&cfg);
    if (PyStatus_Exception(st)) {
        PyConfig_Clear(&cfg);
        serial_puts("[cpython] init failed\n");
        return -1;
    }
    PyConfig_Clear(&cfg);

    int rc = PyRun_SimpleString(
        "import sys\n"
        "print('CPython', sys.version.split()[0], 'on JackBOT')\n"
        "print('1+1 =', 1+1)\n");

    if (Py_FinalizeEx() < 0)
        rc = -1;
    return rc;
}

void cpython_jackbot_task(void)
{
    serial_puts("[cpython] booting upstream CPython 3.12.7\n");
    int rc = run_bootstrap();
    serial_puts(rc == 0 ? "[cpython] bootstrap OK\n"
                        : "[cpython] bootstrap FAILED\n");
}
