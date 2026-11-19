/* AstraScript engine 2 - interpreter.c: evaluates expressions and runs statements. */
#include "as_types.h"

Interp I;

#define STEP_LIMIT 20000000UL
#define HAS_EXC (I.has_exc)

/* ------------------------------------------------------------------ numbers */
static double floor_d(double x)
{
    if (x > 2e9 || x < -2e9) return x;
    double t = (double)(int)x;
    return t > x ? t - 1 : t;
}
static double exp_d(double x)
{
    int k = 0;
    while (x > 0.5) { x /= 2; k++; }
    while (x < -0.5) { x /= 2; k++; }
    double term = 1, sum = 1;
    for (int i = 1; i < 24; i++) { term *= x / i; sum += term; }
    while (k--) sum *= sum;
    return sum;
}
static double ln_d(double x)
{
    int e = 0;
    while (x > 1.5) { x /= 2; e++; }
    while (x < 0.75) { x *= 2; e--; }
    double y = (x - 1) / (x + 1), y2 = y * y, term = y, sum = 0;
    for (int i = 1; i < 60; i += 2) { sum += term / i; term *= y2; }
    return 2 * sum + e * 0.6931471805599453;
}
static double sqrt_d(double x)
{
    if (x <= 0) return 0;
    double g = x > 1 ? x / 2 : 1;
    for (int i = 0; i < 60; i++) g = 0.5 * (g + x / g);
    return g;
}
double as_pow_d(double x, double y)
{
    if (y == 0) return 1;
    if (y == 0.5) return sqrt_d(x);
    if (y == (double)(int)y && y < 2e9 && y > -2e9) {
        int n = (int)y; int neg = n < 0; if (neg) n = -n;
        double r = 1, b = x;
        while (n) { if (n & 1) r *= b; b *= b; n >>= 1; }
        return neg ? 1 / r : r;
    }
    if (x <= 0) return 0;
    return exp_d(y * ln_d(x));
}

static int int_like(Value v) { return v.t == T_INT || v.t == T_BOOL; }

static Value type_err(const char *op, Value a, Value b)
{
    char m[120];
    as_fmt(m, sizeof m, "unsupported operand type(s) for %s: '%s' and '%s'", op, v_typename(a), v_typename(b));
    return rt_err("TypeError", m);
}

static Value seq_concat(Value a, Value b)
{
    Seq *x = a.u.p, *y = b.u.p, *r = seq_new(x->n + y->n);
    for (int i = 0; i < x->n; i++) seq_push(r, x->a[i]);
    for (int i = 0; i < y->n; i++) seq_push(r, y->a[i]);
    return v_ptr(a.t, r);
}

static Value set_op(int op, Value a, Value b)
{
    Seq *x = a.u.p, *y = b.u.p, *r = seq_new(x->n + y->n);
    for (int i = 0; i < x->n; i++) {
        int in_y = 0;
        for (int j = 0; j < y->n && !in_y; j++) in_y = v_eq(x->a[i], y->a[j]);
        if ((op == '|') || (op == '&' && in_y) || ((op == '-' || op == '^') && !in_y)) seq_push(r, x->a[i]);
    }
    if (op == '|' || op == '^')
        for (int j = 0; j < y->n; j++) {
            int in_x = 0;
            for (int i = 0; i < x->n && !in_x; i++) in_x = v_eq(y->a[j], x->a[i]);
            if (!in_x) seq_push(r, y->a[j]);
        }
    return v_ptr(a.t, r);
}

Value binop(int op, Value a, Value b)
{
    int ints = int_like(a) && int_like(b);
    switch (op) {
    case '+':
        if (ints) { int r; if (!__builtin_add_overflow(a.u.i, b.u.i, &r)) return v_int(r); return v_float((double)a.u.i + b.u.i); }
        if (v_isnum(a) && v_isnum(b)) return v_float(v_num(a) + v_num(b));
        if (a.t == T_STR && b.t == T_STR) { Str *x = a.u.p, *y = b.u.p; Str *r = str_new(0, x->len + y->len); as_memcpy(r->s, x->s, (unsigned)x->len); as_memcpy(r->s + x->len, y->s, (unsigned)y->len); r->s[r->len] = 0; return v_ptr(T_STR, r); }
        if ((a.t == T_BYTES || a.t == T_BARRAY) && (b.t == T_BYTES || b.t == T_BARRAY)) { Str *x = a.u.p, *y = b.u.p; Str *r = str_new(0, x->len + y->len); as_memcpy(r->s, x->s, (unsigned)x->len); as_memcpy(r->s + x->len, y->s, (unsigned)y->len); r->s[r->len] = 0; return v_ptr(a.t, r); }
        if ((a.t == T_LIST && b.t == T_LIST) || (a.t == T_TUPLE && b.t == T_TUPLE)) return seq_concat(a, b);
        return type_err("+", a, b);
    case '-':
        if (ints) { int r; if (!__builtin_sub_overflow(a.u.i, b.u.i, &r)) return v_int(r); return v_float((double)a.u.i - b.u.i); }
        if (v_isnum(a) && v_isnum(b)) return v_float(v_num(a) - v_num(b));
        if ((a.t == T_SET || a.t == T_FSET) && (b.t == T_SET || b.t == T_FSET)) return set_op('-', a, b);
        return type_err("-", a, b);
    case '*':
        if (ints) { int r; if (!__builtin_mul_overflow(a.u.i, b.u.i, &r)) return v_int(r); return v_float((double)a.u.i * b.u.i); }
        if (v_isnum(a) && v_isnum(b)) return v_float(v_num(a) * v_num(b));
        if (int_like(b) && (a.t == T_STR || a.t == T_LIST || a.t == T_TUPLE)) { Value t = a; a = b; b = t; }
        if (int_like(a) && (b.t == T_STR || b.t == T_LIST || b.t == T_TUPLE)) {
            int n = a.u.i < 0 ? 0 : a.u.i;
            if (b.t == T_STR) {
                Str *s = b.u.p;
                if ((double)n * s->len > 1e6) return rt_err("MemoryError", "the repeated text is too long");
                Str *r = str_new(0, s->len * n);
                for (int i = 0; i < n; i++) as_memcpy(r->s + i * s->len, s->s, (unsigned)s->len);
                return v_ptr(T_STR, r);
            }
            Seq *s = b.u.p;
            if ((double)n * s->n > 200000) return rt_err("MemoryError", "the repeated list is too long");
            Seq *r = seq_new(s->n * n);
            for (int i = 0; i < n; i++) for (int j = 0; j < s->n; j++) seq_push(r, s->a[j]);
            return v_ptr(b.t, r);
        }
        return type_err("*", a, b);
    case '/':
        if (v_isnum(a) && v_isnum(b)) { if (v_num(b) == 0) return rt_err("ZeroDivisionError", "division by zero"); return v_float(v_num(a) / v_num(b)); }
        return type_err("/", a, b);
    case 'D':
        if (ints) {
            if (b.u.i == 0) return rt_err("ZeroDivisionError", "integer division by zero");
            if (a.u.i == (int)0x80000000 && b.u.i == -1) return v_float(-(double)a.u.i);
            int q = a.u.i / b.u.i;
            if ((a.u.i % b.u.i != 0) && ((a.u.i < 0) != (b.u.i < 0))) q--;
            return v_int(q);
        }
        if (v_isnum(a) && v_isnum(b)) { if (v_num(b) == 0) return rt_err("ZeroDivisionError", "division by zero"); return v_float(floor_d(v_num(a) / v_num(b))); }
        return type_err("//", a, b);
    case '%':
        if (ints) {
            if (b.u.i == 0) return rt_err("ZeroDivisionError", "integer modulo by zero");
            if (b.u.i == -1) return v_int(0);
            int r = a.u.i % b.u.i;
            if (r != 0 && ((r < 0) != (b.u.i < 0))) r += b.u.i;
            return v_int(r);
        }
        if (v_isnum(a) && v_isnum(b)) { double y = v_num(b); if (y == 0) return rt_err("ZeroDivisionError", "modulo by zero"); double x = v_num(a); return v_float(x - floor_d(x / y) * y); }
        return type_err("%", a, b);
    case 'P':
        if (ints && b.u.i >= 0) {
            int r = 1, base = a.u.i, n = b.u.i, ovf = 0;
            while (n) { if (n & 1) { if (__builtin_mul_overflow(r, base, &r)) { ovf = 1; break; } } n >>= 1; if (n && __builtin_mul_overflow(base, base, &base)) { ovf = 1; break; } }
            if (!ovf) return v_int(r);
            return v_float(as_pow_d((double)a.u.i, (double)b.u.i));
        }
        if (v_isnum(a) && v_isnum(b)) {
            if (v_num(a) == 0 && v_num(b) < 0) return rt_err("ZeroDivisionError", "0 cannot be raised to a negative power");
            return v_float(as_pow_d(v_num(a), v_num(b)));
        }
        return type_err("**", a, b);
    case 'L': if (ints) return v_int(b.u.i >= 32 ? 0 : a.u.i << b.u.i); return type_err("<<", a, b);
    case 'R': if (ints) return v_int(b.u.i >= 32 ? (a.u.i < 0 ? -1 : 0) : a.u.i >> b.u.i); return type_err(">>", a, b);
    case '&': if (ints) return v_int(a.u.i & b.u.i); if ((a.t == T_SET || a.t == T_FSET) && (b.t == T_SET || b.t == T_FSET)) return set_op('&', a, b); return type_err("&", a, b);
    case '|': if (ints) return v_int(a.u.i | b.u.i); if ((a.t == T_SET || a.t == T_FSET) && (b.t == T_SET || b.t == T_FSET)) return set_op('|', a, b); return type_err("|", a, b);
    case '^': if (ints) return v_int(a.u.i ^ b.u.i); if ((a.t == T_SET || a.t == T_FSET) && (b.t == T_SET || b.t == T_FSET)) return set_op('^', a, b); return type_err("^", a, b);
    }
    return rt_err("RuntimeError", "unknown operator");
}

int contains(Value c, Value item, int *err)
{
    *err = 0;
    switch (c.t) {
    case T_STR: {
        if (item.t != T_STR) { *err = 1; rt_err("TypeError", "'in <string>' needs a string on the left"); return 0; }
        Str *h = c.u.p, *n = item.u.p;
        for (int i = 0; i + n->len <= h->len; i++) if (!as_memcmp(h->s + i, n->s, (unsigned)n->len)) return 1;
        return 0;
    }
    case T_BYTES: case T_BARRAY: {
        Str *h = c.u.p;
        if (int_like(item)) { for (int i = 0; i < h->len; i++) if ((unsigned char)h->s[i] == (item.u.i & 255)) return 1; return 0; }
        return 0;
    }
    case T_LIST: case T_TUPLE: case T_SET: case T_FSET: { Seq *q = c.u.p; for (int i = 0; i < q->n; i++) if (v_eq(q->a[i], item)) return 1; return 0; }
    case T_DICT: return dict_get(c.u.p, item) != 0;
    case T_RANGE: {
        if (!int_like(item)) return 0;
        Range *r = c.u.p; int x = item.u.i;
        if (r->c > 0 ? (x < r->a || x >= r->b) : (x > r->a || x <= r->b)) return 0;
        return (x - r->a) % r->c == 0;
    }
    default: *err = 1; rt_errf("TypeError", "argument of type '%s' is not iterable", v_typename(c)); return 0;
    }
}

/* ------------------------------------------------------------------ names */
static int in_list(Str **l, int n, Str *s) { for (int i = 0; i < n; i++) if (l[i] == s) return 1; return 0; }

static void env_add_name(Str ***l, int *n, Str *s)
{
    Str **nl = as_alloc((unsigned)(*n + 1) * sizeof(Str *));
    if (*n) as_memcpy(nl, *l, (unsigned)*n * sizeof(Str *));
    nl[(*n)++] = s; *l = nl;
}

void assign_name(Str *name, Value v)
{
    Env *e = I.cur;
    if (e->ngl && in_list(e->gl, e->ngl, name)) { env_set(I.globals, name, v); return; }
    if (e->nnl && in_list(e->nl, e->nnl, name)) {
        for (Env *p = e->parent; p; p = p->parent) { Value *x = env_find_here(p, name); if (x) { *x = v; return; } }
        rt_errf("NameError", "no outer variable '%s' for nonlocal", name->s);
        return;
    }
    env_set(e, name, v);
}

void assign_out(Str *name, Value v)
{
    Value *x = env_find(I.cur, name);
    if (x) *x = v; else env_set(I.globals, name, v);
}

static void assign_target(Node *t, Value v);

static void unpack(Node *t, Value v)
{
    Value *items; int n;
    if (!iter_items(v, &items, &n)) return;
    if (n != t->n) { char m[80]; as_fmt(m, sizeof m, "cannot unpack %d values into %d names", n, t->n); rt_err("ValueError", m); return; }
    for (int i = 0; i < n && !HAS_EXC; i++) assign_target(t->list[i], items[i]);
}

static int norm_index(int i, int n) { return i < 0 ? i + n : i; }

static void assign_target(Node *t, Value v)
{
    switch (t->k) {
    case N_NAME: assign_name(t->s, v); break;
    case N_ATTR: { Value o = eval(t->a); if (!HAS_EXC) set_attr(o, t->s, v); break; }
    case N_INDEX: {
        Value o = eval(t->a); if (HAS_EXC) return;
        Value ix = eval(t->b); if (HAS_EXC) return;
        if (o.t == T_LIST) {
            if (ix.t != T_INT) { rt_err("TypeError", "list indices must be integers"); return; }
            Seq *q = o.u.p; int i = norm_index(ix.u.i, q->n);
            if (i < 0 || i >= q->n) { rt_err("IndexError", "list assignment index out of range"); return; }
            q->a[i] = v;
        } else if (o.t == T_DICT) dict_set(o.u.p, ix, v);
        else if (o.t == T_BARRAY) {
            Str *s = o.u.p; int i = norm_index(ix.u.i, s->len);
            if (ix.t != T_INT || i < 0 || i >= s->len) { rt_err("IndexError", "bytearray index out of range"); return; }
            if (!int_like(v) || v.u.i < 0 || v.u.i > 255) { rt_err("ValueError", "byte must be in range(0, 256)"); return; }
            s->s[i] = (char)v.u.i;
        } else rt_errf("TypeError", "'%s' value does not support item assignment", v_typename(o));
        break;
    }
    case N_TUPLE: case N_LIST: unpack(t, v); break;
    default: rt_err("RuntimeError", "bad assignment target");
    }
}

/* ------------------------------------------------------------------ interpolated text values (the %strt / %flo prefixes) */
Value fstring_eval(Node *n)
{
    Buf b; buf_init(&b);
    for (int i = 0; i < n->n; i++) {
        Node *p = n->list[i];
        if (p->k == N_CONST) { Str *s = p->v.u.p; buf_add(&b, s->s, s->len); continue; }
        Value v = eval(p);
        if (HAS_EXC) return v_none();
        if (v.t == T_FLOAT && (n->flag & 2) && !(n->flag & 4)) { buf_putc(&b, 0); b.n--; fmt_int((int)v.u.d, &b); }
        else if (v.t == T_INT && (n->flag & 4) && !(n->flag & 2)) { fmt_double((double)v.u.i, &b); }
        else v_repr(v, &b, 0);
    }
    return v_str(b.s, b.n);
}

/* ------------------------------------------------------------------ calls */
#define MAXARG 24
static Value eval_call(Node *n)
{
    I.line = n->line;
    Node *cn = n->a;
    Value argbuf[MAXARG]; Value *argv = argbuf; int argc = 0, cap = MAXARG;
    Str *knbuf[8]; Value kvbuf[8]; Str **kn = knbuf; Value *kv = kvbuf; int nkw = 0, kcap = 8;
    Value callee, self = v_none(); int has_self = 0, direct_method = 0; Str *mname = 0;

    if (cn->k == N_ATTR) {
        Value o = eval(cn->a); if (HAS_EXC) return v_none();
        if (o.t == T_STR || o.t == T_LIST || o.t == T_TUPLE || o.t == T_SET || o.t == T_FSET || o.t == T_DICT || o.t == T_BYTES || o.t == T_BARRAY) { direct_method = 1; self = o; mname = cn->s; }
        else if (o.t == T_INST) {
            Inst *in = o.u.p;
            Value *a = dict_gets(&in->attrs, cn->s);
            if (!a) { for (Class *c = in->cls; c && !a; c = c->base) a = dict_gets(&c->attrs, cn->s); if (a && (a->t == T_FUNC || a->t == T_BUILTIN)) { callee = *a; self = o; has_self = 1; } else if (a && a->t == T_DESC) { callee = get_attr(o, cn->s); if (HAS_EXC) return v_none(); } else if (a) callee = *a; }
            else callee = *a;
            if (!a) return rt_errf("AttributeError", "object has no attribute '%s'", cn->s->s);
        } else { callee = get_attr(o, cn->s); if (HAS_EXC) return v_none(); }
    } else { callee = eval(cn); if (HAS_EXC) return v_none(); }

    /* legacy out-parameters: gui_wait(k, ms), gui_mouse(x, y) */
    int outs = 0;
    if (callee.t == T_BUILTIN && ((Builtin *)callee.u.p)->outs > 0) {
        Builtin *bb = callee.u.p; int o = bb->outs, ok = n->n >= o && (bb->nin == 0 || n->n == o + bb->nin);
        for (int i = 0; ok && i < o; i++) if (n->list[i]->k != N_NAME) ok = 0;
        if (ok) outs = o;
    }
    int skip = outs && !((Builtin *)callee.u.p)->inout ? outs : 0;

    for (int i = skip; i < n->n; i++) {
        Node *a = n->list[i];
        if (a->k == N_STAR) {
            Value v = eval(a->a); if (HAS_EXC) return v_none();
            Value *items; int cnt;
            if (!iter_items(v, &items, &cnt)) return v_none();
            for (int k = 0; k < cnt; k++) {
                if (argc == cap) { Value *na = as_alloc((unsigned)cap * 2 * sizeof(Value)); as_memcpy(na, argv, (unsigned)argc * sizeof(Value)); argv = na; cap *= 2; }
                argv[argc++] = items[k];
            }
        } else if (a->k == N_DSTAR) {
            Value v = eval(a->a); if (HAS_EXC) return v_none();
            if (v.t != T_DICT) return rt_err("TypeError", "** needs a dictionary");
            Dict *d = v.u.p;
            for (int k = 0; k < d->n; k++) {
                if (d->k[k].t != T_STR) return rt_err("TypeError", "keyword names must be strings");
                if (nkw == kcap) { Str **nk = as_alloc((unsigned)kcap * 2 * sizeof(Str *)); Value *nv = as_alloc((unsigned)kcap * 2 * sizeof(Value)); as_memcpy(nk, kn, (unsigned)nkw * sizeof(Str *)); as_memcpy(nv, kv, (unsigned)nkw * sizeof(Value)); kn = nk; kv = nv; kcap *= 2; }
                kn[nkw] = as_intern(((Str *)d->k[k].u.p)->s, ((Str *)d->k[k].u.p)->len); kv[nkw++] = d->v[k];
            }
        } else {
            Value v = eval(a); if (HAS_EXC) return v_none();
            if (argc == cap) { Value *na = as_alloc((unsigned)cap * 2 * sizeof(Value)); as_memcpy(na, argv, (unsigned)argc * sizeof(Value)); argv = na; cap *= 2; }
            argv[argc++] = v;
        }
    }
    for (int i = 0; i < n->n2; i++) {
        Value v = eval(n->list2[i]); if (HAS_EXC) return v_none();
        if (nkw == kcap) { Str **nk = as_alloc((unsigned)kcap * 2 * sizeof(Str *)); Value *nv = as_alloc((unsigned)kcap * 2 * sizeof(Value)); as_memcpy(nk, kn, (unsigned)nkw * sizeof(Str *)); as_memcpy(nv, kv, (unsigned)nkw * sizeof(Value)); kn = nk; kv = nv; kcap *= 2; }
        kn[nkw] = n->names[i]; kv[nkw++] = v;
    }

    Value r;
    I.line = n->line;
    if (direct_method) {
        if (nkw && (self.t == T_LIST) && !as_strcmp(mname->s, "sort")) {                 /* list.sort(key=..., reverse=...) */
            Value key = v_none(); int rev = 0;
            for (int i = 0; i < nkw; i++) { if (!as_strcmp(kn[i]->s, "key")) key = kv[i]; else if (!as_strcmp(kn[i]->s, "reverse")) rev = v_truthy(kv[i]); }
            seq_sort(self.u.p, key, rev);
            return v_none();
        }
        r = call_method(self, mname, argc, argv, nkw, kn, kv);
    } else if (has_self && callee.t == T_FUNC) r = call_func(callee.u.p, self, 1, argc, argv, nkw, kn, kv);
    else if (has_self && callee.t == T_BUILTIN) {
        Value tmp[MAXARG + 1]; int c2 = argc > MAXARG ? MAXARG : argc; tmp[0] = self; as_memcpy(tmp + 1, argv, (unsigned)c2 * sizeof(Value));
        r = ((Builtin *)callee.u.p)->fn(c2 + 1, tmp, nkw, kn, kv);
    } else r = call_value(callee, argc, argv, nkw, kn, kv);
    if (HAS_EXC) return v_none();
    if (outs == 1) assign_out(n->list[0]->s, r);
    else if (outs >= 2 && r.t == T_TUPLE) { Seq *q = r.u.p; for (int i = 0; i < outs && i < q->n; i++) assign_out(n->list[i]->s, q->a[i]); }
    return outs ? v_none() : r;
}

/* ------------------------------------------------------------------ expressions */
static Value cmp_values(int op, Value a, Value b)
{
    switch (op) {
    case '=': return v_bool(v_eq(a, b));
    case '!': return v_bool(!v_eq(a, b));
    case 's': return v_bool(v_is(a, b));
    case 'N': return v_bool(!v_is(a, b));
    case 'i': case 'n': { int err; int r = contains(b, a, &err); if (err) return v_none(); return v_bool(op == 'i' ? r : !r); }
    default: {
        int ok; int c = v_cmp(a, b, &ok);
        if (!ok) { char m[120]; as_fmt(m, sizeof m, "cannot compare '%s' with '%s'", v_typename(a), v_typename(b)); return rt_err("TypeError", m); }
        switch (op) { case '<': return v_bool(c < 0); case '>': return v_bool(c > 0); case 'l': return v_bool(c <= 0); default: return v_bool(c >= 0); }
    }
    }
}

static int comp_rec(Node *n, int ci, Seq *q, Dict *d)
{
    if (ci == n->n) {
        Value v = eval(n->a); if (HAS_EXC) return 4;
        if (n->op == 'd') { Value val = eval(n->b); if (HAS_EXC) return 4; dict_set(d, v, val); }
        else if (n->op == 's') { for (int j = 0; j < q->n; j++) if (v_eq(q->a[j], v)) return 0; seq_push(q, v); }
        else seq_push(q, v);
        return 0;
    }
    Node *c = n->list[ci];
    I.line = c->line;
    if (c->flag == 1) { Value t = eval(c->a); if (HAS_EXC) return 4; return v_truthy(t) ? comp_rec(n, ci + 1, q, d) : 0; }
    Value it = eval(c->b); if (HAS_EXC) return 4;
    Value *items; int cnt;
    if (!iter_items(it, &items, &cnt)) return 4;
    for (int i = 0; i < cnt; i++) {
        assign_target(c->a, items[i]); if (HAS_EXC) return 4;
        if (comp_rec(n, ci + 1, q, d)) return 4;
        if (++I.steps > 20000000UL) { rt_err("RuntimeError", "the program ran for too long (20 million steps) - stopped; is there an endless loop?"); return 4; }
    }
    return 0;
}

Value eval(Node *n)
{
    if (!n) return v_none();
    switch (n->k) {
    case N_CONST: return n->v;
    case N_NAME: {
        Value *v = env_find(I.cur, n->s);
        if (v) return *v;
        I.line = n->line;
        return rt_errf("NameError", "name '%s' is not defined", n->s->s);
    }
    case N_BIN: {
        Value a = eval(n->a); if (HAS_EXC) return v_none();
        Value b = eval(n->b); if (HAS_EXC) return v_none();
        I.line = n->line;
        return binop(n->op, a, b);
    }
    case N_UN: {
        Value a = eval(n->a); if (HAS_EXC) return v_none();
        I.line = n->line;
        if (n->op == '-') { if (a.t == T_FLOAT) return v_float(-a.u.d); if (int_like(a)) { if (a.u.i == (int)0x80000000) return v_float(-(double)a.u.i); return v_int(-a.u.i); } }
        else if (n->op == '+') { if (a.t == T_FLOAT) return a; if (int_like(a)) return v_int(a.u.i); }
        else if (n->op == '~') { if (int_like(a)) return v_int(~a.u.i); }
        return rt_errf("TypeError", "bad operand type for unary operator: '%s'", v_typename(a));
    }
    case N_AND: { Value a = eval(n->a); if (HAS_EXC || !v_truthy(a)) return a; return eval(n->b); }
    case N_OR: { Value a = eval(n->a); if (HAS_EXC || v_truthy(a)) return a; return eval(n->b); }
    case N_NOT: { Value a = eval(n->a); if (HAS_EXC) return v_none(); return v_bool(!v_truthy(a)); }
    case N_COND: { Value c = eval(n->a); if (HAS_EXC) return v_none(); return v_truthy(c) ? eval(n->b) : eval(n->c); }
    case N_COMP: {
        Env *saved = I.cur; I.cur = env_new(saved);               /* the loop variables live only inside the comprehension */
        Seq *q = n->op == 'd' ? 0 : seq_new(8); Dict *d = n->op == 'd' ? dict_new() : 0;
        int bad = comp_rec(n, 0, q, d);
        I.cur = saved;
        if (bad || HAS_EXC) return v_none();
        return n->op == 'd' ? v_ptr(T_DICT, d) : v_ptr(n->op == 'l' ? T_LIST : T_SET, q);
    }
    case N_CMP: {
        Value a = eval(n->a); if (HAS_EXC) return v_none();
        Value b = eval(n->b); if (HAS_EXC) return v_none();
        I.line = n->line;
        return cmp_values(n->op, a, b);
    }
    case N_CHAIN: {
        Value prev = eval(n->list[0]); if (HAS_EXC) return v_none();
        Value r = v_bool(1);
        for (int i = 0; i < n->s->len; i++) {
            Value cur = eval(n->list[i + 1]); if (HAS_EXC) return v_none();
            I.line = n->line;
            r = cmp_values((unsigned char)n->s->s[i], prev, cur); if (HAS_EXC) return v_none();
            if (!v_truthy(r)) return r;
            prev = cur;
        }
        return r;
    }
    case N_CALL: return eval_call(n);
    case N_ATTR: { Value o = eval(n->a); if (HAS_EXC) return v_none(); I.line = n->line; return get_attr(o, n->s); }
    case N_INDEX: {
        Value o = eval(n->a); if (HAS_EXC) return v_none();
        Value ix = eval(n->b); if (HAS_EXC) return v_none();
        I.line = n->line;
        switch (o.t) {
        case T_STR: return str_index(o, ix);
        case T_LIST: case T_TUPLE: case T_BYTES: case T_BARRAY: case T_RANGE: return seq_index(o, ix);
        case T_DICT: { Value *v = dict_get(o.u.p, ix); if (v) return *v; Buf b; buf_init(&b); v_repr(ix, &b, 1); return rt_err("KeyError", b.s); }
        default: return rt_errf("TypeError", "'%s' value is not subscriptable", v_typename(o));
        }
    }
    case N_SLICE: {
        Value o = eval(n->a); if (HAS_EXC) return v_none();
        int lo = 0, hi = 0, st = 1, hl = 0, hh = 0;
        if (n->b) { Value v = eval(n->b); if (HAS_EXC) return v_none(); if (v.t != T_NONE) { if (!int_like(v)) return rt_err("TypeError", "slice indices must be integers"); lo = v.u.i; hl = 1; } }
        if (n->c) { Value v = eval(n->c); if (HAS_EXC) return v_none(); if (v.t != T_NONE) { if (!int_like(v)) return rt_err("TypeError", "slice indices must be integers"); hi = v.u.i; hh = 1; } }
        if (n->d) { Value v = eval(n->d); if (HAS_EXC) return v_none(); if (v.t != T_NONE) { if (!int_like(v)) return rt_err("TypeError", "slice indices must be integers"); st = v.u.i; } }
        if (st == 0) return rt_err("ValueError", "slice step cannot be zero");
        I.line = n->line;
        if (o.t == T_STR) return str_slice(o, lo, hi, st, hl, hh);
        if (o.t == T_LIST || o.t == T_TUPLE || o.t == T_BYTES || o.t == T_BARRAY) return seq_slice(o, lo, hi, st, hl, hh);
        return rt_errf("TypeError", "'%s' value cannot be sliced", v_typename(o));
    }
    case N_LIST: case N_TUPLE: case N_SET: {
        Seq *q = seq_new(n->n);
        for (int i = 0; i < n->n; i++) {
            Value v = eval(n->list[i]); if (HAS_EXC) return v_none();
            if (n->k == N_SET) { int dup = 0; for (int j = 0; j < q->n && !dup; j++) dup = v_eq(q->a[j], v); if (dup) continue; }
            seq_push(q, v);
        }
        return v_ptr(n->k == N_LIST ? T_LIST : n->k == N_TUPLE ? T_TUPLE : T_SET, q);
    }
    case N_DICT: {
        Dict *d = dict_new();
        for (int i = 0; i < n->n; i++) {
            Value k = eval(n->list[i]); if (HAS_EXC) return v_none();
            Value v = eval(n->list2[i]); if (HAS_EXC) return v_none();
            dict_set(d, k, v);
        }
        return v_ptr(T_DICT, d);
    }
    case N_LAMBDA: return make_func(n, I.cur, 0);
    case N_FSTR: return fstring_eval(n);
    case N_CONCAT: {
        Value a = eval(n->a); if (HAS_EXC) return v_none();
        Value b = eval(n->b); if (HAS_EXC) return v_none();
        if (a.t != T_STR || b.t != T_STR) { I.line = n->line; return rt_err("TypeError", "only text values can be written next to each other"); }
        return binop('+', a, b);
    }
    case N_STAR: case N_DSTAR: return rt_err("RuntimeError", "'*' is only allowed in a call");
    }
    return rt_err("RuntimeError", "unknown expression");
}

/* ------------------------------------------------------------------ statements */
static Value default_for(Str *type)
{
    if (!type) return v_none();
    const char *t = type->s;
    if (!as_strcmp(t, "inter") || !as_strcmp(t, "int")) return v_int(0);
    if (!as_strcmp(t, "flo") || !as_strcmp(t, "float")) return v_float(0);
    if (!as_strcmp(t, "strt") || !as_strcmp(t, "str")) return v_cstr("");
    if (!as_strcmp(t, "bool")) return v_bool(0);
    if (!as_strcmp(t, "array") || !as_strcmp(t, "list")) return v_ptr(T_LIST, seq_new(4));
    if (!as_strcmp(t, "dict")) return v_ptr(T_DICT, dict_new());
    if (!as_strcmp(t, "tuple")) return v_ptr(T_TUPLE, seq_new(1));
    if (!as_strcmp(t, "set")) return v_ptr(T_SET, seq_new(4));
    return v_none();
}

static Value coerce(Str *type, Value v)
{
    if (!type) return v;
    const char *t = type->s;
    if (!as_strcmp(t, "inter") || !as_strcmp(t, "int")) { if (v.t == T_FLOAT) return v_int((int)v.u.d); if (v.t == T_BOOL) return v_int(v.u.i); }
    else if (!as_strcmp(t, "flo") || !as_strcmp(t, "float")) { if (v.t == T_INT || v.t == T_BOOL) return v_float((double)v.u.i); }
    else if (!as_strcmp(t, "strt") || !as_strcmp(t, "str")) { if (v.t != T_STR) return v_to_str(v); }
    else if (!as_strcmp(t, "bool")) { if (v.t != T_BOOL) return v_bool(v_truthy(v)); }
    return v;
}

static Value make_exc_value(Value v)
{
    if (v.t == T_STR) { rt_err("Error", ((Str *)v.u.p)->s); return I.exc; }
    if (v.t == T_CLASS && ((Class *)v.u.p)->exc) { Value r = instantiate(v.u.p, 0, 0, 0, 0, 0); return r; }
    if (v.t == T_INST && ((Inst *)v.u.p)->cls->exc) return v;
    rt_err("TypeError", "push_error needs a message or an Error object");
    return I.exc;
}

static int run_loop_body(Node *body, int *brk)
{
    int st = exec_block(body);
    *brk = 0;
    if (st == 1) { *brk = 1; return 0; }
    if (st == 2) return 0;
    return st;                                       /* 0 normal, 3 return, 4 exception */
}

static int exec_for(Node *n)
{
    Value it = eval(n->b); if (HAS_EXC) return 4;
    I.line = n->line;
    int brk;
    if (it.t == T_RANGE) {
        Range *r = it.u.p;
        for (int x = r->a; r->c > 0 ? x < r->b : x > r->b; x += r->c) {
            assign_target(n->a, v_int(x)); if (HAS_EXC) return 4;
            int st = run_loop_body(n->c, &brk); if (st) return st; if (brk) break;
        }
        return 0;
    }
    if (it.t == T_LIST || it.t == T_TUPLE) {
        Seq *q = it.u.p;
        for (int i = 0; i < q->n; i++) {
            assign_target(n->a, q->a[i]); if (HAS_EXC) return 4;
            int st = run_loop_body(n->c, &brk); if (st) return st; if (brk) break;
        }
        return 0;
    }
    Value *items; int cnt;
    if (!iter_items(it, &items, &cnt)) return 4;
    for (int i = 0; i < cnt; i++) {
        assign_target(n->a, items[i]); if (HAS_EXC) return 4;
        int st = run_loop_body(n->c, &brk); if (st) return st; if (brk) break;
    }
    return 0;
}

int exec_stmt(Node *n)
{
    if (++I.steps > STEP_LIMIT) { I.steps = 0; I.line = n->line; rt_err("RuntimeError", "the program ran for too long (20 million steps) - stopped; is there an endless loop?"); return 4; }
    I.line = n->line;
    switch (n->k) {
    case S_START: case S_END: case S_PASS: return 0;
    case S_EXPR: eval(n->a); return HAS_EXC ? 4 : 0;
    case S_ASSIGN: { Value v = eval(n->b); if (HAS_EXC) return 4; assign_target(n->a, v); return HAS_EXC ? 4 : 0; }
    case S_AUG: {
        Value cur = eval(n->a); if (HAS_EXC) return 4;
        Value r = eval(n->b); if (HAS_EXC) return 4;
        I.line = n->line;
        if (n->op == '+' && cur.t == T_LIST) { Value *items; int cnt; if (!iter_items(r, &items, &cnt)) return 4; for (int i = 0; i < cnt; i++) seq_push(cur.u.p, items[i]); return 0; }
        Value v = binop(n->op, cur, r); if (HAS_EXC) return 4;
        assign_target(n->a, v); return HAS_EXC ? 4 : 0;
    }
    case S_VAR: {
        Str *type = n->names ? n->names[0] : 0;
        Value v = n->a ? eval(n->a) : default_for(type);
        if (HAS_EXC) return 4;
        env_set(I.cur, n->s, coerce(type, v));
        return 0;
    }
    case S_IF: {
        for (Node *c = n; c; c = c->c && c->c->k == S_IF ? c->c : 0) {
            Value v = eval(c->a); if (HAS_EXC) return 4;
            if (v_truthy(v)) return exec_block(c->b);
            if (c->c && c->c->k == S_BLOCK) return exec_block(c->c);
        }
        return 0;
    }
    case S_WHILE: {
        for (;;) {
            Value v = eval(n->a); if (HAS_EXC) return 4;
            if (!v_truthy(v)) break;
            int brk; int st = run_loop_body(n->b, &brk); if (st) return st; if (brk) break;
        }
        return 0;
    }
    case S_CFOR: {
        if (n->a) { int st = exec_stmt(n->a); if (st) return st; }
        for (;;) {
            if (n->b) { Value v = eval(n->b); if (HAS_EXC) return 4; if (!v_truthy(v)) break; }
            int brk; int st = run_loop_body(n->d, &brk); if (st) return st; if (brk) break;
            if (n->c) { st = exec_stmt(n->c); if (st) return st; }
        }
        return 0;
    }
    case S_FOR: return exec_for(n);
    case S_MATCH: {
        Value s = eval(n->a); if (HAS_EXC) return 4;
        for (int i = 0; i < n->n; i++) {
            int hit = 1;
            if (n->list[i]) { Value p = eval(n->list[i]); if (HAS_EXC) return 4; hit = v_eq(s, p); }
            if (hit) return exec_block(n->list2[i]);
        }
        return 0;
    }
    case S_FUNC: {
        Env *clo = I.cur == I.class_env && I.cur->parent ? I.cur->parent : I.cur;
        Class *cls = I.cur == I.class_env ? (Class *)I.class_env->v[0].u.p : 0;
        Value f = make_func(n, clo, cls); if (HAS_EXC) return 4;
        env_set(I.cur, n->s, f); return 0;
    }
    case S_CLASS: {
        Class *base = 0;
        if (n->a) { Value b = eval(n->a); if (HAS_EXC) return 4; if (b.t != T_CLASS) { rt_err("TypeError", "the base of a class must be a class"); return 4; } base = b.u.p; }
        Value cv = class_new(n->s, base);
        Class *c = cv.u.p;
        Env *saved = I.cur, *saved_ce = I.class_env;
        Env *ce = env_new(I.cur);
        env_set(ce, as_intern("__class__", 9), cv);                 /* slot 0 holds the class being built */
        I.cur = ce; I.class_env = ce;
        int st = exec_block(n->b);
        I.cur = saved; I.class_env = saved_ce;
        if (st) return st;
        for (int i = 1; i < ce->n; i++) dict_sets(&c->attrs, ce->k[i], ce->v[i]);
        env_set(I.cur, n->s, cv);
        return 0;
    }
    case S_RETURN: { I.ret = n->a ? eval(n->a) : v_none(); if (HAS_EXC) return 4; return 3; }
    case S_BREAK: return 1;
    case S_CONTINUE: return 2;
    case S_GLOBAL: for (int i = 0; i < n->n; i++) env_add_name(&I.cur->gl, &I.cur->ngl, n->names[i]); return 0;
    case S_NONLOCAL: for (int i = 0; i < n->n; i++) env_add_name(&I.cur->nl, &I.cur->nnl, n->names[i]); return 0;
    case S_ASSERT: {
        Value c = eval(n->a); if (HAS_EXC) return 4;
        if (!v_truthy(c)) {
            Value m = n->b ? eval(n->b) : v_cstr("assertion failed");
            if (HAS_EXC) return 4;
            Value s = v_to_str(m); rt_err("AssertionError", ((Str *)s.u.p)->s); return 4;
        }
        return 0;
    }
    case S_RAISE: {
        if (!n->a) { rt_err("RuntimeError", "push_error needs a message"); return 4; }
        Value v = eval(n->a); if (HAS_EXC) return 4;
        Value e = make_exc_value(v);
        I.exc = e; I.has_exc = 1;
        return 4;
    }
    case S_TRY: {
        int st = exec_block(n->a);
        if (st != 4) return st;
        Value exc = I.exc;
        for (int i = 0; i < n->n; i++) {
            int match = 1;
            I.has_exc = 0;
            if (n->list[i]) {
                Value t = eval(n->list[i]);
                if (HAS_EXC) return 4;
                match = t.t == T_CLASS && exc_matches(exc, t.u.p);
            }
            if (match) {
                if (n->names[i]) env_set(I.cur, n->names[i], exc);
                return exec_block(n->list2[i]);
            }
        }
        I.exc = exc; I.has_exc = 1;
        return 4;
    }
    case S_WITH: {
        Value ctx = eval(n->a); if (HAS_EXC) return 4;
        Value enter = v_none(), exit_ = v_none();
        if (ctx.t == T_INST) {
            Value r = v_none();
            Inst *in = ctx.u.p; (void)in;
            Value fe = get_attr(ctx, as_intern("enter", 5));
            if (HAS_EXC) { I.has_exc = 0; fe = get_attr(ctx, as_intern("__enter__", 9)); if (HAS_EXC) return 4; }
            enter = fe;
            r = call_value(enter, 0, 0, 0, 0, 0); if (HAS_EXC) return 4;
            Value fx = get_attr(ctx, as_intern("exit", 4));
            if (HAS_EXC) { I.has_exc = 0; fx = get_attr(ctx, as_intern("__exit__", 8)); if (HAS_EXC) return 4; }
            exit_ = fx;
            if (n->s) env_set(I.cur, n->s, r.t == T_NONE ? ctx : r);
        } else if (n->s) env_set(I.cur, n->s, ctx);
        int st = exec_block(n->b);
        Value saved = I.exc; int had = I.has_exc; I.has_exc = 0;
        if (exit_.t != T_NONE) { call_value(exit_, 0, 0, 0, 0, 0); if (HAS_EXC) return 4; }
        if (had) { I.exc = saved; I.has_exc = 1; }
        return st;
    }
    case S_YIELD: {
        if (!I.gen) { rt_err("RuntimeError", "'yield' works only inside a function"); return 4; }
        Value v = n->a ? eval(n->a) : v_none(); if (HAS_EXC) return 4;
        if (I.gen->n >= 100000) { rt_err("RuntimeError", "a generator made more than 100000 values; generators run to the end when called, so endless ones are not possible"); return 4; }
        seq_push(I.gen, v); return 0;
    }
    case S_IMPORT: case S_FROM: case S_USE: exec_import(n); return HAS_EXC ? 4 : 0;
    case S_BLOCK: return exec_block(n);
    }
    rt_err("RuntimeError", "unknown statement");
    return 4;
}

int exec_block(Node *b)
{
    for (int i = 0; i < b->n; i++) {
        int st = exec_stmt(b->list[i]);
        if (st) return st;
    }
    return 0;
}

/* functions of the top level are known before the program starts (a call may come before the definition) */
void hoist(Node *blk)
{
    for (int i = 0; i < blk->n; i++) {
        Node *n = blk->list[i];
        if (n->k == S_FUNC) { Value f = make_func(n, I.cur, 0); if (!HAS_EXC) env_set(I.cur, n->s, f); else I.has_exc = 0; }
    }
}
