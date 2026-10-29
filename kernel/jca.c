#include <stdint.h>
#include <stddef.h>
#include <kernel/jca.h>
#include <kernel/heap.h>
#include <kernel/serial.h>

enum {
    OP_HALT = 0, OP_PUSHI, OP_PUSHS, OP_PUSHNIL,
    OP_LOAD, OP_STORE, OP_POP,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_NEG,
    OP_LT, OP_GT, OP_LE, OP_GE, OP_EQ, OP_NE,
    OP_AND, OP_OR, OP_NOT,
    OP_JMP, OP_JZ, OP_JNZ,
    OP_CALL, OP_RET,
    OP_BPRINT, OP_BLEN, OP_BSTR, OP_BINT, OP_BABS
};

enum { T_NUM, T_STR, T_NAME, T_SYM };

typedef struct {
    int type;
    int64_t num;
    char s[JCA_STRLEN];
    int line;
} Tok;

typedef struct {
    int t;
    int64_t i;
    char *s;
} JV;

typedef struct {
    char name[24];
    int argc;
    char params[8][24];
    int start;
    int body;
    int end;
    int entry;
    int idx;
} FnDecl;

static Tok TK[5000];
static int ntok;
static int tp;

static struct jca_program *P;
static FnDecl fns[64];
static int nfns;

static char lnames[24][24];
static int nlname;

static char g_err[200];
static int g_fail;

static char g_out[2000];
static int g_outn;

static JV stk[600];
static int sp;
typedef struct {
    int ret;
    JV loc[24];
} Frame;
static Frame frm[64];
static int fc;

static char rt[160][JCA_STRLEN];
static int rti;

static void emit(uint8_t op)
{
    if (P->clen < JCA_MAX_CODE)
        P->code[P->clen++] = op;
}
static void emit32(int32_t v)
{
    emit((uint8_t)(v & 0xff));
    emit((uint8_t)((v >> 8) & 0xff));
    emit((uint8_t)((v >> 16) & 0xff));
    emit((uint8_t)((v >> 24) & 0xff));
}
static int here(void) { return P->clen; }
static void patch(int at, int32_t v)
{
    P->code[at] = (uint8_t)(v & 0xff);
    P->code[at + 1] = (uint8_t)((v >> 8) & 0xff);
    P->code[at + 2] = (uint8_t)((v >> 16) & 0xff);
    P->code[at + 3] = (uint8_t)((v >> 24) & 0xff);
}
static int32_t rd32(const uint8_t *c)
{
    return (int32_t)(c[0] | (c[1] << 8) | (c[2] << 16) | ((int32_t)c[3] << 24));
}

static void cfail(const char *m)
{
    if (!g_fail) {
        int n = 0;
        while (m[n] && n < 180) { g_err[n] = m[n]; n++; }
        g_err[n] = 0;
        g_fail = 1;
    }
}

static int seq(const char *a, const char *b)
{
    int i = 0;
    while (a[i] && b[i]) { if (a[i] != b[i]) return 0; i++; }
    return a[i] == b[i];
}
static void scopy(char *d, const char *s, int max)
{
    int i = 0;
    while (s[i] && i < max - 1) { d[i] = s[i]; i++; }
    d[i] = 0;
}

static int intern(const char *s)
{
    for (int i = 0; i < P->nstrs; i++)
        if (seq(P->strs[i], s)) return i;
    if (P->nstrs < JCA_MAX_STRS) {
        scopy(P->strs[P->nstrs], s, JCA_STRLEN);
        return P->nstrs++;
    }
    cfail("too many strings");
    return 0;
}

static int is_digit(int c) { return c >= '0' && c <= '9'; }
static int is_alpha(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static int is_alnum(int c) { return is_alpha(c) || is_digit(c); }

static void tokenize(const char *src)
{
    int i = 0, line = 1;
    ntok = 0;
    while (src[i]) {
        char c = src[i];
        if (c == '\n') { line++; i++; continue; }
        if (c == ' ' || c == '\t' || c == '\r') { i++; continue; }
        if (c == '/' && src[i + 1] == '/') {
            while (src[i] && src[i] != '\n') i++;
            continue;
        }
        if (c == '/' && src[i + 1] == '*') {
            i += 2;
            while (src[i] && !(src[i] == '*' && src[i + 1] == '/')) {
                if (src[i] == '\n') line++;
                i++;
            }
            if (src[i]) i += 2;
            continue;
        }
        if (ntok >= 5000) { cfail("program too large"); return; }
        Tok *t = &TK[ntok];
        t->line = line;
        if (is_digit(c)) {
            int64_t v = 0;
            while (is_digit(src[i])) { v = v * 10 + (src[i] - '0'); i++; }
            t->type = T_NUM; t->num = v; t->s[0] = 0;
            ntok++;
            continue;
        }
        if (c == '"' || c == '\'') {
            char q = c;
            i++;
            int k = 0;
            while (src[i] && src[i] != q && src[i] != '\n') {
                char ch = src[i];
                if (ch == '\\' && src[i + 1]) {
                    char nx = src[i + 1];
                    if (nx == 'n') ch = '\n';
                    else if (nx == 't') ch = '\t';
                    else if (nx == 'r') ch = '\r';
                    else if (nx == '0') ch = 0;
                    else ch = nx;
                    i += 2;
                } else {
                    i++;
                }
                if (k < JCA_STRLEN - 1) t->s[k++] = ch;
            }
            t->s[k] = 0;
            if (src[i] == q) i++;
            else cfail("unterminated string");
            t->type = T_STR; t->num = 0;
            ntok++;
            continue;
        }
        if (is_alpha(c)) {
            int k = 0;
            while (is_alnum(src[i]) && k < JCA_STRLEN - 1) { t->s[k++] = src[i++]; }
            t->s[k] = 0;
            t->type = T_NAME; t->num = 0;
            ntok++;
            continue;
        }
        t->type = T_SYM; t->num = 0;
        if ((c == '=' && src[i + 1] == '=') ||
            (c == '!' && src[i + 1] == '=') ||
            (c == '<' && src[i + 1] == '=') ||
            (c == '>' && src[i + 1] == '=') ||
            (c == '&' && src[i + 1] == '&') ||
            (c == '|' && src[i + 1] == '|')) {
            t->s[0] = c; t->s[1] = src[i + 1]; t->s[2] = 0;
            i += 2;
        } else {
            t->s[0] = c; t->s[1] = 0;
            i++;
        }
        ntok++;
    }
}

static Tok *cur(void) { return &TK[tp]; }
static Tok *pk(int k) { return (tp + k < ntok) ? &TK[tp + k] : &TK[ntok - 1]; }
static int ended(void) { return tp >= ntok; }
static void adv(void) { if (tp < ntok) tp++; }
static int is_sym(const char *s)
{
    return !ended() && cur()->type == T_SYM && seq(cur()->s, s);
}
static int is_kw(const char *s)
{
    return !ended() && cur()->type == T_NAME && seq(cur()->s, s);
}
static int accept_sym(const char *s)
{
    if (is_sym(s)) { adv(); return 1; }
    return 0;
}
static void expect_sym(const char *s)
{
    if (!accept_sym(s)) {
        char m[40];
        int n = 0;
        const char *p = "expected '";
        while (p[n]) { m[n] = p[n]; n++; }
        int k = 0;
        while (s[k] && n < 34) { m[n++] = s[k++]; }
        m[n++] = '\''; m[n] = 0;
        cfail(m);
    }
}

static void enter_locals(void)
{
    nlname = 0;
}
static int declare_local(const char *name)
{
    for (int i = 0; i < nlname; i++)
        if (seq(lnames[i], name)) return i;
    if (nlname < 24) {
        scopy(lnames[nlname], name, 24);
        return nlname++;
    }
    cfail("too many locals");
    return 0;
}
static int resolve_local(const char *name)
{
    for (int i = 0; i < nlname; i++)
        if (seq(lnames[i], name)) return i;
    return -1;
}

static int fn_index(const char *name)
{
    for (int i = 0; i < nfns; i++)
        if (seq(fns[i].name, name)) return i;
    return -1;
}

static int is_builtin(const char *name)
{
    return seq(name, "print") || seq(name, "len") || seq(name, "str")
        || seq(name, "int") || seq(name, "abs");
}

static void scan_functions(void)
{
    nfns = 0;
    int depth = 0;
    int i = 0;
    while (i < ntok) {
        Tok *t = &TK[i];
        if (t->type == T_SYM && seq(t->s, "{")) { depth++; i++; continue; }
        if (t->type == T_SYM && seq(t->s, "}")) { depth--; i++; continue; }
        if (depth == 0 && t->type == T_NAME && seq(t->s, "fn")) {
            if (nfns >= 64) { cfail("too many functions"); return; }
            FnDecl *f = &fns[nfns];
            int j = i + 1;
            if (j >= ntok || TK[j].type != T_NAME) { cfail("bad function name"); return; }
            scopy(f->name, TK[j].s, 24);
            f->start = i;
            f->idx = nfns;
            j++;
            if (j >= ntok || !(TK[j].type == T_SYM && seq(TK[j].s, "("))) { cfail("expected ("); return; }
            j++;
            f->argc = 0;
            while (j < ntok && !(TK[j].type == T_SYM && seq(TK[j].s, ")"))) {
                if (TK[j].type == T_NAME && f->argc < 8) {
                    scopy(f->params[f->argc], TK[j].s, 24);
                    f->argc++;
                }
                j++;
                if (j < ntok && TK[j].type == T_SYM && seq(TK[j].s, ",")) j++;
            }
            if (j >= ntok) { cfail("unclosed function"); return; }
            j++;
            if (j >= ntok || !(TK[j].type == T_SYM && seq(TK[j].s, "{"))) { cfail("expected {"); return; }
            f->body = j + 1;
            int d2 = 1;
            j++;
            while (j < ntok && d2 > 0) {
                if (TK[j].type == T_SYM && seq(TK[j].s, "{")) d2++;
                else if (TK[j].type == T_SYM && seq(TK[j].s, "}")) d2--;
                if (d2 > 0) j++;
            }
            f->end = j;
            nfns++;
            i = j + 1;
            continue;
        }
        i++;
    }
}

static void gen_expr(void);
static void gen_stmt(void);

static void gen_block(void)
{
    expect_sym("{");
    while (!g_fail && !ended() && !is_sym("}"))
        gen_stmt();
    expect_sym("}");
}

static void gen_args_call(const char *name)
{
    expect_sym("(");
    int argc = 0;
    if (!is_sym(")")) {
        gen_expr();
        argc++;
        while (accept_sym(",")) { gen_expr(); argc++; }
    }
    expect_sym(")");
    if (seq(name, "print")) {
        emit(OP_BPRINT); emit32(argc);
    } else if (seq(name, "len")) {
        if (argc != 1) cfail("len takes 1 arg");
        emit(OP_BLEN);
    } else if (seq(name, "str")) {
        if (argc != 1) cfail("str takes 1 arg");
        emit(OP_BSTR);
    } else if (seq(name, "int")) {
        if (argc != 1) cfail("int takes 1 arg");
        emit(OP_BINT);
    } else if (seq(name, "abs")) {
        if (argc != 1) cfail("abs takes 1 arg");
        emit(OP_BABS);
    } else {
        int fi = fn_index(name);
        if (fi < 0) { cfail("unknown function"); return; }
        if (argc != fns[fi].argc) cfail("wrong argument count");
        emit(OP_CALL); emit32(fi); emit32(argc);
    }
}

static void gen_primary(void)
{
    if (g_fail || ended()) { cfail("unexpected end"); return; }
    Tok *t = cur();
    if (t->type == T_NUM) {
        emit(OP_PUSHI); emit32((int32_t)t->num);
        adv();
    } else if (t->type == T_STR) {
        emit(OP_PUSHS); emit32(intern(t->s));
        adv();
    } else if (t->type == T_NAME) {
        char nm[24];
        scopy(nm, t->s, 24);
        if (seq(nm, "true")) { emit(OP_PUSHI); emit32(1); adv(); }
        else if (seq(nm, "false")) { emit(OP_PUSHI); emit32(0); adv(); }
        else if (seq(nm, "nil")) { emit(OP_PUSHNIL); adv(); }
        else {
            adv();
            if (is_sym("(")) {
                if (is_builtin(nm) || fn_index(nm) >= 0)
                    gen_args_call(nm);
                else { cfail("unknown function"); }
            } else {
                int slot = resolve_local(nm);
                if (slot < 0) { cfail("unknown variable"); return; }
                emit(OP_LOAD); emit32(slot);
            }
        }
    } else if (is_sym("(")) {
        adv();
        gen_expr();
        expect_sym(")");
    } else {
        cfail("bad expression");
    }
}

static void gen_unary(void)
{
    if (is_sym("!")) { adv(); gen_unary(); emit(OP_NOT); }
    else if (is_sym("-")) { adv(); gen_unary(); emit(OP_NEG); }
    else gen_primary();
}

static void gen_binop(const char *const *ops, const uint8_t *opcodes, int nops,
                      void (*sub)(void))
{
    sub();
    while (!g_fail && !ended() && cur()->type == T_SYM) {
        int found = -1;
        for (int k = 0; k < nops; k++) {
            if (seq(cur()->s, ops[k])) { found = k; break; }
        }
        if (found < 0) break;
        adv();
        sub();
        emit(opcodes[found]);
    }
}

static void gen_mul(void)
{
    static const char *ops[3] = { "*", "/", "%" };
    static const uint8_t oc[3] = { OP_MUL, OP_DIV, OP_MOD };
    gen_binop(ops, oc, 3, gen_unary);
}
static void gen_add(void)
{
    static const char *ops[2] = { "+", "-" };
    static const uint8_t oc[2] = { OP_ADD, OP_SUB };
    gen_binop(ops, oc, 2, gen_mul);
}
static void gen_rel(void)
{
    static const char *ops[4] = { "<", ">", "<=", ">=" };
    static const uint8_t oc[4] = { OP_LT, OP_GT, OP_LE, OP_GE };
    gen_binop(ops, oc, 4, gen_add);
}
static void gen_eq(void)
{
    static const char *ops[2] = { "==", "!=" };
    static const uint8_t oc[2] = { OP_EQ, OP_NE };
    gen_binop(ops, oc, 2, gen_rel);
}
static void gen_and(void)
{
    static const char *ops[1] = { "&&" };
    static const uint8_t oc[1] = { OP_AND };
    gen_binop(ops, oc, 1, gen_eq);
}
static void gen_or(void)
{
    static const char *ops[1] = { "||" };
    static const uint8_t oc[1] = { OP_OR };
    gen_binop(ops, oc, 1, gen_and);
}
static void gen_expr(void) { gen_or(); }

static void gen_stmt(void)
{
    if (g_fail || ended()) return;
    if (is_sym("{")) { gen_block(); return; }
    if (is_kw("if")) {
        adv();
        expect_sym("(");
        gen_expr();
        expect_sym(")");
        emit(OP_JZ); int jz = here(); emit32(0);
        gen_block();
        int jend = -1;
        if (is_kw("else")) {
            adv();
            emit(OP_JMP); jend = here(); emit32(0);
            patch(jz, here());
            if (is_kw("if")) gen_stmt();
            else gen_block();
        }
        if (jend >= 0) patch(jend, here());
        else patch(jz, here());
        return;
    }
    if (is_kw("while")) {
        adv();
        int top = here();
        expect_sym("(");
        gen_expr();
        expect_sym(")");
        emit(OP_JZ); int jz = here(); emit32(0);
        gen_block();
        emit(OP_JMP); emit32(top);
        patch(jz, here());
        return;
    }
    if (is_kw("return")) {
        adv();
        if (is_sym(";") || is_sym("}")) {
            emit(OP_PUSHNIL);
        } else {
            gen_expr();
        }
        if (is_sym(";")) adv();
        emit(OP_RET);
        return;
    }
    if (is_kw("var")) {
        adv();
        if (ended() || cur()->type != T_NAME) { cfail("expected variable name"); return; }
        char nm[24];
        scopy(nm, cur()->s, 24);
        adv();
        int slot = declare_local(nm);
        if (accept_sym("=")) {
            gen_expr();
        } else {
            emit(OP_PUSHNIL);
        }
        emit(OP_STORE); emit32(slot);
        accept_sym(";");
        return;
    }
    if (cur()->type == T_NAME && pk(1)->type == T_SYM && seq(pk(1)->s, "=")) {
        char nm[24];
        scopy(nm, cur()->s, 24);
        adv();
        adv();
        gen_expr();
        int slot = resolve_local(nm);
        if (slot < 0) slot = declare_local(nm);
        emit(OP_STORE); emit32(slot);
        accept_sym(";");
        return;
    }
    gen_expr();
    emit(OP_POP);
    accept_sym(";");
}

static void compile_main(void)
{
    tp = 0;
    enter_locals();
    while (!g_fail && !ended()) {
        int skip = -1;
        if (cur()->type == T_NAME && seq(cur()->s, "fn")) {
            for (int i = 0; i < nfns; i++)
                if (fns[i].start == tp) { skip = i; break; }
            if (skip >= 0) { tp = fns[skip].end + 1; continue; }
        }
        gen_stmt();
    }
    emit(OP_HALT);
}

static void compile_functions(void)
{
    for (int fi = 0; fi < nfns; fi++) {
        FnDecl *f = &fns[fi];
        f->entry = here();
        enter_locals();
        for (int a = 0; a < f->argc; a++)
            declare_local(f->params[a]);
        tp = f->body;
        while (!g_fail && tp < f->end)
            gen_stmt();
        emit(OP_PUSHNIL);
        emit(OP_RET);
    }
}

int jca_compile(const char *src, struct jca_program *p, char *err, int errsz)
{
    P = p;
    P->clen = 0; P->nstrs = 0; P->nfuncs = 0;
    g_fail = 0; g_err[0] = 0;
    tokenize(src);
    if (g_fail) goto bad;
    scan_functions();
    if (g_fail) goto bad;
    for (int i = 0; i < nfns; i++) {
        scopy(P->fname[i], fns[i].name, 24);
        P->fargc[i] = fns[i].argc;
        P->nfuncs++;
    }
    compile_main();
    if (g_fail) goto bad;
    compile_functions();
    if (g_fail) goto bad;
    for (int i = 0; i < nfns; i++)
        P->fentry[i] = fns[i].entry;
    return 0;
bad:
    {
        int n = 0;
        while (g_err[n] && n < errsz - 1) { err[n] = g_err[n]; n++; }
        err[n] = 0;
    }
    return -1;
}

static void outstr(const char *s)
{
    int n = 0;
    while (s[n] && g_outn < (int)sizeof(g_out) - 2)
        g_out[g_outn++] = s[n++];
}
static void outchar(char c)
{
    if (g_outn < (int)sizeof(g_out) - 2)
        g_out[g_outn++] = c;
}
static void outi(int64_t v)
{
    char tmp[24];
    int n = 0;
    int neg = 0;
    uint64_t u;
    if (v < 0) { neg = 1; u = (uint64_t)(-v); } else u = (uint64_t)v;
    if (u == 0) tmp[n++] = '0';
    while (u) { tmp[n++] = '0' + (u % 10); u /= 10; }
    if (neg) outchar('-');
    while (n) outchar(tmp[--n]);
}
static char *rt_new(void)
{
    char *r = rt[rti % 160];
    rti++;
    r[0] = 0;
    return r;
}
static int slen(const char *s) { int n = 0; while (s && s[n]) n++; return n; }

static JV jv_int(int64_t i) { JV v; v.t = 1; v.i = i; v.s = 0; return v; }
static JV jv_str(const char *s) { JV v; v.t = 2; v.i = 0; v.s = (char *)s; return v; }
static JV jv_nil(void) { JV v; v.t = 0; v.i = 0; v.s = 0; return v; }

static void push(JV v)
{
    if (sp < 600) stk[sp++] = v;
    else cfail("stack overflow");
}
static JV pop(void)
{
    if (sp > 0) return stk[--sp];
    cfail("stack underflow");
    return jv_nil();
}

static int truthy(JV v)
{
    if (v.t == 1) return v.i != 0;
    if (v.t == 2) return v.s && v.s[0];
    return 0;
}

static char *num_to_str(int64_t v)
{
    char *r = rt_new();
    int n = 0, neg = 0;
    uint64_t u;
    char tmp[24];
    int k = 0;
    if (v < 0) { neg = 1; u = (uint64_t)(-v); } else u = (uint64_t)v;
    if (u == 0) tmp[k++] = '0';
    while (u) { tmp[k++] = '0' + (u % 10); u /= 10; }
    if (neg && n < JCA_STRLEN - 1) r[n++] = '-';
    while (k && n < JCA_STRLEN - 1) r[n++] = tmp[--k];
    r[n] = 0;
    return r;
}
static char *str_add(JV a, JV b)
{
    char *r = rt_new();
    int n = 0;
    const char *sa = a.t == 2 ? a.s : num_to_str(a.i);
    const char *sb = b.t == 2 ? b.s : num_to_str(b.i);
    int ia = a.t == 2 ? 0 : 1, ib = b.t == 2 ? 0 : 1;
    (void)ia; (void)ib;
    int la = slen(sa), lb = slen(sb);
    for (int i = 0; i < la && n < JCA_STRLEN - 1; i++) r[n++] = sa[i];
    for (int i = 0; i < lb && n < JCA_STRLEN - 1; i++) r[n++] = sb[i];
    r[n] = 0;
    return r;
}

static void print_val(JV v)
{
    if (v.t == 1) outi(v.i);
    else if (v.t == 2) outstr(v.s ? v.s : "");
    else outstr("nil");
}

static int jv_eq(JV a, JV b)
{
    if (a.t == 1 && b.t == 1) return a.i == b.i;
    if (a.t == 2 && b.t == 2) {
        const char *x = a.s ? a.s : "", *y = b.s ? b.s : "";
        int i = 0;
        while (x[i] && y[i]) { if (x[i] != y[i]) return 0; i++; }
        return x[i] == y[i];
    }
    if (a.t == 0 && b.t == 0) return 1;
    return 0;
}

int jca_run(struct jca_program *p, char *out, int outsz)
{
    g_fail = 0; g_err[0] = 0; g_outn = 0; g_out[0] = 0;
    sp = 0; fc = 0; rti = 0;
    frm[0].ret = -1;
    for (int k = 0; k < 24; k++) frm[0].loc[k] = jv_nil();
    fc = 1;
    int ip = 0;
    int steps = 0;
    while (!g_fail && ip < p->clen) {
        if (++steps > 5000000) { cfail("execution step limit"); break; }
        uint8_t op = p->code[ip++];
        switch (op) {
        case OP_HALT: goto done;
        case OP_PUSHI: push(jv_int(rd32(p->code + ip))); ip += 4; break;
        case OP_PUSHS: {
            int si = rd32(p->code + ip); ip += 4;
            if (si < 0 || si >= p->nstrs) { cfail("bad string index"); break; }
            push(jv_str(p->strs[si]));
            break;
        }
        case OP_PUSHNIL: push(jv_nil()); break;
        case OP_LOAD: {
            int slot = rd32(p->code + ip); ip += 4;
            if (slot < 0 || slot >= 24) { cfail("bad local slot"); break; }
            push(frm[fc - 1].loc[slot]);
            break;
        }
        case OP_STORE: {
            int slot = rd32(p->code + ip); ip += 4;
            if (slot < 0 || slot >= 24) { cfail("bad local slot"); break; }
            frm[fc - 1].loc[slot] = pop();
            break;
        }
        case OP_POP: pop(); break;
        case OP_ADD: { JV b = pop(), a = pop();
            if (a.t == 2 || b.t == 2) push(jv_str(str_add(a, b)));
            else push(jv_int(a.i + b.i));
            break; }
        case OP_SUB: { JV b = pop(), a = pop(); push(jv_int(a.i - b.i)); break; }
        case OP_MUL: { JV b = pop(), a = pop(); push(jv_int(a.i * b.i)); break; }
        case OP_DIV: { JV b = pop(), a = pop();
            if (b.t == 1 && b.i == 0) { cfail("division by zero"); break; }
            push(jv_int(a.i / b.i)); break; }
        case OP_MOD: { JV b = pop(), a = pop();
            if (b.t == 1 && b.i == 0) { cfail("mod by zero"); break; }
            push(jv_int(a.i % b.i)); break; }
        case OP_NEG: { JV a = pop(); push(jv_int(-a.i)); break; }
        case OP_LT: { JV b = pop(), a = pop(); push(jv_int(a.i < b.i)); break; }
        case OP_GT: { JV b = pop(), a = pop(); push(jv_int(a.i > b.i)); break; }
        case OP_LE: { JV b = pop(), a = pop(); push(jv_int(a.i <= b.i)); break; }
        case OP_GE: { JV b = pop(), a = pop(); push(jv_int(a.i >= b.i)); break; }
        case OP_EQ: { JV b = pop(), a = pop(); push(jv_int(jv_eq(a, b))); break; }
        case OP_NE: { JV b = pop(), a = pop(); push(jv_int(!jv_eq(a, b))); break; }
        case OP_AND: { JV b = pop(), a = pop(); push(jv_int(truthy(a) && truthy(b))); break; }
        case OP_OR: { JV b = pop(), a = pop(); push(jv_int(truthy(a) || truthy(b))); break; }
        case OP_NOT: { JV a = pop(); push(jv_int(!truthy(a))); break; }
        case OP_JMP: {
            int32_t t = rd32(p->code + ip); ip += 4;
            if (t < 0 || t >= p->clen) { cfail("jump out of range"); break; }
            ip = t;
            break;
        }
        case OP_JZ: {
            int32_t t = rd32(p->code + ip); ip += 4;
            JV c = pop();
            if (!truthy(c)) {
                if (t < 0 || t >= p->clen) { cfail("jump out of range"); break; }
                ip = t;
            }
            break;
        }
        case OP_JNZ: {
            int32_t t = rd32(p->code + ip); ip += 4;
            JV c = pop();
            if (truthy(c)) {
                if (t < 0 || t >= p->clen) { cfail("jump out of range"); break; }
                ip = t;
            }
            break;
        }
        case OP_CALL: {
            int fi = rd32(p->code + ip); ip += 4;
            int argc = rd32(p->code + ip); ip += 4;
            if (fi < 0 || fi >= p->nfuncs) { cfail("bad function index"); break; }
            if (argc < 0 || argc > 24 || argc > sp) { cfail("bad call argc"); break; }
            if (fc >= 64) { cfail("call depth overflow"); break; }
            Frame *fr = &frm[fc++];
            fr->ret = ip;
            for (int k = 0; k < 24; k++) fr->loc[k] = jv_nil();
            for (int k = 0; k < argc; k++)
                fr->loc[k] = stk[sp - argc + k];
            sp -= argc;
            ip = p->fentry[fi];
            break;
        }
        case OP_RET: {
            JV r = pop();
            int rip = frm[fc - 1].ret;
            fc--;
            if (rip < 0) { push(r); goto done; }
            push(r);
            ip = rip;
            break;
        }
        case OP_BPRINT: {
            int argc = rd32(p->code + ip); ip += 4;
            if (argc < 0 || argc > sp) { cfail("bad print argc"); break; }
            for (int k = 0; k < argc; k++) {
                if (k) outchar(' ');
                print_val(stk[sp - argc + k]);
            }
            sp -= argc;
            outchar('\n');
            push(jv_nil());
            break;
        }
        case OP_BLEN: { JV a = pop();
            push(jv_int(a.t == 2 ? slen(a.s) : 0));
            break; }
        case OP_BSTR: { JV a = pop(); push(jv_str(num_to_str(a.i))); break; }
        case OP_BINT: { JV a = pop();
            int64_t v = 0;
            if (a.t == 2 && a.s) {
                int k = 0, neg = 0;
                if (a.s[0] == '-') { neg = 1; k = 1; }
                while (a.s[k] >= '0' && a.s[k] <= '9') { v = v * 10 + (a.s[k] - '0'); k++; }
                if (neg) v = -v;
            }
            push(jv_int(v));
            break; }
        case OP_BABS: { JV a = pop(); push(jv_int(a.i < 0 ? -a.i : a.i)); break; }
        default: cfail("bad opcode"); goto done;
        }
    }
done:
    if (g_fail) {
        outstr("Error: ");
        int n = 0;
        while (g_err[n] && g_outn < (int)sizeof(g_out) - 2)
            g_out[g_outn++] = g_err[n++];
        outchar('\n');
    }
    int n = 0;
    while (n < g_outn && n < outsz - 1) { out[n] = g_out[n]; n++; }
    out[n] = 0;
    return g_fail ? -1 : 0;
}

static const char *opname(uint8_t op)
{
    switch (op) {
    case OP_HALT: return "HALT";
    case OP_PUSHI: return "PUSHI";
    case OP_PUSHS: return "PUSHS";
    case OP_PUSHNIL: return "PUSHNIL";
    case OP_LOAD: return "LOAD";
    case OP_STORE: return "STORE";
    case OP_POP: return "POP";
    case OP_ADD: return "ADD";
    case OP_SUB: return "SUB";
    case OP_MUL: return "MUL";
    case OP_DIV: return "DIV";
    case OP_MOD: return "MOD";
    case OP_NEG: return "NEG";
    case OP_LT: return "LT";
    case OP_GT: return "GT";
    case OP_LE: return "LE";
    case OP_GE: return "GE";
    case OP_EQ: return "EQ";
    case OP_NE: return "NE";
    case OP_AND: return "AND";
    case OP_OR: return "OR";
    case OP_NOT: return "NOT";
    case OP_JMP: return "JMP";
    case OP_JZ: return "JZ";
    case OP_JNZ: return "JNZ";
    case OP_CALL: return "CALL";
    case OP_RET: return "RET";
    case OP_BPRINT: return "BPRINT";
    case OP_BLEN: return "BLEN";
    case OP_BSTR: return "BSTR";
    case OP_BINT: return "BINT";
    case OP_BABS: return "BABS";
    default: return "?";
    }
}

static int oplen(uint8_t op)
{
    switch (op) {
    case OP_PUSHI: case OP_PUSHS: case OP_LOAD: case OP_STORE:
    case OP_JMP: case OP_JZ: case OP_JNZ:
    case OP_BPRINT:
        return 5;
    case OP_CALL:
        return 9;
    default:
        return 1;
    }
}

static int emit_str(char *out, int outsz, int n, const char *s)
{
    int k = 0;
    while (s[k] && n < outsz - 4)
        out[n++] = s[k++];
    return n;
}
static int emit_dec(char *out, int outsz, int n, int32_t v)
{
    char tmp[12];
    int k = 0;
    uint32_t u;
    if (v < 0) {
        if (n < outsz - 4) out[n++] = '-';
        u = (uint32_t)(-v);
    } else {
        u = (uint32_t)v;
    }
    if (u == 0) tmp[k++] = '0';
    while (u) { tmp[k++] = '0' + (u % 10); u /= 10; }
    while (k && n < outsz - 4) out[n++] = tmp[--k];
    return n;
}

void jca_disasm(struct jca_program *p, char *out, int outsz)
{
    int n = 0;
    int ip = 0;
    while (ip < p->clen && n < outsz - 80) {
        uint8_t op = p->code[ip];
        out[n++] = '[';
        n = emit_dec(out, outsz, n, ip);
        out[n++] = ']';
        out[n++] = ' ';
        n = emit_str(out, outsz, n, opname(op));
        if (op == OP_PUSHI || op == OP_LOAD || op == OP_STORE ||
            op == OP_JMP || op == OP_JZ || op == OP_JNZ || op == OP_BPRINT) {
            int32_t v = rd32(p->code + ip + 1);
            out[n++] = ' ';
            n = emit_dec(out, outsz, n, v);
        } else if (op == OP_PUSHS) {
            int32_t si = rd32(p->code + ip + 1);
            out[n++] = ' ';
            out[n++] = '"';
            n = emit_str(out, outsz, n, p->strs[si]);
            out[n++] = '"';
        } else if (op == OP_CALL) {
            int32_t fi = rd32(p->code + ip + 1);
            int32_t ac = rd32(p->code + ip + 5);
            out[n++] = ' ';
            if (fi >= 0 && fi < p->nfuncs)
                n = emit_str(out, outsz, n, p->fname[fi]);
            out[n++] = '(';
            n = emit_dec(out, outsz, n, ac);
            out[n++] = ')';
        }
        out[n++] = '\n';
        ip += oplen(op);
    }
    out[n] = 0;
}

static int check(const char *src, const char *want)
{
    static struct jca_program pp;
    static char err[120];
    static char out[600];
    int rc = jca_compile(src, &pp, err, sizeof(err));
    if (rc < 0) { serial_puts("JCA compile FAIL: "); serial_puts(err); serial_puts("\n"); return 0; }
    rc = jca_run(&pp, out, sizeof(out));
    if (rc < 0 || !seq(out, want)) {
        serial_puts("JCA FAIL got=["); serial_puts(out); serial_puts("] want=[");
        serial_puts(want); serial_puts("]\n");
        return 0;
    }
    return 1;
}

void jca_self_test(void)
{
    int ok = 1;
    ok &= check("var x = 3; var y = 4; print(x + y);", "7\n");
    ok &= check("var s = 0; var i = 1; while (i <= 5) { s = s + i; i = i + 1; } print(s);", "15\n");
    ok &= check("fn fib(n) { if (n < 2) { return n; } return fib(n - 1) + fib(n - 2); } print(fib(10));", "55\n");
    ok &= check("fn gcd(a, b) { while (b != 0) { var t = b; b = a % b; a = t; } return a; } print(gcd(48, 36));", "12\n");
    ok &= check("fn fact(n) { var r = 1; var i = 2; while (i <= n) { r = r * i; i = i + 1; } return r; } print(fact(5));", "120\n");
    ok &= check("var a = 0; if (3 > 2) { a = 1; } else { a = 2; } print(a);", "1\n");
    ok &= check("print(\"JackBOT\" + \" \" + \"OS\");", "JackBOT OS\n");
    ok &= check("print(len(\"hello\"));", "5\n");
    ok &= check("var x = 0; var i = 0; while (i < 4) { if (i == 2) { x = x + 10; } else { x = x + i; } i = i + 1; } print(x);", "14\n");
    serial_puts(ok ? "[JCA] self-test OK\n" : "[JCA] self-test FAIL\n");
}
