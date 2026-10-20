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

#endif
