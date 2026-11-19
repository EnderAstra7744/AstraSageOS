/* AstraScript engine 2 - types.c: values, the text builder, printing, comparing, environments. */
#include "as_types.h"

Value v_none(void) { Value v; v.t = T_NONE; v.u.i = 0; return v; }
Value v_bool(int b) { Value v; v.t = T_BOOL; v.u.i = b ? 1 : 0; return v; }
Value v_int(int i) { Value v; v.t = T_INT; v.u.i = i; return v; }
Value v_float(double d) { Value v; v.t = T_FLOAT; v.u.d = d; return v; }
Value v_ptr(int t, void *p) { Value v; v.t = (unsigned char)t; v.u.p = p; return v; }

Str *str_new(const char *s, int len)
{
    Str *r = as_alloc(sizeof(Str) + (unsigned)len);
    r->len = len;
    if (s) as_memcpy(r->s, s, (unsigned)len);
    r->s[len] = 0;
    return r;
}
Value v_str(const char *s, int len) { return v_ptr(T_STR, str_new(s, len)); }
Value v_cstr(const char *s) { return v_str(s, (int)as_strlen(s)); }

Seq *seq_new(int cap)
{
    Seq *q = as_alloc(sizeof(Seq));
    q->cap = cap < 4 ? 4 : cap;
    q->a = as_alloc((unsigned)q->cap * sizeof(Value));
    return q;
}
void seq_push(Seq *q, Value v)
{
    if (q->n == q->cap) {
        Value *na = as_alloc((unsigned)q->cap * 2 * sizeof(Value));
        as_memcpy(na, q->a, (unsigned)q->n * sizeof(Value));
        q->a = na; q->cap *= 2;
    }
    q->a[q->n++] = v;
}

Dict *dict_new(void)
{
    Dict *d = as_alloc(sizeof(Dict));
    d->cap = 4;
    d->k = as_alloc(4 * sizeof(Value)); d->v = as_alloc(4 * sizeof(Value));
    return d;
}
Value *dict_get(Dict *d, Value key)
{
    for (int i = 0; i < d->n; i++) if (v_eq(d->k[i], key)) return &d->v[i];
    return 0;
}
void dict_set(Dict *d, Value k, Value v)
{
    Value *p = dict_get(d, k);
    if (p) { *p = v; return; }
    if (d->n == d->cap) {
        Value *nk = as_alloc((unsigned)d->cap * 2 * sizeof(Value)), *nv = as_alloc((unsigned)d->cap * 2 * sizeof(Value));
        as_memcpy(nk, d->k, (unsigned)d->n * sizeof(Value)); as_memcpy(nv, d->v, (unsigned)d->n * sizeof(Value));
        d->k = nk; d->v = nv; d->cap *= 2;
    }
    d->k[d->n] = k; d->v[d->n] = v; d->n++;
}
int dict_del(Dict *d, Value k)
{
    for (int i = 0; i < d->n; i++)
        if (v_eq(d->k[i], k)) {
            as_memmove(&d->k[i], &d->k[i + 1], (unsigned)(d->n - i - 1) * sizeof(Value));
            as_memmove(&d->v[i], &d->v[i + 1], (unsigned)(d->n - i - 1) * sizeof(Value));
            d->n--;
            return 1;
        }
    return 0;
}
Value *dict_gets(Dict *d, Str *name)
{
    for (int i = 0; i < d->n; i++) if (d->k[i].t == T_STR && d->k[i].u.p == name) return &d->v[i];
    return 0;
}
void dict_sets(Dict *d, Str *name, Value v)
{
    Value *p = dict_gets(d, name);
    if (p) *p = v; else dict_set(d, v_ptr(T_STR, name), v);
}

/* ------------------------------------------------------------------ environments */
Env *env_new(Env *parent) { Env *e = as_alloc(sizeof(Env)); e->parent = parent; return e; }
Value *env_find_here(Env *e, Str *name)
{
    for (int i = 0; i < e->n; i++) if (e->k[i] == name) return &e->v[i];
    return 0;
}
Value *env_find(Env *e, Str *name)
{
    for (; e; e = e->parent) { Value *p = env_find_here(e, name); if (p) return p; }
    return 0;
}
void env_set(Env *e, Str *name, Value v)
{
    Value *p = env_find_here(e, name);
    if (p) { *p = v; return; }
    if (e->n == e->cap) {
        int nc = e->cap ? e->cap * 2 : 6;
        Str **nk = as_alloc((unsigned)nc * sizeof(Str *)); Value *nv = as_alloc((unsigned)nc * sizeof(Value));
        if (e->n) { as_memcpy(nk, e->k, (unsigned)e->n * sizeof(Str *)); as_memcpy(nv, e->v, (unsigned)e->n * sizeof(Value)); }
        e->k = nk; e->v = nv; e->cap = nc;
    }
    e->k[e->n] = name; e->v[e->n] = v; e->n++;
}

/* ------------------------------------------------------------------ truth, equality, order */
int v_isnum(Value v) { return v.t == T_INT || v.t == T_FLOAT || v.t == T_BOOL; }
double v_num(Value v) { return v.t == T_FLOAT ? v.u.d : (double)v.u.i; }

int v_truthy(Value v)
{
    switch (v.t) {
    case T_NONE: return 0;
    case T_BOOL: case T_INT: return v.u.i != 0;
    case T_FLOAT: return v.u.d != 0.0;
    case T_STR: case T_BYTES: case T_BARRAY: return ((Str *)v.u.p)->len != 0;
    case T_LIST: case T_TUPLE: case T_SET: case T_FSET: return ((Seq *)v.u.p)->n != 0;
    case T_DICT: return ((Dict *)v.u.p)->n != 0;
    case T_RANGE: { Range *r = v.u.p; return (r->c > 0 ? r->a < r->b : r->a > r->b); }
    default: return 1;
    }
}

int v_is(Value a, Value b)
{
    if (a.t != b.t) return 0;
    if (a.t == T_NONE) return 1;
    if (a.t == T_BOOL || a.t == T_INT) return a.u.i == b.u.i;
    if (a.t == T_FLOAT) return a.u.d == b.u.d;
    return a.u.p == b.u.p;
}

static int seq_eq(Seq *x, Seq *y)
{
    if (x->n != y->n) return 0;
    for (int i = 0; i < x->n; i++) if (!v_eq(x->a[i], y->a[i])) return 0;
    return 1;
}
static int set_eq(Seq *x, Seq *y)
{
    if (x->n != y->n) return 0;
    for (int i = 0; i < x->n; i++) {
        int f = 0;
        for (int j = 0; j < y->n && !f; j++) f = v_eq(x->a[i], y->a[j]);
        if (!f) return 0;
    }
    return 1;
}

int v_eq(Value a, Value b)
{
    if (v_isnum(a) && v_isnum(b)) return v_num(a) == v_num(b);
    if (a.t != b.t) {
        if ((a.t == T_SET && b.t == T_FSET) || (a.t == T_FSET && b.t == T_SET)) return set_eq(a.u.p, b.u.p);
        return 0;
    }
    switch (a.t) {
    case T_NONE: return 1;
    case T_STR: case T_BYTES: case T_BARRAY: { Str *x = a.u.p, *y = b.u.p; return x->len == y->len && !as_memcmp(x->s, y->s, (unsigned)x->len); }
    case T_LIST: case T_TUPLE: return seq_eq(a.u.p, b.u.p);
    case T_SET: case T_FSET: return set_eq(a.u.p, b.u.p);
    case T_DICT: {
        Dict *x = a.u.p, *y = b.u.p;
        if (x->n != y->n) return 0;
        for (int i = 0; i < x->n; i++) { Value *o = dict_get(y, x->k[i]); if (!o || !v_eq(x->v[i], *o)) return 0; }
        return 1;
    }
    case T_RANGE: { Range *x = a.u.p, *y = b.u.p; return x->a == y->a && x->b == y->b && x->c == y->c; }
    default: return a.u.p == b.u.p;
    }
}

int v_cmp(Value a, Value b, int *ok)
{
    *ok = 1;
    if (v_isnum(a) && v_isnum(b)) {
        if (a.t != T_FLOAT && b.t != T_FLOAT) return a.u.i < b.u.i ? -1 : a.u.i > b.u.i;
        double x = v_num(a), y = v_num(b);
        return x < y ? -1 : x > y;
    }
    if (a.t == b.t && (a.t == T_STR || a.t == T_BYTES)) {
        Str *x = a.u.p, *y = b.u.p;
        int n = x->len < y->len ? x->len : y->len;
        for (int i = 0; i < n; i++) if (x->s[i] != y->s[i]) return (unsigned char)x->s[i] < (unsigned char)y->s[i] ? -1 : 1;
        return x->len < y->len ? -1 : x->len > y->len;
    }
    if (a.t == b.t && (a.t == T_LIST || a.t == T_TUPLE)) {
        Seq *x = a.u.p, *y = b.u.p;
        int n = x->n < y->n ? x->n : y->n;
        for (int i = 0; i < n; i++) { int c = v_cmp(x->a[i], y->a[i], ok); if (!*ok) return 0; if (c) return c; }
        return x->n < y->n ? -1 : x->n > y->n;
    }
    *ok = 0;
    return 0;
}

const char *v_typename(Value v)
{
    switch (v.t) {
    case T_NONE: return "None"; case T_BOOL: return "bool"; case T_INT: return "int"; case T_FLOAT: return "float";
    case T_STR: return "str"; case T_BYTES: return "bytes"; case T_BARRAY: return "bytearray"; case T_LIST: return "list";
    case T_TUPLE: return "tuple"; case T_SET: return "set"; case T_FSET: return "frozenset"; case T_DICT: return "dict";
    case T_RANGE: return "range"; case T_FUNC: return "function"; case T_BUILTIN: return "builtin"; case T_CLASS: return "class";
    case T_INST: return ((Inst *)v.u.p)->cls->name->s; case T_BOUND: case T_BMETH: return "method"; case T_MODULE: return "module"; case T_DESC: return "descriptor"; case T_GEN: return "generator";
    default: return "object";
    }
}

/* ------------------------------------------------------------------ text builder */
void buf_init(Buf *b) { b->cap = 64; b->s = as_alloc(64); b->n = 0; }
static void buf_room(Buf *b, int more)
{
    if (b->n + more + 1 <= b->cap) return;
    int nc = b->cap * 2;
    while (nc < b->n + more + 1) nc *= 2;
    char *ns = as_alloc((unsigned)nc);
    as_memcpy(ns, b->s, (unsigned)b->n);
    b->s = ns; b->cap = nc;
}
void buf_add(Buf *b, const char *s, int len) { buf_room(b, len); as_memcpy(b->s + b->n, s, (unsigned)len); b->n += len; b->s[b->n] = 0; }
void buf_addc(Buf *b, const char *s) { buf_add(b, s, (int)as_strlen(s)); }
void buf_putc(Buf *b, char c) { buf_add(b, &c, 1); }

void fmt_int(int i, Buf *b)
{
    char t[16]; int k = 0; unsigned u = i < 0 ? 0u - (unsigned)i : (unsigned)i;
    do { t[k++] = (char)('0' + u % 10); u /= 10; } while (u);
    if (i < 0) buf_putc(b, '-');
    while (k) buf_putc(b, t[--k]);
}

/* 15 significant digits, shortest form; "2.0" for whole numbers, 1e+20 for very large or small ones */
void fmt_double(double d, Buf *b)
{
    if (d != d) { buf_addc(b, "nan"); return; }
    if (d > 1.7e308) { buf_addc(b, "inf"); return; }
    if (d < -1.7e308) { buf_addc(b, "-inf"); return; }
    if (d < 0) { buf_putc(b, '-'); d = -d; }
    if (d == 0.0) { buf_addc(b, "0.0"); return; }
    int e = 0;
    double m = d;
    while (m >= 1e16) { m /= 1e16; e += 16; }
    while (m >= 10.0) { m /= 10.0; e++; }
    while (m < 1e-16) { m *= 1e16; e -= 16; }
    while (m < 1.0) { m *= 10.0; e--; }
    char dig[20];
    for (int i = 0; i < 17; i++) { int x = (int)m; if (x > 9) x = 9; dig[i] = (char)('0' + x); m = (m - x) * 10.0; }
    int nd = 15;                                              /* round to 15 digits */
    if (dig[15] >= '5') {
        int i = 14;
        while (i >= 0) { if (dig[i] == '9') { dig[i] = '0'; i--; } else { dig[i]++; break; } }
        if (i < 0) { as_memmove(dig + 1, dig, 15); dig[0] = '1'; e++; }
    }
    while (nd > 1 && dig[nd - 1] == '0') nd--;
    if (e >= -5 && e < 16) {
        if (e >= 0) {
            for (int i = 0; i <= e; i++) buf_putc(b, i < nd ? dig[i] : '0');
            buf_putc(b, '.');
            if (nd > e + 1) for (int i = e + 1; i < nd; i++) buf_putc(b, dig[i]); else buf_putc(b, '0');
        } else {
            buf_addc(b, "0.");
            for (int i = 0; i < -e - 1; i++) buf_putc(b, '0');
            for (int i = 0; i < nd; i++) buf_putc(b, dig[i]);
        }
    } else {
        buf_putc(b, dig[0]);
        if (nd > 1) { buf_putc(b, '.'); for (int i = 1; i < nd; i++) buf_putc(b, dig[i]); }
        buf_addc(b, e < 0 ? "e-" : "e+");
        int ae = e < 0 ? -e : e;
        if (ae < 10) buf_putc(b, '0');
        fmt_int(ae, b);
    }
}

static void repr_str(Str *s, Buf *b, int is_bytes)
{
    char q = '\'';
    int has1 = 0, has2 = 0;
    for (int i = 0; i < s->len; i++) { if (s->s[i] == '\'') has1 = 1; if (s->s[i] == '"') has2 = 1; }
    if (has1 && !has2) q = '"';
    buf_putc(b, q);
    for (int i = 0; i < s->len; i++) {
        unsigned char c = (unsigned char)s->s[i];
        if (c == '\n') buf_addc(b, "\\n"); else if (c == '\t') buf_addc(b, "\\t"); else if (c == '\r') buf_addc(b, "\\r");
        else if (c == '\\') buf_addc(b, "\\\\"); else if (c == (unsigned char)q) { buf_putc(b, '\\'); buf_putc(b, (char)c); }
        else if (c < 32 || (is_bytes && c >= 127)) { const char *h = "0123456789abcdef"; buf_addc(b, "\\x"); buf_putc(b, h[c >> 4]); buf_putc(b, h[c & 15]); }
        else buf_putc(b, (char)c);
    }
    buf_putc(b, q);
}

static int repr_depth;
void v_repr(Value v, Buf *b, int quote)
{
    switch (v.t) {
    case T_NONE: buf_addc(b, "None"); return;
    case T_BOOL: buf_addc(b, v.u.i ? "True" : "False"); return;
    case T_INT: fmt_int(v.u.i, b); return;
    case T_FLOAT: fmt_double(v.u.d, b); return;
    case T_STR: if (quote) repr_str(v.u.p, b, 0); else buf_add(b, ((Str *)v.u.p)->s, ((Str *)v.u.p)->len); return;
    case T_BYTES: buf_putc(b, 'b'); repr_str(v.u.p, b, 1); return;
    case T_BARRAY: buf_addc(b, "bytearray(b"); repr_str(v.u.p, b, 1); buf_putc(b, ')'); return;
    case T_RANGE: { Range *r = v.u.p; buf_addc(b, "range("); fmt_int(r->a, b); buf_addc(b, ", "); fmt_int(r->b, b); if (r->c != 1) { buf_addc(b, ", "); fmt_int(r->c, b); } buf_putc(b, ')'); return; }
    case T_FUNC: buf_addc(b, "<function "); buf_add(b, ((Func *)v.u.p)->name->s, ((Func *)v.u.p)->name->len); buf_putc(b, '>'); return;
    case T_BUILTIN: buf_addc(b, "<built-in "); buf_addc(b, ((Builtin *)v.u.p)->name); buf_putc(b, '>'); return;
    case T_CLASS: buf_addc(b, "<class "); buf_add(b, ((Class *)v.u.p)->name->s, ((Class *)v.u.p)->name->len); buf_putc(b, '>'); return;
    case T_MODULE: buf_addc(b, "<module "); buf_add(b, ((Module *)v.u.p)->name->s, ((Module *)v.u.p)->name->len); buf_putc(b, '>'); return;
    case T_BOUND: case T_BMETH: buf_addc(b, "<method>"); return;
    case T_DESC: buf_addc(b, "<descriptor>"); return;
    case T_GEN: buf_addc(b, "<generator>"); return;
    case T_SUPER: buf_addc(b, "<super>"); return;
    case T_INST: {
        Inst *in = v.u.p;
        if (in->cls->exc) { Value m = exc_message(v); v_repr(m, b, 0); return; }
        buf_addc(b, "<"); buf_add(b, in->cls->name->s, in->cls->name->len); buf_addc(b, " object>"); return;
    }
    }
    if (repr_depth > 12) { buf_addc(b, "..."); return; }
    repr_depth++;
    if (v.t == T_LIST || v.t == T_TUPLE || v.t == T_SET || v.t == T_FSET) {
        Seq *q = v.u.p;
        const char *o = v.t == T_LIST ? "[" : v.t == T_TUPLE ? "(" : v.t == T_SET ? "{" : "frozenset({";
        const char *c = v.t == T_LIST ? "]" : v.t == T_TUPLE ? ")" : v.t == T_SET ? "}" : "})";
        if ((v.t == T_SET || v.t == T_FSET) && q->n == 0) buf_addc(b, v.t == T_SET ? "set()" : "frozenset()");
        else {
            buf_addc(b, o);
            for (int i = 0; i < q->n; i++) { if (i) buf_addc(b, ", "); v_repr(q->a[i], b, 1); }
            if (v.t == T_TUPLE && q->n == 1) buf_putc(b, ',');
            buf_addc(b, c);
        }
    } else if (v.t == T_DICT) {
        Dict *d = v.u.p;
        buf_putc(b, '{');
        for (int i = 0; i < d->n; i++) { if (i) buf_addc(b, ", "); v_repr(d->k[i], b, 1); buf_addc(b, ": "); v_repr(d->v[i], b, 1); }
        buf_putc(b, '}');
    }
    repr_depth--;
}

Value v_to_str(Value v)
{
    if (v.t == T_STR) return v;
    Buf b; buf_init(&b);
    v_repr(v, &b, 0);
    return v_str(b.s, b.n);
}
