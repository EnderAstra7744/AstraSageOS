/* AstraScript engine 2 - modules.c: #import / #from / import / from / use, and running program text inside an environment. */
#include "as_types.h"

extern const char *const as_lib_names[];
extern const char *const as_lib_text[];

#define MAX_MODS 16
static struct { Str *name; Value mod; } cache[MAX_MODS];
static int nmods;
static char modbuf[16384];

void modules_reset(void) { nmods = 0; }

/* parses and runs text with `env` as the current scope. 0 = ran (an exception may be pending), -1 = syntax error (as_parse_error*) */
int as_run_block_text(const char *src, int len, Env *env, const char *label)
{
    (void)label;
    int nt = 0;
    Token *t = as_lex(src, (unsigned)len, &nt, 1);
    if (!t) { as_parse_error_line = as_lex_error_line; as_fmt(as_parse_error, sizeof as_parse_error, "%s", as_lex_error); return -1; }
    Node *b = as_parse(t, nt, "module");
    if (!b) return -1;
    Env *save = I.cur; I.cur = env;
    hoist(b);
    exec_block(b);
    I.cur = save;
    return 0;
}

static Str *base_name(Str *n)
{
    int len = n->len;
    if (len > 4 && !as_strcmp(n->s + len - 4, ".ash")) len -= 4;
    return as_intern(n->s, len);
}

int import_module(Str *name, Value *out)
{
    Str *base = base_name(name);
    for (int i = 0; i < nmods; i++) if (cache[i].name == base) { *out = cache[i].mod; return 1; }
    char file[80]; as_fmt(file, sizeof file, "%s.ash", base->s);
    int n = as_h_module(file, modbuf, sizeof modbuf - 1);
    const char *text = modbuf;
    if (n < 0) {
        n = -1;
        for (int i = 0; as_lib_names[i]; i++) if (!as_strcmp(as_lib_names[i], file)) { text = as_lib_text[i]; n = (int)as_strlen(text); break; }
    }
    if (n < 0) { rt_errf("ImportError", "no module named '%s' (looked in your folder, in installed packages and in the built-in library)", file); return 0; }
    if (nmods >= MAX_MODS) { rt_err("ImportError", "too many modules are imported (16 at most)"); return 0; }
    Module *m = as_alloc(sizeof(Module));
    m->name = base; m->env = env_new(I.builtins);
    Value mv = v_ptr(T_MODULE, m);
    cache[nmods].name = base; cache[nmods].mod = mv; nmods++;      /* registered first: a module that imports itself does not loop */
    Env *save = I.cur, *sg = I.globals;
    I.globals = m->env;
    int r = as_run_block_text(text, n, m->env, file);
    I.globals = sg; I.cur = save;
    if (r < 0) { char msg[140]; as_fmt(msg, sizeof msg, "%s (in module %s)", as_parse_error, file); nmods--; rt_err("ImportError", msg); return 0; }
    if (I.has_exc) { nmods--; return 0; }
    *out = mv;
    return 1;
}

Value exec_import(Node *n)
{
    if (n->k == S_USE) {
        int len = as_h_package(n->s->s, modbuf, sizeof modbuf - 1);
        if (len < 0) return rt_errf("ImportError", "the package '%s' is not installed", n->s->s);
        if (as_run_block_text(modbuf, len, I.cur, n->s->s) < 0) { char msg[140]; as_fmt(msg, sizeof msg, "%s (in package %s)", as_parse_error, n->s->s); return rt_err("ImportError", msg); }
        return v_none();
    }
    Value mod;
    if (!import_module(n->s, &mod)) return v_none();
    if (n->k == S_IMPORT) {
        Str *alias = n->d ? (Str *)n->d->v.u.p : base_name(n->s);
        env_set(I.cur, alias, mod);
        return v_none();
    }
    Env *e = ((Module *)mod.u.p)->env;
    for (int i = 0; i < n->n; i++) {
        Value *v = env_find_here(e, n->names[2 * i]);
        if (!v) return rt_errf("ImportError", "the module '%s' has no item with that name", n->s->s);
        env_set(I.cur, n->names[2 * i + 1], *v);
    }
    return v_none();
}
