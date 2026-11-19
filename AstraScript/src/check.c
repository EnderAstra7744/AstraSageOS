/* AstraScript engine 2 - check.c: the editor's static check. Finds names that are never defined and invalid AST commands
 * without running the program. Conservative on purpose: a name counts as defined when it is bound anywhere in the program
 * (no scope analysis), so a reported name is really never defined, and it is skipped entirely when 'use Package;' is present. */
#include "as_types.h"

static Str **bound; static int nbound, cbound;
static int has_use, nerr;
static int last_err_line;

static void bind(Str *s)
{
    for (int i = 0; i < nbound; i++) if (bound[i] == s) return;
    if (nbound == cbound) { int nc = cbound ? cbound * 2 : 64; Str **nb = as_alloc((unsigned)nc * sizeof(Str *)); if (nbound) as_memcpy(nb, bound, (unsigned)nbound * sizeof(Str *)); bound = nb; cbound = nc; }
    bound[nbound++] = s;
}
static int is_bound(Str *s)
{
    for (int i = 0; i < nbound; i++) if (bound[i] == s) return 1;
    return env_find_here(I.builtins, s) != 0;
}

static void walk(Node *n, int mode);
static void bind_target(Node *t, int mode)
{
    if (!t) return;
    if (t->k == N_NAME) { if (mode == 0) bind(t->s); return; }
    if (t->k == N_TUPLE || t->k == N_LIST) { for (int i = 0; i < t->n; i++) bind_target(t->list[i], mode); return; }
    if (t->k == N_STAR) { bind_target(t->a, mode); return; }
    walk(t, mode);                                              /* a.b = / a[i] = : the object is used */
}

static void report(Node *n, Str *name)
{
    if (n->line == last_err_line) return;
    last_err_line = n->line; nerr++;
    char m[60]; as_fmt(m, sizeof m, "Variable not found: %s", name->s);
    as_h_error(n->line, m);
}

static void walk(Node *n, int mode)
{
    if (!n) return;
    switch (n->k) {
    case N_NAME: if (mode == 1 && !has_use && !is_bound(n->s)) report(n, n->s); return;
    case N_CONST: return;
    case S_ASSIGN: bind_target(n->a, mode); walk(n->b, mode); return;
    case S_VAR: if (mode == 0) bind(n->s); walk(n->a, mode); return;
    case S_FOR: bind_target(n->a, mode); walk(n->b, mode); walk(n->c, mode); return;
    case S_FUNC: case S_CLASS:
        if (mode == 0) { bind(n->s); for (int i = 0; i < n->np; i++) bind(n->params[i].name); }
        for (int i = 0; i < n->np; i++) walk(n->params[i].def, mode);
        break;
    case N_LAMBDA:
        if (mode == 0) for (int i = 0; i < n->np; i++) bind(n->params[i].name);
        for (int i = 0; i < n->np; i++) walk(n->params[i].def, mode);
        break;
    case N_COMP:
        for (int i = 0; i < n->n; i++) { Node *c = n->list[i]; if (c->flag == 0) bind_target(c->a, mode); }
        break;
    case S_IMPORT: if (mode == 0) bind(n->d ? (Str *)n->d->v.u.p : n->s); return;
    case S_FROM: if (mode == 0) for (int i = 0; i < n->n; i++) bind(n->names[2 * i + 1]); return;
    case S_USE: has_use = 1; return;
    case S_GLOBAL: case S_NONLOCAL: if (mode == 0) for (int i = 0; i < n->n; i++) bind(n->names[i]); return;
    case S_TRY:
        if (mode == 0) for (int i = 0; i < n->n; i++) if (n->names && n->names[i]) bind(n->names[i]);
        break;
    case S_WITH: if (mode == 0 && n->s) bind(n->s); break;
    case N_CALL: {
        Node *cn = n->a;
        if (cn && cn->k == N_NAME) {
            const char *nm = cn->s->s; int outs = as_builtin_outs(nm, n->n);
            if (outs) {                                         /* legacy out-parameters: the first arguments are written to */
                for (int i = 0; i < outs && i < n->n; i++) if (n->list[i]->k == N_NAME) { if (mode == 0) bind(n->list[i]->s); } else walk(n->list[i], mode);
                for (int i = outs; i < n->n; i++) walk(n->list[i], mode);
                for (int i = 0; i < n->n2; i++) walk(n->list2[i], mode);
                return;
            }
            if (mode == 1 && !as_strcmp(nm, "__as_cmd") && n->n == 1 && n->list[0]->k == N_CONST) {
                char line[260], msg[80];
                as_fmt(line, sizeof line, "as %s", ((Str *)n->list[0]->v.u.p)->s);
                if (as_h_check_command(line, msg, sizeof msg) && n->line != last_err_line) { last_err_line = n->line; nerr++; as_h_error(n->line, msg); }
                return;
            }
        }
        break;
    }
    default: break;
    }
    walk(n->a, mode); walk(n->b, mode); walk(n->c, mode); walk(n->d, mode);
    for (int i = 0; i < n->n; i++) walk(n->list[i], mode);
    for (int i = 0; i < n->n2; i++) walk(n->list2[i], mode);
}

/* returns the number of problems reported */
static const char *ctx_text; static unsigned ctx_len;
void as_check_context(const char *t, unsigned n) { ctx_text = t; ctx_len = n; }

static int idc(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }
static void bind_context(void)                                  /* names defined by the packages / files this one is checked together with */
{
    for (unsigned i = 0; i < ctx_len; i++) {
        if (i && idc(ctx_text[i - 1])) continue;
        unsigned k = 0;
        if (i + 5 <= ctx_len && !as_strncmp(ctx_text + i, "func ", 5)) k = i + 5;
        else if (i + 6 <= ctx_len && !as_strncmp(ctx_text + i, "class ", 6)) k = i + 6;
        if (!k) continue;
        while (k < ctx_len && ctx_text[k] == ' ') k++;
        unsigned e = k; while (e < ctx_len && idc(ctx_text[e])) e++;
        if (e > k) bind(as_intern(ctx_text + k, (int)(e - k)));
    }
}

int as_static_check(Node *prog)
{
    bound = 0; nbound = cbound = 0; has_use = 0; nerr = 0; last_err_line = -1;
    bind_context();
    walk(prog, 0);
    walk(prog, 1);
    return nerr;
}
