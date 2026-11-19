/* AstraScript engine 2 - values, objects, syntax tree and the functions the modules share. */
#ifndef AS_TYPES_H
#define AS_TYPES_H

#include "as_memory.h"
#include "as_host.h"

typedef enum {
    T_NONE = 0, T_BOOL, T_INT, T_FLOAT, T_STR, T_BYTES, T_BARRAY, T_LIST, T_TUPLE, T_SET, T_FSET, T_DICT, T_RANGE,
    T_FUNC, T_BUILTIN, T_CLASS, T_INST, T_BOUND, T_BMETH, T_MODULE, T_SUPER, T_DESC, T_GEN
} VType;

typedef struct Value { unsigned char t; union { int i; double d; void *p; } u; } Value;

typedef struct Str { int len; char s[4]; } Str;                       /* NUL terminated, also used for bytes */
typedef struct Seq { int n, cap; Value *a; } Seq;                     /* list, tuple, set, frozenset */
typedef struct Dict { int n, cap; Value *k, *v; } Dict;               /* insertion ordered */
typedef struct Env { struct Env *parent; int n, cap; Str **k; Value *v; Str **gl; int ngl; Str **nl; int nnl; } Env;
typedef struct Range { int a, b, c; } Range;

struct Node; struct Class;
typedef struct Param { Str *name; struct Node *def; unsigned char kind; } Param;   /* kind: 0 normal, 1 *args, 2 **kwargs, 3 keyword-only */
typedef struct Func { struct Node *def; Env *clo; Str *name; Value *defv; struct Class *cls; } Func;
typedef Value (*BuiltinFn)(int argc, Value *argv, int nkw, Str **kn, Value *kv);
typedef struct Builtin { const char *name; BuiltinFn fn; int outs; int nin; int inout; } Builtin;   /* nin>0: out form only when exactly outs+nin args; inout: the out names are also passed in. outs: results written to the first N argument names (legacy gui_wait(k, ms)) */
typedef struct Class { Str *name; struct Class *base; Dict attrs; int exc; } Class;
typedef struct Inst { Class *cls; Dict attrs; } Inst;
typedef struct Bound { Value self, fn; } Bound;
typedef struct BMeth { Value self; Str *name; } BMeth;
typedef struct Module { Str *name; Env *env; } Module;
typedef struct Desc { int kind; Value f, setter; } Desc;       /* kind 1 classmethod, 2 staticmethod, 3 property (class attribute wrappers) */
typedef struct Gen { Seq *items; int pos; } Gen;                  /* generators run eagerly: the yielded values are collected when the function is called */
typedef struct Super { Value self; Class *from; } Super;

/* ------------------------------------------------------------------ syntax tree */
enum {
    /* expressions */
    N_CONST, N_NAME, N_BIN, N_UN, N_AND, N_OR, N_NOT, N_CMP, N_CALL, N_ATTR, N_INDEX, N_SLICE, N_LIST, N_TUPLE, N_SET, N_DICT,
    N_LAMBDA, N_COND, N_FSTR, N_CONCAT, N_STAR, N_DSTAR, N_KWARG, N_CHAIN, N_COMP,
    /* statements */
    S_EXPR, S_ASSIGN, S_AUG, S_VAR, S_IF, S_WHILE, S_FOR, S_CFOR, S_MATCH, S_FUNC, S_CLASS, S_RETURN, S_BREAK, S_CONTINUE, S_PASS,
    S_GLOBAL, S_NONLOCAL, S_ASSERT, S_RAISE, S_TRY, S_WITH, S_IMPORT, S_FROM, S_BLOCK, S_START, S_END, S_YIELD, S_USE
};

typedef struct Node {
    unsigned char k, op, flag;
    int line;
    Str *s;                    /* name, text */
    Value v;                   /* constant */
    struct Node *a, *b, *c, *d;
    struct Node **list; int n; /* children */
    struct Node **list2; int n2;
    Param *params; int np;
    Str **names;               /* keyword names of a call, imported names */
} Node;

/* ------------------------------------------------------------------ interpreter state */
typedef struct Frame { Func *fn; Value self; int line; } Frame;
typedef struct Interp {
    Env *globals, *builtins, *cur, *class_env;
    Value exc; int has_exc;        /* a pending error (an Inst of an Error class) */
    Value ret;
    int depth, line, errors;
    Frame frames[64];
    unsigned long steps;
    Seq *gen;                      /* the list that 'yield' appends to while a generator function runs */
} Interp;
extern Interp I;

/* ------------------------------------------------------------------ lexer.c */
enum { K_EOF, K_NUM_I, K_NUM_F, K_STR, K_BSTR, K_FSTR, K_NAME, K_OP, K_DIRECT };
typedef struct Token { unsigned char k, aux; unsigned short line; union { Str *s; int i; double d; } u; } Token;
Token *as_lex(const char *src, unsigned len, int *ntok, int line0);       /* NULL after reporting an error */
Str  *as_intern(const char *s, int len);
extern int as_lex_error_line; extern char as_lex_error[96];
extern int as_parse_error_line; extern char as_parse_error[96];

/* ------------------------------------------------------------------ parser.c */
extern int as_relaxed;
Node *as_parse(Token *t, int n, const char *what);                        /* returns an S_BLOCK, NULL after reporting an error */
Node *as_parse_expr_text(const char *s, int len, int line);

/* ------------------------------------------------------------------ types.c */
Value v_none(void); Value v_bool(int b); Value v_int(int i); Value v_float(double d);
Value v_ptr(int t, void *p);
Str  *str_new(const char *s, int len);
Value v_str(const char *s, int len); Value v_cstr(const char *s);
Seq  *seq_new(int cap); void seq_push(Seq *q, Value v);
Dict *dict_new(void); Value *dict_get(Dict *d, Value key); void dict_set(Dict *d, Value k, Value v); int dict_del(Dict *d, Value k);
Value *dict_gets(Dict *d, Str *name);                                       /* by interned name */
void  dict_sets(Dict *d, Str *name, Value v);
Env  *env_new(Env *parent); Value *env_find(Env *e, Str *name); Value *env_find_here(Env *e, Str *name); void env_set(Env *e, Str *name, Value v);
int   v_truthy(Value v); int v_eq(Value a, Value b); int v_is(Value a, Value b);
int   v_cmp(Value a, Value b, int *ok);                                      /* -1 0 1 for numbers, strings, sequences */
const char *v_typename(Value v);
double v_num(Value v);                                                       /* int / float / bool as double */
int   v_isnum(Value v);

typedef struct Buf { char *s; int n, cap; } Buf;
void buf_init(Buf *b); void buf_add(Buf *b, const char *s, int len); void buf_addc(Buf *b, const char *s); void buf_putc(Buf *b, char c);
void v_repr(Value v, Buf *b, int quote); Value v_to_str(Value v);          /* str(v) */
void fmt_double(double d, Buf *b);
void fmt_int(int i, Buf *b);

static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ------------------------------------------------------------------ errors.c */
Value rt_err(const char *type, const char *msg);                            /* sets I.exc and returns None */
Value rt_errf(const char *type, const char *fmt, const char *a);
void  errors_init(void);
Class *err_class(const char *name);
int   exc_matches(Value exc, Class *c);
Value exc_message(Value exc);

/* ------------------------------------------------------------------ strings.c, collections.c, classes.c, functions.c */
Value str_method(Value self, Str *name, int argc, Value *argv, int *found);
Value str_index(Value s, Value idx); Value str_slice(Value s, int lo, int hi, int step, int has_lo, int has_hi);
Value seq_method(Value self, Str *name, int argc, Value *argv, int *found);
Value dict_method(Value self, Str *name, int argc, Value *argv, int *found);
Value set_method(Value self, Str *name, int argc, Value *argv, int *found);
Value seq_index(Value s, Value idx); Value seq_slice(Value s, int lo, int hi, int step, int has_lo, int has_hi);
Value seq_from_iter(int type, Value src);                                    /* list(x), tuple(x), set(x) */
int   iter_items(Value v, Value **out, int *n);                              /* materialise anything iterable; 0 = not iterable (error set) */
void  seq_sort(Seq *q, Value key, int reverse);
Value class_new(Str *name, Class *base);
Value instantiate(Class *c, int argc, Value *argv, int nkw, Str **kn, Value *kv);
Value get_attr(Value obj, Str *name);                                        /* sets I.exc on failure */
void  set_attr(Value obj, Str *name, Value v);
int   class_isa(Class *c, Class *base);
Value call_value(Value f, int argc, Value *argv, int nkw, Str **kn, Value *kv);
Value call_func(Func *f, Value self, int has_self, int argc, Value *argv, int nkw, Str **kn, Value *kv);
Value make_func(Node *def, Env *clo, Class *cls);

/* ------------------------------------------------------------------ interpreter.c, builtins.c, modules.c */
Value eval(Node *n);
int   exec_block(Node *blk);                                                 /* 0 ok, 1 break, 2 continue, 3 return, 4 exception */
int   exec_stmt(Node *n);
Value binop(int op, Value a, Value b);
void  assign_name(Str *name, Value v);                                       /* python-like: local unless it exists in an outer scope of a global/nonlocal decl */
void  assign_out(Str *name, Value v);                                        /* legacy out-parameters: set where found, else a global */
int   as_builtin_outs(const char *name, int nargs);
void  builtins_init(void);
Value builtin_type_of(Value v);
void  rt_print_exc_top(void);
int   import_module(Str *name, Value *out);                                  /* modules.c */
Value fstring_eval(Node *n);
Value exec_import(Node *n);
Value call_method(Value self, Str *name, int argc, Value *argv, int nkw, Str **kn, Value *kv);
int   contains(Value container, Value item, int *err);
void  hoist(Node *blk);
void  as_check_context(const char *t, unsigned n);
int   as_static_check(Node *prog);                                         /* check.c: undefined names, invalid AST commands */
int   as_run_block_text(const char *src, int len, Env *env, const char *label);
Value bytes_new(int type, const char *s, int len);

#endif
