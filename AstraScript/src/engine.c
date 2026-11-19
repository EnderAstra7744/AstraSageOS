/* AstraScript engine 2 - engine.c: the public entry points. */
#include "astrascript.h"
#include "as_types.h"

extern const char *as_abort_message(void);
void modules_reset(void);

static Str *s_line_name;

void rt_print_exc_top(void)
{
    Value e = I.exc;
    if (e.t != T_INST) { as_h_error(I.line, "Error"); return; }
    Inst *in = e.u.p;
    Value *ln = dict_gets(&in->attrs, as_intern("line", 4));
    Value m = exc_message(e);
    char msg[220];
    as_fmt(msg, sizeof msg, "%s: %s", in->cls->name->s, m.t == T_STR ? ((Str *)m.u.p)->s : "");
    as_h_error(ln && ln->t == T_INT ? ln->u.i : I.line, msg);
    I.errors++;
}

static void init_state(void)
{
    as_mem_reset();
    as_memset(&I, 0, sizeof I);
    modules_reset();
    I.builtins = env_new(0);
    I.globals = env_new(I.builtins);
    I.cur = I.globals;
    errors_init();
    builtins_init();
    (void)s_line_name;
}

static Node *parse_all(const char *src, unsigned len)
{
    int nt = 0;
    Token *t = as_lex(src, len, &nt, 1);
    if (!t) { as_h_error(as_lex_error_line, as_lex_error); return 0; }
    Node *b = as_parse(t, nt, "program");
    if (!b) { as_h_error(as_parse_error_line, as_parse_error); return 0; }
    return b;
}

int as2_run_ex(const char *src, unsigned len, const char *ctx, unsigned ctxlen, int strict)
{
    (void)ctx; (void)ctxlen;
    as_relaxed = !strict;
    init_state();
    if (__builtin_setjmp(as_abort_target())) { as_h_error(I.line, as_abort_message()); return 1; }
    Node *prog = parse_all(src, len);
    if (!prog) return 1;
    hoist(prog);
    exec_block(prog);
    if (I.has_exc) { rt_print_exc_top(); return 1; }
    Value *m = strict ? env_find_here(I.globals, as_intern("main", 4)) : 0;
    if (m && m->t == T_FUNC) {                                    /* a main() function starts by itself */
        I.depth = 0;
        call_value(*m, 0, 0, 0, 0, 0);
        if (I.has_exc) { rt_print_exc_top(); return 1; }
    }
    return 0;
}

int as2_run(const char *src, unsigned len, const char *ctx, unsigned ctxlen) { return as2_run_ex(src, len, ctx, ctxlen, 1); }

/* Does the program parse? (no output). Returns 1 when it does; otherwise 0 with the first problem in *line / *msg. */
static int probe_body(const char *src, unsigned len, int *line, const char **msg)
{
    int nt = 0;
    Token *t = as_lex(src, len, &nt, 1);
    if (!t) { *line = as_lex_error_line; *msg = as_lex_error; return 0; }
    if (!as_parse(t, nt, "program")) { *line = as_parse_error_line; *msg = as_parse_error; return 0; }
    return 1;
}
int as2_probe(const char *src, unsigned len, int strict, int *line, const char **msg)
{
    as_relaxed = !strict;
    init_state();
    if (__builtin_setjmp(as_abort_target())) { *line = 0; *msg = "out of memory"; return 0; }
    return probe_body(src, len, line, msg);
}

int as2_check(const char *src, unsigned len);
int as2_check_ctx(const char *src, unsigned len, const char *ctx, unsigned ctxlen)
{
    as_check_context(ctx, ctxlen);
    int r = as2_check(src, len);
    as_check_context(0, 0);
    return r;
}

int as2_check(const char *src, unsigned len)
{
    as_relaxed = 0;
    init_state();
    if (__builtin_setjmp(as_abort_target())) return 1;
    Node *prog = parse_all(src, len);
    if (!prog) return 1;
    return as_static_check(prog);
}

int as2_is_strict(const char *src, unsigned len)
{
    unsigned i = 0; int in_c = 0;
    while (i < len) {
        unsigned st = i, en;
        while (i < len && src[i] != '\n') i++;
        en = i; if (i < len) i++;
        while (st < en && (src[st] == ' ' || src[st] == '\t' || src[st] == '\r')) st++;
        while (en > st && (src[en - 1] == ' ' || src[en - 1] == '\t' || src[en - 1] == '\r')) en--;
        if (en == st) continue;
        if (in_c) { for (unsigned k = st; k + 2 < en + 0 || k + 2 == en; k++) if (k + 2 < en + 1 && src[k] == '*' && src[k + 1] == '*' && src[k + 2] == '@') { in_c = 0; break; } continue; }
        if (src[st] == '@') { if (en - st >= 3 && !as_strncmp(src + st, "@**", 3)) { in_c = 1; for (unsigned k = st + 3; k + 2 < en + 1 && k + 2 < len; k++) if (src[k] == '*' && src[k + 1] == '*' && src[k + 2] == '@') { in_c = 0; break; } } continue; }
        if (src[st] == '#' && ((en - st > 7 && !as_strncmp(src + st, "#import", 7)) || (en - st > 5 && !as_strncmp(src + st, "#from", 5)))) continue;
        return en - st == 6 && !as_strncmp(src + st, "start;", 6);
    }
    return 0;
}

/* Does a file without "start;" use the new language? (words and forms the old interpreter does not have) */
static int uses_new_body(const char *src, unsigned len)
{
    static const char *const W[] = { "True", "False", "None", "elif", "lambda", "class", "yield", "with", "assert", "ask", "error_for", "push_error",
        "global", "nonlocal", "pass", "bool", "list", "tuple", "set", "frozenset", "dict", "bytes", "bytearray", "def", 0 };
    int nt = 0;
    Token *t = as_lex(src, len, &nt, 1);
    if (!t) return 0;
    for (int i = 0; i < nt; i++) {
        if (t[i].k == K_DIRECT && t[i].aux == 0) return 1;                      /* #import / #from */
        if (t[i].k == K_FSTR || t[i].k == K_BSTR) return 1;
        if (t[i].k == K_OP && !as_strcmp(t[i].u.s->s, ".")) return 1;           /* attribute / method */
        if (t[i].k == K_OP && !as_strcmp(t[i].u.s->s, "[")) return 1;           /* list / indexing */
        if (t[i].k == K_NAME) for (int w = 0; W[w]; w++) if (!as_strcmp(t[i].u.s->s, W[w])) return 1;
    }
    return 0;
}

int as2_uses_new(const char *src, unsigned len)
{
    as_relaxed = 1;
    init_state();
    if (__builtin_setjmp(as_abort_target())) return 0;
    return uses_new_body(src, len);
}
