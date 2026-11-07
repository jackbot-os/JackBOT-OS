/*
 * JCA C front end.
 *
 * Compiles a C subset straight into JCA bytecode, so one VM executes both the
 * JCA language and C sources. Supported subset:
 *   - int/char/long locals (one slot each, 24 slots per function)
 *   - functions with int parameters, prototypes not required
 *   - if/else, while, for, break, continue, return, blocks
 *   - + - * / % comparisons, && || (short circuit), ! , unary -, ++/--
 *   - assignment and compound assignment to locals
 *   - printf("fmt", args) with %d/%i/%s/%% and puts("s")
 * Not supported (deliberate, the VM is stack based and has no address space):
 *   arrays, pointers, structs, globals, string variables, varargs.
 *
 * Layout of the emitted program: code[0] is a JMP to the entry of main, then
 * every function body follows in source order. Each body ends in RET, so the
 * fall-through path after main stops in the VM instead of running the next
 * function body.
 */
#include <stdint.h>
#include <kernel/jca.h>
#include <kernel/jca_op.h>
#include <kernel/jcac.h>
#include <kernel/serial.h>

#define CT_MAX_TOK  2048
#define CT_MAX_FN   16
#define CT_MAX_LOC  24
#define CT_MAX_STR  96
#define CT_MAX_FIX  32

enum { CT_NUM = 1, CT_ID, CT_STR, CT_SYM, CT_EOF };

typedef struct {
    int type;
    int32_t num;
    const char *s;   /* identifier/symbol/string slice start */
    int len;
} CTok;

static CTok tk[CT_MAX_TOK];
static int nt, tp;

struct cfn {
    char name[24];
    int argc;
    int entry;
};
static struct cfn fns[CT_MAX_FN];
static int nfns;
static int main_fi;

static struct jca_program *P;
static char *E;
static int esz;
static int failed;

static char lnames[CT_MAX_LOC][24];
static int locn;

/* pending jump operands for break/continue */
static int brk_fix[CT_MAX_FIX], nbrk;
static int cnt_fix[CT_MAX_FIX], ncnt;

static void fail(const char *m)
{
    if (failed)
        return;
    failed = 1;
    int i = 0;
    if (E && esz > 0) {
        while (m[i] && i < esz - 1) { E[i] = m[i]; i++; }
        E[i] = 0;
    }
}

static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static int seq(const char *a, const char *b)
{
    int i = 0;
    while (a[i] && b[i]) { if (a[i] != b[i]) return 0; i++; }
    return a[i] == b[i];
}

static int is_digit(int c) { return c >= '0' && c <= '9'; }
static int is_alpha(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static int is_alnum(int c) { return is_alpha(c) || is_digit(c); }

/* ---------------- lexer ---------------- */

static void push_tok(int type, int32_t num, const char *s, int len)
{
    if (nt >= CT_MAX_TOK) { fail("source too large"); return; }
    tk[nt].type = type;
    tk[nt].num = num;
    tk[nt].s = s;
    tk[nt].len = len;
    nt++;
}

static void lex(const char *src)
{
    nt = 0;
    int i = 0;
    while (src[i] && !failed) {
        char c = src[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { i++; continue; }
        /* preprocessor lines are skipped: the payload has no includes */
        if (c == '#') {
            while (src[i] && src[i] != '\n') i++;
            continue;
        }
        if (c == '/' && src[i + 1] == '/') {
            while (src[i] && src[i] != '\n') i++;
            continue;
        }
        if (c == '/' && src[i + 1] == '*') {
            i += 2;
            while (src[i] && !(src[i] == '*' && src[i + 1] == '/')) i++;
            if (src[i]) i += 2;
            continue;
        }
        if (is_digit(c)) {
            int32_t v = 0;
            if (c == '0' && (src[i + 1] == 'x' || src[i + 1] == 'X')) {
                i += 2;
                while (1) {
                    char h = src[i];
                    int d;
                    if (h >= '0' && h <= '9') d = h - '0';
                    else if (h >= 'a' && h <= 'f') d = h - 'a' + 10;
                    else if (h >= 'A' && h <= 'F') d = h - 'A' + 10;
                    else break;
                    v = v * 16 + d;
                    i++;
                }
            } else {
                while (is_digit(src[i])) { v = v * 10 + (src[i] - '0'); i++; }
            }
            push_tok(CT_NUM, v, src + i, 0);
            continue;
        }
        if (is_alpha(c)) {
            int st = i;
            while (is_alnum(src[i])) i++;
            push_tok(CT_ID, 0, src + st, i - st);
            continue;
        }
        if (c == '"') {
            int st = ++i;
            while (src[i] && src[i] != '"') {
                if (src[i] == '\\' && src[i + 1]) i++;
                i++;
            }
            push_tok(CT_STR, 0, src + st, i - st);
            if (src[i] == '"') i++;
            continue;
        }
        if (c == '\'') {
            /* char literal: 'a' or '\n' */
            i++;
            int32_t v = 0;
            if (src[i] == '\\' && src[i + 1]) {
                char e = src[i + 1];
                v = (e == 'n') ? 10 : (e == 't') ? 9 : (e == '0') ? 0 : e;
                i += 2;
            } else {
                v = src[i];
                i++;
            }
            if (src[i] == '\'') i++;
            push_tok(CT_NUM, v, src + i, 0);
            continue;
        }
        /* multi-character operators first */
        {
            char a = src[i], b = src[i + 1];
            if ((a == '=' && b == '=') || (a == '!' && b == '=') ||
                (a == '<' && b == '=') || (a == '>' && b == '=') ||
                (a == '&' && b == '&') || (a == '|' && b == '|') ||
                (a == '+' && b == '+') || (a == '-' && b == '-') ||
                (a == '+' && b == '=') || (a == '-' && b == '=') ||
                (a == '*' && b == '=') || (a == '/' && b == '=') ||
                (a == '%' && b == '=')) {
                push_tok(CT_SYM, 0, src + i, 2);
                i += 2;
                continue;
            }
        }
        push_tok(CT_SYM, 0, src + i, 1);
        i++;
    }
    push_tok(CT_EOF, 0, "", 0);
}

static CTok *cur(void) { return &tk[tp]; }
static CTok *nxt(void) { return (tp + 1 < nt) ? &tk[tp + 1] : &tk[nt - 1]; }
/* token slices are not NUL terminated, so compare by length only */
static int tok_eq(const CTok *t, const char *s)
{
    int n = slen(s);
    if (t->len != n) return 0;
    for (int i = 0; i < n; i++)
        if (t->s[i] != s[i]) return 0;
    return 1;
}
static int tok_is(const CTok *t, const char *s)
{
    return t->type == CT_SYM && tok_eq(t, s);
}
static int id_is(const CTok *t, const char *s)
{
    return t->type == CT_ID && tok_eq(t, s);
}
static int is_sym(const char *s) { return tok_is(cur(), s); }
static int is_kw(const char *s) { return id_is(cur(), s); }
static void adv(void) { if (tp < nt - 1) tp++; }
static int accept(const char *s)
{
    if (is_sym(s)) { adv(); return 1; }
    return 0;
}
static void expect(const char *s)
{
    if (!accept(s)) fail("expected token");
}
static int accept_kw(const char *s)
{
    if (is_kw(s)) { adv(); return 1; }
    return 0;
}

/* ---------------- program emission ---------------- */

static void e8(uint8_t v)
{
    if (failed) return;
    if (P->clen >= JCA_MAX_CODE) { fail("code too large"); return; }
    P->code[P->clen++] = v;
}
static void e32(int32_t v)
{
    e8((uint8_t)(v & 0xFF));
    e8((uint8_t)((v >> 8) & 0xFF));
    e8((uint8_t)((v >> 16) & 0xFF));
    e8((uint8_t)((v >> 24) & 0xFF));
}
static int here(void) { return P->clen; }
static void patch(int at, int32_t v)
{
    if (at < 0 || at + 4 > P->clen) return;
    P->code[at] = (uint8_t)(v & 0xFF);
    P->code[at + 1] = (uint8_t)((v >> 8) & 0xFF);
    P->code[at + 2] = (uint8_t)((v >> 16) & 0xFF);
    P->code[at + 3] = (uint8_t)((v >> 24) & 0xFF);
}
/* emits JMP/JZ; returns the operand offset for a later patch */
static int emit_jump(uint8_t op)
{
    e8(op);
    int at = here();
    e32(0);
    return at;
}

static int intern(const char *s)
{
    for (int i = 0; i < P->nstrs; i++)
        if (seq(P->strs[i], s)) return i;
    if (P->nstrs >= JCA_MAX_STRS) { fail("too many strings"); return 0; }
    int i = P->nstrs++;
    int k = 0;
    while (s[k] && k < JCA_STRLEN - 1) { P->strs[i][k] = s[k]; k++; }
    P->strs[i][k] = 0;
    return i;
}

/* decodes C escapes from a token slice into a NUL terminated buffer */
static void unescape(const CTok *t, char *dst, int cap)
{
    int i = 0, o = 0;
    while (i < t->len && o < cap - 1) {
        char c = t->s[i];
        if (c == '\\' && i + 1 < t->len) {
            char e = t->s[i + 1];
            if (e == 'n') c = '\n';
            else if (e == 't') c = '\t';
            else if (e == 'r') c = '\r';
            else c = e;
            i += 2;
        } else {
            i++;
        }
        dst[o++] = c;
    }
    dst[o] = 0;
}

static void emit_str(const char *s) { e8(OP_PUSHS); e32(intern(s)); }

/* ---------------- local slots ---------------- */

static void scopy(char *d, const char *s, int cap)
{
    int i = 0;
    while (s[i] && i < cap - 1) { d[i] = s[i]; i++; }
    d[i] = 0;
}

static int slot_of(const CTok *t)
{
    for (int i = 0; i < locn; i++) {
        if (t->len == slen(lnames[i]) && tok_eq(t, lnames[i]))
            return i;
    }
    return -1;
}

static int declare(const CTok *t)
{
    if (locn >= CT_MAX_LOC) { fail("too many locals"); return 0; }
    /* token slices are not NUL terminated: rebuild by length */
    int n = t->len < 23 ? t->len : 23;
    for (int i = 0; i < n; i++) lnames[locn][i] = t->s[i];
    lnames[locn][n] = 0;
    return locn++;
}

/* ---------------- expressions ---------------- */

static void gen_expr(void);

static void gen_call(const CTok *name)
{
    int fi = -1;
    for (int i = 0; i < nfns; i++)
        if (name->len == slen(fns[i].name) && tok_eq(name, fns[i].name)) { fi = i; break; }
    if (fi < 0) { fail("call to unknown function"); return; }
    adv(); /* name */
    expect("(");
    int argc = 0;
    if (!is_sym(")")) {
        while (!failed) {
            gen_expr();
            argc++;
            if (accept(",")) continue;
            break;
        }
    }
    expect(")");
    e8(OP_CALL);
    e32(fi);
    e32(argc);
}

/* printf("fmt", a, b) is lowered into string concatenation + one BPRINT,
   because BPRINT inserts a space between its arguments. */
static void gen_printf(void)
{
    adv(); /* name */
    expect("(");
    if (cur()->type != CT_STR) { fail("printf needs a literal format"); return; }
    char fmt[CT_MAX_STR];
    unescape(cur(), fmt, sizeof fmt);
    adv();

    int started = 0;
    char lit[CT_MAX_STR];
    int ln = 0;
    for (int i = 0; fmt[i] && !failed; i++) {
        if (fmt[i] == '%' && (fmt[i + 1] == 'd' || fmt[i + 1] == 'i' || fmt[i + 1] == 's')) {
            if (ln) {
                lit[ln] = 0;
                emit_str(lit);
                if (started) e8(OP_ADD);
                started = 1;
                ln = 0;
            }
            if (!accept(",")) { fail("printf needs one argument per %"); return; }
            gen_expr();
            e8(OP_BSTR);
            if (started) e8(OP_ADD);
            started = 1;
            i++; /* skip the conversion letter */
            continue;
        }
        if (fmt[i] == '%' && fmt[i + 1] == '%') {
            if (ln < CT_MAX_STR - 1) lit[ln++] = '%';
            i++;
            continue;
        }
        if (ln < CT_MAX_STR - 1) lit[ln++] = fmt[i];
    }
    if (ln) {
        lit[ln] = 0;
        emit_str(lit);
        if (started) e8(OP_ADD);
        started = 1;
    }
    if (!started) emit_str("");
    expect(")");
    e8(OP_BPRINT);
    e32(1);
}

static void gen_puts(void)
{
    adv();
    expect("(");
    if (cur()->type != CT_STR) { fail("puts needs a literal string"); return; }
    char buf[CT_MAX_STR];
    unescape(cur(), buf, sizeof buf);
    adv();
    emit_str(buf);
    expect(")");
    e8(OP_BPRINT);
    e32(1);
}

static void gen_primary(void)
{
    if (failed) return;
    if (is_sym("(")) { adv(); gen_expr(); expect(")"); return; }
    if (is_sym("!")) { adv(); gen_primary(); e8(OP_NOT); return; }
    if (is_sym("-")) { adv(); gen_primary(); e8(OP_NEG); return; }
    if (is_sym("+")) { adv(); gen_primary(); return; }
    if (is_sym("++")) {
        adv();
        int slot = slot_of(cur());
        if (slot < 0) { fail("++ needs a variable"); return; }
        adv();
        e8(OP_LOAD); e32(slot);
        e8(OP_PUSHI); e32(1);
        e8(OP_ADD);
        e8(OP_STORE); e32(slot);
        e8(OP_LOAD); e32(slot); /* prefix yields the new value */
        return;
    }
    if (is_sym("--")) {
        adv();
        int slot = slot_of(cur());
        if (slot < 0) { fail("-- needs a variable"); return; }
        adv();
        e8(OP_LOAD); e32(slot);
        e8(OP_PUSHI); e32(1);
        e8(OP_SUB);
        e8(OP_STORE); e32(slot);
        e8(OP_LOAD); e32(slot);
        return;
    }
    if (cur()->type == CT_NUM) { e8(OP_PUSHI); e32(cur()->num); adv(); return; }
    if (cur()->type == CT_STR) { fail("string literal only allowed in printf/puts"); return; }
    if (cur()->type == CT_ID) {
        if (id_is(cur(), "true")) { e8(OP_PUSHI); e32(1); adv(); return; }
        if (id_is(cur(), "false")) { e8(OP_PUSHI); e32(0); adv(); return; }
        if (nxt()->type == CT_SYM && tok_is(nxt(), "(")) {
            if (id_is(cur(), "printf")) { gen_printf(); return; }
            if (id_is(cur(), "puts")) { gen_puts(); return; }
            gen_call(cur());
            return;
        }
        int slot = slot_of(cur());
        if (slot < 0) { fail("unknown variable"); return; }
        adv();
        e8(OP_LOAD); e32(slot);
        /* postfix ++/-- keeps the old value as the expression result */
        if (is_sym("++") || is_sym("--")) {
            int inc = is_sym("++");
            adv();
            e8(OP_LOAD); e32(slot);
            e8(OP_PUSHI); e32(1);
            e8(inc ? OP_ADD : OP_SUB);
            e8(OP_STORE); e32(slot);
        }
        return;
    }
    fail("bad expression");
}

/* precedence: 0 || , 1 && , 2 equality, 3 relational, 4 additive, 5 multiplicative */
static void gen_bin(int level)
{
    if (level > 5) { gen_primary(); return; }
    gen_bin(level + 1);
    while (!failed) {
        uint8_t op = 0xFF;
        int short_circuit = 0;
        if (level == 0) { if (is_sym("||")) { op = OP_OR; short_circuit = 1; } }
        else if (level == 1) { if (is_sym("&&")) { op = OP_AND; short_circuit = 2; } }
        else if (level == 2) {
            if (is_sym("==")) op = OP_EQ;
            else if (is_sym("!=")) op = OP_NE;
        } else if (level == 3) {
            if (is_sym("<")) op = OP_LT;
            else if (is_sym(">")) op = OP_GT;
            else if (is_sym("<=")) op = OP_LE;
            else if (is_sym(">=")) op = OP_GE;
        } else if (level == 4) {
            if (is_sym("+")) op = OP_ADD;
            else if (is_sym("-")) op = OP_SUB;
        } else {
            if (is_sym("*")) op = OP_MUL;
            else if (is_sym("/")) op = OP_DIV;
            else if (is_sym("%")) op = OP_MOD;
        }
        if (op == 0xFF) break;
        adv();
        if (short_circuit) {
            /* the left value already sits on the stack; the branch taken here
               decides the constant result, otherwise the right side runs */
            int jdecide = emit_jump(short_circuit == 2 ? OP_JZ : OP_JNZ);
            gen_bin(level + 1);
            int jfalse = emit_jump(OP_JZ);
            int jend = emit_jump(OP_JMP);        /* right side was true */
            int c_false = here();
            e8(OP_PUSHI); e32(0);
            int jout = emit_jump(OP_JMP);
            int c_true = here();
            e8(OP_PUSHI); e32(1);
            patch(jend, c_true);
            patch(jout, here());
            patch(jdecide, short_circuit == 2 ? c_false : c_true);
            patch(jfalse, c_false);
        } else {
            gen_bin(level + 1);
            e8(op);
        }
    }
}

static void gen_expr(void) { gen_bin(0); }

/* ---------------- statements ---------------- */

static int is_type_kw(const CTok *t)
{
    return id_is(t, "int") || id_is(t, "char") || id_is(t, "long") ||
           id_is(t, "short") || id_is(t, "unsigned") || id_is(t, "signed") ||
           id_is(t, "void") || id_is(t, "const") || id_is(t, "static");
}

static void skip_type(void)
{
    while (is_type_kw(cur())) adv();
}

static void gen_decl(void)
{
    /* caller checked the type keyword */
    skip_type();
    while (!failed) {
        if (cur()->type != CT_ID) { fail("expected variable name"); return; }
        int slot = declare(cur());
        adv();
        if (accept("=")) {
            gen_expr();
        } else {
            e8(OP_PUSHI); e32(0);
        }
        e8(OP_STORE); e32(slot);
        if (accept(",")) continue;
        break;
    }
    expect(";");
}

static void gen_assign(void)
{
    int slot = slot_of(cur());
    if (slot < 0) { fail("unknown variable"); return; }
    adv();
    uint8_t op = 0xFF;
    if (is_sym("=")) op = 0;
    else if (is_sym("+=")) op = OP_ADD;
    else if (is_sym("-=")) op = OP_SUB;
    else if (is_sym("*=")) op = OP_MUL;
    else if (is_sym("/=")) op = OP_DIV;
    else if (is_sym("%=")) op = OP_MOD;
    adv();
    if (op) {
        e8(OP_LOAD); e32(slot);
        gen_expr();
        e8(op);
    } else {
        gen_expr();
    }
    e8(OP_STORE); e32(slot);
    expect(";");
}

static void gen_stmt(void);

static void gen_block(void)
{
    expect("{");
    while (!failed && cur()->type != CT_EOF && !is_sym("}"))
        gen_stmt();
    expect("}");
}

static void gen_stmt(void)
{
    if (failed || cur()->type == CT_EOF) return;

    if (is_sym("{")) { gen_block(); return; }
    if (accept(";")) return;

    if (is_type_kw(cur())) { gen_decl(); return; }

    if (is_kw("if")) {
        adv();
        expect("(");
        gen_expr();
        expect(")");
        int jz = emit_jump(OP_JZ);
        gen_stmt();
        if (accept_kw("else")) {
            int jend = emit_jump(OP_JMP);
            patch(jz, here());
            gen_stmt();
            patch(jend, here());
        } else {
            patch(jz, here());
        }
        return;
    }

    if (is_kw("while")) {
        adv();
        int top = here();
        expect("(");
        gen_expr();
        expect(")");
        int jz = emit_jump(OP_JZ);
        int saved_brk = nbrk, saved_cnt = ncnt;
        gen_stmt();
        /* continue targets the loop head */
        for (int i = saved_cnt; i < ncnt; i++) patch(cnt_fix[i], top);
        ncnt = saved_cnt;
        e8(OP_JMP); e32(top);
        patch(jz, here());
        for (int i = saved_brk; i < nbrk; i++) patch(brk_fix[i], here());
        nbrk = saved_brk;
        return;
    }

    if (is_kw("for")) {
        adv();
        expect("(");
        if (is_sym(";")) {
            adv();
        } else if (is_type_kw(cur())) {
            gen_decl(); /* consumes ';' */
        } else {
            /* plain init expression/assignment without a trailing ';' */
            if (cur()->type == CT_ID && nxt()->type == CT_SYM &&
                (tok_is(nxt(), "=") || tok_is(nxt(), "+=") || tok_is(nxt(), "-=") ||
                 tok_is(nxt(), "*=") || tok_is(nxt(), "/=") || tok_is(nxt(), "%="))) {
                int slot = slot_of(cur());
                if (slot < 0) { fail("unknown variable"); return; }
                adv();
                uint8_t op = 0xFF;
                if (is_sym("=")) op = 0;
                else if (is_sym("+=")) op = OP_ADD;
                else if (is_sym("-=")) op = OP_SUB;
                else if (is_sym("*=")) op = OP_MUL;
                else if (is_sym("/=")) op = OP_DIV;
                else if (is_sym("%=")) op = OP_MOD;
                adv();
                if (op) { e8(OP_LOAD); e32(slot); gen_expr(); e8(op); }
                else gen_expr();
                e8(OP_STORE); e32(slot);
            } else {
                gen_expr();
                e8(OP_POP);
            }
            expect(";");
        }
        /* The header puts the step before ')', yet the VM must run it after the
           body, so the condition and the step are both skipped here and emitted
           later. Layout: JMP cond | body | step | cond | JNZ body. */
        int cond_start = tp;
        int depth = 0;
        while (!failed && cur()->type != CT_EOF) {
            if (is_sym("(")) depth++;
            else if (is_sym(";") || is_sym(")")) { if (depth == 0) break; if (is_sym(")")) depth--; }
            adv();
        }
        int cond_end = tp;   /* just before the ';' */
        adv();               /* ';' */
        int step_start = tp;
        depth = 0;
        while (!failed && cur()->type != CT_EOF) {
            if (is_sym("(")) depth++;
            else if (is_sym(")")) { if (depth == 0) break; depth--; }
            adv();
        }
        expect(")");         /* now the body */
        int jmp_cond = emit_jump(OP_JMP);
        int body_start = here();
        int saved_brk = nbrk, saved_cnt = ncnt;
        gen_stmt();
        int after_body_tp = tp;
        /* continue jumps land on the step */
        for (int i = saved_cnt; i < ncnt; i++) patch(cnt_fix[i], here());
        ncnt = saved_cnt;
        /* step expression, if any */
        tp = step_start;
        if (!is_sym(")")) {
            if (cur()->type == CT_ID && nxt()->type == CT_SYM &&
                (tok_is(nxt(), "=") || tok_is(nxt(), "+=") || tok_is(nxt(), "-=") ||
                 tok_is(nxt(), "*=") || tok_is(nxt(), "/=") || tok_is(nxt(), "%="))) {
                int slot = slot_of(cur());
                if (slot < 0) { fail("unknown variable"); return; }
                adv();
                uint8_t op = 0xFF;
                if (is_sym("=")) op = 0;
                else if (is_sym("+=")) op = OP_ADD;
                else if (is_sym("-=")) op = OP_SUB;
                else if (is_sym("*=")) op = OP_MUL;
                else if (is_sym("/=")) op = OP_DIV;
                else if (is_sym("%=")) op = OP_MOD;
                adv();
                if (op) { e8(OP_LOAD); e32(slot); gen_expr(); e8(op); }
                else gen_expr();
                e8(OP_STORE); e32(slot);
            } else {
                gen_expr();
                e8(OP_POP);
            }
            expect(")");
        }
        patch(jmp_cond, here());
        if (cond_start != cond_end) {
            tp = cond_start;
            gen_expr();
            int jnz = emit_jump(OP_JNZ);
            patch(jnz, body_start);
        }
        tp = after_body_tp;
        for (int i = saved_brk; i < nbrk; i++) patch(brk_fix[i], here());
        nbrk = saved_brk;
        return;
    }

    if (is_kw("break")) {
        adv();
        expect(";");
        if (nbrk >= CT_MAX_FIX) { fail("break too deep"); return; }
        brk_fix[nbrk++] = emit_jump(OP_JMP);
        return;
    }

    if (is_kw("continue")) {
        adv();
        expect(";");
        if (ncnt >= CT_MAX_FIX) { fail("continue too deep"); return; }
        cnt_fix[ncnt++] = emit_jump(OP_JMP);
        return;
    }

    if (is_kw("return")) {
        adv();
        if (is_sym(";")) {
            e8(OP_PUSHI); e32(0);
        } else {
            gen_expr();
        }
        expect(";");
        e8(OP_RET);
        return;
    }

    /* assignment statement */
    if (cur()->type == CT_ID && nxt()->type == CT_SYM &&
        (tok_is(nxt(), "=") || tok_is(nxt(), "+=") || tok_is(nxt(), "-=") ||
         tok_is(nxt(), "*=") || tok_is(nxt(), "/=") || tok_is(nxt(), "%="))) {
        gen_assign();
        return;
    }

    /* expression statement */
    gen_expr();
    e8(OP_POP);
    expect(";");
}

/* ---------------- function table ---------------- */

static void scan_functions(void)
{
    nfns = 0;
    tp = 0;
    while (!failed && cur()->type != CT_EOF) {
        if (!is_type_kw(cur()) && cur()->type != CT_ID) { adv(); continue; }
        /* optional type keywords then a name */
        while (!failed && is_type_kw(cur())) adv();
        if (cur()->type != CT_ID) { adv(); continue; }
        CTok *nm = cur();
        if (!(nxt()->type == CT_SYM && tok_is(nxt(), "("))) { adv(); continue; }
        if (nfns >= CT_MAX_FN) { fail("too many functions"); return; }
        int argc = 0;
        adv(); /* name */
        adv(); /* ( */
        if (!is_sym(")")) {
            /* a lone "void" means an empty parameter list */
            if (is_type_kw(cur()) && nxt()->type == CT_SYM && tok_is(nxt(), ")")) {
                adv();
            }
            while (!failed) {
                if (is_sym(")")) break;
                skip_type();
                if (cur()->type != CT_ID) { fail("bad parameter"); return; }
                adv();
                argc++;
                if (accept(",")) continue;
                break;
            }
        }
        expect(")");
        /* skip a body if this was only a prototype */
        if (is_sym(";")) { adv(); continue; }
        if (!is_sym("{")) { fail("expected function body"); return; }
        int n = nm->len < 23 ? nm->len : 23;
        for (int i = 0; i < n; i++) fns[nfns].name[i] = nm->s[i];
        fns[nfns].name[n] = 0;
        fns[nfns].argc = argc;
        fns[nfns].entry = -1;
        nfns++;
        /* skip the body with brace counting */
        int depth = 0;
        while (!failed) {
            if (is_sym("{")) depth++;
            else if (is_sym("}")) { depth--; if (depth == 0) { adv(); break; } }
            else if (cur()->type == CT_EOF) { fail("unterminated body"); return; }
            adv();
        }
    }
    for (int i = 0; i < nfns; i++)
        if (seq(fns[i].name, "main")) main_fi = i;
}

int jcac_compile(const char *src, struct jca_program *p, char *err, int errsz)
{
    P = p;
    E = err;
    esz = errsz;
    failed = 0;
    if (err && errsz) err[0] = 0;
    p->clen = 0;
    p->nstrs = 0;
    p->nfuncs = 0;
    p->main_entry = 0;
    locn = 0;
    nbrk = ncnt = 0;
    main_fi = -1;

    lex(src);
    scan_functions();
    if (failed) return -1;
    if (main_fi < 0) { fail("no main()"); return -1; }

    /* JMP main entry; patched once every body is emitted */
    int jmp_main = emit_jump(OP_JMP);

    tp = 0;
    for (int fi = 0; fi < nfns && !failed; fi++) {
        /* walk to this function's body: type keywords, name, params, '{' */
        while (!is_type_kw(cur()) && cur()->type != CT_ID) adv();
        while (is_type_kw(cur())) adv();
        if (cur()->type != CT_ID) { fail("lost function"); return -1; }
        adv();
        expect("(");
        locn = 0;
        if (!is_sym(")")) {
            if (is_type_kw(cur()) && nxt()->type == CT_SYM && tok_is(nxt(), ")")) {
                adv();
            }
            while (!failed) {
                if (is_sym(")")) break;
                skip_type();
                if (cur()->type != CT_ID) { fail("bad parameter"); return -1; }
                declare(cur());
                adv();
                if (accept(",")) continue;
                break;
            }
        }
        expect(")");
        if (!is_sym("{")) { fail("expected body"); return -1; }
        fns[fi].entry = here();
        adv(); /* { */
        while (!failed && cur()->type != CT_EOF && !is_sym("}"))
            gen_stmt();
        expect("}");
        /* implicit return 0 for a body that falls through */
        e8(OP_PUSHI); e32(0);
        e8(OP_RET);
    }
    if (failed)
        return -1;

    p->nfuncs = nfns;
    for (int i = 0; i < nfns; i++) {
        p->fentry[i] = fns[i].entry;
        p->fargc[i] = fns[i].argc;
        scopy(p->fname[i], fns[i].name, 24);
    }
    p->main_entry = fns[main_fi].entry;
    patch(jmp_main, p->main_entry);
    return 0;
}

/* ---------------- self test ---------------- */

static struct jca_program CT_P;

static int run_c(const char *name, const char *src, const char *want)
{
    char err[120];
    char out[512];
    serial_puts("[C] ");
    serial_puts(name);
    serial_puts(": ");
    if (jcac_compile(src, &CT_P, err, sizeof err) != 0) {
        serial_puts("compile failed (");
        serial_puts(err);
        serial_puts(")\n");
        return 0;
    }
    out[0] = 0;
    jca_run(&CT_P, out, sizeof out);
    /* drop the trailing newline jca_run adds */
    int n = 0;
    while (out[n]) n++;
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == ' ')) out[--n] = 0;
    serial_puts(out);
    serial_puts("\n");
    return seq(out, want);
}

void jcac_self_test(void)
{
    int ok = 1;
    serial_puts("[C] selftest\n");

    static const char *src_sum =
        "int main() {\n"
        "  int s = 0;\n"
        "  for (int i = 1; i <= 10; i++) { s = s + i; }\n"
        "  printf(\"sum=%d\\n\", s);\n"
        "  return 0;\n"
        "}\n";
    ok &= run_c("loop-sum", src_sum, "sum=55");

    static const char *src_fib =
        "int fib(int n) {\n"
        "  if (n < 2) { return n; }\n"
        "  return fib(n - 1) + fib(n - 2);\n"
        "}\n"
        "int main(void) {\n"
        "  printf(\"fib(10)=%d\\n\", fib(10));\n"
        "  return 0;\n"
        "}\n";
    ok &= run_c("fib", src_fib, "fib(10)=55");

    static const char *src_gcd =
        "int gcd(int a, int b) {\n"
        "  while (b != 0) {\n"
        "    int t = b;\n"
        "    b = a % b;\n"
        "    a = t;\n"
        "  }\n"
        "  return a;\n"
        "}\n"
        "int main() {\n"
        "  printf(\"gcd=%d\\n\", gcd(48, 18));\n"
        "  return 0;\n"
        "}\n";
    ok &= run_c("gcd", src_gcd, "gcd=6");

    static const char *src_logic =
        "int main() {\n"
        "  int a = 3;\n"
        "  int b = 0;\n"
        "  if (a > 1 && b == 0) { puts(\"and-ok\"); }\n"
        "  if (b || a > 2) { puts(\"or-ok\"); }\n"
        "  int n = 0;\n"
        "  while (n < 5) { n++; if (n == 3) { continue; } if (n > 4) { break; } }\n"
        "  printf(\"n=%d\\n\", n);\n"
        "  return 0;\n"
        "}\n";
    ok &= run_c("logic", src_logic, "and-ok\nor-ok\nn=5");

    serial_puts(ok ? "[C] PASS\n" : "[C] FAIL\n");
}
