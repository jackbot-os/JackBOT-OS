#ifndef JACKBOT_JCAC_H
#define JACKBOT_JCAC_H
#include <kernel/jca.h>

/* C subset front end: compiles C source into JCA bytecode.
   Returns 0 on success; on failure -1 and a message in err. */
int jcac_compile(const char *src, struct jca_program *p, char *err, int errsz);
/* Compiles and runs four C programs, prints [C] results to the serial log. */
void jcac_self_test(void);

#endif
