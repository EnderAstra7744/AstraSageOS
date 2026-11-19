/* AstraScript engine 2 - errors.c: the Error classes, raising and matching errors. */
#include "as_types.h"

static const char *const NAMES[] = { "ValueError", "TypeError", "NameError", "IndexError", "KeyError", "ZeroDivisionError", "AssertionError",
                                     "RuntimeError", "AttributeError", "ImportError", "StopIteration", "MemoryError", "OverflowError", "OSError", "FileNotFoundError", 0 };
static Class *error_base;
static Class *classes[16];
static Str *s_msg, *s_line;

static Value exc_init(int argc, Value *argv, int nkw, Str **kn, Value *kv)
{
    (void)nkw; (void)kn; (void)kv;
    if (argc >= 1 && argv[0].t == T_INST) {
        Inst *in = argv[0].u.p;
        dict_sets(&in->attrs, s_msg, argc >= 2 ? v_to_str(argv[1]) : v_cstr(""));
    }
    return v_none();
}
static Builtin b_exc_init = { "init", exc_init, 0 };

void errors_link(void);
void errors_init(void)
{
    s_msg = as_intern("msg", 3); s_line = as_intern("line", 4);
    Value ev = class_new(as_intern("Error", 5), 0);
    error_base = ev.u.p; error_base->exc = 1;
    dict_sets(&error_base->attrs, as_intern("init", 4), v_ptr(T_BUILTIN, &b_exc_init));
    env_set(I.builtins, error_base->name, ev);
    for (int i = 0; NAMES[i]; i++) {
        Value cv = class_new(as_intern(NAMES[i], (int)as_strlen(NAMES[i])), error_base);
        ((Class *)cv.u.p)->exc = 1;
        classes[i] = cv.u.p;
        env_set(I.builtins, ((Class *)cv.u.p)->name, cv);
    }
    errors_link();
}

void errors_link(void)
{
    Class *os = err_class("OSError");
    for (int i = 0; NAMES[i]; i++) if (!as_strcmp(NAMES[i], "FileNotFoundError")) classes[i]->base = os;
}

Class *err_class(const char *name)
{
    for (int i = 0; NAMES[i]; i++) if (!as_strcmp(NAMES[i], name)) return classes[i];
    return error_base;
}

Value rt_err(const char *type, const char *msg)
{
    Class *c = err_class(type);
    Inst *in = as_alloc(sizeof(Inst));
    in->cls = c;
    in->attrs.cap = 4; in->attrs.k = as_alloc(4 * sizeof(Value)); in->attrs.v = as_alloc(4 * sizeof(Value));
    dict_sets(&in->attrs, s_msg, v_cstr(msg));
    dict_sets(&in->attrs, s_line, v_int(I.line));
    I.exc = v_ptr(T_INST, in); I.has_exc = 1;
    return v_none();
}

Value rt_errf(const char *type, const char *fmt, const char *a)
{
    char b[160];
    as_fmt(b, sizeof b, fmt, a);
    return rt_err(type, b);
}

int exc_matches(Value exc, Class *c)
{
    if (exc.t != T_INST) return 0;
    return class_isa(((Inst *)exc.u.p)->cls, c);
}

Value exc_message(Value exc)
{
    if (exc.t != T_INST) return v_cstr("");
    Value *m = dict_gets(&((Inst *)exc.u.p)->attrs, s_msg);
    return m ? *m : v_cstr("");
}
