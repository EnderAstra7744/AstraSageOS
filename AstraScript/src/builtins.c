/* AstraScript engine 2 - builtins.c: the functions that need no import (abs, len, range, printc, ... and the graphics / file / AST helpers). */
#include "as_types.h"

#define ARGS(lo, hi, name) do { if (argc < (lo) || argc > (hi)) { char m_[80]; as_fmt(m_, sizeof m_, "%s() takes %d to %d arguments", name, lo, hi); return rt_err("TypeError", m_); } } while (0)
#define FN(n) static Value n(int argc, Value *argv, int nkw, Str **kn, Value *kv)
#define UNUSED (void)argc; (void)argv; (void)nkw; (void)kn; (void)kv

static int is_int(Value v) { return v.t == T_INT || v.t == T_BOOL; }
double as_pow_d(double x, double y);
int str_letters(Value v);

static Value *kwarg(const char *name, int nkw, Str **kn, Value *kv)
{
    for (int i = 0; i < nkw; i++) if (!as_strcmp(kn[i]->s, name)) return &kv[i];
    return 0;
}

/* ------------------------------------------------------------------ output / input */
FN(b_printc)
{
    (void)kn; (void)kv;
    const char *sep = " ", *end = "\n"; int seplen = 1, endlen = 1;
    Value *s = kwarg("sep", nkw, kn, kv), *e = kwarg("end", nkw, kn, kv);
    if (s && s->t == T_STR) { sep = ((Str *)s->u.p)->s; seplen = ((Str *)s->u.p)->len; }
    if (e && e->t == T_STR) { end = ((Str *)e->u.p)->s; endlen = ((Str *)e->u.p)->len; }
    Buf b; buf_init(&b);
    for (int i = 0; i < argc; i++) { if (i) buf_add(&b, sep, seplen); v_repr(argv[i], &b, 0); }
    buf_add(&b, end, endlen);
    as_h_puts(b.s);
    return v_none();
}

FN(b_input)
{
    UNUSED;
    char line[200];
    Buf p; buf_init(&p);
    if (argc >= 1) v_repr(argv[0], &p, 0);
    if (!as_h_readline(p.s, line, sizeof line)) return v_cstr("");
    return v_cstr(line);
}

/* ------------------------------------------------------------------ conversions */
FN(b_int)
{
    UNUSED;
    if (argc == 0) return v_int(0);
    Value v = argv[0];
    if (v.t == T_INT || v.t == T_BOOL) return v_int(v.u.i);
    if (v.t == T_FLOAT) { if (v.u.d > 2147483647.0 || v.u.d < -2147483648.0) return rt_err("OverflowError", "the number is too big for an int"); return v_int((int)v.u.d); }
    if (v.t == T_STR) {
        Str *s = v.u.p; int base = argc >= 2 && is_int(argv[1]) ? argv[1].u.i : 10, i = 0, neg = 0; double r = 0; int any = 0;
        while (i < s->len && (s->s[i] == ' ' || s->s[i] == '\t' || s->s[i] == '\n')) i++;
        if (i < s->len && (s->s[i] == '-' || s->s[i] == '+')) { neg = s->s[i] == '-'; i++; }
        if (base == 16 && i + 1 < s->len && s->s[i] == '0' && (s->s[i + 1] == 'x' || s->s[i + 1] == 'X')) i += 2;
        for (; i < s->len; i++) {
            char c = s->s[i]; int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'z' ? c - 'a' + 10 : c >= 'A' && c <= 'Z' ? c - 'A' + 10 : -1;
            if (d < 0 || d >= base) break;
            r = r * base + d; any = 1;
        }
        while (i < s->len && (s->s[i] == ' ' || s->s[i] == '\n')) i++;
        if (!any || i != s->len) { Buf b; buf_init(&b); v_repr(v, &b, 1); char m[120]; as_fmt(m, sizeof m, "invalid literal for int(): %s", b.s); return rt_err("ValueError", m); }
        if (r > 2147483647.0) return rt_err("OverflowError", "the number is too big for an int");
        return v_int(neg ? -(int)r : (int)r);
    }
    return rt_errf("TypeError", "int() cannot convert a '%s' value", v_typename(v));
}

FN(b_flo)
{
    UNUSED;
    if (argc == 0) return v_float(0);
    Value v = argv[0];
    if (v.t == T_FLOAT) return v;
    if (v.t == T_INT || v.t == T_BOOL) return v_float((double)v.u.i);
    if (v.t == T_STR) {
        Str *s = v.u.p; Value t = v_none();
        int nt = 0; Token *tk = as_lex(s->s, (unsigned)s->len, &nt, 1);
        int neg = 0, k = 0;
        if (tk && tk[k].k == K_OP && (!as_strcmp(tk[k].u.s->s, "-") || !as_strcmp(tk[k].u.s->s, "+"))) { neg = tk[k].u.s->s[0] == '-'; k++; }
        if (tk && nt == k + 2 && (tk[k].k == K_NUM_I || tk[k].k == K_NUM_F)) { t = v_float(tk[k].k == K_NUM_I ? (double)tk[k].u.i : tk[k].u.d); if (neg) t.u.d = -t.u.d; return t; }
        Buf b; buf_init(&b); v_repr(v, &b, 1); char m[120]; as_fmt(m, sizeof m, "could not convert text to float: %s", b.s); return rt_err("ValueError", m);
    }
    return rt_errf("TypeError", "flo() cannot convert a '%s' value", v_typename(v));
}

FN(b_strt) { UNUSED; if (argc == 0) return v_cstr(""); return v_to_str(argv[0]); }
FN(b_bool) { UNUSED; return v_bool(argc ? v_truthy(argv[0]) : 0); }

FN(b_list) { UNUSED; if (argc == 0) return v_ptr(T_LIST, seq_new(4)); return seq_from_iter(T_LIST, argv[0]); }
FN(b_tuple) { UNUSED; if (argc == 0) return v_ptr(T_TUPLE, seq_new(1)); return seq_from_iter(T_TUPLE, argv[0]); }
FN(b_set) { UNUSED; if (argc == 0) return v_ptr(T_SET, seq_new(4)); return seq_from_iter(T_SET, argv[0]); }
FN(b_frozenset) { UNUSED; if (argc == 0) return v_ptr(T_FSET, seq_new(1)); return seq_from_iter(T_FSET, argv[0]); }
FN(b_dict)
{
    Dict *d = dict_new();
    if (argc >= 1) {
        if (argv[0].t == T_DICT) { Dict *o = argv[0].u.p; for (int i = 0; i < o->n; i++) dict_set(d, o->k[i], o->v[i]); }
        else { Value *items; int n; if (!iter_items(argv[0], &items, &n)) return v_none(); for (int i = 0; i < n; i++) { Value *p; int pn; if (!iter_items(items[i], &p, &pn) || pn != 2) return rt_err("ValueError", "dict() needs pairs (key, value)"); dict_set(d, p[0], p[1]); } }
    }
    for (int i = 0; i < nkw; i++) dict_set(d, v_ptr(T_STR, kn[i]), kv[i]);
    return v_ptr(T_DICT, d);
}
FN(b_bytes)
{
    UNUSED;
    if (argc == 0) return bytes_new(T_BYTES, "", 0);
    if (argv[0].t == T_STR || argv[0].t == T_BYTES || argv[0].t == T_BARRAY) return bytes_new(T_BYTES, ((Str *)argv[0].u.p)->s, ((Str *)argv[0].u.p)->len);
    if (is_int(argv[0])) { Str *s = str_new(0, argv[0].u.i < 0 ? 0 : argv[0].u.i); return v_ptr(T_BYTES, s); }
    Value *items; int n; if (!iter_items(argv[0], &items, &n)) return v_none();
    Str *s = str_new(0, n); for (int i = 0; i < n; i++) { if (!is_int(items[i]) || items[i].u.i < 0 || items[i].u.i > 255) return rt_err("ValueError", "bytes must be in range(0, 256)"); s->s[i] = (char)items[i].u.i; }
    return v_ptr(T_BYTES, s);
}
FN(b_bytearray) { Value r = b_bytes(argc, argv, nkw, kn, kv); if (r.t == T_BYTES) r.t = T_BARRAY; return r; }

FN(b_range)
{
    UNUSED; ARGS(1, 3, "range");
    for (int i = 0; i < argc; i++) if (!is_int(argv[i])) return rt_err("TypeError", "range() needs integers");
    Range *r = as_alloc(sizeof(Range));
    if (argc == 1) { r->a = 0; r->b = argv[0].u.i; r->c = 1; }
    else { r->a = argv[0].u.i; r->b = argv[1].u.i; r->c = argc == 3 ? argv[2].u.i : 1; }
    if (r->c == 0) return rt_err("ValueError", "range() step cannot be zero");
    return v_ptr(T_RANGE, r);
}

/* ------------------------------------------------------------------ numbers */
FN(b_abs) { UNUSED; ARGS(1, 1, "abs"); Value v = argv[0]; if (v.t == T_FLOAT) return v_float(v.u.d < 0 ? -v.u.d : v.u.d); if (is_int(v)) return v_int(v.u.i < 0 ? -v.u.i : v.u.i); return rt_errf("TypeError", "abs() needs a number, not '%s'", v_typename(v)); }
FN(b_divmod) { UNUSED; ARGS(2, 2, "divmod"); Value q = binop('D', argv[0], argv[1]); if (I.has_exc) return v_none(); Value r = binop('%', argv[0], argv[1]); if (I.has_exc) return v_none(); Seq *s = seq_new(2); seq_push(s, q); seq_push(s, r); return v_ptr(T_TUPLE, s); }
static int mulmod(int a, int b, int m)                /* (a * b) % m without 64-bit arithmetic */
{
    int r = 0;
    while (b) { if (b & 1) { r += a; if (r >= m || r < 0) r -= m; } a += a; if (a >= m || a < 0) a -= m; b >>= 1; }
    return r;
}
FN(b_pow)
{
    UNUSED; ARGS(2, 3, "pow");
    if (argc == 3) {
        if (!is_int(argv[0]) || !is_int(argv[1]) || !is_int(argv[2]) || argv[1].u.i < 0 || argv[2].u.i == 0) return rt_err("ValueError", "pow(a, b, m) needs integers, b >= 0 and m != 0");
        int m = argv[2].u.i; if (m < 0) m = -m;
        int base = argv[0].u.i % m; if (base < 0) base += m;
        int e = argv[1].u.i, r = 1 % m;
        while (e) { if (e & 1) r = mulmod(r, base, m); base = mulmod(base, base, m); e >>= 1; }
        return v_int(r);
    }
    return binop('P', argv[0], argv[1]);
}
static Value round_to(double x, int nd)
{
    double m = 1; for (int i = 0; i < nd; i++) m *= 10;
    double y = x * m, f = (double)(int)y; if (f > y) f -= 1;
    double diff = y - f, r;
    if (diff > 0.5) r = f + 1; else if (diff < 0.5) r = f; else r = ((int)f % 2 == 0) ? f : f + 1;
    return v_float(r / m);
}
FN(b_round)
{
    UNUSED; ARGS(1, 2, "round");
    if (is_int(argv[0])) return v_int(argv[0].u.i);
    if (argv[0].t != T_FLOAT) return rt_errf("TypeError", "round() needs a number, not '%s'", v_typename(argv[0]));
    if (argc == 1 || argv[1].t == T_NONE) { if (argv[0].u.d > 2e9 || argv[0].u.d < -2e9) return argv[0]; Value r = round_to(argv[0].u.d, 0); return v_int((int)r.u.d); }
    return round_to(argv[0].u.d, argv[1].u.i);
}
FN(b_sum)
{
    UNUSED; ARGS(1, 2, "sum");
    Value *items; int n; if (!iter_items(argv[0], &items, &n)) return v_none();
    Value acc = argc == 2 ? argv[1] : v_int(0);
    for (int i = 0; i < n; i++) { acc = binop('+', acc, items[i]); if (I.has_exc) return v_none(); }
    return acc;
}
static Value minmax(int argc, Value *argv, int nkw, Str **kn, Value *kv, int want_max)
{
    Value *items; int n;
    if (argc == 1) { if (!iter_items(argv[0], &items, &n)) return v_none(); } else { items = argv; n = argc; }
    Value *key = kwarg("key", nkw, kn, kv), *def = kwarg("default", nkw, kn, kv);
    if (n == 0) { if (def) return *def; return rt_err("ValueError", want_max ? "max() of an empty sequence" : "min() of an empty sequence"); }
    Value best = items[0], bk = key ? call_value(*key, 1, &items[0], 0, 0, 0) : items[0];
    for (int i = 1; i < n; i++) {
        Value k = key ? call_value(*key, 1, &items[i], 0, 0, 0) : items[i]; if (I.has_exc) return v_none();
        int ok; int c = v_cmp(k, bk, &ok);
        if (!ok) return rt_err("TypeError", "the values cannot be compared with each other");
        if (want_max ? c > 0 : c < 0) { best = items[i]; bk = k; }
    }
    return best;
}
FN(b_max) { return minmax(argc, argv, nkw, kn, kv, 1); }
FN(b_min) { return minmax(argc, argv, nkw, kn, kv, 0); }

static void base_str(Buf *b, int v, int base, const char *prefix)
{
    unsigned u = v < 0 ? 0u - (unsigned)v : (unsigned)v; char t[40]; int k = 0;
    do { int d = (int)(u % (unsigned)base); t[k++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); u /= (unsigned)base; } while (u);
    if (v < 0) buf_putc(b, '-');
    buf_addc(b, prefix);
    while (k) buf_putc(b, t[--k]);
}
FN(b_bin) { UNUSED; ARGS(1, 1, "bin"); if (!is_int(argv[0])) return rt_err("TypeError", "bin() needs an integer"); Buf b; buf_init(&b); base_str(&b, argv[0].u.i, 2, "0b"); return v_str(b.s, b.n); }
FN(b_hex) { UNUSED; ARGS(1, 1, "hex"); if (!is_int(argv[0])) return rt_err("TypeError", "hex() needs an integer"); Buf b; buf_init(&b); base_str(&b, argv[0].u.i, 16, "0x"); return v_str(b.s, b.n); }
FN(b_oct) { UNUSED; ARGS(1, 1, "oct"); if (!is_int(argv[0])) return rt_err("TypeError", "oct() needs an integer"); Buf b; buf_init(&b); base_str(&b, argv[0].u.i, 8, "0o"); return v_str(b.s, b.n); }
FN(b_chr)
{
    UNUSED; ARGS(1, 1, "chr");
    if (!is_int(argv[0]) || argv[0].u.i < 0 || argv[0].u.i > 0x10FFFF) return rt_err("ValueError", "chr() argument out of range");
    unsigned c = (unsigned)argv[0].u.i; char o[4]; int n = 0;
    if (c < 0x80) o[n++] = (char)c; else if (c < 0x800) { o[n++] = (char)(0xC0 | (c >> 6)); o[n++] = (char)(0x80 | (c & 63)); }
    else if (c < 0x10000) { o[n++] = (char)(0xE0 | (c >> 12)); o[n++] = (char)(0x80 | ((c >> 6) & 63)); o[n++] = (char)(0x80 | (c & 63)); }
    else { o[n++] = (char)(0xF0 | (c >> 18)); o[n++] = (char)(0x80 | ((c >> 12) & 63)); o[n++] = (char)(0x80 | ((c >> 6) & 63)); o[n++] = (char)(0x80 | (c & 63)); }
    return v_str(o, n);
}
FN(b_ord)
{
    UNUSED; ARGS(1, 1, "ord");
    if (argv[0].t != T_STR || !((Str *)argv[0].u.p)->len) return rt_err("TypeError", "ord() needs a text value with one letter");
    unsigned char *s = (unsigned char *)((Str *)argv[0].u.p)->s; unsigned c = s[0];
    if (c >= 0xF0) c = ((c & 7) << 18) | ((s[1] & 63) << 12) | ((s[2] & 63) << 6) | (s[3] & 63);
    else if (c >= 0xE0) c = ((c & 15) << 12) | ((s[1] & 63) << 6) | (s[2] & 63);
    else if (c >= 0xC0) c = ((c & 31) << 6) | (s[1] & 63);
    return v_int((int)c);
}

/* ------------------------------------------------------------------ sequences */
FN(b_len)
{
    UNUSED; ARGS(1, 1, "len");
    Value v = argv[0];
    switch (v.t) {
    case T_STR: return v_int(str_letters(v));
    case T_BYTES: case T_BARRAY: return v_int(((Str *)v.u.p)->len);
    case T_LIST: case T_TUPLE: case T_SET: case T_FSET: return v_int(((Seq *)v.u.p)->n);
    case T_DICT: return v_int(((Dict *)v.u.p)->n);
    case T_RANGE: { Range *r = v.u.p; int n = 0; if (r->c > 0 && r->b > r->a) n = (r->b - r->a + r->c - 1) / r->c; else if (r->c < 0 && r->a > r->b) n = (r->a - r->b - r->c - 1) / -r->c; return v_int(n); }
    default: return rt_errf("TypeError", "'%s' value has no length", v_typename(v));
    }
}
FN(b_all) { UNUSED; ARGS(1, 1, "all"); Value *it; int n; if (!iter_items(argv[0], &it, &n)) return v_none(); for (int i = 0; i < n; i++) if (!v_truthy(it[i])) return v_bool(0); return v_bool(1); }
FN(b_any) { UNUSED; ARGS(1, 1, "any"); Value *it; int n; if (!iter_items(argv[0], &it, &n)) return v_none(); for (int i = 0; i < n; i++) if (v_truthy(it[i])) return v_bool(1); return v_bool(0); }
FN(b_enumerate)
{
    UNUSED; ARGS(1, 2, "enumerate");
    Value *it; int n; if (!iter_items(argv[0], &it, &n)) return v_none();
    int start = argc == 2 && is_int(argv[1]) ? argv[1].u.i : 0;
    Seq *q = seq_new(n);
    for (int i = 0; i < n; i++) { Seq *p = seq_new(2); seq_push(p, v_int(start + i)); seq_push(p, it[i]); seq_push(q, v_ptr(T_TUPLE, p)); }
    return v_ptr(T_LIST, q);
}
FN(b_zip)
{
    UNUSED;
    Value *its[8]; int ns[8], min = 0x7FFFFFFF;
    if (argc > 8) return rt_err("TypeError", "zip() takes at most 8 sequences");
    for (int i = 0; i < argc; i++) { if (!iter_items(argv[i], &its[i], &ns[i])) return v_none(); if (ns[i] < min) min = ns[i]; }
    if (!argc) min = 0;
    Seq *q = seq_new(min);
    for (int k = 0; k < min; k++) { Seq *p = seq_new(argc); for (int i = 0; i < argc; i++) seq_push(p, its[i][k]); seq_push(q, v_ptr(T_TUPLE, p)); }
    return v_ptr(T_LIST, q);
}
FN(b_map)
{
    UNUSED; ARGS(2, 2, "map");
    Value *it; int n; if (!iter_items(argv[1], &it, &n)) return v_none();
    Seq *q = seq_new(n);
    for (int i = 0; i < n; i++) { Value r = call_value(argv[0], 1, &it[i], 0, 0, 0); if (I.has_exc) return v_none(); seq_push(q, r); }
    return v_ptr(T_LIST, q);
}
FN(b_filter)
{
    UNUSED; ARGS(2, 2, "filter");
    Value *it; int n; if (!iter_items(argv[1], &it, &n)) return v_none();
    Seq *q = seq_new(n);
    for (int i = 0; i < n; i++) { Value r = argv[0].t == T_NONE ? it[i] : call_value(argv[0], 1, &it[i], 0, 0, 0); if (I.has_exc) return v_none(); if (v_truthy(r)) seq_push(q, it[i]); }
    return v_ptr(T_LIST, q);
}
FN(b_sorted)
{
    UNUSED; ARGS(1, 1, "sorted");
    Value l = seq_from_iter(T_LIST, argv[0]); if (I.has_exc) return v_none();
    Value *key = kwarg("key", nkw, kn, kv), *rev = kwarg("reverse", nkw, kn, kv);
    seq_sort(l.u.p, key ? *key : v_none(), rev && v_truthy(*rev));
    return I.has_exc ? v_none() : l;
}
FN(b_reversed)
{
    UNUSED; ARGS(1, 1, "reversed");
    Value l = seq_from_iter(T_LIST, argv[0]); if (I.has_exc) return v_none();
    Seq *q = l.u.p; for (int i = 0, j = q->n - 1; i < j; i++, j--) { Value t = q->a[i]; q->a[i] = q->a[j]; q->a[j] = t; }
    return l;
}
FN(b_iter)
{
    UNUSED; ARGS(1, 1, "iter");
    if (argv[0].t == T_GEN) return argv[0];
    Value l = seq_from_iter(T_LIST, argv[0]); if (I.has_exc) return v_none();
    Gen *g = as_alloc(sizeof(Gen)); g->items = l.u.p; g->pos = 0;
    return v_ptr(T_GEN, g);
}
FN(b_next)
{
    UNUSED; ARGS(1, 2, "next");
    if (argv[0].t != T_GEN) return rt_err("TypeError", "next() needs a generator or a value made by iter()");
    Gen *g = argv[0].u.p;
    if (g->pos >= g->items->n) { if (argc == 2) return argv[1]; return rt_err("StopIteration", "no more items"); }
    return g->items->a[g->pos++];
}

/* ------------------------------------------------------------------ text helpers */
FN(b_repr) { UNUSED; ARGS(1, 1, "repr"); Buf b; buf_init(&b); v_repr(argv[0], &b, 1); return v_str(b.s, b.n); }
FN(b_ascii) { UNUSED; ARGS(1, 1, "ascii"); Buf b; buf_init(&b); v_repr(argv[0], &b, 1); Buf o; buf_init(&o); for (int i = 0; i < b.n; i++) { unsigned char c = (unsigned char)b.s[i]; if (c < 128) buf_putc(&o, (char)c); else { const char *h = "0123456789abcdef"; buf_addc(&o, "\\x"); buf_putc(&o, h[c >> 4]); buf_putc(&o, h[c & 15]); } } return v_str(o.s, o.n); }
FN(b_format)
{
    UNUSED; ARGS(1, 2, "format");
    if (argc == 1 || argv[1].t != T_STR || !((Str *)argv[1].u.p)->len) return v_to_str(argv[0]);
    const char *sp = ((Str *)argv[1].u.p)->s; int i = 0, zero = 0, width = 0, prec = -1; char type = 0;
    if (sp[i] == '0') { zero = 1; i++; }
    while (sp[i] >= '0' && sp[i] <= '9') width = width * 10 + (sp[i++] - '0');
    if (sp[i] == '.') { i++; prec = 0; while (sp[i] >= '0' && sp[i] <= '9') prec = prec * 10 + (sp[i++] - '0'); }
    if (sp[i]) type = sp[i];
    Buf b; buf_init(&b);
    if (type == 'f' && v_isnum(argv[0])) {
        Value r = round_to(v_num(argv[0]), prec < 0 ? 6 : prec);
        Buf t; buf_init(&t); fmt_double(r.u.d, &t);
        int dot = -1; for (int k = 0; k < t.n; k++) if (t.s[k] == '.') dot = k;
        int want = prec < 0 ? 6 : prec;
        if (dot < 0) { buf_putc(&t, '.'); dot = t.n - 1; }
        while (t.n - dot - 1 < want) buf_putc(&t, '0');
        if (want == 0) t.n = dot;
        buf_add(&b, t.s, t.n);
    } else if (type == 'x' && is_int(argv[0])) base_str(&b, argv[0].u.i, 16, "");
    else if (type == 'b' && is_int(argv[0])) base_str(&b, argv[0].u.i, 2, "");
    else v_repr(argv[0], &b, 0);
    Buf o; buf_init(&o);
    for (int k = b.n; k < width; k++) buf_putc(&o, zero ? '0' : ' ');
    buf_add(&o, b.s, b.n);
    return v_str(o.s, o.n);
}
FN(b_hash) { UNUSED; ARGS(1, 1, "hash"); Buf b; buf_init(&b); v_repr(argv[0], &b, 1); unsigned h = 5381; for (int i = 0; i < b.n; i++) h = h * 33 + (unsigned char)b.s[i]; return v_int((int)(h & 0x7FFFFFFF)); }
FN(b_id) { UNUSED; ARGS(1, 1, "id"); return v_int((int)(unsigned long)(argv[0].t >= T_STR ? argv[0].u.p : (void *)(long)argv[0].u.i) & 0x7FFFFFFF); }

/* ------------------------------------------------------------------ objects and types */
static Builtin t_int, t_flo, t_strt, t_bool, t_list, t_tuple, t_set, t_fset, t_dict, t_bytes, t_barray, t_range;
static Builtin *type_objs[] = { &t_int, &t_flo, &t_strt, &t_bool, &t_list, &t_tuple, &t_set, &t_fset, &t_dict, &t_bytes, &t_barray, &t_range };
static int type_code[] = { T_INT, T_FLOAT, T_STR, T_BOOL, T_LIST, T_TUPLE, T_SET, T_FSET, T_DICT, T_BYTES, T_BARRAY, T_RANGE };

Value builtin_type_of(Value v)
{
    if (v.t == T_INST) return v_ptr(T_CLASS, ((Inst *)v.u.p)->cls);
    for (unsigned i = 0; i < sizeof type_code / sizeof type_code[0]; i++) if (type_code[i] == v.t) return v_ptr(T_BUILTIN, type_objs[i]);
    return v_cstr(v_typename(v));
}
FN(b_type) { UNUSED; ARGS(1, 1, "type"); return builtin_type_of(argv[0]); }

static int is_instance(Value v, Value t)
{
    if (t.t == T_CLASS) return v.t == T_INST && class_isa(((Inst *)v.u.p)->cls, t.u.p);
    if (t.t == T_BUILTIN) { for (unsigned i = 0; i < sizeof type_code / sizeof type_code[0]; i++) if (type_objs[i] == t.u.p) return v.t == type_code[i] || (type_code[i] == T_INT && v.t == T_BOOL && 0); }
    if (t.t == T_TUPLE) { Seq *q = t.u.p; for (int i = 0; i < q->n; i++) if (is_instance(v, q->a[i])) return 1; }
    return 0;
}
FN(b_isinstance) { UNUSED; ARGS(2, 2, "isinstance"); return v_bool(is_instance(argv[0], argv[1])); }
FN(b_issubclass) { UNUSED; ARGS(2, 2, "issubclass"); if (argv[0].t != T_CLASS || argv[1].t != T_CLASS) return rt_err("TypeError", "issubclass() needs two classes"); return v_bool(class_isa(argv[0].u.p, argv[1].u.p)); }
FN(b_callable) { UNUSED; ARGS(1, 1, "callable"); int t = argv[0].t; return v_bool(t == T_FUNC || t == T_BUILTIN || t == T_CLASS || t == T_BOUND || t == T_BMETH); }
FN(b_hasattr) { UNUSED; ARGS(2, 2, "hasattr"); if (argv[1].t != T_STR) return rt_err("TypeError", "the attribute name must be text"); Str *n = as_intern(((Str *)argv[1].u.p)->s, ((Str *)argv[1].u.p)->len); (void)get_attr(argv[0], n); int bad = I.has_exc; I.has_exc = 0; return v_bool(!bad); }
FN(b_getattr)
{
    UNUSED; ARGS(2, 3, "getattr");
    if (argv[1].t != T_STR) return rt_err("TypeError", "the attribute name must be text");
    Str *n = as_intern(((Str *)argv[1].u.p)->s, ((Str *)argv[1].u.p)->len);
    Value r = get_attr(argv[0], n);
    if (I.has_exc && argc == 3) { I.has_exc = 0; return argv[2]; }
    return r;
}
FN(b_setattr) { UNUSED; ARGS(3, 3, "setattr"); if (argv[1].t != T_STR) return rt_err("TypeError", "the attribute name must be text"); set_attr(argv[0], as_intern(((Str *)argv[1].u.p)->s, ((Str *)argv[1].u.p)->len), argv[2]); return v_none(); }
FN(b_delattr)
{
    UNUSED; ARGS(2, 2, "delattr");
    if (argv[0].t != T_INST || argv[1].t != T_STR) return rt_err("TypeError", "delattr(object, name)");
    if (!dict_del(&((Inst *)argv[0].u.p)->attrs, v_ptr(T_STR, as_intern(((Str *)argv[1].u.p)->s, ((Str *)argv[1].u.p)->len)))) return rt_err("AttributeError", "no such attribute");
    return v_none();
}
FN(b_vars) { UNUSED; ARGS(1, 1, "vars"); if (argv[0].t != T_INST) return rt_err("TypeError", "vars() needs an object"); Dict *d = dict_new(), *s = &((Inst *)argv[0].u.p)->attrs; for (int i = 0; i < s->n; i++) dict_set(d, s->k[i], s->v[i]); return v_ptr(T_DICT, d); }
FN(b_dir)
{
    UNUSED; Seq *q = seq_new(8);
    if (argc == 0) { for (int i = 0; i < I.cur->n; i++) seq_push(q, v_ptr(T_STR, I.cur->k[i])); }
    else if (argv[0].t == T_INST) { Dict *d = &((Inst *)argv[0].u.p)->attrs; for (int i = 0; i < d->n; i++) seq_push(q, d->k[i]); for (Class *c = ((Inst *)argv[0].u.p)->cls; c; c = c->base) for (int i = 0; i < c->attrs.n; i++) seq_push(q, c->attrs.k[i]); }
    else if (argv[0].t == T_CLASS) { for (Class *c = argv[0].u.p; c; c = c->base) for (int i = 0; i < c->attrs.n; i++) seq_push(q, c->attrs.k[i]); }
    else if (argv[0].t == T_MODULE) { Env *e = ((Module *)argv[0].u.p)->env; for (int i = 0; i < e->n; i++) seq_push(q, v_ptr(T_STR, e->k[i])); }
    Value l = v_ptr(T_LIST, q); seq_sort(q, v_none(), 0); I.has_exc = 0; return l;
}
static Value env_dict(Env *e) { Dict *d = dict_new(); for (int i = 0; i < e->n; i++) dict_set(d, v_ptr(T_STR, e->k[i]), e->v[i]); return v_ptr(T_DICT, d); }
FN(b_globals) { UNUSED; return env_dict(I.globals); }
FN(b_locals) { UNUSED; return env_dict(I.cur); }
static Value obj_base;
FN(b_object) { UNUSED; if (obj_base.t == T_NONE) obj_base = class_new(as_intern("object", 6), 0); return instantiate(obj_base.u.p, 0, 0, 0, 0, 0); }
FN(b_super)
{
    UNUSED; ARGS(0, 0, "super");
    Frame *f = I.depth > 0 ? &I.frames[I.depth - 1] : 0;
    if (!f || !f->fn || !f->fn->cls || !f->fn->cls->base) return rt_err("RuntimeError", "super() works only inside a method of a class that has a base class");
    Super *s = as_alloc(sizeof(Super)); s->self = f->self; s->from = f->fn->cls->base;
    return v_ptr(T_SUPER, s);
}

/* ------------------------------------------------------------------ eval / exec / help / unsupported */
int as_run_block_text(const char *src, int len, Env *env, const char *label);
FN(b_eval)
{
    UNUSED; ARGS(1, 1, "eval");
    if (argv[0].t != T_STR) return rt_err("TypeError", "eval() needs text");
    Str *s = argv[0].u.p;
    Node *e = as_parse_expr_text(s->s, s->len, I.line);
    if (!e) return rt_err("Error", as_parse_error);
    return eval(e);
}
FN(b_exec)
{
    UNUSED; ARGS(1, 1, "exec");
    if (argv[0].t != T_STR) return rt_err("TypeError", "exec() needs text");
    Str *s = argv[0].u.p;
    int r = as_run_block_text(s->s, s->len, I.cur, "exec");
    if (r < 0) return rt_err("Error", as_parse_error);
    return v_none();
}
FN(b_help)
{
    UNUSED;
    as_h_puts("AstraScript: printc(...), len(x), range(n), input(), type(x), isinstance(x, T) and many more built-ins;\n"
              "methods: s.bigger() s.smalest() s.split() ... ; blocks use { ... end; }; see the README for the full language.\n");
    return v_none();
}
static Value make_desc(int kind, Value f, Value setter)
{
    if (f.t != T_FUNC && f.t != T_BUILTIN) return rt_err("TypeError", "this needs a function");
    Desc *d = as_alloc(sizeof(Desc)); d->kind = kind; d->f = f; d->setter = setter;
    return v_ptr(T_DESC, d);
}
FN(b_classmethod) { UNUSED; ARGS(1, 1, "classmethod"); return make_desc(1, argv[0], v_none()); }
FN(b_staticmethod) { UNUSED; ARGS(1, 1, "staticmethod"); return make_desc(2, argv[0], v_none()); }
FN(b_property) { UNUSED; ARGS(1, 2, "property"); Value *fs = kwarg("setter", nkw, kn, kv); return make_desc(3, argv[0], argc == 2 ? argv[1] : fs ? *fs : v_none()); }
Value file_open(int argc, Value *argv, int nkw, Str **kn, Value *kv);
FN(b_unsupported)
{
    UNUSED;
    return rt_err("RuntimeError", "this built-in (complex, compile, memoryview, breakpoint, slice) is not supported yet");
}

/* ------------------------------------------------------------------ graphics: Init(...) and the gui_ family */
static int gui_color(Value v, unsigned *out)
{
    if (is_int(v)) { *out = (unsigned)v.u.i & 0xFFFFFF; return 1; }
    if (v.t == T_STR) {
        Str *s = v.u.p; const char *p = s->s; if (*p == '#') p++;
        unsigned c = 0; int n = 0;
        for (; *p && n < 6; p++, n++) { char h = *p; int d = h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : -1; if (d < 0) return 0; c = c * 16 + (unsigned)d; }
        if (n == 6 && !*p) { *out = c; return 1; }
    }
    rt_err("ValueError", "a colour is written like \"#RRGGBB\" or as a number");
    return 0;
}
static int need_int(Value v, int *o) { if (!is_int(v)) { rt_err("TypeError", "a whole number was expected"); return 0; } *o = v.u.i; return 1; }
static Value open_window(const char *name, int w, int h)
{
    if (!as_h_gui_open(name, w, h)) return rt_err("RuntimeError", "there is no graphical desktop: windows need the AstraSageOS desktop");
    return v_none();
}

FN(b_Init)
{
    const char *name = "AstraScript"; int w = 320, h = 200;
    Value *nm = kwarg("Name", nkw, kn, kv), *sz = kwarg("Size", nkw, kn, kv);
    if (argc >= 1 && argv[0].t == T_STR) name = ((Str *)argv[0].u.p)->s;
    if (nm && nm->t == T_STR) name = ((Str *)nm->u.p)->s;
    if (argc >= 3 && is_int(argv[1]) && is_int(argv[2])) { w = argv[1].u.i; h = argv[2].u.i; }
    if (sz) {
        if (sz->t == T_TUPLE || sz->t == T_LIST) { Seq *q = sz->u.p; if (q->n == 2 && is_int(q->a[0]) && is_int(q->a[1])) { w = q->a[0].u.i; h = q->a[1].u.i; } else return rt_err("ValueError", "Size is (width, height)"); }
        else return rt_err("TypeError", "Size is (width, height)");
    }
    Value *ic = kwarg("Icon", nkw, kn, kv);
    if (ic && ic->t != T_STR && ic->t != T_NONE) return rt_err("TypeError", "Icon is the name of a BMP file");
    Value r = open_window(name, w, h); if (I.has_exc) return r;
    if (ic && ic->t == T_STR && !as_h_gui_icon(((Str *)ic->u.p)->s)) return rt_errf("Error", "the icon '%s' cannot be used: it must be a valid BMP file of at most 4096 bytes", ((Str *)ic->u.p)->s);
    return r;
}
FN(b_gui_open) { UNUSED; ARGS(3, 3, "gui_open"); int w, h; if (argv[0].t != T_STR || !need_int(argv[1], &w) || !need_int(argv[2], &h)) return I.has_exc ? v_none() : rt_err("TypeError", "gui_open(title, w, h)"); return open_window(((Str *)argv[0].u.p)->s, w, h); }
FN(b_gui_close) { UNUSED; as_h_gui_close(); return v_none(); }
FN(b_gui_clear) { UNUSED; ARGS(1, 1, "gui_clear"); unsigned c; if (!gui_color(argv[0], &c)) return v_none(); as_h_gui_clear(c); return v_none(); }
FN(b_gui_rect) { UNUSED; ARGS(5, 5, "gui_rect"); int x, y, w, h; unsigned c; if (!need_int(argv[0], &x) || !need_int(argv[1], &y) || !need_int(argv[2], &w) || !need_int(argv[3], &h) || !gui_color(argv[4], &c)) return v_none(); as_h_gui_rect(x, y, w, h, c); return v_none(); }
FN(b_gui_pixel) { UNUSED; ARGS(3, 3, "gui_pixel"); int x, y; unsigned c; if (!need_int(argv[0], &x) || !need_int(argv[1], &y) || !gui_color(argv[2], &c)) return v_none(); as_h_gui_pixel(x, y, c); return v_none(); }
FN(b_gui_text)
{
    UNUSED; ARGS(4, 5, "gui_text");
    int x, y, sc = 1; unsigned c;
    if (!need_int(argv[0], &x) || !need_int(argv[1], &y) || !gui_color(argv[3], &c)) return v_none();
    if (argc == 5 && !need_int(argv[4], &sc)) return v_none();
    Value s = v_to_str(argv[2]); as_h_gui_text(x, y, ((Str *)s.u.p)->s, c, sc); return v_none();
}
FN(b_gui_pal) { UNUSED; ARGS(2, 2, "gui_pal"); int i; unsigned c; if (!need_int(argv[0], &i) || !gui_color(argv[1], &c)) return v_none(); as_h_gui_pal(i, c); return v_none(); }
FN(b_gui_sprite)
{
    UNUSED; ARGS(4, 11, "gui_sprite");
    int x, y, sc; if (!need_int(argv[0], &x) || !need_int(argv[1], &y) || !need_int(argv[2], &sc)) return v_none();
    int ys = sc < 0 ? -sc : sc;
    for (int i = 3; i < argc; i++) { if (argv[i].t != T_STR) return rt_err("TypeError", "sprite rows are text values"); as_h_gui_sprite_row(x, y + (i - 3) * ys, sc, ((Str *)argv[i].u.p)->s); }
    return v_none();
}
FN(b_gui_show) { UNUSED; as_h_gui_show(); return v_none(); }
FN(b_gui_wait) { UNUSED; ARGS(1, 1, "gui_wait"); int ms; if (!need_int(argv[0], &ms)) return v_none(); return v_int(as_h_gui_wait(ms)); }
FN(b_gui_mouse) { UNUSED; int x = 0, y = 0; as_h_gui_mouse(&x, &y); Seq *q = seq_new(2); seq_push(q, v_int(x)); seq_push(q, v_int(y)); return v_ptr(T_TUPLE, q); }

/* ------------------------------------------------------------------ files and AST commands */
FN(b_file_read) { UNUSED; ARGS(1, 1, "file_read"); if (argv[0].t != T_STR) return rt_err("TypeError", "file_read(name)"); static char buf[4200]; int n = as_h_file_read(((Str *)argv[0].u.p)->s, buf, sizeof buf - 1); if (n < 0) return rt_errf("Error", "cannot read the file '%s'", ((Str *)argv[0].u.p)->s); return v_str(buf, n); }
FN(b_file_put) { UNUSED; ARGS(2, 2, "file_put"); if (argv[0].t != T_STR) return rt_err("TypeError", "file_put(name, text)"); Value t = v_to_str(argv[1]); if (as_h_file_put(((Str *)argv[0].u.p)->s, ((Str *)t.u.p)->s, 0) < 0) return rt_errf("Error", "cannot write the file '%s'", ((Str *)argv[0].u.p)->s); return v_none(); }
FN(b_file_add) { UNUSED; ARGS(2, 2, "file_add"); if (argv[0].t != T_STR) return rt_err("TypeError", "file_add(name, text)"); Value t = v_to_str(argv[1]); if (as_h_file_put(((Str *)argv[0].u.p)->s, ((Str *)t.u.p)->s, 1) < 0) return rt_errf("Error", "cannot write the file '%s'", ((Str *)argv[0].u.p)->s); return v_none(); }
FN(b_file_has) { UNUSED; ARGS(1, 1, "file_has"); if (argv[0].t != T_STR) return rt_err("TypeError", "file_has(name)"); static char buf[8]; return v_int(as_h_file_read(((Str *)argv[0].u.p)->s, buf, 1) >= 0 ? 1 : 0); }
FN(b_ast_do) { UNUSED; ARGS(1, 1, "ast_do"); if (argv[0].t != T_STR) return rt_err("TypeError", "ast_do(command)"); static char out[3000]; if (as_h_exec(((Str *)argv[0].u.p)->s, out, sizeof out) < 0) return v_cstr("(this command cannot be used from a program)"); return v_cstr(out); }

FN(b_as_cmd)
{
    UNUSED; ARGS(1, 1, "as");
    static char out[3000];
    if (as_h_exec(((Str *)argv[0].u.p)->s, out, sizeof out) < 0) return rt_err("RuntimeError", "this command cannot be used from a program");
    as_h_puts(out);
    return v_none();
}


/* ------------------------------------------------------------------ legacy text helpers (str_*): "str_cat(dest, a, b)" writes dest; the value is also returned */
static int lc(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static Str *txt(Value v) { return (Str *)v_to_str(v).u.p; }
FN(b_str_set) { UNUSED; ARGS(1, 1, "str_set"); return v_str(txt(argv[0])->s, txt(argv[0])->len); }
FN(b_str_cat) { UNUSED; ARGS(2, 2, "str_cat"); Buf b; buf_init(&b); Str *a = txt(argv[0]), *c = txt(argv[1]); buf_add(&b, a->s, a->len); buf_add(&b, c->s, c->len); return v_str(b.s, b.n); }
FN(b_str_add) { UNUSED; ARGS(2, 2, "str_add"); int v; if (!need_int(argv[1], &v)) return v_none(); Str *d = txt(argv[0]); Buf b; buf_init(&b); buf_add(&b, d->s, d->len); if (v >= 32 && v < 127) { char ch = (char)v; buf_add(&b, &ch, 1); } return v_str(b.s, b.n); }
FN(b_str_del) { UNUSED; ARGS(1, 1, "str_del"); Str *d = txt(argv[0]); return v_str(d->s, d->len ? d->len - 1 : 0); }
FN(b_str_low) { UNUSED; ARGS(1, 1, "str_low"); Str *d = txt(argv[0]); Buf b; buf_init(&b); for (int i = 0; i < d->len; i++) { char ch = (char)lc((unsigned char)d->s[i]); buf_add(&b, &ch, 1); } return v_str(b.s, b.n); }
static Value word_fn(int argc, Value *argv, int rest)
{
    if (argc != 2) return rt_err("TypeError", "wrong number of arguments");
    int n; if (!need_int(argv[1], &n)) return v_none();
    Str *t = txt(argv[0]); int i = 0;
    for (int w = 1; w < (n < 1 ? 1 : n); w++) { while (i < t->len && t->s[i] != ' ') i++; while (i < t->len && t->s[i] == ' ') i++; }
    while (i < t->len && t->s[i] == ' ') i++;
    int e = i; if (!rest) while (e < t->len && t->s[e] != ' ') e++; else e = t->len;
    return v_str(t->s + i, e - i);
}
FN(b_str_word) { UNUSED; return word_fn(argc, argv, 0); }
FN(b_str_rest) { UNUSED; return word_fn(argc, argv, 1); }
FN(b_str_sub) { UNUSED; ARGS(3, 3, "str_sub"); int st, cnt; if (!need_int(argv[1], &st) || !need_int(argv[2], &cnt)) return v_none(); Str *t = txt(argv[0]); int l = t->len; if (st < 1) st = 1; if (st > l + 1) st = l + 1; if (cnt < 0) cnt = 0; if (cnt > l - st + 1) cnt = l - st + 1; return v_str(t->s + st - 1, cnt); }
FN(b_str_line) { UNUSED; ARGS(2, 2, "str_line"); int n; if (!need_int(argv[1], &n)) return v_none(); Str *t = txt(argv[0]); int i = 0; for (int k = 1; k < n && i < t->len; k++) { while (i < t->len && t->s[i] != '\n') i++; if (i < t->len) i++; } int e = i; while (e < t->len && t->s[e] != '\n') e++; return v_str(t->s + i, e - i); }
FN(b_str_len) { UNUSED; ARGS(1, 1, "str_len"); return v_int(txt(argv[0])->len); }
FN(b_str_num) { UNUSED; ARGS(1, 1, "str_num"); Str *t = txt(argv[0]); int i = 0, neg = 0, r = 0; while (i < t->len && t->s[i] == ' ') i++; if (i < t->len && (t->s[i] == '-' || t->s[i] == '+')) { neg = t->s[i] == '-'; i++; } for (; i < t->len && t->s[i] >= '0' && t->s[i] <= '9'; i++) r = r * 10 + (t->s[i] - '0'); return v_int(neg ? -r : r); }
static Value has_is(int argc, Value *argv, int whole)
{
    if (argc != 2) return rt_err("TypeError", "wrong number of arguments");
    Str *t = txt(argv[0]), *u = txt(argv[1]); int p = 0;
    for (;;) {
        int e = p; while (e < u->len && u->s[e] != '|') e++;
        int pl = e - p;
        if (whole) { if (pl == t->len) { int k = 0; while (k < pl && lc((unsigned char)t->s[k]) == lc((unsigned char)u->s[p + k])) k++; if (k == pl) return v_int(1); } }
        else if (pl > 0) for (int i = 0; i + pl <= t->len; i++) { int k = 0; while (k < pl && lc((unsigned char)t->s[i + k]) == lc((unsigned char)u->s[p + k])) k++; if (k == pl) return v_int(1); }
        if (e >= u->len) break;
        p = e + 1;
    }
    return v_int(0);
}
FN(b_str_has) { UNUSED; return has_is(argc, argv, 0); }
FN(b_str_is) { UNUSED; return has_is(argc, argv, 1); }

int as_builtin_outs(const char *name, int nargs);

/* ------------------------------------------------------------------ the table */
typedef struct { const char *name; BuiltinFn fn; int outs; int nin; int inout; } BEnt;
static const BEnt TABLE[] = {
    { "printc", b_printc, 0 }, { "print", b_printc, 0 }, { "input", b_input, 0 }, { "inputc", b_input, 1 },
    { "abs", b_abs, 0 }, { "all", b_all, 0 }, { "any", b_any, 0 }, { "ascii", b_ascii, 0 }, { "bin", b_bin, 0 }, { "breakpoint", b_unsupported, 0 },
    { "callable", b_callable, 0 }, { "chr", b_chr, 0 }, { "classmethod", b_classmethod, 0 }, { "compile", b_unsupported, 0 }, { "complex", b_unsupported, 0 },
    { "delattr", b_delattr, 0 }, { "dir", b_dir, 0 }, { "divmod", b_divmod, 0 }, { "enumerate", b_enumerate, 0 }, { "eval", b_eval, 0 }, { "exec", b_exec, 0 },
    { "filter", b_filter, 0 }, { "format", b_format, 0 }, { "getattr", b_getattr, 0 }, { "globals", b_globals, 0 }, { "hasattr", b_hasattr, 0 }, { "hash", b_hash, 0 },
    { "help", b_help, 0 }, { "hex", b_hex, 0 }, { "id", b_id, 0 }, { "isinstance", b_isinstance, 0 }, { "issubclass", b_issubclass, 0 }, { "iter", b_iter, 0 },
    { "len", b_len, 0 }, { "length", b_len, 0 }, { "lenght", b_len, 0 }, { "locals", b_locals, 0 }, { "map", b_map, 0 }, { "max", b_max, 0 }, { "memoryview", b_unsupported, 0 },
    { "min", b_min, 0 }, { "next", b_next, 0 }, { "object", b_object, 0 }, { "oct", b_oct, 0 }, { "open", file_open, 0 }, { "ord", b_ord, 0 }, { "pow", b_pow, 0 },
    { "property", b_property, 0 }, { "repr", b_repr, 0 }, { "reversed", b_reversed, 0 }, { "round", b_round, 0 }, { "setattr", b_setattr, 0 }, { "slice", b_unsupported, 0 },
    { "sorted", b_sorted, 0 }, { "staticmethod", b_staticmethod, 0 }, { "sum", b_sum, 0 }, { "super", b_super, 0 }, { "type", b_type, 0 }, { "vars", b_vars, 0 }, { "zip", b_zip, 0 },
    { "Init", b_Init, 0 }, { "\xC4\xB0nit", b_Init, 0 }, { "gui_open", b_gui_open, 0 }, { "gui_close", b_gui_close, 0 }, { "gui_clear", b_gui_clear, 0 }, { "gui_rect", b_gui_rect, 0 },
    { "gui_pixel", b_gui_pixel, 0 }, { "gui_text", b_gui_text, 0 }, { "gui_pal", b_gui_pal, 0 }, { "gui_sprite", b_gui_sprite, 0 }, { "gui_show", b_gui_show, 0 },
    { "gui_wait", b_gui_wait, 1 }, { "gui_mouse", b_gui_mouse, 2 },
    { "__as_cmd", b_as_cmd, 0 }, { "file_read", b_file_read, 1, 1 }, { "file_put", b_file_put, 0 }, { "file_add", b_file_add, 0 }, { "file_has", b_file_has, 1, 1 }, { "ast_do", b_ast_do, 1, 1 },
    { "str_set", b_str_set, 1 }, { "str_cat", b_str_cat, 1 }, { "str_add", b_str_add, 1, 0, 1 }, { "str_del", b_str_del, 1, 0, 1 }, { "str_low", b_str_low, 1 },
    { "str_word", b_str_word, 1 }, { "str_rest", b_str_rest, 1 }, { "str_sub", b_str_sub, 1 }, { "str_line", b_str_line, 1 }, { "str_len", b_str_len, 1 },
    { "str_num", b_str_num, 1 }, { "str_has", b_str_has, 1 }, { "str_is", b_str_is, 1 },
    { 0, 0, 0, 0, 0 }
};

int as_builtin_outs(const char *name, int nargs)
{
    for (int i = 0; TABLE[i].name; i++)
        if (TABLE[i].outs && !as_strcmp(TABLE[i].name, name)) return nargs >= TABLE[i].outs && (TABLE[i].nin == 0 || nargs == TABLE[i].outs + TABLE[i].nin) ? TABLE[i].outs : 0;
    return 0;
}

static void reg(Builtin *b, const char *name, BuiltinFn fn, int outs, int nin, int inout)
{
    b->name = name; b->fn = fn; b->outs = outs; b->nin = nin; b->inout = inout;
    env_set(I.builtins, as_intern(name, (int)as_strlen(name)), v_ptr(T_BUILTIN, b));
}

void fileobj_reset(void);
void builtins_init(void)
{
    obj_base = v_none(); fileobj_reset();
    for (int i = 0; TABLE[i].name; i++) { Builtin *b = as_alloc(sizeof(Builtin)); reg(b, TABLE[i].name, TABLE[i].fn, TABLE[i].outs, TABLE[i].nin, TABLE[i].inout); }
    reg(&t_int, "int", b_int, 0, 0, 0); reg(&t_flo, "flo", b_flo, 0, 0, 0); env_set(I.builtins, as_intern("float", 5), v_ptr(T_BUILTIN, &t_flo));
    reg(&t_strt, "strt", b_strt, 0, 0, 0); env_set(I.builtins, as_intern("str", 3), v_ptr(T_BUILTIN, &t_strt));
    reg(&t_bool, "bool", b_bool, 0, 0, 0); reg(&t_list, "list", b_list, 0, 0, 0); reg(&t_tuple, "tuple", b_tuple, 0, 0, 0); reg(&t_set, "set", b_set, 0, 0, 0);
    reg(&t_fset, "frozenset", b_frozenset, 0, 0, 0); reg(&t_dict, "dict", b_dict, 0, 0, 0); reg(&t_bytes, "bytes", b_bytes, 0, 0, 0); reg(&t_barray, "bytearray", b_bytearray, 0, 0, 0);
    reg(&t_range, "range", b_range, 0, 0, 0);
    env_set(I.builtins, as_intern("array", 5), v_ptr(T_BUILTIN, &t_list));
}
