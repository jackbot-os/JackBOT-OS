#ifndef JCA_H
#define JCA_H
#include <stdint.h>

#define JCA_MAX_CODE 12000
#define JCA_MAX_STRS 400
#define JCA_STRLEN  200

struct jca_program {
    uint8_t code[JCA_MAX_CODE];
    int clen;
    char strs[JCA_MAX_STRS][JCA_STRLEN];
    int nstrs;
    char fname[64][24];
    int fentry[64];
    int fargc[64];
    int nfuncs;
    int main_entry;
};

void jca_init(void);
int jca_compile(const char *src, struct jca_program *p, char *err, int errsz);
int jca_run(struct jca_program *p, char *out, int outsz);
void jca_disasm(struct jca_program *p, char *out, int outsz);
void jca_self_test(void);

/* Native payloads: a .jca file starting with the JCAN magic carries a flat
   x86-64 image (built with tools/jcacc.py) instead of JCA bytecode. This
   spawns it as a ring-3 process; returns 0 on success. Header layout:
   "JCAN", u32 payload length, u32 entry offset (must be 0). */
int jca_run_native(const unsigned char *bin, int len);

#endif
