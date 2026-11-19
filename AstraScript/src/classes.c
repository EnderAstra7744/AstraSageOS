/* AstraScript engine 2 - classes.c: classes, instances, attribute access, super(). */
#include "as_types.h"

Value class_new(Str *name, Class *base)
{
    Class *c = as_alloc(sizeof(Class));
    c->name = name; c->base = base;
    c->attrs.cap = 4; c->attrs.k = as_alloc(4 * sizeof(Value)); c->attrs.v = as_alloc(4 * sizeof(Value));
    if (base) c->exc = base->exc;
    return v_ptr(T_CLASS, c);
}

int class_isa(Class *c, Class *base)
{
    for (; c; c = c->base) if (c == base) return 1;
    return 0;
}

static Value *class_lookup(Class *c, Str *name)
{
    for (; c; c = c->base) { Value *v = dict_gets(&c->attrs, name); if (v) return v; }
    return 0;
}

static Value bind(Value self, Value f)
{
    if (f.t == T_DESC) {
        Desc *d = f.u.p;
        if (d->kind == 2) return d->f;
        if (d->kind == 1) { Value cls = self.t == T_INST ? v_ptr(T_CLASS, ((Inst *)self.u.p)->cls) : self; Bound *b = as_alloc(sizeof(Bound)); b->self = cls; b->fn = d->f; return v_ptr(T_BOUND, b); }
        return call_value(d->f, 1, &self, 0, 0, 0);                  /* property: the value is computed on every read */
    }
    if (f.t != T_FUNC && f.t != T_BUILTIN) return f;
    Bound *b = as_alloc(sizeof(Bound)); b->self = self; b->fn = f;
    return v_ptr(T_BOUND, b);
}

Value instantiate(Class *c, int argc, Value *argv, int nkw, Str **kn, Value *kv)
{
    Inst *in = as_alloc(sizeof(Inst));
    in->cls = c;
    in->attrs.cap = 4; in->attrs.k = as_alloc(4 * sizeof(Value)); in->attrs.v = as_alloc(4 * sizeof(Value));
    Value self = v_ptr(T_INST, in);
    Value *init = class_lookup(c, as_intern("init", 4));
    if (!init) init = class_lookup(c, as_intern("__init__", 8));
    if (init) {
        Value r;
        if (init->t == T_FUNC) r = call_func(init->u.p, self, 1, argc, argv, nkw, kn, kv);
        else { Value tmp[9]; int n = argc > 8 ? 8 : argc; tmp[0] = self; as_memcpy(tmp + 1, argv, (unsigned)n * sizeof(Value)); r = ((Builtin *)init->u.p)->fn(n + 1, tmp, nkw, kn, kv); }
        (void)r;
        if (I.has_exc) return v_none();
    } else if (argc || nkw) {
        return rt_errf("TypeError", "%s() takes no arguments", c->name->s);
    }
    return self;
}

Value get_attr(Value obj, Str *name)
{
    switch (obj.t) {
    case T_INST: {
        Inst *in = obj.u.p;
        Value *v = dict_gets(&in->attrs, name);
        if (v) return *v;
        v = class_lookup(in->cls, name);
        if (v) return bind(obj, *v);
        return rt_errf("AttributeError", "object has no attribute '%s'", name->s);
    }
    case T_CLASS: {
        Value *v = class_lookup(obj.u.p, name);
        if (v && v->t == T_DESC && ((Desc *)v->u.p)->kind != 3) return bind(obj, *v);
        if (v) return *v;
        return rt_errf("AttributeError", "class has no attribute '%s'", name->s);
    }
    case T_MODULE: {
        Module *m = obj.u.p;
        Value *v = env_find_here(m->env, name);
        if (v) return *v;
        return rt_errf("AttributeError", "module has no attribute '%s'", name->s);
    }
    case T_SUPER: {
        Super *s = obj.u.p;
        Value *v = class_lookup(s->from, name);
        if (v) return bind(s->self, *v);
        return rt_errf("AttributeError", "super has no attribute '%s'", name->s);
    }
    case T_STR: case T_BYTES: case T_BARRAY: case T_LIST: case T_TUPLE: case T_SET: case T_FSET: case T_DICT: {
        BMeth *m = as_alloc(sizeof(BMeth)); m->self = obj; m->name = name;
        return v_ptr(T_BMETH, m);
    }
    default:
        return rt_errf("AttributeError", "this value has no attribute '%s'", name->s);
    }
}

void set_attr(Value obj, Str *name, Value v)
{
    if (obj.t == T_INST) {
        Value *cv = class_lookup(((Inst *)obj.u.p)->cls, name);
        if (cv && cv->t == T_DESC && ((Desc *)cv->u.p)->kind == 3) {
            Desc *d = cv->u.p;
            if (d->setter.t == T_NONE) { rt_errf("AttributeError", "the property '%s' cannot be set (it has no setter)", name->s); return; }
            Value args[2]; args[0] = obj; args[1] = v; call_value(d->setter, 2, args, 0, 0, 0); return;
        }
        dict_sets(&((Inst *)obj.u.p)->attrs, name, v); return;
    }
    if (obj.t == T_CLASS) { dict_sets(&((Class *)obj.u.p)->attrs, name, v); return; }
    if (obj.t == T_MODULE) { env_set(((Module *)obj.u.p)->env, name, v); return; }
    rt_errf("AttributeError", "cannot set the attribute '%s' on this value", name->s);
}
