/* AstraScript engine 2 - collections.c: list, tuple, set, frozenset and dict behaviour. */
#include "as_types.h"

static int is_int(Value v) { return v.t == T_INT || v.t == T_BOOL; }

int iter_items(Value v, Value **out, int *n)
{
    switch (v.t) {
    case T_LIST: case T_TUPLE: case T_SET: case T_FSET: { Seq *q = v.u.p; *out = q->a; *n = q->n; return 1; }
    case T_GEN: { Gen *g = v.u.p; *out = g->items->a + g->pos; *n = g->items->n - g->pos; g->pos = g->items->n; return 1; }
    case T_DICT: { Dict *d = v.u.p; *out = d->k; *n = d->n; return 1; }
    case T_STR: {
        Str *s = v.u.p; Seq *q = seq_new(s->len);
        for (int i = 0; i < s->len;) { int j = i + 1; while (j < s->len && ((unsigned char)s->s[j] & 0xC0) == 0x80) j++; seq_push(q, v_str(s->s + i, j - i)); i = j; }
        *out = q->a; *n = q->n; return 1;
    }
    case T_BYTES: case T_BARRAY: {
        Str *s = v.u.p; Seq *q = seq_new(s->len);
        for (int i = 0; i < s->len; i++) seq_push(q, v_int((unsigned char)s->s[i]));
        *out = q->a; *n = q->n; return 1;
    }
    case T_RANGE: {
        Range *r = v.u.p; Seq *q = seq_new(16);
        for (int x = r->a; r->c > 0 ? x < r->b : x > r->b; x += r->c) { if (q->n > 400000) { rt_err("MemoryError", "the range is too long to turn into a list"); return 0; } seq_push(q, v_int(x)); }
        *out = q->a; *n = q->n; return 1;
    }
    default: rt_errf("TypeError", "'%s' value is not iterable", v_typename(v)); return 0;
    }
}

Value seq_index(Value s, Value ix)
{
    if (!is_int(ix)) return rt_err("TypeError", "indices must be integers");
    if (s.t == T_RANGE) { Range *r = s.u.p; int n = 0; for (int x = r->a; r->c > 0 ? x < r->b : x > r->b; x += r->c) n++; int i = ix.u.i < 0 ? ix.u.i + n : ix.u.i; if (i < 0 || i >= n) return rt_err("IndexError", "range index out of range"); return v_int(r->a + i * r->c); }
    if (s.t == T_BYTES || s.t == T_BARRAY) { Str *b = s.u.p; int i = ix.u.i < 0 ? ix.u.i + b->len : ix.u.i; if (i < 0 || i >= b->len) return rt_err("IndexError", "index out of range"); return v_int((unsigned char)b->s[i]); }
    Seq *q = s.u.p; int i = ix.u.i < 0 ? ix.u.i + q->n : ix.u.i;
    if (i < 0 || i >= q->n) return rt_err("IndexError", s.t == T_LIST ? "list index out of range" : "tuple index out of range");
    return q->a[i];
}

Value seq_slice(Value s, int lo, int hi, int step, int has_lo, int has_hi)
{
    int n = (s.t == T_BYTES || s.t == T_BARRAY) ? ((Str *)s.u.p)->len : ((Seq *)s.u.p)->n, a, b;
    if (step > 0) {
        a = has_lo ? (lo < 0 ? lo + n : lo) : 0; b = has_hi ? (hi < 0 ? hi + n : hi) : n;
        a = clampi(a, 0, n); b = clampi(b, 0, n);
    } else {
        a = has_lo ? (lo < 0 ? lo + n : lo) : n - 1; b = has_hi ? (hi < 0 ? hi + n : hi) : -1;
        a = clampi(a, -1, n - 1); b = clampi(b, -1, n - 1);
    }
    if (s.t == T_BYTES || s.t == T_BARRAY) {
        Str *src = s.u.p; Buf out; buf_init(&out);
        for (int i = a; step > 0 ? i < b : i > b; i += step) buf_putc(&out, src->s[i]);
        return bytes_new(s.t, out.s, out.n);
    }
    Seq *src = s.u.p, *r = seq_new(8);
    for (int i = a; step > 0 ? i < b : i > b; i += step) seq_push(r, src->a[i]);
    return v_ptr(s.t, r);
}

Value seq_from_iter(int type, Value src)
{
    Value *items; int n;
    if (!iter_items(src, &items, &n)) return v_none();
    Seq *q = seq_new(n);
    for (int i = 0; i < n; i++) {
        if (type == T_SET || type == T_FSET) { int dup = 0; for (int j = 0; j < q->n && !dup; j++) dup = v_eq(q->a[j], items[i]); if (dup) continue; }
        seq_push(q, items[i]);
    }
    return v_ptr(type, q);
}

/* stable merge sort; key (a function or None) and reverse */
static int less(Value a, Value b, int *bad)
{
    int ok; int c = v_cmp(a, b, &ok);
    if (!ok) { *bad = 1; return 0; }
    return c < 0;
}
void seq_sort(Seq *q, Value key, int reverse)
{
    int n = q->n;
    if (n < 2) return;
    Value *keys = q->a;
    if (key.t != T_NONE) {
        keys = as_alloc((unsigned)n * sizeof(Value));
        for (int i = 0; i < n; i++) { keys[i] = call_value(key, 1, &q->a[i], 0, 0, 0); if (I.has_exc) return; }
    }
    int *idx = as_alloc((unsigned)n * sizeof(int) * 2), *tmp = idx + n, bad = 0;
    for (int i = 0; i < n; i++) idx[i] = i;
    for (int w = 1; w < n; w *= 2) {
        for (int lo = 0; lo < n; lo += 2 * w) {
            int mid = lo + w < n ? lo + w : n, hi = lo + 2 * w < n ? lo + 2 * w : n, i = lo, j = mid, k = lo;
            while (i < mid && j < hi) {
                int take_right = reverse ? less(keys[idx[i]], keys[idx[j]], &bad) : less(keys[idx[j]], keys[idx[i]], &bad);
                tmp[k++] = take_right ? idx[j++] : idx[i++];
            }
            while (i < mid) tmp[k++] = idx[i++];
            while (j < hi) tmp[k++] = idx[j++];
        }
        as_memcpy(idx, tmp, (unsigned)n * sizeof(int));
    }
    if (bad) { rt_err("TypeError", "the values cannot be compared with each other"); return; }
    Value *sorted = as_alloc((unsigned)n * sizeof(Value));
    for (int i = 0; i < n; i++) sorted[i] = q->a[idx[i]];
    as_memcpy(q->a, sorted, (unsigned)n * sizeof(Value));
}

Value seq_method(Value self, Str *name, int argc, Value *argv, int *found)
{
    Seq *q = self.u.p; const char *m = name->s; int list = self.t == T_LIST;
    *found = 1;
    if (!as_strcmp(m, "count")) { int c = 0; if (argc < 1) return rt_err("TypeError", "count() needs a value"); for (int i = 0; i < q->n; i++) if (v_eq(q->a[i], argv[0])) c++; return v_int(c); }
    if (!as_strcmp(m, "index")) { if (argc < 1) return rt_err("TypeError", "index() needs a value"); for (int i = 0; i < q->n; i++) if (v_eq(q->a[i], argv[0])) return v_int(i); return rt_err("ValueError", "the value is not in the list"); }
    if (!list) { *found = 0; return v_none(); }
    if (!as_strcmp(m, "append")) { if (argc < 1) return rt_err("TypeError", "append() needs a value"); seq_push(q, argv[0]); return v_none(); }
    if (!as_strcmp(m, "insert")) {
        if (argc < 2 || !is_int(argv[0])) return rt_err("TypeError", "insert(index, value)");
        int i = argv[0].u.i; if (i < 0) i += q->n; if (i < 0) i = 0; if (i > q->n) i = q->n;
        seq_push(q, v_none());
        as_memmove(&q->a[i + 1], &q->a[i], (unsigned)(q->n - 1 - i) * sizeof(Value)); q->a[i] = argv[1]; return v_none();
    }
    if (!as_strcmp(m, "extend")) { Value *items; int n; if (argc < 1 || !iter_items(argv[0], &items, &n)) return v_none(); for (int i = 0; i < n; i++) seq_push(q, items[i]); return v_none(); }
    if (!as_strcmp(m, "remove")) { if (argc < 1) return rt_err("TypeError", "remove() needs a value"); for (int i = 0; i < q->n; i++) if (v_eq(q->a[i], argv[0])) { as_memmove(&q->a[i], &q->a[i + 1], (unsigned)(q->n - i - 1) * sizeof(Value)); q->n--; return v_none(); } return rt_err("ValueError", "the value is not in the list"); }
    if (!as_strcmp(m, "rear") || !as_strcmp(m, "pop")) {
        if (!q->n) return rt_err("IndexError", "pop from an empty list");
        int i = q->n - 1;
        if (argc >= 1) { if (!is_int(argv[0])) return rt_err("TypeError", "the index must be an integer"); i = argv[0].u.i < 0 ? argv[0].u.i + q->n : argv[0].u.i; if (i < 0 || i >= q->n) return rt_err("IndexError", "pop index out of range"); }
        Value v = q->a[i]; as_memmove(&q->a[i], &q->a[i + 1], (unsigned)(q->n - i - 1) * sizeof(Value)); q->n--; return v;
    }
    if (!as_strcmp(m, "clear")) { q->n = 0; return v_none(); }
    if (!as_strcmp(m, "sort")) { seq_sort(q, v_none(), 0); return v_none(); }
    if (!as_strcmp(m, "reverse")) { for (int i = 0, j = q->n - 1; i < j; i++, j--) { Value t = q->a[i]; q->a[i] = q->a[j]; q->a[j] = t; } return v_none(); }
    if (!as_strcmp(m, "copy")) { Seq *r = seq_new(q->n); for (int i = 0; i < q->n; i++) seq_push(r, q->a[i]); return v_ptr(T_LIST, r); }
    *found = 0; return v_none();
}

static int has(Seq *q, Value v) { for (int i = 0; i < q->n; i++) if (v_eq(q->a[i], v)) return 1; return 0; }

Value set_method(Value self, Str *name, int argc, Value *argv, int *found)
{
    Seq *q = self.u.p; const char *m = name->s; int mut = self.t == T_SET;
    *found = 1;
    if (mut && !as_strcmp(m, "add")) { if (argc < 1) return rt_err("TypeError", "add() needs a value"); if (!has(q, argv[0])) seq_push(q, argv[0]); return v_none(); }
    if (mut && (!as_strcmp(m, "remove") || !as_strcmp(m, "discard"))) {
        if (argc < 1) return rt_err("TypeError", "needs a value");
        for (int i = 0; i < q->n; i++) if (v_eq(q->a[i], argv[0])) { as_memmove(&q->a[i], &q->a[i + 1], (unsigned)(q->n - i - 1) * sizeof(Value)); q->n--; return v_none(); }
        if (m[0] == 'r') { Buf b; buf_init(&b); v_repr(argv[0], &b, 1); return rt_err("KeyError", b.s); }
        return v_none();
    }
    if (mut && !as_strcmp(m, "pop")) { if (!q->n) return rt_err("KeyError", "pop from an empty set"); Value v = q->a[q->n - 1]; q->n--; return v; }
    if (mut && !as_strcmp(m, "clear")) { q->n = 0; return v_none(); }
    if (!as_strcmp(m, "copy")) { Seq *r = seq_new(q->n); for (int i = 0; i < q->n; i++) seq_push(r, q->a[i]); return v_ptr(self.t, r); }
    int op = !as_strcmp(m, "union") ? '|' : !as_strcmp(m, "intersection") ? '&' : !as_strcmp(m, "difference") ? '-' : !as_strcmp(m, "symmetric_difference") ? '^' : 0;
    if (op) {
        if (argc < 1) return rt_err("TypeError", "needs another set");
        Value other = argv[0];
        if (other.t != T_SET && other.t != T_FSET) other = seq_from_iter(T_SET, other);
        if (I.has_exc) return v_none();
        return binop(op, self, other);
    }
    if (!as_strcmp(m, "issubset") || !as_strcmp(m, "issuperset")) {
        if (argc < 1) return rt_err("TypeError", "needs another set");
        Value o = seq_from_iter(T_SET, argv[0]); if (I.has_exc) return v_none();
        Seq *a = m[2] == 's' && m[3] == 'u' && m[5] == 's' ? q : o.u.p, *b = a == q ? o.u.p : q;
        for (int i = 0; i < a->n; i++) if (!has(b, a->a[i])) return v_bool(0);
        return v_bool(1);
    }
    if (mut && !as_strcmp(m, "update")) { Value *items; int n; if (argc < 1 || !iter_items(argv[0], &items, &n)) return v_none(); for (int i = 0; i < n; i++) if (!has(q, items[i])) seq_push(q, items[i]); return v_none(); }
    *found = 0; return v_none();
}

Value dict_method(Value self, Str *name, int argc, Value *argv, int *found)
{
    Dict *d = self.u.p; const char *m = name->s;
    *found = 1;
    if (!as_strcmp(m, "get")) { if (argc < 1) return rt_err("TypeError", "get() needs a key"); Value *v = dict_get(d, argv[0]); return v ? *v : (argc >= 2 ? argv[1] : v_none()); }
    if (!as_strcmp(m, "keys") || !as_strcmp(m, "values")) { Seq *q = seq_new(d->n); for (int i = 0; i < d->n; i++) seq_push(q, m[0] == 'k' ? d->k[i] : d->v[i]); return v_ptr(T_LIST, q); }
    if (!as_strcmp(m, "items")) { Seq *q = seq_new(d->n); for (int i = 0; i < d->n; i++) { Seq *p = seq_new(2); seq_push(p, d->k[i]); seq_push(p, d->v[i]); seq_push(q, v_ptr(T_TUPLE, p)); } return v_ptr(T_LIST, q); }
    if (!as_strcmp(m, "update")) {
        if (argc < 1 || argv[0].t != T_DICT) return rt_err("TypeError", "update() needs a dictionary");
        Dict *o = argv[0].u.p; for (int i = 0; i < o->n; i++) dict_set(d, o->k[i], o->v[i]); return v_none();
    }
    if (!as_strcmp(m, "pop")) {
        if (argc < 1) return rt_err("TypeError", "pop() needs a key");
        Value *v = dict_get(d, argv[0]);
        if (!v) { if (argc >= 2) return argv[1]; Buf b; buf_init(&b); v_repr(argv[0], &b, 1); return rt_err("KeyError", b.s); }
        Value r = *v; dict_del(d, argv[0]); return r;
    }
    if (!as_strcmp(m, "clear_end")) { if (!d->n) return rt_err("KeyError", "the dictionary is empty"); d->n--; return v_none(); }
    if (!as_strcmp(m, "clear")) { d->n = 0; return v_none(); }
    if (!as_strcmp(m, "setdefault")) { if (argc < 1) return rt_err("TypeError", "setdefault() needs a key"); Value *v = dict_get(d, argv[0]); if (v) return *v; Value dv = argc >= 2 ? argv[1] : v_none(); dict_set(d, argv[0], dv); return dv; }
    if (!as_strcmp(m, "copy")) { Dict *r = dict_new(); for (int i = 0; i < d->n; i++) dict_set(r, d->k[i], d->v[i]); return v_ptr(T_DICT, r); }
    *found = 0; return v_none();
}
