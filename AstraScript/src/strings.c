/* AstraScript engine 2 - strings.c: text values and their methods. Text is UTF-8; positions count letters, not bytes. */
#include "as_types.h"

Value bytes_new(int type, const char *s, int len) { return v_ptr(type, str_new(s, len)); }

static int is_cont(char c) { return ((unsigned char)c & 0xC0) == 0x80; }
static int u_len(Str *s) { int n = 0; for (int i = 0; i < s->len; i++) if (!is_cont(s->s[i])) n++; return n; }
static int u_off(Str *s, int idx)                       /* byte offset of letter number idx (idx may equal the length) */
{
    int n = 0, i = 0;
    while (i < s->len && n < idx) { i++; while (i < s->len && is_cont(s->s[i])) i++; n++; }
    return i;
}
static int u_idx(Str *s, int off) { int n = 0; for (int i = 0; i < off && i < s->len; i++) if (!is_cont(s->s[i])) n++; return n; }

int str_letters(Value v) { return v.t == T_STR ? u_len(v.u.p) : ((Str *)v.u.p)->len; }

Value str_index(Value sv, Value ix)
{
    Str *s = sv.u.p;
    if (ix.t != T_INT) return rt_err("TypeError", "string indices must be integers");
    int n = u_len(s), i = ix.u.i < 0 ? ix.u.i + n : ix.u.i;
    if (i < 0 || i >= n) return rt_err("IndexError", "string index out of range");
    int a = u_off(s, i), b = a + 1;
    while (b < s->len && is_cont(s->s[b])) b++;
    return v_str(s->s + a, b - a);
}

static void slice_bounds(int n, int lo, int hi, int step, int has_lo, int has_hi, int *a, int *b)
{
    if (step > 0) {
        *a = has_lo ? (lo < 0 ? lo + n : lo) : 0; *b = has_hi ? (hi < 0 ? hi + n : hi) : n;
        *a = clampi(*a, 0, n); *b = clampi(*b, 0, n);
    } else {
        *a = has_lo ? (lo < 0 ? lo + n : lo) : n - 1; *b = has_hi ? (hi < 0 ? hi + n : hi) : -1;
        *a = clampi(*a, -1, n - 1); *b = clampi(*b, -1, n - 1);
    }
}

Value str_slice(Value sv, int lo, int hi, int step, int has_lo, int has_hi)
{
    Str *s = sv.u.p; int n = u_len(s), a, b;
    slice_bounds(n, lo, hi, step, has_lo, has_hi, &a, &b);
    Buf out; buf_init(&out);
    if (step == 1) { if (a < b) { int x = u_off(s, a), y = u_off(s, b); buf_add(&out, s->s + x, y - x); } return v_str(out.s, out.n); }
    for (int i = a; step > 0 ? i < b : i > b; i += step) {
        int x = u_off(s, i), y = x + 1;
        while (y < s->len && is_cont(s->s[y])) y++;
        buf_add(&out, s->s + x, y - x);
    }
    return v_str(out.s, out.n);
}

static int is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

static int find_in(Str *h, Str *n, int from)
{
    for (int i = from; i + n->len <= h->len; i++) if (!as_memcmp(h->s + i, n->s, (unsigned)n->len)) return i;
    return -1;
}

static Value want_str(Value v, const char *meth, Str **out)
{
    if (v.t != T_STR) { char m[80]; as_fmt(m, sizeof m, "%s() needs text values", meth); return rt_err("TypeError", m); }
    *out = v.u.p;
    return v_none();
}

Value str_method(Value self, Str *name, int argc, Value *argv, int *found)
{
    Str *s = self.u.p;
    const char *m = name->s;
    *found = 1;
    if (self.t != T_STR) {                                   /* bytes / bytearray: a few methods */
        if (!as_strcmp(m, "decode")) return v_str(s->s, s->len);
        if (!as_strcmp(m, "hex")) { Buf b; buf_init(&b); const char *h = "0123456789abcdef"; for (int i = 0; i < s->len; i++) { buf_putc(&b, h[(unsigned char)s->s[i] >> 4]); buf_putc(&b, h[(unsigned char)s->s[i] & 15]); } return v_str(b.s, b.n); }
        *found = 0; return v_none();
    }
    if (!as_strcmp(m, "bigger") || !as_strcmp(m, "upper") || !as_strcmp(m, "smalest") || !as_strcmp(m, "lower")) {
        int up = (m[0] == 'b' || m[0] == 'u');
        Str *r = str_new(s->s, s->len);
        for (int i = 0; i < r->len; i++) { char c = r->s[i]; if (up && c >= 'a' && c <= 'z') r->s[i] = (char)(c - 32); else if (!up && c >= 'A' && c <= 'Z') r->s[i] = (char)(c + 32); }
        return v_ptr(T_STR, r);
    }
    if (!as_strcmp(m, "strip") || !as_strcmp(m, "lstrip") || !as_strcmp(m, "rstrip")) {
        int a = 0, b = s->len;
        if (m[0] != 'r') while (a < b && is_space(s->s[a])) a++;
        if (m[0] != 'l') while (b > a && is_space(s->s[b - 1])) b--;
        return v_str(s->s + a, b - a);
    }
    if (!as_strcmp(m, "replace")) {
        Str *x = 0, *y = 0;
        if (argc < 2) return rt_err("TypeError", "replace() needs two values");
        Value e = want_str(argv[0], "replace", &x); if (I.has_exc) return e;
        e = want_str(argv[1], "replace", &y); if (I.has_exc) return e;
        int limit = argc >= 3 && argv[2].t == T_INT ? argv[2].u.i : -1;
        Buf b; buf_init(&b);
        int i = 0;
        if (x->len == 0) return self;
        while (i < s->len) {
            if (limit != 0 && i + x->len <= s->len && !as_memcmp(s->s + i, x->s, (unsigned)x->len)) { buf_add(&b, y->s, y->len); i += x->len; if (limit > 0) limit--; }
            else buf_putc(&b, s->s[i++]);
        }
        return v_str(b.s, b.n);
    }
    if (!as_strcmp(m, "startswith") || !as_strcmp(m, "endswith")) {
        Str *x; if (argc < 1) return rt_err("TypeError", "needs a text value");
        Value e = want_str(argv[0], m, &x); if (I.has_exc) return e;
        if (x->len > s->len) return v_bool(0);
        return v_bool(!as_memcmp(m[0] == 's' ? s->s : s->s + s->len - x->len, x->s, (unsigned)x->len));
    }
    if (!as_strcmp(m, "find") || !as_strcmp(m, "index") || !as_strcmp(m, "count")) {
        Str *x; if (argc < 1) return rt_err("TypeError", "needs a text value");
        Value e = want_str(argv[0], m, &x); if (I.has_exc) return e;
        if (m[0] == 'c') { int c = 0, i = 0; if (!x->len) return v_int(u_len(s) + 1); while ((i = find_in(s, x, i)) >= 0) { c++; i += x->len; } return v_int(c); }
        int p = find_in(s, x, 0);
        if (p < 0) { if (m[0] == 'i') return rt_err("ValueError", "substring not found"); return v_int(-1); }
        return v_int(u_idx(s, p));
    }
    if (!as_strcmp(m, "split")) {
        Seq *q = seq_new(8);
        if (argc == 0 || argv[0].t == T_NONE) {
            int i = 0;
            while (i < s->len) { while (i < s->len && is_space(s->s[i])) i++; int a = i; while (i < s->len && !is_space(s->s[i])) i++; if (i > a) seq_push(q, v_str(s->s + a, i - a)); }
        } else {
            Str *x; Value e = want_str(argv[0], "split", &x); if (I.has_exc) return e;
            if (!x->len) return rt_err("ValueError", "empty separator");
            int i = 0, a = 0;
            while (i <= s->len - x->len) { if (!as_memcmp(s->s + i, x->s, (unsigned)x->len)) { seq_push(q, v_str(s->s + a, i - a)); i += x->len; a = i; } else i++; }
            seq_push(q, v_str(s->s + a, s->len - a));
        }
        return v_ptr(T_LIST, q);
    }
    if (!as_strcmp(m, "join")) {
        Value *items; int n; if (argc < 1 || !iter_items(argv[0], &items, &n)) return v_none();
        Buf b; buf_init(&b);
        for (int i = 0; i < n; i++) { if (items[i].t != T_STR) return rt_err("TypeError", "join() needs a sequence of text values"); if (i) buf_add(&b, s->s, s->len); buf_add(&b, ((Str *)items[i].u.p)->s, ((Str *)items[i].u.p)->len); }
        return v_str(b.s, b.n);
    }
    if (!as_strcmp(m, "isdigit") || !as_strcmp(m, "isalpha") || !as_strcmp(m, "isspace")) {
        if (!s->len) return v_bool(0);
        for (int i = 0; i < s->len; i++) { char c = s->s[i]; int ok = m[2] == 'd' ? (c >= '0' && c <= '9') : m[2] == 'a' ? ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (unsigned char)c >= 0x80) : is_space(c); if (!ok) return v_bool(0); }
        return v_bool(1);
    }
    if (!as_strcmp(m, "capitalize")) { Str *r = str_new(s->s, s->len); if (r->len && r->s[0] >= 'a' && r->s[0] <= 'z') r->s[0] = (char)(r->s[0] - 32); return v_ptr(T_STR, r); }
    if (!as_strcmp(m, "encode")) return bytes_new(T_BYTES, s->s, s->len);
    if (!as_strcmp(m, "length")) return v_int(u_len(s));
    *found = 0;
    return v_none();
}
