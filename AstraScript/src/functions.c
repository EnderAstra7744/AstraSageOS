/* AstraScript engine 2 - functions.c: defining and calling functions (defaults, *args, **kwargs, keyword-only, closures, lambdas). */
#include "as_types.h"

#define MAX_DEPTH 60

Value make_func(Node *def, Env *clo, Class *cls)
{
    Func *f = as_alloc(sizeof(Func));
    f->def = def; f->clo = clo; f->cls = cls;
    f->name = def->s ? def->s : as_intern("<lambda>", 8);
    if (def->np) {
        f->defv = as_alloc((unsigned)def->np * sizeof(Value));
        for (int i = 0; i < def->np; i++)
            if (def->params[i].def) { f->defv[i] = eval(def->params[i].def); if (I.has_exc) return v_none(); }
    }
    return v_ptr(T_FUNC, f);
}

Value call_func(Func *f, Value self, int has_self, int argc, Value *argv, int nkw, Str **kn, Value *kv)
{
    Node *def = f->def;
    if (I.depth >= MAX_DEPTH) return rt_err("RuntimeError", "maximum recursion depth exceeded");
    Value stackpos[12]; Value *pos = stackpos;
    int npos = argc + (has_self ? 1 : 0);
    if (npos > 12) pos = as_alloc((unsigned)npos * sizeof(Value));
    if (has_self) pos[0] = self;
    if (argc) as_memcpy(pos + (has_self ? 1 : 0), argv, (unsigned)argc * sizeof(Value));

    Env *e = env_new(f->clo);
    unsigned char used[32]; as_memset(used, 0, sizeof used);
    int idx = 0, have_star = 0, have_dstar = 0;
    for (int i = 0; i < def->np; i++) {
        Param *p = &def->params[i];
        if (p->kind == 0) {
            if (idx < npos) env_set(e, p->name, pos[idx++]);
            else {
                int hit = -1;
                for (int k = 0; k < nkw && k < 32; k++) if (kn[k] == p->name) { hit = k; break; }
                if (hit >= 0) { env_set(e, p->name, kv[hit]); used[hit] = 1; }
                else if (p->def) env_set(e, p->name, f->defv[i]);
                else return rt_errf("TypeError", "missing argument '%s'", p->name->s);
            }
        } else if (p->kind == 1) {
            have_star = 1;
            Seq *q = seq_new(npos - idx > 0 ? npos - idx : 1);
            while (idx < npos) seq_push(q, pos[idx++]);
            env_set(e, p->name, v_ptr(T_TUPLE, q));
        } else if (p->kind == 3) {
            int hit = -1;
            for (int k = 0; k < nkw && k < 32; k++) if (kn[k] == p->name) { hit = k; break; }
            if (hit >= 0) { env_set(e, p->name, kv[hit]); used[hit] = 1; }
            else if (p->def) env_set(e, p->name, f->defv[i]);
            else return rt_errf("TypeError", "missing keyword-only argument '%s'", p->name->s);
        } else if (p->kind == 2) {
            have_dstar = 1;
            Dict *d = dict_new();
            for (int k = 0; k < nkw && k < 32; k++) if (!used[k]) { dict_set(d, v_ptr(T_STR, kn[k]), kv[k]); used[k] = 1; }
            env_set(e, p->name, v_ptr(T_DICT, d));
        }
    }
    if (idx < npos && !have_star) {
        char m[100]; as_fmt(m, sizeof m, "%s() takes %d positional argument(s) but %d were given", f->name->s, idx, npos);
        return rt_err("TypeError", m);
    }
    if (!have_dstar) for (int k = 0; k < nkw && k < 32; k++) if (!used[k]) return rt_errf("TypeError", "unexpected keyword argument '%s'", kn[k]->s);

    Env *saved = I.cur;
    I.cur = e;
    I.frames[I.depth].fn = f; I.frames[I.depth].self = has_self ? self : v_none(); I.frames[I.depth].line = I.line;
    I.depth++;
    Value r = v_none();
    if (def->k == N_LAMBDA) r = eval(def->a);
    else if (def->flag) {                                          /* a generator function: collect everything it yields */
        Seq *saved_gen = I.gen; I.gen = seq_new(8);
        exec_block(def->a);
        I.ret = v_none();
        Gen *g = as_alloc(sizeof(Gen)); g->items = I.gen; g->pos = 0;
        I.gen = saved_gen;
        r = v_ptr(T_GEN, g);
    } else {
        int st = exec_block(def->a);
        if (st == 3) r = I.ret;
        I.ret = v_none();
    }
    I.depth--;
    I.cur = saved;
    if (I.has_exc) return v_none();
    return r;
}

Value call_method(Value self, Str *name, int argc, Value *argv, int nkw, Str **kn, Value *kv)
{
    int found = 0;
    Value r;
    switch (self.t) {
    case T_STR: case T_BYTES: case T_BARRAY: r = str_method(self, name, argc, argv, &found); break;
    case T_LIST: case T_TUPLE: r = seq_method(self, name, argc, argv, &found); break;
    case T_SET: case T_FSET: r = set_method(self, name, argc, argv, &found); break;
    case T_DICT: r = dict_method(self, name, argc, argv, &found); break;
    default: r = v_none(); break;
    }
    (void)nkw; (void)kn; (void)kv;
    if (!found && !I.has_exc) return rt_errf("AttributeError", "this value has no method '%s'", name->s);
    return r;
}

Value call_value(Value f, int argc, Value *argv, int nkw, Str **kn, Value *kv)
{
    switch (f.t) {
    case T_FUNC: return call_func(f.u.p, v_none(), 0, argc, argv, nkw, kn, kv);
    case T_BUILTIN: return ((Builtin *)f.u.p)->fn(argc, argv, nkw, kn, kv);
    case T_CLASS: return instantiate(f.u.p, argc, argv, nkw, kn, kv);
    case T_BOUND: {
        Bound *b = f.u.p;
        if (b->fn.t == T_FUNC) return call_func(b->fn.u.p, b->self, 1, argc, argv, nkw, kn, kv);
        Value tmp[10]; int n = argc > 9 ? 9 : argc; tmp[0] = b->self; as_memcpy(tmp + 1, argv, (unsigned)n * sizeof(Value));
        return ((Builtin *)b->fn.u.p)->fn(n + 1, tmp, nkw, kn, kv);
    }
    case T_BMETH: { BMeth *m = f.u.p; return call_method(m->self, m->name, argc, argv, nkw, kn, kv); }
    default: return rt_errf("TypeError", "'%s' value is not callable", v_typename(f));
    }
}
