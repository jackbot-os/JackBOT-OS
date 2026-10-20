#include <stdint.h>
#include <stddef.h>
#include <kernel/py.h>
#include <kernel/heap.h>
#include <kernel/serial.h>

void *kmalloc(size_t n);
void kfree(void *p);

#define LOOP_CAP 300000
#define MAX_GVARS 128
#define MAX_FRAMES 24
#define MAX_LVARS 48
#define MAX_FUNCS 64
#define MAX_LINES 600
#define MAX_PARAMS 8
#define MAX_STR 512
#define MAX_LISTE 256
#define NAME_LEN 28

enum { T_INT, T_STR, T_LIST, T_NONE, T_DICT };

typedef struct V {
    int t;
    int64_t i;
    char *s;
    struct V *a;
    int n;
    struct V *dk;
    struct V *dv;
    int dn;
} V;

typedef struct {
    char name[NAME_LEN];
    V v;
} Env;

typedef struct {
    char name[NAME_LEN];
    char params[MAX_PARAMS][NAME_LEN];
    int np;
    int body_line;
    int body_indent;
} Func;

static Env genv[MAX_GVARS];
static int genvn;
static Env lframes[MAX_FRAMES][MAX_LVARS];
static int lcnt[MAX_FRAMES];
static int depth;
static Func funcs[MAX_FUNCS];
static int nfuncs;

static char *g_out;
static int g_outn, g_outcap;
static int g_err;
static char g_errmsg[96];

static int g_ret, g_brk, g_cnt;
static V g_retval;
static int g_retval_set;

enum { TK_END, TK_NUM, TK_NAME, TK_STR, TK_OP };

typedef struct {
    const char *p;
    int tok;
    int64_t num;
    char name[NAME_LEN];
    char op[4];
    char strbuf[MAX_STR];
} Lex;

typedef struct {
    int indent;
    char text[240];
} Line;

static Line lines[MAX_LINES];
static int nlines;
static int gline;

static void outc(char c)
{
    if (g_outn < g_outcap - 1)
        g_out[g_outn++] = c;
    g_out[g_outn] = 0;
}
static void outs(const char *s)
{
    while (*s)
        outc(*s++);
}
static void outi(int64_t v)
{
    char b[32];
    int n = 0;
    int neg = v < 0;
    uint64_t x = neg ? (uint64_t)(-v) : (uint64_t)v;
    do { b[n++] = (char)('0' + x % 10); x /= 10; } while (x);
    if (neg)
        b[n++] = '-';
    while (n)
        outc(b[--n]);
}
static void fail(const char *m)
{
    if (!g_err) {
        int i = 0;
        while (m[i] && i < 94) { g_errmsg[i] = m[i]; i++; }
        g_errmsg[i] = 0;
        g_err = 1;
    }
}
static int name_eq(const char *a, const char *b)
{
    int i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i])
            return 0;
        i++;
    }
    return a[i] == b[i];
}
static int slen(const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}
static char *sdup(const char *s)
{
    int n = slen(s);
    char *p = (char *)kmalloc((size_t)n + 1);
    if (!p) { fail("out of memory"); return 0; }
    for (int i = 0; i <= n; i++)
        p[i] = s[i];
    return p;
}

static V vint(int64_t i)
{
    V v; v.t = T_INT; v.i = i; v.s = 0; v.a = 0; v.n = 0;
    v.dk = 0; v.dv = 0; v.dn = 0;
    return v;
}
static V vnone(void)
{
    V v; v.t = T_NONE; v.i = 0; v.s = 0; v.a = 0; v.n = 0;
    v.dk = 0; v.dv = 0; v.dn = 0;
    return v;
}
static V vstr(const char *s)
{
    V v; v.t = T_STR; v.i = 0; v.a = 0; v.n = 0;
    v.dk = 0; v.dv = 0; v.dn = 0;
    v.s = sdup(s);
    if (!v.s) v.t = T_NONE;
    return v;
}
static V vlist(int n)
{
    V v; v.t = T_LIST; v.i = 0; v.s = 0; v.n = n;
    v.dk = 0; v.dv = 0; v.dn = 0;
    v.a = n ? (V *)kmalloc((size_t)n * sizeof(V)) : 0;
    for (int i = 0; i < n; i++)
        v.a[i] = vnone();
    if (n && !v.a) fail("out of memory");
    return v;
}
static V vdict(int n)
{
    V v; v.t = T_DICT; v.i = 0; v.s = 0; v.a = 0; v.n = 0;
    v.dn = n;
    v.dk = n ? (V *)kmalloc((size_t)n * sizeof(V)) : 0;
    v.dv = n ? (V *)kmalloc((size_t)n * sizeof(V)) : 0;
    for (int i = 0; i < n; i++) { v.dk[i] = vnone(); v.dv[i] = vnone(); }
    return v;
}
static void vfree(V *v)
{
    if (!v) return;
    if (v->t == T_STR && v->s) { kfree(v->s); v->s = 0; }
    else if (v->t == T_LIST && v->a) {
        for (int i = 0; i < v->n; i++)
            vfree(&v->a[i]);
        kfree(v->a); v->a = 0;
    } else if (v->t == T_DICT) {
        for (int i = 0; i < v->dn; i++) { vfree(&v->dk[i]); vfree(&v->dv[i]); }
        if (v->dk) kfree(v->dk);
        if (v->dv) kfree(v->dv);
        v->dk = v->dv = 0;
    }
    v->t = T_NONE;
}
static V vcopy(V x)
{
    if (x.t == T_STR)
        return vstr(x.s ? x.s : "");
    if (x.t == T_LIST) {
        V r = vlist(x.n);
        for (int i = 0; i < x.n; i++)
            r.a[i] = vcopy(x.a[i]);
        return r;
    }
    if (x.t == T_DICT) {
        V r = vdict(x.dn);
        for (int i = 0; i < x.dn; i++) {
            vfree(&r.dk[i]); vfree(&r.dv[i]);
            r.dk[i] = vcopy(x.dk[i]);
            r.dv[i] = vcopy(x.dv[i]);
        }
        return r;
    }
    return x;
}
static int truth(V v)
{
    if (v.t == T_INT) return v.i != 0;
    if (v.t == T_STR) return v.s && v.s[0];
    if (v.t == T_LIST) return v.n != 0;
    if (v.t == T_DICT) return v.dn != 0;
    return 0;
}
static int vals_eq(V a, V b);

static int issp(char c) { return c == ' ' || c == '\t' || c == '\r'; }
static int isdg(char c) { return c >= '0' && c <= '9'; }
static int isal(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

static void lex_next(Lex *L)
{
    while (issp(*L->p))
        L->p++;
    char c = *L->p;
    if (c == 0 || c == '\n' || c == ';') {
        L->tok = TK_END;
        return;
    }
    if (c == '#') {
        L->tok = TK_END;
        return;
    }
    if (isdg(c)) {
        int base = 10;
        const char *q = L->p;
        if (c == '0' && (L->p[1] == 'x' || L->p[1] == 'X')) { base = 16; q += 2; }
        else if (c == '0' && (L->p[1] == 'b' || L->p[1] == 'B')) { base = 2; q += 2; }
        else if (c == '0' && (L->p[1] == 'o' || L->p[1] == 'O')) { base = 8; q += 2; }
        int64_t v = 0;
        while (*q) {
            int d = -1;
            char h = *q;
            if (h >= '0' && h <= '9') d = h - '0';
            else if (h >= 'a' && h <= 'f') d = h - 'a' + 10;
            else if (h >= 'A' && h <= 'F') d = h - 'A' + 10;
            if (d < 0 || d >= base)
                break;
            v = v * base + d;
            q++;
        }
        L->num = v;
        L->p = q;
        L->tok = TK_NUM;
        return;
    }
    if (isal(c) || c == '_') {
        int n = 0;
        while (isal(*L->p) && n < NAME_LEN - 1)
            L->name[n++] = *L->p++;
        L->name[n] = 0;
        L->tok = TK_NAME;
        return;
    }
    if (c == '"' || c == '\'') {
        char q = c;
        L->p++;
        int n = 0;
        while (*L->p && *L->p != q && *L->p != '\n' && n < MAX_STR - 2) {
            char ch = *L->p++;
            if (ch == '\\' && *L->p) {
                char e = *L->p++;
                if (e == 'n') ch = '\n';
                else if (e == 't') ch = '\t';
                else if (e == 'r') ch = '\r';
                else if (e == '\\') ch = '\\';
                else if (e == '\'') ch = '\'';
                else if (e == '"') ch = '"';
                else if (e == '0') ch = 0;
                else ch = e;
            }
            L->strbuf[n++] = ch;
        }
        if (*L->p == q)
            L->p++;
        L->strbuf[n] = 0;
        L->tok = TK_STR;
        return;
    }
    L->op[0] = c;
    L->op[1] = 0;
    L->op[2] = 0;
    char n2 = L->p[1];
    if ((c == '*' && n2 == '*') || (c == '/' && n2 == '/') ||
        (c == '=' && n2 == '=') || (c == '!' && n2 == '=') ||
        (c == '<' && n2 == '=') || (c == '>' && n2 == '=') ||
        (c == '+' && n2 == '=') || (c == '-' && n2 == '=') ||
        (c == '*' && n2 == '=') || (c == '/' && n2 == '=') ||
        (c == '%' && n2 == '=') ||
        (c == '<' && n2 == '<') || (c == '>' && n2 == '>') ||
        (c == '&' && n2 == '&') || (c == '|' && n2 == '|')) {
        L->op[1] = n2;
        L->p += 2;
    } else {
        L->p += 1;
    }
    L->tok = TK_OP;
}

static V *find_var(const char *n)
{
    for (int d = depth; d >= 1; d--)
        for (int i = 0; i < lcnt[d]; i++)
            if (name_eq(lframes[d][i].name, n))
                return &lframes[d][i].v;
    for (int i = 0; i < genvn; i++)
        if (name_eq(genv[i].name, n))
            return &genv[i].v;
    return 0;
}
static void assign_var(const char *n, V val)
{
    if (depth >= 1) {
        for (int i = 0; i < lcnt[depth]; i++)
            if (name_eq(lframes[depth][i].name, n)) {
                vfree(&lframes[depth][i].v);
                lframes[depth][i].v = vcopy(val);
                return;
            }
        if (lcnt[depth] >= MAX_LVARS) { fail("too many locals"); return; }
        int k = 0;
        while (n[k] && k < NAME_LEN - 1) { lframes[depth][lcnt[depth]].name[k] = n[k]; k++; }
        lframes[depth][lcnt[depth]].name[k] = 0;
        lframes[depth][lcnt[depth]].v = vcopy(val);
        lcnt[depth]++;
        return;
    }
    for (int i = 0; i < genvn; i++)
        if (name_eq(genv[i].name, n)) {
            vfree(&genv[i].v);
            genv[i].v = vcopy(val);
            return;
        }
    if (genvn >= MAX_GVARS) { fail("too many variables"); return; }
    int k = 0;
    while (n[k] && k < NAME_LEN - 1) { genv[genvn].name[k] = n[k]; k++; }
    genv[genvn].name[k] = 0;
    genv[genvn].v = vcopy(val);
    genvn++;
}
static Func *find_func(const char *n)
{
    for (int i = 0; i < nfuncs; i++)
        if (name_eq(funcs[i].name, n))
            return &funcs[i];
    return 0;
}

static void emit_val(V v)
{
    if (v.t == T_INT) {
        outi(v.i);
    } else if (v.t == T_STR) {
        outs(v.s ? v.s : "");
    } else if (v.t == T_LIST) {
        outc('[');
        for (int i = 0; i < v.n; i++) {
            if (i) outc(',');
            if (i) outc(' ');
            if (v.a[i].t == T_STR) { outc('\''); outs(v.a[i].s ? v.a[i].s : ""); outc('\''); }
            else emit_val(v.a[i]);
        }
        outc(']');
    } else if (v.t == T_DICT) {
        outc('{');
        for (int i = 0; i < v.dn; i++) {
            if (i) { outc(','); outc(' '); }
            if (v.dk[i].t == T_STR) { outc('\''); outs(v.dk[i].s ? v.dk[i].s : ""); outc('\''); }
            else emit_val(v.dk[i]);
            outc(':'); outc(' ');
            if (v.dv[i].t == T_STR) { outc('\''); outs(v.dv[i].s ? v.dv[i].s : ""); outc('\''); }
            else emit_val(v.dv[i]);
        }
        outc('}');
    } else {
        outs("None");
    }
}
static void repr_into(V v, char *out, int cap)
{
    int n = 0;
    out[0] = 0;
    if (v.t == T_INT) {
        int neg = v.i < 0;
        uint64_t x = neg ? (uint64_t)(-v.i) : (uint64_t)v.i;
        char b[24]; int k = 0;
        do { b[k++] = (char)('0' + x % 10); x /= 10; } while (x);
        if (neg) b[k++] = '-';
        while (k && n < cap - 1) out[n++] = b[--k];
    } else if (v.t == T_STR) {
        const char *s = v.s ? v.s : "";
        while (*s && n < cap - 1) out[n++] = *s++;
    }
    out[n] = 0;
}

static V eval_expr(Lex *L);
static void exec_block(int indent);

static int vals_eq(V a, V b)
{
    if (a.t != b.t) {
        if ((a.t == T_INT && b.t == T_INT) ||
            (a.t == T_NONE && b.t == T_NONE))
            return a.t == b.t;
        return 0;
    }
    if (a.t == T_INT) return a.i == b.i;
    if (a.t == T_NONE) return 1;
    if (a.t == T_STR) {
        const char *x = a.s ? a.s : "", *y = b.s ? b.s : "";
        int i = 0;
        while (x[i] || y[i]) {
            if (x[i] != y[i]) return 0;
            i++;
        }
        return 1;
    }
    if (a.t == T_LIST) {
        if (a.n != b.n) return 0;
        for (int i = 0; i < a.n; i++)
            if (!vals_eq(a.a[i], b.a[i])) return 0;
        return 1;
    }
    if (a.dn != b.dn) return 0;
    for (int i = 0; i < a.dn; i++) {
        int found = 0;
        for (int j = 0; j < b.dn; j++)
            if (vals_eq(a.dk[i], b.dk[j]) && vals_eq(a.dv[i], b.dv[j]))
                found = 1;
        if (!found) return 0;
    }
    return 1;
}

static V str_to_upper(const char *s)
{
    char *b = sdup(s);
    if (b) { for (int i = 0; b[i]; i++) if (b[i] >= 'a' && b[i] <= 'z') b[i] -= 32; }
    V v = vstr("");
    if (v.s) kfree(v.s);
    v.s = b ? b : sdup("");
    return v;
}
static V str_to_lower(const char *s)
{
    char *b = sdup(s);
    if (b) { for (int i = 0; b[i]; i++) if (b[i] >= 'A' && b[i] <= 'Z') b[i] += 32; }
    V v = vstr("");
    if (v.s) kfree(v.s);
    v.s = b ? b : sdup("");
    return v;
}
static V str_to_title(const char *s)
{
    char *b = sdup(s);
    if (b) {
        int word = 1;
        for (int i = 0; b[i]; i++) {
            if (b[i] == ' ' || b[i] == '\t' || b[i] == '_') { word = 1; }
            else if (word) { if (b[i] >= 'a' && b[i] <= 'z') b[i] -= 32; word = 0; }
            else { if (b[i] >= 'A' && b[i] <= 'Z') b[i] += 32; }
        }
    }
    V v = vstr("");
    if (v.s) kfree(v.s);
    v.s = b ? b : sdup("");
    return v;
}
static V str_strip(const char *s)
{
    while (issp(*s) || *s == '\n') s++;
    const char *e = s;
    while (*e) e++;
    while (e > s && (issp(e[-1]) || e[-1] == '\n')) e--;
    int n = (int)(e - s);
    char *b = (char *)kmalloc((size_t)n + 1);
    for (int i = 0; i < n; i++) b[i] = s[i];
    if (b) b[n] = 0;
    V v = vstr("");
    if (v.s) kfree(v.s);
    v.s = b ? b : sdup("");
    return v;
}

static V call_builtin(Lex *L, const char *fn, V args[], int ac);
static V call_user(Func *f, V args[], int ac);

static V call_name(Lex *L, const char *fn)
{
    V args[12];
    int ac = 0;
    lex_next(L);
    if (!(L->tok == TK_OP && L->op[0] == ')')) {
        for (;;) {
            if (ac >= 12) { fail("too many args"); break; }
            args[ac] = eval_expr(L);
            ac++;
            if (g_err) goto done;
            if (L->tok == TK_OP && L->op[0] == ',') { lex_next(L); continue; }
            break;
        }
    }
    if (!(L->tok == TK_OP && L->op[0] == ')'))
        fail("missing )");
    lex_next(L);
    {
        Func *f = find_func(fn);
        V r;
        if (f) r = call_user(f, args, ac);
        else r = call_builtin(L, fn, args, ac);
        for (int i = 0; i < ac; i++) vfree(&args[i]);
        return r;
    }
done:
    for (int i = 0; i < ac; i++) vfree(&args[i]);
    return vnone();
}

static V method_call(Lex *L, V recv, const char *m)
{
    V args[8];
    int ac = 0;
    lex_next(L);
    if (!(L->tok == TK_OP && L->op[0] == ')')) {
        for (;;) {
            if (ac >= 8) { fail("too many args"); break; }
            args[ac] = eval_expr(L);
            ac++;
            if (g_err) break;
            if (L->tok == TK_OP && L->op[0] == ',') { lex_next(L); continue; }
            break;
        }
    }
    if (!(L->tok == TK_OP && L->op[0] == ')'))
        fail("missing )");
    lex_next(L);

    V r = vnone();
    if (recv.t == T_STR) {
        const char *s = recv.s ? recv.s : "";
        if (name_eq(m, "upper")) r = str_to_upper(s);
        else if (name_eq(m, "lower")) r = str_to_lower(s);
        else if (name_eq(m, "title") || name_eq(m, "capitalize")) r = str_to_title(s);
        else if (name_eq(m, "strip")) r = str_strip(s);
        else if (name_eq(m, "isdigit")) {
            int ok = s[0] != 0;
            for (int i = 0; s[i]; i++) if (!isdg(s[i])) ok = 0;
            r = vint(ok);
        }
        else if (name_eq(m, "startswith") && ac >= 1 && args[0].t == T_STR) {
            int ok = 1;
            const char *p = args[0].s;
            for (int i = 0; p[i]; i++) if (s[i] != p[i]) { ok = 0; break; }
            r = vint(ok);
        }
        else if (name_eq(m, "endswith") && ac >= 1 && args[0].t == T_STR) {
            int ls = slen(s), lp = slen(args[0].s);
            int ok = ls >= lp;
            for (int i = 0; i < lp && ok; i++) if (s[ls - lp + i] != args[0].s[i]) ok = 0;
            r = vint(ok);
        }
        else if (name_eq(m, "replace") && ac >= 2 && args[0].t == T_STR && args[1].t == T_STR) {
            const char *a = args[0].s, *b = args[1].s;
            int la = slen(a);
            char *out = (char *)kmalloc(MAX_STR);
            int oi = 0;
            for (int i = 0; s[i] && oi < MAX_STR - 2; ) {
                int match = 1;
                for (int k = 0; k < la; k++) if (s[i + k] != a[k]) { match = 0; break; }
                if (match && la) {
                    const char *q = b;
                    while (*q && oi < MAX_STR - 2) out[oi++] = *q++;
                    i += la;
                } else out[oi++] = s[i++];
            }
            if (out) { out[oi] = 0; r = vstr(out); kfree(out); }
        }
        else if (name_eq(m, "split")) {
            int parts = 1;
            for (int i = 0; s[i]; i++) if (s[i] == ' ') { if (i && s[i-1] != ' ') parts++; }
            V lst = vlist(parts);
            int pi = 0, started = 0;
            char tok[MAX_STR]; int ti = 0;
            for (int i = 0; ; i++) {
                if (s[i] == ' ' || s[i] == 0) {
                    if (started) {
                        tok[ti] = 0;
                        if (pi < lst.n) { vfree(&lst.a[pi]); lst.a[pi] = vstr(tok); pi++; }
                        ti = 0; started = 0;
                    }
                    if (s[i] == 0) break;
                } else { tok[ti++] = s[i]; started = 1; }
            }
            V trimmed = vlist(pi);
            for (int i = 0; i < pi; i++) { vfree(&trimmed.a[i]); trimmed.a[i] = vcopy(lst.a[i]); }
            vfree(&lst);
            r = trimmed;
        }
        else if (name_eq(m, "index") && ac >= 1 && args[0].t == T_STR) {
            const char *p = args[0].s;
            int lp = slen(p), found = -1;
            for (int i = 0; s[i] && lp; i++) {
                int ok = 1;
                for (int k = 0; k < lp; k++) if (s[i+k] != p[k]) { ok = 0; break; }
                if (ok) { found = i; break; }
            }
            r = vint(found);
        }
        else { fail("unknown str method"); }
    } else if (recv.t == T_LIST) {
        if (name_eq(m, "index") && ac >= 1) {
            int found = -1;
            for (int i = 0; i < recv.n; i++) if (vals_eq(recv.a[i], args[0])) { found = i; break; }
            r = vint(found);
        } else if (name_eq(m, "count") && ac >= 1) {
            int c = 0;
            for (int i = 0; i < recv.n; i++) if (vals_eq(recv.a[i], args[0])) c++;
            r = vint(c);
        } else { fail("unknown list method"); }
    } else {
        fail("method on non-object");
    }
    for (int i = 0; i < ac; i++) vfree(&args[i]);
    return r;
}

static V primary(Lex *L)
{
    if (g_err) return vnone();
    if (L->tok == TK_NUM) {
        int64_t x = L->num;
        lex_next(L);
        return vint(x);
    }
    if (L->tok == TK_STR) {
        V v = vstr(L->strbuf);
        lex_next(L);
        return v;
    }
    if (L->tok == TK_OP && L->op[0] == '(') {
        lex_next(L);
        V v = eval_expr(L);
        if (L->tok == TK_OP && L->op[0] == ')') lex_next(L);
        else fail("missing )");
        return v;
    }
    if (L->tok == TK_OP && L->op[0] == '[') {
        lex_next(L);
        V el[MAX_LISTE];
        int n = 0;
        if (!(L->tok == TK_OP && L->op[0] == ']')) {
            for (;;) {
                if (n >= MAX_LISTE) { fail("list too long"); break; }
                el[n] = eval_expr(L);
                n++;
                if (g_err) break;
                if (L->tok == TK_OP && L->op[0] == ',') { lex_next(L); continue; }
                break;
            }
        }
        if (L->tok == TK_OP && L->op[0] == ']') lex_next(L);
        else fail("missing ]");
        V r = vlist(n);
        for (int i = 0; i < n; i++) { vfree(&r.a[i]); r.a[i] = vcopy(el[i]); vfree(&el[i]); }
        return r;
    }
    if (L->tok == TK_OP && L->op[0] == '{') {
        lex_next(L);
        V keys[MAX_LISTE];
        V vals[MAX_LISTE];
        int n = 0;
        if (!(L->tok == TK_OP && L->op[0] == '}')) {
            for (;;) {
                if (n >= MAX_LISTE) { fail("dict too long"); break; }
                keys[n] = eval_expr(L);
                if (L->tok != TK_OP || L->op[0] != ':') { fail("expected :"); vfree(&keys[n]); n++; break; }
                lex_next(L);
                vals[n] = eval_expr(L);
                n++;
                if (g_err) break;
                if (L->tok == TK_OP && L->op[0] == ',') { lex_next(L); continue; }
                break;
            }
        }
        if (L->tok == TK_OP && L->op[0] == '}') lex_next(L);
        else fail("missing }");
        V r = vdict(n);
        for (int i = 0; i < n; i++) {
            vfree(&r.dk[i]); vfree(&r.dv[i]);
            r.dk[i] = vcopy(keys[i]); r.dv[i] = vcopy(vals[i]);
            vfree(&keys[i]); vfree(&vals[i]);
        }
        return r;
    }
    if (L->tok == TK_NAME) {
        char nm[NAME_LEN];
        int k = 0;
        while (L->name[k] && k < NAME_LEN - 1) { nm[k] = L->name[k]; k++; }
        nm[k] = 0;
        if (name_eq(nm, "True")) { lex_next(L); return vint(1); }
        if (name_eq(nm, "False")) { lex_next(L); return vint(0); }
        if (name_eq(nm, "None")) { lex_next(L); return vnone(); }
        lex_next(L);
        if (L->tok == TK_OP && L->op[0] == '(')
            return call_name(L, nm);
        V *e = find_var(nm);
        if (!e) { fail("name error"); return vnone(); }
        return vcopy(*e);
    }
    fail("unexpected token");
    lex_next(L);
    return vnone();
}

static void list_append_to(V *lst, V item)
{
    if (lst->n >= MAX_LISTE) { fail("list too long"); return; }
    V *na = (V *)kmalloc((size_t)(lst->n + 1) * sizeof(V));
    if (!na) { fail("out of memory"); return; }
    for (int i = 0; i < lst->n; i++)
        na[i] = vcopy(lst->a[i]);
    na[lst->n] = vcopy(item);
    if (lst->a) kfree(lst->a);
    lst->a = na;
    lst->n++;
}
static V list_pop_from(V *lst)
{
    if (lst->n <= 0)
        return vnone();
    V r = vcopy(lst->a[lst->n - 1]);
    vfree(&lst->a[lst->n - 1]);
    lst->n--;
    return r;
}

static V postfix(Lex *L)
{
    V *slot = 0;
    V v;
    if (L->tok == TK_NAME) {
        char nm[NAME_LEN];
        int k = 0;
        while (L->name[k] && k < NAME_LEN - 1) { nm[k] = L->name[k]; k++; }
        nm[k] = 0;
        lex_next(L);
        if (L->tok == TK_OP && L->op[0] == '(') {
            v = call_name(L, nm);
        } else {
            V *e = find_var(nm);
            if (!e) { fail("name error"); v = vnone(); }
            else { slot = e; v = vcopy(*e); }
        }
    } else {
        v = primary(L);
    }
    while (!g_err) {
        if (L->tok == TK_OP && L->op[0] == '[') {
            lex_next(L);
            V idx = eval_expr(L);
            if (L->tok == TK_OP && L->op[0] == ']') lex_next(L);
            else fail("missing ]");
            if (v.t == T_LIST && idx.t == T_INT) {
                int64_t ix = idx.i;
                if (ix < 0) ix += v.n;
                if (ix < 0 || ix >= v.n) { fail("index out of range"); vfree(&idx); break; }
                if (slot) {
                    V *ns = &slot->a[ix];
                    V r = vcopy(*ns);
                    vfree(&v);
                    slot = ns;
                    v = r;
                } else {
                    V r = vcopy(v.a[ix]);
                    vfree(&v);
                    v = r;
                }
            } else if (v.t == T_DICT) {
                V r = vnone();
                int found = 0;
                for (int i = 0; i < v.dn; i++)
                    if (vals_eq(v.dk[i], idx)) { r = vcopy(v.dv[i]); found = 1; break; }
                vfree(&v);
                if (!found) { vfree(&idx); fail("key error"); break; }
                slot = 0;
                v = r;
            } else if (v.t == T_STR && idx.t == T_INT) {
                const char *s = v.s ? v.s : "";
                int64_t ix = idx.i;
                int l = slen(s);
                if (ix < 0) ix += l;
                char one[2] = {0, 0};
                if (ix < 0 || ix >= l) { fail("index out of range"); vfree(&idx); break; }
                one[0] = s[ix];
                V r = vstr(one);
                vfree(&v);
                slot = 0;
                v = r;
            } else {
                fail("index needs list/str/dict");
            }
            vfree(&idx);
        } else if (L->tok == TK_OP && L->op[0] == '.') {
            lex_next(L);
            if (L->tok != TK_NAME) { fail("expected method"); break; }
            char m[NAME_LEN];
            int k = 0;
            while (L->name[k] && k < NAME_LEN - 1) { m[k] = L->name[k]; k++; }
            m[k] = 0;
            lex_next(L);
            if (L->tok == TK_OP && L->op[0] == '(') {
                if (slot && slot->t == T_LIST &&
                    (name_eq(m, "append") || name_eq(m, "pop"))) {
                    lex_next(L);
                    V item = vnone();
                    int has = 0;
                    if (!(L->tok == TK_OP && L->op[0] == ')')) {
                        item = eval_expr(L);
                        has = 1;
                    }
                    if (!(L->tok == TK_OP && L->op[0] == ')'))
                        fail("missing )");
                    else lex_next(L);
                    if (name_eq(m, "append")) {
                        if (has) list_append_to(slot, item);
                        vfree(&item);
                        vfree(&v);
                        v = vnone();
                        slot = 0;
                    } else {
                        V r = list_pop_from(slot);
                        vfree(&item);
                        vfree(&v);
                        v = r;
                        slot = 0;
                    }
                } else {
                    V r = method_call(L, v, m);
                    vfree(&v);
                    slot = 0;
                    v = r;
                }
            } else {
                fail("attribute unsupported");
                break;
            }
        } else {
            break;
        }
    }
    return v;
}

static V unary(Lex *L)
{
    if (L->tok == TK_OP && L->op[0] == '-') {
        lex_next(L);
        V v = unary(L);
        if (v.t == T_INT) v.i = -v.i;
        else fail("unary - needs int");
        return v;
    }
    if (L->tok == TK_OP && L->op[0] == '+') { lex_next(L); return unary(L); }
    if (L->tok == TK_OP && L->op[0] == '~') {
        lex_next(L);
        V v = unary(L);
        if (v.t == T_INT) v.i = ~v.i;
        else fail("~ needs int");
        return v;
    }
    if (L->tok == TK_NAME && name_eq(L->name, "not")) {
        lex_next(L);
        V v = unary(L);
        int t = truth(v);
        vfree(&v);
        return vint(!t);
    }
    return postfix(L);
}
static V power(Lex *L)
{
    V v = unary(L);
    if (L->tok == TK_OP && L->op[0] == '*' && L->op[1] == '*') {
        lex_next(L);
        V e = power(L);
        if (v.t != T_INT || e.t != T_INT) fail("** needs ints");
        else {
            int64_t ex = e.i, r = 1;
            if (ex < 0) r = 0;
            else while (ex-- > 0) r *= v.i;
            v.i = r;
        }
        vfree(&e);
    }
    return v;
}
static V mul(Lex *L)
{
    V v = power(L);
    while (L->tok == TK_OP &&
           ((L->op[0] == '*' && L->op[1] == 0) ||
            L->op[0] == '/' || L->op[0] == '%')) {
        char op = L->op[0];
        int dbl = L->op[1] == '/';
        lex_next(L);
        V r = power(L);
        if (v.t == T_INT && r.t == T_INT) {
            if (r.i == 0 && op != '*') { fail("division by zero"); }
            else if (op == '*') v.i *= r.i;
            else if (op == '%') v.i %= r.i;
            else {
                int64_t q = v.i / r.i;
                int64_t rem = v.i % r.i;
                if (!dbl) q = (v.i - rem) / r.i;
                if ((rem != 0) && ((v.i < 0) != (r.i < 0)) && op == '/') q--;
                v.i = q;
            }
        } else if (op == '*' && v.t == T_STR && r.t == T_INT) {
            int cnt = (int)(r.i < 0 ? 0 : r.i);
            int l = slen(v.s);
            if ((long)cnt * l > MAX_STR - 1) cnt = (MAX_STR - 1) / (l ? l : 1);
            char *b = (char *)kmalloc((size_t)cnt * l + 1);
            int oi = 0;
            for (int t = 0; t < cnt; t++) for (int i = 0; i < l; i++) b[oi++] = v.s[i];
            b[oi] = 0;
            vfree(&v);
            v = vstr(b);
            kfree(b);
        } else if (op == '*' && v.t == T_INT && r.t == T_STR) {
            V tmp = vcopy(r);
            vfree(&r);
            r = tmp;
            int cnt = (int)(v.i < 0 ? 0 : v.i);
            int l = slen(r.s);
            if ((long)cnt * l > MAX_STR - 1) cnt = (MAX_STR - 1) / (l ? l : 1);
            char *b = (char *)kmalloc((size_t)cnt * l + 1);
            int oi = 0;
            for (int t = 0; t < cnt; t++) for (int i = 0; i < l; i++) b[oi++] = r.s[i];
            b[oi] = 0;
            vfree(&r);
            v = vstr(b);
            kfree(b);
        } else if (op == '*' && v.t == T_LIST && r.t == T_INT) {
            int cnt = (int)(r.i < 0 ? 0 : r.i);
            if (cnt > 4096) cnt = 4096;
            V nl = vlist(v.n * cnt);
            for (int t = 0; t < cnt; t++)
                for (int i = 0; i < v.n; i++) {
                    int idx = t * v.n + i;
                    if (idx < nl.n) { vfree(&nl.a[idx]); nl.a[idx] = vcopy(v.a[i]); }
                }
            vfree(&v);
            v = nl;
        } else {
            fail("bad operand for * / %");
        }
        vfree(&r);
    }
    return v;
}
static V add(Lex *L)
{
    V v = mul(L);
    while (L->tok == TK_OP && (L->op[0] == '+' || L->op[0] == '-')) {
        char op = L->op[0];
        lex_next(L);
        V r = mul(L);
        if (v.t == T_INT && r.t == T_INT) {
            v.i += op == '+' ? r.i : -r.i;
        } else if (op == '+' && v.t == T_STR && r.t == T_STR) {
            int la = slen(v.s), lb = slen(r.s);
            if (la + lb > MAX_STR - 1) fail("string too long");
            else {
                char *b = (char *)kmalloc((size_t)la + lb + 1);
                int oi = 0;
                for (int i = 0; i < la; i++) b[oi++] = v.s[i];
                for (int i = 0; i < lb; i++) b[oi++] = r.s[i];
                b[oi] = 0;
                vfree(&v);
                v = vstr(b);
                kfree(b);
            }
        } else if (op == '+' && v.t == T_LIST && r.t == T_LIST) {
            if (v.n + r.n > MAX_LISTE) fail("list too long");
            else {
                V nl = vlist(v.n + r.n);
                for (int i = 0; i < v.n; i++) { vfree(&nl.a[i]); nl.a[i] = vcopy(v.a[i]); }
                for (int i = 0; i < r.n; i++) { vfree(&nl.a[v.n + i]); nl.a[v.n + i] = vcopy(r.a[i]); }
                vfree(&v);
                v = nl;
            }
        } else {
            fail("add/sub needs ints/strs/lists");
        }
        vfree(&r);
    }
    return v;
}
static V shift(Lex *L)
{
    V v = add(L);
    while (L->tok == TK_OP && ((L->op[0] == '<' && L->op[1] == '<') ||
                               (L->op[0] == '>' && L->op[1] == '>'))) {
        int left = L->op[0] == '<';
        lex_next(L);
        V r = add(L);
        if (v.t == T_INT && r.t == T_INT) {
            int64_t sh = r.i;
            if (sh < 0) fail("negative shift");
            else if (sh > 63) v.i = 0;
            else v.i = left ? (v.i << sh) : (v.i >> sh);
        } else fail("shift needs ints");
        vfree(&r);
    }
    return v;
}
static V cmp(Lex *L)
{
    V v = shift(L);
    while (L->tok == TK_OP &&
           ((L->op[0] == '=' && L->op[1] == '=') ||
            (L->op[0] == '!' && L->op[1] == '=') ||
            L->op[0] == '<' || L->op[0] == '>')) {
        char o0 = L->op[0], o1 = L->op[1];
        lex_next(L);
        V r = shift(L);
        int res = 0;
        if (o0 == '=' && o1 == '=') res = vals_eq(v, r);
        else if (o0 == '!' && o1 == '=') res = !vals_eq(v, r);
        else if (v.t == T_INT && r.t == T_INT) {
            if (o0 == '<') res = o1 == '=' ? v.i <= r.i : v.i < r.i;
            else res = o1 == '=' ? v.i >= r.i : v.i > r.i;
        } else if (v.t == T_STR && r.t == T_STR) {
            int c = 0;
            const char *a = v.s ? v.s : "", *b = r.s ? r.s : "";
            while (*a && *b && *a == *b) { a++; b++; }
            c = (int)(unsigned char)*a - (int)(unsigned char)*b;
            if (o0 == '<') res = o1 == '=' ? c <= 0 : c < 0;
            else res = o1 == '=' ? c >= 0 : c > 0;
        } else fail("compare needs ints/strs");
        vfree(&r);
        v.t = T_INT;
        v.i = res;
    }
    return v;
}
static V bit_and(Lex *L)
{
    V v = cmp(L);
    while (L->tok == TK_OP && L->op[0] == '&' && L->op[1] == 0) {
        lex_next(L);
        V r = cmp(L);
        if (v.t == T_INT && r.t == T_INT) v.i &= r.i;
        else fail("& needs ints");
        vfree(&r);
    }
    return v;
}
static V bit_xor(Lex *L)
{
    V v = bit_and(L);
    while (L->tok == TK_OP && L->op[0] == '^') {
        lex_next(L);
        V r = bit_and(L);
        if (v.t == T_INT && r.t == T_INT) v.i ^= r.i;
        else fail("^ needs ints");
        vfree(&r);
    }
    return v;
}
static V bit_or(Lex *L)
{
    V v = bit_xor(L);
    while (L->tok == TK_OP && L->op[0] == '|' && L->op[1] == 0) {
        lex_next(L);
        V r = bit_xor(L);
        if (v.t == T_INT && r.t == T_INT) v.i |= r.i;
        else fail("| needs ints");
        vfree(&r);
    }
    return v;
}
static V logic_and(Lex *L)
{
    V v = bit_or(L);
    while (L->tok == TK_NAME && name_eq(L->name, "and")) {
        lex_next(L);
        int lv = truth(v);
        vfree(&v);
        V r = bit_or(L);
        v = vint(lv && truth(r));
        vfree(&r);
    }
    return v;
}
static V eval_expr(Lex *L)
{
    V v = logic_and(L);
    while (L->tok == TK_NAME && name_eq(L->name, "or")) {
        lex_next(L);
        int lv = truth(v);
        vfree(&v);
        V r = logic_and(L);
        v = vint(lv || truth(r));
        vfree(&r);
    }
    return v;
}

static int64_t to_int(V v)
{
    if (v.t == T_INT) return v.i;
    if (v.t == T_STR && v.s) {
        const char *p = v.s;
        while (issp(*p)) p++;
        int neg = 0;
        if (*p == '-') { neg = 1; p++; }
        else if (*p == '+') p++;
        int64_t x = 0;
        while (isdg(*p)) { x = x * 10 + (*p - '0'); p++; }
        return neg ? -x : x;
    }
    if (v.t == T_NONE) return 0;
    if (v.t == T_LIST) return v.n;
    if (v.t == T_DICT) return v.dn;
    return 0;
}

static V call_builtin(Lex *L, const char *fn, V args[], int ac)
{
    (void)L;
    if (name_eq(fn, "print")) {
        for (int i = 0; i < ac; i++) {
            if (i) outc(' ');
            emit_val(args[i]);
        }
        outc('\n');
        return vnone();
    }
    if (name_eq(fn, "range")) {
        int64_t lo = 0, hi = 0, step = 1;
        if (ac == 1 && args[0].t == T_INT) hi = args[0].i;
        else if (ac == 2 && args[0].t == T_INT && args[1].t == T_INT) { lo = args[0].i; hi = args[1].i; }
        else if (ac == 3 && args[0].t == T_INT && args[1].t == T_INT && args[2].t == T_INT) {
            lo = args[0].i; hi = args[1].i; step = args[2].i;
            if (step == 0) { fail("range step 0"); return vnone(); }
        } else { fail("range needs ints"); return vnone(); }
        int cap = 0;
        if (step > 0) { cap = (int)((hi - lo + step - 1) / step); if (cap < 0) cap = 0; }
        else { cap = (int)((lo - hi - step - 1) / (-step)); if (cap < 0) cap = 0; }
        if (cap > 100000) cap = 100000;
        V r = vlist(cap);
        int64_t x = lo;
        int k = 0;
        while (k < cap && ((step > 0 && x < hi) || (step < 0 && x > hi))) {
            r.a[k] = vint(x);
            x += step;
            k++;
        }
        r.n = k;
        return r;
    }
    if (name_eq(fn, "len") && ac == 1) {
        if (args[0].t == T_STR) return vint(slen(args[0].s ? args[0].s : ""));
        if (args[0].t == T_LIST) return vint(args[0].n);
        if (args[0].t == T_DICT) return vint(args[0].dn);
        fail("len needs str/list/dict");
        return vnone();
    }
    if (name_eq(fn, "str") && ac == 1) {
        char b[64];
        repr_into(args[0], b, sizeof(b));
        if (args[0].t == T_NONE) return vstr("None");
        if (args[0].t == T_LIST) {
            V r = vlist(0);
            (void)r;
            char big[260];
            int n = 0;
            big[n++] = '[';
            for (int i = 0; i < args[0].n; i++) {
                if (i) { big[n++] = ','; big[n++] = ' '; }
                if (args[0].a[i].t == T_STR) { big[n++] = '\''; const char *q = args[0].a[i].s; while (*q && n < 255) big[n++] = *q++; big[n++] = '\''; }
                else { char t[40]; repr_into(args[0].a[i], t, 40); const char *q = t; while (*q && n < 255) big[n++] = *q++; }
            }
            big[n++] = ']'; big[n] = 0;
            return vstr(big);
        }
        if (args[0].t == T_DICT) {
            char big[260]; int n = 0;
            big[n++] = '{';
            for (int i = 0; i < args[0].dn; i++) {
                if (i) { big[n++] = ','; big[n++] = ' '; }
                char t[40]; repr_into(args[0].dk[i], t, 40);
                const char *q = t; while (*q && n < 255) big[n++] = *q++;
                big[n++] = ':'; big[n++] = ' ';
                repr_into(args[0].dv[i], t, 40);
                q = t; while (*q && n < 255) big[n++] = *q++;
            }
            big[n++] = '}'; big[n] = 0;
            return vstr(big);
        }
        return vstr(b);
    }
    if (name_eq(fn, "int") && ac == 1)
        return vint(to_int(args[0]));
    if (name_eq(fn, "abs") && ac == 1) {
        int64_t x = to_int(args[0]);
        return vint(x < 0 ? -x : x);
    }
    if ((name_eq(fn, "min") || name_eq(fn, "max") || name_eq(fn, "sum")) && ac >= 1) {
        int have = 0;
        int64_t acc = 0, best = 0;
        for (int i = 0; i < ac; i++) {
            if (args[i].t == T_INT) {
                int64_t x = args[i].i;
                if (!have) { best = x; have = 1; }
                else if (name_eq(fn, "min") && x < best) best = x;
                else if (name_eq(fn, "max") && x > best) best = x;
                acc += x;
            } else if (args[i].t == T_LIST) {
                for (int k = 0; k < args[i].n; k++) {
                    if (args[i].a[k].t != T_INT) { fail("aggregator needs ints"); return vnone(); }
                    int64_t x = args[i].a[k].i;
                    if (!have) { best = x; have = 1; }
                    else if (name_eq(fn, "min") && x < best) best = x;
                    else if (name_eq(fn, "max") && x > best) best = x;
                    acc += x;
                }
            } else { fail("aggregator needs ints/list"); return vnone(); }
        }
        if (!have) { fail("empty aggregate"); return vnone(); }
        return vint(name_eq(fn, "sum") ? acc : best);
    }
    if (name_eq(fn, "sorted") && ac >= 1 && args[0].t == T_LIST) {
        V r = vcopy(args[0]);
        for (int i = 0; i < r.n; i++) {
            for (int j = i + 1; j < r.n; j++) {
                int swap = 0;
                if (r.a[i].t == T_INT && r.a[j].t == T_INT)
                    swap = r.a[j].i < r.a[i].i;
                else if (r.a[i].t == T_STR && r.a[j].t == T_STR) {
                    const char *a = r.a[i].s, *b = r.a[j].s;
                    while (*a && *b && *a == *b) { a++; b++; }
                    swap = (int)(unsigned char)*b < (int)(unsigned char)*a;
                }
                if (swap) { V t = r.a[i]; r.a[i] = r.a[j]; r.a[j] = t; }
            }
        }
        (void)ac;
        return r;
    }
    if (name_eq(fn, "reversed") && ac == 1 && args[0].t == T_LIST) {
        V r = vlist(args[0].n);
        for (int i = 0; i < args[0].n; i++) {
            vfree(&r.a[i]);
            r.a[i] = vcopy(args[0].a[args[0].n - 1 - i]);
        }
        return r;
    }
    if (name_eq(fn, "chr") && ac == 1 && args[0].t == T_INT) {
        char b[2] = { (char)(args[0].i & 0xff), 0 };
        return vstr(b);
    }
    if (name_eq(fn, "ord") && ac == 1 && args[0].t == T_STR && args[0].s && args[0].s[0])
        return vint((int64_t)(unsigned char)args[0].s[0]);
    if (name_eq(fn, "list") && ac == 1) {
        if (args[0].t == T_LIST) return vcopy(args[0]);
        if (args[0].t == T_STR) {
            int l = slen(args[0].s ? args[0].s : "");
            V r = vlist(l);
            for (int i = 0; i < l; i++) {
                char b[2] = { args[0].s[i], 0 };
                vfree(&r.a[i]); r.a[i] = vstr(b);
            }
            return r;
        }
    }
    if (name_eq(fn, "type") && ac == 1) {
        const char *t = "object";
        if (args[0].t == T_INT) t = "int";
        else if (args[0].t == T_STR) t = "str";
        else if (args[0].t == T_LIST) t = "list";
        else if (args[0].t == T_DICT) t = "dict";
        else if (args[0].t == T_NONE) t = "NoneType";
        return vstr(t);
    }
    if (name_eq(fn, "hex") && ac == 1) {
        char b[40];
        int64_t v = to_int(args[0]);
        const char *hx = "0123456789abcdef";
        int n = 0;
        b[n++] = '0'; b[n++] = 'x';
        if (v == 0) b[n++] = '0';
        else { char tmp[20]; int k = 0; uint64_t x = (uint64_t)v; while (x) { tmp[k++] = hx[x & 15]; x >>= 4; } while (k) b[n++] = tmp[--k]; }
        b[n] = 0;
        return vstr(b);
    }
    if (name_eq(fn, "bin") && ac == 1) {
        char b[80];
        uint64_t v = (uint64_t)to_int(args[0]);
        int n = 0;
        b[n++] = '0'; b[n++] = 'b';
        if (v == 0) b[n++] = '0';
        else { char tmp[64]; int k = 0; while (v) { tmp[k++] = '0' + (v & 1); v >>= 1; } while (k) b[n++] = tmp[--k]; }
        b[n] = 0;
        return vstr(b);
    }
    if (name_eq(fn, "input")) {
        return vstr("");
    }
    fail("unknown function");
    return vnone();
}

static V call_user(Func *f, V args[], int ac)
{
    if (ac != f->np) { fail("argument count mismatch"); return vnone(); }
    if (depth + 1 >= MAX_FRAMES) { fail("recursion too deep"); return vnone(); }
    depth++;
    lcnt[depth] = 0;
    for (int i = 0; i < f->np; i++)
        assign_var(f->params[i], args[i]);
    int save_line = gline;
    int save_brk = g_brk, save_cnt = g_cnt;
    g_brk = 0; g_cnt = 0; g_ret = 0;
    gline = f->body_line;
    exec_block(f->body_indent);
    V r = vnone();
    if (g_retval_set) {
        r = vcopy(g_retval);
        vfree(&g_retval);
        g_retval_set = 0;
    }
    for (int i = 0; i < lcnt[depth]; i++)
        vfree(&lframes[depth][i].v);
    lcnt[depth] = 0;
    depth--;
    g_ret = 0;
    g_brk = save_brk;
    g_cnt = save_cnt;
    gline = save_line;
    return r;
}

static void dict_set(V *d, V key, V val)
{
    for (int i = 0; i < d->dn; i++)
        if (vals_eq(d->dk[i], key)) {
            vfree(&d->dv[i]);
            d->dv[i] = vcopy(val);
            return;
        }
    int n = d->dn;
    V *nk = (V *)kmalloc((size_t)(n + 1) * sizeof(V));
    V *nv = (V *)kmalloc((size_t)(n + 1) * sizeof(V));
    if (!nk || !nv) { fail("out of memory"); return; }
    for (int i = 0; i < n; i++) { nk[i] = vcopy(d->dk[i]); nv[i] = vcopy(d->dv[i]); }
    nk[n] = vcopy(key);
    nv[n] = vcopy(val);
    if (d->dk) kfree(d->dk);
    if (d->dv) kfree(d->dv);
    d->dk = nk;
    d->dv = nv;
    d->dn = n + 1;
}

static int starts_kw(const char *t, const char *kw)
{
    int kl = slen(kw);
    for (int i = 0; i < kl; i++)
        if (t[i] != kw[i]) return 0;
    char c = t[kl];
    return c == 0 || c == ' ' || c == '\t' || c == ':' || c == '(';
}

static void exec_block(int indent);

static void exec_if(int headInd)
{
    int take = 0;
    {
        Lex X;
        X.p = lines[gline].text;
        X.tok = TK_END;
        lex_next(&X);
        lex_next(&X);
        V cond = eval_expr(&X);
        take = truth(cond);
        vfree(&cond);
        if (X.tok != TK_OP || X.op[0] != ':') { fail("expected :"); return; }
    }
    int head = gline;
    int bodyStart = head + 1;
    int bodyIndent = (bodyStart < nlines) ? lines[bodyStart].indent : headInd + 1;
    if (bodyIndent <= headInd) { fail("expected indented block"); return; }
    if (take) {
        gline = bodyStart;
        exec_block(bodyIndent);
        while (gline < nlines && lines[gline].indent == headInd &&
               (starts_kw(lines[gline].text, "elif") || starts_kw(lines[gline].text, "else"))) {
            int c2 = gline + 1;
            int bi2 = (c2 < nlines) ? lines[c2].indent : headInd + 1;
            if (bi2 > headInd) {
                gline = c2;
                while (gline < nlines && lines[gline].indent > headInd)
                    gline++;
            } else {
                gline++;
            }
        }
    } else {
        gline = bodyStart;
        while (gline < nlines && lines[gline].indent > headInd)
            gline++;
        if (gline < nlines && lines[gline].indent == headInd &&
            starts_kw(lines[gline].text, "else")) {
            int eb = gline + 1;
            int ebi = (eb < nlines) ? lines[eb].indent : headInd + 1;
            if (ebi > headInd) { gline = eb; exec_block(ebi); }
            else fail("expected indented block");
        } else if (gline < nlines && lines[gline].indent == headInd &&
                   starts_kw(lines[gline].text, "elif")) {
            exec_if(headInd);
        }
    }
}

static void exec_line(void)
{
    const char *txt = lines[gline].text;
    int ind = lines[gline].indent;
    Lex X;
    X.p = txt;
    X.tok = TK_END;
    lex_next(&X);

    if (X.tok == TK_NAME) {
        if (starts_kw(txt, "if")) {
            exec_if(ind);
            return;
        }
        if (starts_kw(txt, "elif") || starts_kw(txt, "else")) {
            fail("unexpected elif/else");
            gline++;
            return;
        }
        if (starts_kw(txt, "pass")) {
            gline++;
            return;
        }
        if (starts_kw(txt, "break")) {
            g_brk = 1;
            gline++;
            return;
        }
        if (starts_kw(txt, "continue")) {
            g_cnt = 1;
            gline++;
            return;
        }
        if (starts_kw(txt, "return")) {
            Lex R;
            R.p = txt;
            R.tok = TK_END;
            lex_next(&R);
            lex_next(&R);
            V rv = vnone();
            if (R.tok != TK_END)
                rv = eval_expr(&R);
            if (g_retval_set)
                vfree(&g_retval);
            g_retval = rv;
            g_retval_set = 1;
            g_ret = 1;
            gline++;
            return;
        }
        if (starts_kw(txt, "while")) {
            int head = gline;
            int bodyStart = head + 1;
            int bodyIndent = (bodyStart < nlines) ? lines[bodyStart].indent : ind + 1;
            if (bodyIndent <= ind) { fail("expected indented block"); return; }
            int guard = 0;
            while (!g_err && !g_ret) {
                Lex C;
                C.p = lines[head].text;
                C.tok = TK_END;
                lex_next(&C);
                lex_next(&C);
                V cond = eval_expr(&C);
                int t = truth(cond);
                vfree(&cond);
                if (!t) break;
                gline = bodyStart;
                exec_block(bodyIndent);
                if (g_ret) break;
                if (g_brk) { g_brk = 0; break; }
                if (g_cnt) { g_cnt = 0; }
                if (++guard > LOOP_CAP) { fail("loop cap"); break; }
            }
            gline = bodyStart;
            while (gline < nlines && lines[gline].indent > ind)
                gline++;
            return;
        }
        if (starts_kw(txt, "for")) {
            int head = gline;
            int bodyStart = head + 1;
            int bodyIndent = (bodyStart < nlines) ? lines[bodyStart].indent : ind + 1;
            if (bodyIndent <= ind) { fail("expected indented block"); return; }
            Lex F;
            F.p = txt;
            F.tok = TK_END;
            lex_next(&F);
            lex_next(&F);
            char var[NAME_LEN];
            if (F.tok != TK_NAME) { fail("expected loop variable"); return; }
            int k = 0;
            while (F.name[k] && k < NAME_LEN - 1) { var[k] = F.name[k]; k++; }
            var[k] = 0;
            lex_next(&F);
            if (!(F.tok == TK_NAME && name_eq(F.name, "in"))) { fail("expected in"); return; }
            lex_next(&F);
            V iter = eval_expr(&F);
            if (iter.t != T_LIST) { vfree(&iter); fail("for needs list"); return; }
            int guard = 0;
            for (int i = 0; i < iter.n && !g_err && !g_ret; i++) {
                assign_var(var, iter.a[i]);
                gline = bodyStart;
                exec_block(bodyIndent);
                if (g_ret) break;
                if (g_brk) { g_brk = 0; break; }
                if (g_cnt) { g_cnt = 0; }
                if (++guard > LOOP_CAP) { fail("loop cap"); break; }
            }
            vfree(&iter);
            gline = bodyStart;
            while (gline < nlines && lines[gline].indent > ind)
                gline++;
            return;
        }
        if (starts_kw(txt, "def")) {
            Lex D;
            D.p = txt;
            D.tok = TK_END;
            lex_next(&D);
            lex_next(&D);
            if (D.tok != TK_NAME) { fail("expected function name"); return; }
            if (nfuncs >= MAX_FUNCS) { fail("too many functions"); return; }
            Func f;
            int q = 0;
            while (D.name[q] && q < NAME_LEN - 1) { f.name[q] = D.name[q]; q++; }
            f.name[q] = 0;
            f.np = 0;
            lex_next(&D);
            if (D.tok != TK_OP || D.op[0] != '(') { fail("expected ("); return; }
            lex_next(&D);
            if (!(D.tok == TK_OP && D.op[0] == ')')) {
                for (;;) {
                    if (D.tok != TK_NAME) { fail("expected param name"); break; }
                    if (f.np < MAX_PARAMS) {
                        int q2 = 0;
                        while (D.name[q2] && q2 < NAME_LEN - 1) { f.params[f.np][q2] = D.name[q2]; q2++; }
                        f.params[f.np][q2] = 0;
                        f.np++;
                    }
                    lex_next(&D);
                    if (D.tok == TK_OP && D.op[0] == ',') { lex_next(&D); continue; }
                    break;
                }
            }
            if (!(D.tok == TK_OP && D.op[0] == ')')) { fail("expected )"); return; }
            lex_next(&D);
            if (!(D.tok == TK_OP && D.op[0] == ':')) { fail("expected :"); return; }
            int bodyStart = gline + 1;
            f.body_line = bodyStart;
            f.body_indent = (bodyStart < nlines) ? lines[bodyStart].indent : ind + 1;
            if (f.body_indent <= ind) { fail("expected indented block"); return; }
            int slot = -1;
            for (int i = 0; i < nfuncs; i++)
                if (name_eq(funcs[i].name, f.name)) { slot = i; break; }
            if (slot < 0) { funcs[nfuncs++] = f; }
            else funcs[slot] = f;
            gline = bodyStart;
            while (gline < nlines && lines[gline].indent > ind)
                gline++;
            return;
        }

        char nm[NAME_LEN];
        int k = 0;
        while (X.name[k] && k < NAME_LEN - 1) { nm[k] = X.name[k]; k++; }
        nm[k] = 0;
        if (!name_eq(nm, "and") && !name_eq(nm, "or") &&
            !name_eq(nm, "not") && !name_eq(nm, "in")) {
            lex_next(&X);
            if (X.tok == TK_OP && X.op[0] == '=' && X.op[1] == 0) {
                lex_next(&X);
                V val = eval_expr(&X);
                if (!g_err) assign_var(nm, val);
                vfree(&val);
                gline++;
                return;
            }
            if (X.tok == TK_OP && X.op[0] == '[' ) {
                lex_next(&X);
                V idx = eval_expr(&X);
                if (!(X.tok == TK_OP && X.op[0] == ']')) { fail("missing ]"); vfree(&idx); return; }
                lex_next(&X);
                if (X.tok == TK_OP && X.op[0] == '=' && X.op[1] == 0) {
                    lex_next(&X);
                    V val = eval_expr(&X);
                    V *tgt = find_var(nm);
                    if (!tgt) {
                        V box = vdict(0);
                        assign_var(nm, box);
                        vfree(&box);
                        tgt = find_var(nm);
                    }
                    if (tgt && tgt->t == T_LIST && idx.t == T_INT) {
                        int64_t ix = idx.i;
                        if (ix < 0) ix += tgt->n;
                        if (ix >= 0 && ix < tgt->n) { vfree(&tgt->a[ix]); tgt->a[ix] = vcopy(val); }
                        else fail("index out of range");
                    } else if (tgt && tgt->t == T_DICT) {
                        dict_set(tgt, idx, val);
                    } else {
                        fail("cannot index-assign");
                    }
                    vfree(&idx);
                    vfree(&val);
                    gline++;
                    return;
                }
                vfree(&idx);
            }
            if (X.tok == TK_OP && X.op[1] == '=') {
                char op = X.op[0];
                lex_next(&X);
                V rhs = eval_expr(&X);
                V *tgt = find_var(nm);
                if (!tgt) { fail("name error"); vfree(&rhs); return; }
                V cur = vcopy(*tgt);
                if (op == '+' && cur.t == T_STR && rhs.t == T_STR) {
                    int la = slen(cur.s), lb = slen(rhs.s);
                    char *b = (char *)kmalloc((size_t)la + lb + 1);
                    int oi = 0;
                    for (int i = 0; i < la; i++) b[oi++] = cur.s[i];
                    for (int i = 0; i < lb; i++) b[oi++] = rhs.s[i];
                    b[oi] = 0;
                    V nv = vstr(b);
                    kfree(b);
                    assign_var(nm, nv);
                    vfree(&nv);
                } else if (cur.t == T_INT && rhs.t == T_INT) {
                    int64_t r;
                    if (op == '+') r = cur.i + rhs.i;
                    else if (op == '-') r = cur.i - rhs.i;
                    else if (op == '*') r = cur.i * rhs.i;
                    else if (op == '/') {
                        if (rhs.i == 0) { fail("division by zero"); r = 0; }
                        else r = cur.i / rhs.i;
                    } else if (op == '%') {
                        if (rhs.i == 0) { fail("mod by zero"); r = 0; }
                        else r = cur.i % rhs.i;
                    } else { r = 0; fail("bad aug op"); }
                    assign_var(nm, vint(r));
                } else {
                    fail("augmented assign type mismatch");
                }
                vfree(&cur);
                vfree(&rhs);
                gline++;
                return;
            }
        }
    }

    {
        Lex E;
        E.p = txt;
        E.tok = TK_END;
        lex_next(&E);
        V v = eval_expr(&E);
        if (!g_err && v.t != T_NONE) {
            emit_val(v);
            outc('\n');
        }
        vfree(&v);
    }
    gline++;
}

static void exec_block(int indent)
{
    int guard = 0;
    while (gline < nlines && !g_err && !g_ret) {
        if (lines[gline].indent < indent)
            break;
        if (lines[gline].indent > indent) {
            fail("unexpected indent");
            return;
        }
        if (g_brk || g_cnt)
            return;
        exec_line();
        if (++guard > LOOP_CAP) { fail("loop cap"); return; }
    }
}

static void parse_lines(const char *src)
{
    nlines = 0;
    const char *p = src;
    while (*p && nlines < MAX_LINES) {
        int indent = 0;
        const char *q = p;
        while (*q == ' ') { indent++; q++; }
        while (*q == '\t') { indent += 4; q++; }
        const char *e = q;
        while (*e && *e != '\n')
            e++;
        int len = (int)(e - q);
        while (len > 0 && (q[len - 1] == ' ' || q[len - 1] == '\t' || q[len - 1] == '\r'))
            len--;
        int blank = 1;
        for (int i = 0; i < len; i++)
            if (!issp(q[i])) { blank = 0; break; }
        if (!blank && q[0] != '#') {
            int c = len < 239 ? len : 239;
            lines[nlines].indent = indent;
            for (int i = 0; i < c; i++)
                lines[nlines].text[i] = q[i];
            lines[nlines].text[c] = 0;
            nlines++;
        }
        p = (*e == '\n') ? e + 1 : e;
        if (*e == 0)
            break;
    }
}

void py_init(void)
{
    genvn = 0;
    depth = 0;
    nfuncs = 0;
    g_err = 0;
    g_ret = g_brk = g_cnt = 0;
    g_retval_set = 0;
}

int py_exec(const char *src, char *out, int outsz)
{
    g_out = out;
    g_outn = 0;
    g_outcap = outsz;
    g_out[0] = 0;
    g_err = 0;
    g_ret = g_brk = g_cnt = 0;
    g_retval_set = 0;
    parse_lines(src);
    gline = 0;
    int baseIndent = nlines ? lines[0].indent : 0;
    while (gline < nlines && !g_err) {
        if (lines[gline].indent < baseIndent)
            break;
        exec_line();
        if (g_ret) { g_ret = 0; }
        if (g_brk || g_cnt) { fail("unexpected break/continue"); break; }
    }
    if (g_err) {
        g_outn = 0;
        g_out[0] = 0;
        outs("Error: ");
        outs(g_errmsg);
        outc('\n');
        return -1;
    }
    return 0;
}

static int check(const char *src, const char *expect)
{
    char out[400];
    int rc = py_exec(src, out, sizeof(out));
    if (rc < 0)
        return 0;
    int i = 0;
    while (expect[i] && out[i] && expect[i] == out[i])
        i++;
    return expect[i] == 0 && out[i] == 0;
}

void py_self_test(void)
{
    py_init();
    int ok = 1;
    ok &= check("1+2*3", "7\n");
    ok &= check("(1+2)*3", "9\n");
    ok &= check("7//2", "3\n");
    ok &= check("7%3", "1\n");
    ok &= check("2**10", "1024\n");
    ok &= check("0xff", "255\n");
    ok &= check("0b1010", "10\n");
    ok &= check("1<2 and 3>2", "1\n");
    ok &= check("10 & 6", "2\n");
    ok &= check("1 << 4", "16\n");
    ok &= check("12 ^ 10", "6\n");
    ok &= check("abs(-4)", "4\n");
    ok &= check("sum([1,2,3,4])", "10\n");
    ok &= check("max([3,9,2])", "9\n");
    ok &= check("sorted([3,1,2])", "[1, 2, 3]\n");
    ok &= check("range(2,6)", "[2, 3, 4, 5]\n");
    ok &= check("range(0,10,3)", "[0, 3, 6, 9]\n");
    ok &= check("len('hello')", "5\n");
    ok &= check("'ab' + 'cd'", "abcd\n");
    ok &= check("'ab' * 3", "ababab\n");
    ok &= check("'abc'.upper()", "ABC\n");
    ok &= check("'  Hello  '.strip()", "Hello\n");
    ok &= check("'a,b,c'.split()", "['a,b,c']\n");
    ok &= check("s='hi'\ns", "hi\n");
    ok &= check("s='hi'\nt=s+'!'\nt", "hi!\n");
    ok &= check("a=[1,2,3]\na.append(4)\na", "[1, 2, 3, 4]\n");
    ok &= check("a=[1,2,3]\na[0]=9\na", "[9, 2, 3]\n");
    ok &= check("d={'a':1}\nd['b']=2\nd['b']", "2\n");
    ok &= check("a=0\nfor i in range(5):\n    a=a+i\na", "10\n");
    ok &= check("n=5\nr=1\nwhile n>1:\n    r=r*n\n    n=n-1\nr", "120\n");
    ok &= check("x=7\nif x>5:\n    y=100\nelse:\n    y=1\ny", "100\n");
    ok &= check("x=2\nif x>5:\n    y=1\nelif x>1:\n    y=2\nelse:\n    y=3\ny", "2\n");
    ok &= check("def add(a,b):\n    return a+b\nadd(3,4)", "7\n");
    ok &= check("def fact(n):\n    if n<=1:\n        return 1\n    return n*fact(n-1)\nfact(5)", "120\n");
    ok &= check("def fib(n):\n    if n<2:\n        return n\n    return fib(n-1)+fib(n-2)\nfib(10)", "55\n");
    ok &= check("tot=0\nfor i in range(10):\n    if i%2==0:\n        tot=tot+i\n    else:\n        tot=tot+0\ntot", "20\n");
    ok &= check("a=[]\nfor i in range(4):\n    a.append(i*i)\na[3]", "9\n");
    ok &= check("for i in range(3):\n    if i==1:\n        break\n    z=i\nz", "0\n");
    ok &= check("s=0\nfor i in range(5):\n    if i==2:\n        continue\n    s=s+i\ns", "8\n");
    ok &= check("n=0\nfor i in range(3):\n    for j in range(3):\n        n=n+1\nn", "9\n");
    ok &= check("chr(65)", "A\n");
    ok &= check("ord('A')", "65\n");
    ok &= check("list('ab')", "['a', 'b']\n");
    {
        char out[200];
        int rc = py_exec("1/0", out, sizeof(out));
        ok &= (rc < 0);
        rc = py_exec("undefined_xyz", out, sizeof(out));
        ok &= (rc < 0);
        rc = py_exec("a=[1]\na[5]=9", out, sizeof(out));
        ok &= (rc < 0);
    }
    serial_puts(ok ? "[PY] self-test OK\n" : "[PY] self-test FAIL\n");
}
