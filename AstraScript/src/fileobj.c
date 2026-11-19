/* AstraScript engine 2 - fileobj.c: open(name, mode) for the AstraSageOS files (text, up to 4096 bytes per file, modes r / w / a). */
#include "as_types.h"

static Class *fcls;
static Str *s_name, *s_mode, *s_buf, *s_pos, *s_closed;
static Builtin m_read, m_readline, m_readlines, m_write, m_close, m_enter, m_exit;

void fileobj_reset(void) { fcls = 0; }

static Inst *self_of(int argc, Value *argv)
{
    if (argc < 1 || argv[0].t != T_INST) { rt_err("TypeError", "a file method needs its file"); return 0; }
    return argv[0].u.p;
}
static Value attr(Inst *in, Str *n) { Value *v = dict_gets(&in->attrs, n); return v ? *v : v_none(); }
static int open_check(Inst *in)
{
    if (attr(in, s_closed).u.i) { rt_err("ValueError", "the file is closed"); return 0; }
    return 1;
}

static Value f_read(int argc, Value *argv, int nkw, Str **kn, Value *kv)
{
    (void)nkw; (void)kn; (void)kv;
    Inst *in = self_of(argc, argv); if (!in || !open_check(in)) return v_none();
    if (as_strcmp(((Str *)attr(in, s_mode).u.p)->s, "r")) return rt_err("OSError", "the file is not open for reading");
    Str *b = attr(in, s_buf).u.p; int pos = attr(in, s_pos).u.i, n = b->len - pos;
    if (argc >= 2 && argv[1].t == T_INT && argv[1].u.i >= 0 && argv[1].u.i < n) n = argv[1].u.i;
    dict_sets(&in->attrs, s_pos, v_int(pos + n));
    return v_str(b->s + pos, n);
}
static Value f_readline(int argc, Value *argv, int nkw, Str **kn, Value *kv)
{
    (void)nkw; (void)kn; (void)kv;
    Inst *in = self_of(argc, argv); if (!in || !open_check(in)) return v_none();
    if (as_strcmp(((Str *)attr(in, s_mode).u.p)->s, "r")) return rt_err("OSError", "the file is not open for reading");
    Str *b = attr(in, s_buf).u.p; int pos = attr(in, s_pos).u.i, e = pos;
    while (e < b->len && b->s[e] != '\n') e++;
    if (e < b->len) e++;
    dict_sets(&in->attrs, s_pos, v_int(e));
    return v_str(b->s + pos, e - pos);
}
static Value f_readlines(int argc, Value *argv, int nkw, Str **kn, Value *kv)
{
    Inst *in = self_of(argc, argv); if (!in) return v_none();
    Seq *q = seq_new(8);
    for (;;) {
        Value l = f_readline(argc, argv, nkw, kn, kv); if (I.has_exc) return v_none();
        if (!((Str *)l.u.p)->len) break;
        seq_push(q, l);
    }
    return v_ptr(T_LIST, q);
}
static Value f_write(int argc, Value *argv, int nkw, Str **kn, Value *kv)
{
    (void)nkw; (void)kn; (void)kv;
    Inst *in = self_of(argc, argv); if (!in || !open_check(in)) return v_none();
    if (argc < 2 || argv[1].t != T_STR) return rt_err("TypeError", "write() needs text");
    const char *m = ((Str *)attr(in, s_mode).u.p)->s;
    if (!as_strcmp(m, "r")) return rt_err("OSError", "the file is not open for writing");
    Str *t = argv[1].u.p;
    if (as_h_file_put(((Str *)attr(in, s_name).u.p)->s, t->s, 1) < 0) return rt_err("OSError", "cannot write: a file can hold at most 4096 bytes, and you must be signed in");
    return v_int(t->len);
}
static Value f_close(int argc, Value *argv, int nkw, Str **kn, Value *kv)
{
    (void)nkw; (void)kn; (void)kv;
    Inst *in = self_of(argc, argv); if (!in) return v_none();
    dict_sets(&in->attrs, s_closed, v_bool(1));
    return v_none();
}
static Value f_enter(int argc, Value *argv, int nkw, Str **kn, Value *kv) { (void)nkw; (void)kn; (void)kv; return argc ? argv[0] : v_none(); }

Value file_open(int argc, Value *argv, int nkw, Str **kn, Value *kv)
{
    (void)nkw; (void)kn; (void)kv;
    if (argc < 1 || argv[0].t != T_STR) return rt_err("TypeError", "open(name, mode) needs the file name as text");
    const char *mode = "r";
    if (argc >= 2) { if (argv[1].t != T_STR) return rt_err("TypeError", "the mode must be text: \"r\", \"w\" or \"a\""); mode = ((Str *)argv[1].u.p)->s; }
    if (as_strcmp(mode, "r") && as_strcmp(mode, "w") && as_strcmp(mode, "a")) return rt_err("ValueError", "unsupported mode: use \"r\", \"w\" or \"a\"");
    if (!fcls) {
        s_name = as_intern("name", 4); s_mode = as_intern("mode", 4); s_buf = as_intern("_buf", 4); s_pos = as_intern("_pos", 4); s_closed = as_intern("closed", 6);
        fcls = class_new(as_intern("file", 4), 0).u.p;
        struct { Builtin *b; const char *n; BuiltinFn f; } t[] = { { &m_read, "read", f_read }, { &m_readline, "readline", f_readline }, { &m_readlines, "readlines", f_readlines },
            { &m_write, "write", f_write }, { &m_close, "close", f_close }, { &m_enter, "enter", f_enter }, { &m_exit, "exit", f_close } };
        for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) { t[i].b->name = t[i].n; t[i].b->fn = t[i].f; dict_sets(&fcls->attrs, as_intern(t[i].n, (int)as_strlen(t[i].n)), v_ptr(T_BUILTIN, t[i].b)); }
    }
    Str *nm = argv[0].u.p;
    Value buf = v_cstr("");
    if (!as_strcmp(mode, "r")) {
        static char tmp[4200];
        int n = as_h_file_read(nm->s, tmp, sizeof tmp - 1);
        if (n < 0) return rt_errf("FileNotFoundError", "cannot open the file '%s' (it does not exist, or you are not signed in)", nm->s);
        buf = v_str(tmp, n);
    } else if (!as_strcmp(mode, "w")) {
        if (as_h_file_put(nm->s, "", 0) < 0) return rt_errf("OSError", "cannot create the file '%s'", nm->s);
    } else {
        static char probe[4];
        if (as_h_file_read(nm->s, probe, 1) < 0 && as_h_file_put(nm->s, "", 0) < 0) return rt_errf("OSError", "cannot create the file '%s'", nm->s);
    }
    Inst *in = as_alloc(sizeof(Inst));
    in->cls = fcls; in->attrs.cap = 8; in->attrs.k = as_alloc(8 * sizeof(Value)); in->attrs.v = as_alloc(8 * sizeof(Value));
    dict_sets(&in->attrs, s_name, argv[0]); dict_sets(&in->attrs, s_mode, v_cstr(mode)); dict_sets(&in->attrs, s_buf, buf);
    dict_sets(&in->attrs, s_pos, v_int(0)); dict_sets(&in->attrs, s_closed, v_bool(0));
    return v_ptr(T_INST, in);
}
