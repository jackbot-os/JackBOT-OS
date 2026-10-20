#ifndef PY_H
#define PY_H
#include <stdint.h>

void py_init(void);
int py_exec(const char *src, char *out, int outsz);
void py_self_test(void);

#endif
