/* AstraScript engine 2 - parser.c: tokens -> syntax tree (recursive descent, Python-like expressions, brace blocks). */
#include "as_types.h"

int as_parse_error_line;
char as_parse_error[96];

static struct { Token *t; int pos, n; } P;
static void *pbuf[32];
static int in_parse;
int as_relaxed;                                /* 1: a line break also ends a statement (files without 'start;') */
static int yield_flag;                         /* set when the function being parsed contains 'yield' */

static void perr(int line, const char *msg)
{
    as_parse_error_line = line;
    as_fmt(as_parse_error, sizeof as_parse_error, "%s", msg);
    __builtin_longjmp(pbuf, 1);
}

static Token *cur(void) { return &P.t[P.pos]; }
static Token *peekn(int k) { int p = P.pos + k; return p < P.n ? &P.t[p] : &P.t[P.n - 1]; }
static void adv(void) { if (P.t[P.pos].k != K_EOF) P.pos++; }
static int cline(void) { return cur()->line; }

static int is_op(Token *t, const char *op) { return t->k == K_OP && !as_strcmp(t->u.s->s, op); }
static int at_op(const char *op) { return is_op(cur(), op); }
static int is_nm(Token *t, const char *nm) { return t->k == K_NAME && !as_strcmp(t->u.s->s, nm); }
static int at_nm(const char *nm) { return is_nm(cur(), nm); }
static int eat_op(const char *op) { if (at_op(op)) { adv(); return 1; } return 0; }
static int eat_nm(const char *nm) { if (at_nm(nm)) { adv(); return 1; } return 0; }
static void need_op(const char *op, const char *msg) { if (!eat_op(op)) perr(P.pos > 0 && !at_op("}") ? cline() : cline(), msg); }

static Node *mk(int k, int line) { Node *n = as_alloc(sizeof(Node)); n->k = (unsigned char)k; n->line = line; return n; }

typedef struct { Node **a; int n, cap; } NL;
static void nl_push(NL *l, Node *x)
{
    if (l->n == l->cap) {
        int nc = l->cap ? l->cap * 2 : 4;
        Node **na = as_alloc((unsigned)nc * sizeof(Node *));
        if (l->n) as_memcpy(na, l->a, (unsigned)l->n * sizeof(Node *));
        l->a = na; l->cap = nc;
    }
    l->a[l->n++] = x;
}

static Node *expr(void);
static Node *stmt(void);
static Node *block(void);

static const char *const RESERVED[] = {
    "if", "elif", "else", "and", "or", "not", "in", "is", "for", "fr", "while", "whle", "lambda", "as", "func", "def", "class", "return",
    "mtch", "cse", "ask", "error_for", "with", "import", "from", "end", "vr", 0 };
static int reserved(Token *t)
{
    if (t->k != K_NAME) return 0;
    for (int i = 0; RESERVED[i]; i++) if (!as_strcmp(t->u.s->s, RESERVED[i])) return 1;
    return 0;
}

/* ------------------------------------------------------------------ expressions */
static Node *const_str(Str *s, int line) { Node *n = mk(N_CONST, line); n->v = v_ptr(T_STR, s); return n; }

static Node *parse_sub_expr(const char *s, int len, int line)
{
    int nt = 0;
    Token *t = as_lex(s, (unsigned)len, &nt, line);
    if (!t) perr(as_lex_error_line ? as_lex_error_line : line, as_lex_error);
    Token *st = P.t; int sp = P.pos, sn = P.n;
    P.t = t; P.pos = 0; P.n = nt;
    Node *e = expr();
    if (cur()->k != K_EOF) perr(line, "Unexpected text inside the string value.");
    P.t = st; P.pos = sp; P.n = sn;
    return e;
}

static Node *fstring(Token *tk)
{
    Node *n = mk(N_FSTR, tk->line);
    n->flag = tk->aux;
    NL parts = {0};
    Str *raw = tk->u.s;
    Buf lit; buf_init(&lit);
    int i = 0;
    while (i < raw->len) {
        char c = raw->s[i];
        if (c == '{' && i + 1 < raw->len && raw->s[i + 1] == '{') { buf_putc(&lit, '{'); i += 2; continue; }
        if (c == '}' && i + 1 < raw->len && raw->s[i + 1] == '}') { buf_putc(&lit, '}'); i += 2; continue; }
        int open = 0, close = 0;
        if (c == '{') { open = 1; int d = 0, j = i + 1; for (; j < raw->len; j++) { if (raw->s[j] == '{') d++; else if (raw->s[j] == '}') { if (!d) break; d--; } else if (raw->s[j] == '"' || raw->s[j] == '\'') { char q = raw->s[j]; j++; while (j < raw->len && raw->s[j] != q) j++; } } close = j; if (j >= raw->len) perr(tk->line, "A '{' in the text value is not closed."); }
        else if (c == '*' && i + 1 < raw->len && raw->s[i + 1] == '/') { open = 2; int d = 0, j = i + 2; for (; j < raw->len; j++) { if (raw->s[j] == '(' || raw->s[j] == '[') d++; else if (raw->s[j] == ')' || raw->s[j] == ']') d--; else if (raw->s[j] == '"') { j++; while (j < raw->len && raw->s[j] != '"') j++; } else if (raw->s[j] == '*' && d <= 0) break; } close = j; if (j >= raw->len) perr(tk->line, "A '*/' in the text value needs a closing '*'."); }
        if (open) {
            if (lit.n) { nl_push(&parts, const_str(as_intern(lit.s, lit.n), tk->line)); buf_init(&lit); }
            int st = i + (open == 1 ? 1 : 2);
            nl_push(&parts, parse_sub_expr(raw->s + st, close - st, tk->line));
            i = close + 1;
            continue;
        }
        buf_putc(&lit, c); i++;
    }
    if (lit.n || !parts.n) nl_push(&parts, const_str(as_intern(lit.s, lit.n), tk->line));
    n->list = parts.a; n->n = parts.n;
    return n;
}

static void parse_params(Node *n, const char *close);

static Node *parse_args_into(Node *call)
{
    NL args = {0}, kvals = {0};
    Str *kn[32]; int nk = 0;
    while (!at_op(")")) {
        if (cur()->k == K_EOF) perr(cline(), "Missing ')' after the arguments.");
        if (at_op("*") || at_op("**")) {
            int two = at_op("**"); int line = cline(); adv();
            Node *s = mk(two ? N_DSTAR : N_STAR, line); s->a = expr(); nl_push(&args, s);
        } else if (cur()->k == K_NAME && is_op(peekn(1), "=") && (!reserved(cur()) || !as_strcmp(cur()->u.s->s, "end"))) {
            if (nk >= 32) perr(cline(), "Too many keyword arguments.");
            kn[nk++] = cur()->u.s; adv(); adv();
            nl_push(&kvals, expr());
        } else {
            Node *e = expr();
            nl_push(&args, e);
        }
        if (!eat_op(",")) break;
    }
    need_op(")", "Missing ')' after the arguments.");
    call->list = args.a; call->n = args.n;
    call->list2 = kvals.a; call->n2 = kvals.n;
    if (nk) { call->names = as_alloc((unsigned)nk * sizeof(Str *)); as_memcpy(call->names, kn, (unsigned)nk * sizeof(Str *)); }
    return call;
}

static Node *or_expr(void);
static Node *bitor_(void);
static int is_target(Node *n);
static void comp_clauses(Node *c)
{
    NL cl = {0};
    while (at_nm("for") || at_nm("fr")) {
        int line = cline(); adv();
        Node *t = bitor_();
        if (at_op(",")) { NL tl = {0}; nl_push(&tl, t); while (eat_op(",")) nl_push(&tl, bitor_()); Node *tp = mk(N_TUPLE, line); tp->list = tl.a; tp->n = tl.n; t = tp; }
        if (!is_target(t)) perr(line, "A name was expected after 'for' inside [ ] / { }.");
        if (!eat_nm("in")) perr(line, "'for' inside [ ] / { } needs 'in'.");
        Node *f = mk(N_COMP, line); f->flag = 0; f->a = t; f->b = or_expr(); nl_push(&cl, f);
        while (at_nm("if")) { adv(); Node *g = mk(N_COMP, line); g->flag = 1; g->a = or_expr(); nl_push(&cl, g); }
    }
    c->list = cl.a; c->n = cl.n;
}

static Node *primary(void)
{
    Token *t = cur();
    int line = t->line;
    switch (t->k) {
    case K_NUM_I: { Node *n = mk(N_CONST, line); n->v = v_int(t->u.i); adv(); return n; }
    case K_NUM_F: { Node *n = mk(N_CONST, line); n->v = v_float(t->u.d); adv(); return n; }
    case K_STR: { Node *n = const_str(t->u.s, line); adv(); return n; }
    case K_BSTR: { Node *n = mk(N_CONST, line); n->v = bytes_new(T_BYTES, t->u.s->s, t->u.s->len); adv(); return n; }
    case K_FSTR: { Node *n = fstring(t); adv(); return n; }
    case K_NAME: {
        const char *s = t->u.s->s;
        if (!as_strcmp(s, "True") || !as_strcmp(s, "true")) { Node *n = mk(N_CONST, line); n->v = v_bool(1); adv(); return n; }
        if (!as_strcmp(s, "False") || !as_strcmp(s, "false")) { Node *n = mk(N_CONST, line); n->v = v_bool(0); adv(); return n; }
        if (!as_strcmp(s, "None") || !as_strcmp(s, "none")) { Node *n = mk(N_CONST, line); n->v = v_none(); adv(); return n; }
        if (!as_strcmp(s, "lambda")) {
            adv();
            Node *n = mk(N_LAMBDA, line);
            parse_params(n, ":");
            need_op(":", "A lambda needs ':' before its expression.");
            n->a = expr();
            return n;
        }
        if (reserved(t)) { char m[80]; as_fmt(m, sizeof m, "Unexpected '%s'.", s); perr(line, m); }
        Node *n = mk(N_NAME, line); n->s = t->u.s; adv(); return n;
    }
    case K_OP: {
        if (at_op("(")) {
            adv();
            if (eat_op(")")) { Node *n = mk(N_TUPLE, line); return n; }
            Node *e = expr();
            if (at_op(",")) {
                NL l = {0}; nl_push(&l, e);
                while (eat_op(",")) { if (at_op(")")) break; nl_push(&l, expr()); }
                need_op(")", "Missing ')'.");
                Node *n = mk(N_TUPLE, line); n->list = l.a; n->n = l.n; return n;
            }
            need_op(")", "Missing ')'.");
            return e;
        }
        if (at_op("[")) {
            adv();
            NL l = {0};
            while (!at_op("]")) {
                if (cur()->k == K_EOF) perr(line, "Missing ']'.");
                nl_push(&l, expr());
                if (l.n == 1 && (at_nm("for") || at_nm("fr"))) {          /* [x * x for x in items if cond] */
                    Node *c = mk(N_COMP, line); c->op = 'l'; c->a = l.a[0]; comp_clauses(c);
                    need_op("]", "Missing ']' after the list comprehension."); return c;
                }
                if (!eat_op(",")) break;
            }
            need_op("]", "Missing ']'.");
            Node *n = mk(N_LIST, line); n->list = l.a; n->n = l.n; return n;
        }
        if (at_op("{")) {
            adv();
            NL ks = {0}, vs = {0};
            int is_dict = 0, is_set = 0;
            while (!at_op("}")) {
                if (cur()->k == K_EOF) perr(line, "Missing '}'.");
                Node *k = expr();
                if (eat_op(":")) { is_dict = 1; nl_push(&ks, k); nl_push(&vs, expr()); }
                else { is_set = 1; nl_push(&ks, k); }
                if (ks.n == 1 && (at_nm("for") || at_nm("fr"))) {          /* {x for ...}  and  {k: v for ...} */
                    Node *c = mk(N_COMP, line); c->op = is_dict ? 'd' : 's'; c->a = ks.a[0]; c->b = is_dict ? vs.a[0] : 0; comp_clauses(c);
                    need_op("}", "Missing '}' after the comprehension."); return c;
                }
                if (!eat_op(",")) break;
            }
            need_op("}", "Missing '}'.");
            if (is_dict && is_set) perr(line, "A {} value is either a dictionary (key: value) or a set.");
            Node *n = mk(is_set ? N_SET : N_DICT, line); n->list = ks.a; n->n = ks.n; n->list2 = vs.a; n->n2 = vs.n; return n;
        }
        break;
    }
    default: break;
    }
    if (t->k == K_EOF) perr(line, "The expression is not finished.");
    { char m[60]; as_fmt(m, sizeof m, "Unexpected '%s'.", t->u.s->s); perr(line, m); }
    return 0;
}

static Node *postfix_core(int allow_cat)
{
    Node *e = primary();
    for (;;) {
        int line = cline();
        if (at_op("(")) { adv(); Node *c = mk(N_CALL, line); c->a = e; parse_args_into(c); e = c; continue; }
        if (at_op(".")) {
            adv();
            if (cur()->k != K_NAME) perr(line, "A name must follow the '.'.");
            Node *a = mk(N_ATTR, line); a->a = e; a->s = cur()->u.s; adv(); e = a; continue;
        }
        if (at_op("[")) {
            adv();
            Node *lo = 0, *hi = 0, *st = 0; int slice = 0;
            if (!at_op(":")) lo = expr();
            if (eat_op(":")) {
                slice = 1;
                if (!at_op("]") && !at_op(":")) hi = expr();
                if (eat_op(":")) { if (!at_op("]")) st = expr(); }
            }
            need_op("]", "Missing ']'.");
            Node *x = mk(slice ? N_SLICE : N_INDEX, line); x->a = e; x->b = lo; x->c = hi; x->d = st; e = x; continue;
        }
        if (allow_cat && P.pos > 0 && cur()->line == P.t[P.pos - 1].line) {
            int strish = e->k == N_FSTR || e->k == N_CONCAT || (e->k == N_CONST && e->v.t == T_STR);
            if (cur()->k == K_STR || cur()->k == K_FSTR || (strish && cur()->k == K_NAME && !reserved(cur()))) {
                Node *c = mk(N_CONCAT, line); c->a = e; c->b = postfix_core(0); e = c; continue;      /* "Red text" Color.RESET  /  Color.RED "text" */
            }
        }
        break;
    }
    return e;
}
static Node *postfix(void) { return postfix_core(1); }

static Node *power(void);
static Node *unary(void)
{
    int line = cline();
    if (at_op("-") || at_op("+") || at_op("~")) {
        char o = cur()->u.s->s[0]; adv();
        Node *n = mk(N_UN, line); n->op = (unsigned char)o; n->a = unary();
        if (n->a->k == N_CONST && o == '-' && n->a->v.t == T_INT && n->a->v.u.i != (int)0x80000000) { n->a->v.u.i = -n->a->v.u.i; return n->a; }
        if (n->a->k == N_CONST && o == '-' && n->a->v.t == T_FLOAT) { n->a->v.u.d = -n->a->v.u.d; return n->a; }
        return n;
    }
    return power();
}
static Node *power(void)
{
    Node *b = postfix();
    if (at_op("**")) { int line = cline(); adv(); Node *n = mk(N_BIN, line); n->op = 'P'; n->a = b; n->b = unary(); return n; }
    return b;
}

/* binary operator codes: + - * / // % ** (P) << (L) >> (R) & | ^ ; comparison codes: = != < > l(<=) g(>=) */
static Node *bin_level(Node *(*sub)(void), const char *const *ops, const char *codes)
{
    Node *l = sub();
    for (;;) {
        int hit = -1;
        for (int i = 0; ops[i]; i++) if (at_op(ops[i])) { hit = i; break; }
        if (hit < 0) return l;
        int line = cline(); adv();
        Node *n = mk(N_BIN, line); n->op = (unsigned char)codes[hit]; n->a = l; n->b = sub(); l = n;
        if (n->a->k == N_CONST && n->b->k == N_CONST && v_isnum(n->a->v) && v_isnum(n->b->v) && n->op != '/' && n->op != 'D' && n->op != '%' && n->op != 'P') {
            /* constant folding: 2 + 3 is 5 before the program runs (only the safe operators; errors stay runtime errors) */
            Value r = binop(n->op, n->a->v, n->b->v);
            if (!I.has_exc) { Node *c = mk(N_CONST, line); c->v = r; l = c; } else I.has_exc = 0;
        }
    }
}
static Node *term(void) { static const char *const o[] = { "*", "/", "//", "%", 0 }; return bin_level(unary, o, "*/D%"); }
static Node *arith(void) { static const char *const o[] = { "+", "-", 0 }; return bin_level(term, o, "+-"); }
static Node *shift(void) { static const char *const o[] = { "<<", ">>", 0 }; return bin_level(arith, o, "LR"); }
static Node *bitand(void) { static const char *const o[] = { "&", 0 }; return bin_level(shift, o, "&"); }
static Node *bitxor(void) { static const char *const o[] = { "^", 0 }; return bin_level(bitand, o, "^"); }
static Node *bitor_(void) { static const char *const o[] = { "|", 0 }; return bin_level(bitxor, o, "|"); }

static int cmp_op(void)
{
    if (at_op("==")) return '='; if (at_op("!=")) return '!'; if (at_op("<=")) return 'l'; if (at_op(">=")) return 'g';
    if (at_op("<")) return '<'; if (at_op(">")) return '>';
    if (at_nm("in")) return 'i';
    if (at_nm("not") && is_nm(peekn(1), "in")) return 'n';
    if (at_nm("is")) return is_nm(peekn(1), "not") ? 'N' : 's';
    return 0;
}
static Node *comparison(void)
{
    Node *l = bitor_();
    int line = cline();
    int op = cmp_op();
    if (!op) return l;
    NL operands = {0}; char ops[32]; int nops = 0;
    nl_push(&operands, l);
    while (op) {
        if (nops >= 30) perr(line, "Too many comparisons in one expression.");
        adv();
        if (op == 'n' || op == 'N') adv();
        ops[nops++] = (char)op;
        nl_push(&operands, bitor_());
        op = cmp_op();
    }
    if (nops == 1) { Node *n = mk(N_CMP, line); n->op = (unsigned char)ops[0]; n->a = operands.a[0]; n->b = operands.a[1]; return n; }
    Node *n = mk(N_CHAIN, line);                         /* a < b < c: every operand is evaluated once, stops at the first false link */
    n->list = operands.a; n->n = operands.n; n->s = str_new(ops, nops);
    return n;
}
static Node *not_expr(void)
{
    if (at_nm("not") && !is_nm(peekn(1), "in")) { int line = cline(); adv(); Node *n = mk(N_NOT, line); n->a = not_expr(); return n; }
    return comparison();
}
static Node *and_expr(void)
{
    Node *l = not_expr();
    while (at_nm("and")) { int line = cline(); adv(); Node *n = mk(N_AND, line); n->a = l; n->b = not_expr(); l = n; }
    return l;
}
static Node *or_expr(void)
{
    Node *l = and_expr();
    while (at_nm("or")) { int line = cline(); adv(); Node *n = mk(N_OR, line); n->a = l; n->b = and_expr(); l = n; }
    return l;
}
static Node *expr(void)
{
    Node *e = or_expr();
    if (at_nm("if")) {
        int line = cline(); adv();
        Node *c = mk(N_COND, line); c->b = e; c->a = or_expr();
        if (!eat_nm("else")) perr(line, "An 'x if cond else y' value needs the 'else' part.");
        c->c = expr(); return c;
    }
    return e;
}

/* ------------------------------------------------------------------ parameters */
static void parse_params(Node *n, const char *close)
{
    Param tmp[32]; int np = 0, kwonly = 0;
    while (!at_op(close)) {
        if (np >= 31) perr(cline(), "Too many parameters.");
        if (cur()->k == K_EOF) perr(cline(), "The parameter list is not closed.");
        if (at_op("/")) { adv(); }
        else if (at_op("**")) { adv(); if (cur()->k != K_NAME) perr(cline(), "A name must follow '**'."); tmp[np].name = cur()->u.s; tmp[np].def = 0; tmp[np].kind = 2; np++; adv(); }
        else if (at_op("*")) {
            adv();
            if (cur()->k == K_NAME) { tmp[np].name = cur()->u.s; tmp[np].def = 0; tmp[np].kind = 1; np++; adv(); }
            kwonly = 1;
        } else if (cur()->k == K_NAME && !as_strcmp(cur()->u.s->s, "void") && np == 0 && (is_op(peekn(1), close))) { adv(); }
        else {
            if (cur()->k != K_NAME || reserved(cur())) perr(cline(), "A parameter name was expected.");
            tmp[np].name = cur()->u.s; tmp[np].def = 0; tmp[np].kind = kwonly ? 3 : 0; adv();
            if (eat_op("=")) tmp[np].def = expr();
            np++;
        }
        if (!eat_op(",")) break;
    }
    n->params = as_alloc((unsigned)(np ? np : 1) * sizeof(Param));
    as_memcpy(n->params, tmp, (unsigned)np * sizeof(Param));
    n->np = np;
}

/* ------------------------------------------------------------------ statements */
static void semi(const char *what)
{
    if (eat_op(";")) return;
    if (at_op("}") || cur()->k == K_EOF) return;
    if (as_relaxed && P.pos > 0 && P.t[P.pos - 1].line < cur()->line) return;
    { char m[80]; as_fmt(m, sizeof m, "Missing ';' at the end of %s.", what); perr(P.pos > 0 ? P.t[P.pos - 1].line : cline(), m); }
}

static Node *exprlist(void)
{
    int line = cline();
    Node *e = expr();
    if (!at_op(",")) return e;
    NL l = {0}; nl_push(&l, e);
    while (eat_op(",")) { if (at_op(";") || at_op("=") || at_op("}")) break; nl_push(&l, expr()); }
    Node *t = mk(N_TUPLE, line); t->list = l.a; t->n = l.n; return t;
}

static int is_target(Node *n)
{
    if (n->k == N_NAME || n->k == N_ATTR || n->k == N_INDEX) return 1;
    if (n->k == N_TUPLE || n->k == N_LIST) { for (int i = 0; i < n->n; i++) if (!is_target(n->list[i])) return 0; return n->n > 0; }
    return 0;
}

static Node *simple(int want_semi)
{
    int line = cline();
    Node *e = exprlist();
    Node *r;
    static const char *const aug[] = { "+=", "-=", "*=", "/=", "//=", "%=", "**=", 0 };
    static const char augc[] = "+-*/D%P";
    if (at_op("=")) {
        if (!is_target(e)) perr(line, "The left side of '=' must be a name, an item or an attribute.");
        adv();
        r = mk(S_ASSIGN, line); r->a = e; r->b = exprlist();
        if (at_op("=")) perr(line, "Chained assignments (a = b = 1) are not supported.");
    } else {
        int hit = -1;
        for (int i = 0; aug[i]; i++) if (at_op(aug[i])) hit = i;
        if (hit >= 0) {
            if (e->k != N_NAME && e->k != N_ATTR && e->k != N_INDEX) perr(line, "The left side of the operator must be a name, an item or an attribute.");
            adv(); r = mk(S_AUG, line); r->op = (unsigned char)augc[hit]; r->a = e; r->b = expr();
        } else { r = mk(S_EXPR, line); r->a = e; }
    }
    if (want_semi) semi("the line");
    return r;
}

static Node *block(void)
{
    int line = cline();
    if (!at_op("{")) perr(line, "A '{' was expected here.");
    adv();
    NL l = {0};
    while (!at_op("}")) {
        if (cur()->k == K_EOF) perr(line, "A block is not closed: missing '}'.");
        nl_push(&l, stmt());
    }
    adv();
    Node *b = mk(S_BLOCK, line); b->list = l.a; b->n = l.n;
    return b;
}
static void opt_semi(void) { eat_op(";"); }

static Node *var_decl(int want_semi)
{
    int line = cline(); adv();                                  /* vr */
    if (cur()->k != K_NAME) perr(line, "vr needs a type and a name: vr inter x = 5;");
    Str *type = 0, *name = cur()->u.s; adv();
    if (cur()->k == K_NAME && !reserved(cur())) { type = name; name = cur()->u.s; adv(); }
    Node *n = mk(S_VAR, line); n->s = name;
    n->names = as_alloc(sizeof(Str *)); n->names[0] = type;
    if (eat_op("=")) n->a = exprlist();
    if (want_semi) semi("the declaration");
    return n;
}

static int scan_cfor(void)
{
    int depth = 0;
    for (int p = P.pos; p < P.n; p++) {
        Token *t = &P.t[p];
        if (t->k == K_EOF) return 0;
        if (t->k == K_OP) {
            char c = t->u.s->s[0]; int one = t->u.s->len == 1;
            if (one && (c == '(' || c == '[')) depth++;
            else if (one && (c == ')' || c == ']')) depth--;
            else if (one && c == '{' && depth <= 1) return 0;
            else if (one && c == ';' && depth <= 1) return 1;
        }
    }
    return 0;
}

static Node *if_chain(int line)
{
    Node *n = mk(S_IF, line);
    n->a = expr();
    n->b = block();
    if (at_nm("elif") || (at_nm("else") && is_nm(peekn(1), "if"))) {
        int l2 = cline();
        if (at_nm("else")) adv();
        adv();
        n->c = if_chain(l2);
        return n;
    }
    if (at_nm("else")) { adv(); n->c = block(); }
    return n;
}

static Node *stmt(void)
{
    Token *t = cur();
    int line = t->line;
    if (t->k == K_DIRECT && t->aux == 1) {                            /* as <command>;  ->  ast_do("<command>") */
        Node *call = mk(N_CALL, line), *nm = mk(N_NAME, line);
        nm->s = as_intern("__as_cmd", 8); call->a = nm;
        call->list = as_alloc(sizeof(Node *)); call->list[0] = const_str(t->u.s, line); call->n = 1;
        Node *e = mk(S_EXPR, line); e->a = call;
        adv(); semi("as"); return e;
    }
    if (t->k == K_DIRECT) {
        Node *n = mk(S_IMPORT, line);
        const char *s = t->u.s->s; int len = t->u.s->len, i = 0;
        int is_from = !as_strncmp(s, "from", 4);
        i = is_from ? 4 : 6;
        while (i < len && (s[i] == ' ' || s[i] == '\t')) i++;
        int st = i, en;
        if (i < len && s[i] == '<') { st = ++i; while (i < len && s[i] != '>') i++; en = i; if (i < len) i++; }
        else { while (i < len && s[i] != ' ' && s[i] != '\t') i++; en = i; }
        if (en <= st) perr(line, "The module name is missing: #import <name.ash> as alias");
        n->s = as_intern(s + st, en - st);
        Str *names[16]; Str *alias[16]; int nn = 0;
        if (is_from) {
            n->k = S_FROM;
            while (i < len && s[i] == ' ') i++;
            if (!(i + 6 <= len && !as_strncmp(s + i, "import", 6))) perr(line, "Use: #from <name.ash> import Name");
            i += 6;
            while (i < len && nn < 16) {
                while (i < len && (s[i] == ' ' || s[i] == ',')) i++;
                int a0 = i;
                while (i < len && s[i] != ' ' && s[i] != ',') i++;
                if (i > a0) { names[nn] = as_intern(s + a0, i - a0); alias[nn] = names[nn]; nn++; }
                int save = i;
                while (i < len && s[i] == ' ') i++;
                if (i + 2 < len && !as_strncmp(s + i, "as ", 3) && nn) { i += 3; while (i < len && s[i] == ' ') i++; int b0 = i; while (i < len && s[i] != ' ' && s[i] != ',') i++; alias[nn - 1] = as_intern(s + b0, i - b0); } else i = save;
            }
            if (!nn) perr(line, "Use: #from <name.ash> import Name");
            n->names = as_alloc((unsigned)nn * 2 * sizeof(Str *));
            for (int k = 0; k < nn; k++) { n->names[2 * k] = names[k]; n->names[2 * k + 1] = alias[k]; }
            n->n = nn;
        } else {
            while (i < len && s[i] == ' ') i++;
            if (i + 3 <= len && !as_strncmp(s + i, "as ", 3)) { i += 3; while (i < len && s[i] == ' ') i++; int b0 = i; while (i < len && s[i] != ' ') i++; n->d = const_str(as_intern(s + b0, i - b0), line); }
        }
        adv();
        return n;
    }
    if (t->k == K_NAME) {
        const char *s = t->u.s->s;
        if (!as_strcmp(s, "start") && is_op(peekn(1), ";")) { adv(); adv(); return mk(S_START, line); }
        if (!as_strcmp(s, "end") && (is_op(peekn(1), ";") || peekn(1)->k == K_EOF || is_op(peekn(1), "}"))) { adv(); opt_semi(); return mk(S_END, line); }
        if (!as_strcmp(s, "vr")) return var_decl(1);
        if (!as_strcmp(s, "if")) { adv(); Node *n = if_chain(line); opt_semi(); return n; }
        if (!as_strcmp(s, "elif") || !as_strcmp(s, "else")) perr(line, "'elif' / 'else' without a matching 'if'.");
        if (!as_strcmp(s, "whle") || !as_strcmp(s, "while")) { adv(); Node *n = mk(S_WHILE, line); n->a = expr(); n->b = block(); opt_semi(); return n; }
        if (!as_strcmp(s, "fr") || !as_strcmp(s, "for")) {
            adv();
            if (scan_cfor()) {
                Node *n = mk(S_CFOR, line);
                int paren = eat_op("(");
                if (at_nm("vr")) n->a = var_decl(0); else if (!at_op(";")) n->a = simple(0);
                need_op(";", "fr needs ';' after the start part: fr (vr inter i = 0; i < 5; i = i + 1) {");
                if (!at_op(";")) n->b = expr();
                need_op(";", "fr needs ';' after the condition.");
                if (!at_op(")") && !at_op("{")) n->c = simple(0);
                if (paren) need_op(")", "fr is missing ')'.");
                n->d = block(); opt_semi();
                return n;
            }
            Node *n = mk(S_FOR, line);
            int paren = at_op("(") && 0;
            (void)paren;
            NL tl = {0};
            for (;;) {
                if (cur()->k != K_NAME || reserved(cur())) perr(line, "fr needs a loop variable: fr x in items {");
                Node *nm = mk(N_NAME, cline()); nm->s = cur()->u.s; adv(); nl_push(&tl, nm);
                if (!eat_op(",")) break;
            }
            if (!eat_nm("in")) perr(line, "fr needs 'in': fr x in items {");
            if (tl.n == 1) n->a = tl.a[0]; else { n->a = mk(N_TUPLE, line); n->a->list = tl.a; n->a->n = tl.n; }
            n->b = exprlist(); n->c = block(); opt_semi();
            return n;
        }
        if (!as_strcmp(s, "mtch") || !as_strcmp(s, "match")) {
            adv();
            Node *n = mk(S_MATCH, line);
            n->a = expr(); eat_op(":");
            need_op("{", "mtch needs a '{' after the value.");
            NL pats = {0}, bodies = {0};
            while (!at_op("}")) {
                if (cur()->k == K_EOF) perr(line, "mtch is not closed: missing '}'.");
                if (at_nm("end")) { adv(); opt_semi(); continue; }
                if (!eat_nm("cse") && !eat_nm("case")) perr(cline(), "mtch needs 'cse value { ... }' branches.");
                Node *pat;
                if (cur()->k == K_NAME && !as_strcmp(cur()->u.s->s, "_")) { adv(); pat = 0; } else pat = expr();
                eat_op(":");
                nl_push(&pats, pat);
                nl_push(&bodies, block()); opt_semi();
            }
            adv(); opt_semi();
            n->list = pats.a; n->n = pats.n; n->list2 = bodies.a; n->n2 = bodies.n;
            return n;
        }
        if (!as_strcmp(s, "func") || !as_strcmp(s, "def")) {
            adv();
            if (cur()->k != K_NAME) perr(line, "A function needs a name.");
            Node *n = mk(S_FUNC, line); n->s = cur()->u.s; adv();
            need_op("(", "A '(' was expected after the function name.");
            parse_params(n, ")");
            need_op(")", "Missing ')' after the parameters.");
            { int saved_y = yield_flag; yield_flag = 0; n->a = block(); n->flag = (unsigned char)yield_flag; yield_flag = saved_y; }
            opt_semi();
            return n;
        }
        if (!as_strcmp(s, "class")) {
            adv();
            if (cur()->k != K_NAME) perr(line, "A class needs a name.");
            Node *n = mk(S_CLASS, line); n->s = cur()->u.s; adv();
            if (eat_op("(")) { if (!at_op(")")) n->a = expr(); need_op(")", "Missing ')' after the base class."); }
            n->b = block(); opt_semi();
            return n;
        }
        if (!as_strcmp(s, "return")) {
            adv(); Node *n = mk(S_RETURN, line);
            if (!at_op(";") && !at_op("}") && cur()->k != K_EOF) n->a = exprlist();
            semi("return"); return n;
        }
        if (!as_strcmp(s, "break")) { adv(); semi("break"); return mk(S_BREAK, line); }
        if (!as_strcmp(s, "continue")) { adv(); semi("continue"); return mk(S_CONTINUE, line); }
        if (!as_strcmp(s, "pass")) { adv(); semi("pass"); return mk(S_PASS, line); }
        if (!as_strcmp(s, "global") || !as_strcmp(s, "nonlocal")) {
            int g = !as_strcmp(s, "global"); adv();
            Node *n = mk(g ? S_GLOBAL : S_NONLOCAL, line); Str *nm[16]; int k = 0;
            for (;;) { if (cur()->k != K_NAME) perr(line, "A variable name was expected."); if (k < 16) nm[k++] = cur()->u.s; adv(); if (!eat_op(",")) break; }
            n->names = as_alloc((unsigned)k * sizeof(Str *)); as_memcpy(n->names, nm, (unsigned)k * sizeof(Str *)); n->n = k;
            semi("the declaration"); return n;
        }
        if (!as_strcmp(s, "assert")) {
            adv(); Node *n = mk(S_ASSERT, line); n->a = expr(); if (eat_op(",")) n->b = expr();
            semi("assert"); return n;
        }
        if (!as_strcmp(s, "push_error") || !as_strcmp(s, "raise")) {
            adv(); Node *n = mk(S_RAISE, line);
            if (!at_op(";") && !at_op("}")) n->a = expr();
            semi("push_error"); return n;
        }
        if (!as_strcmp(s, "ask") || !as_strcmp(s, "try")) {
            adv();
            Node *n = mk(S_TRY, line);
            n->a = block();
            NL types = {0}, bodies = {0}; Str *nm[16]; int k = 0;
            while (at_nm("error_for") || at_nm("except")) {
                adv();
                Node *ty = 0; Str *var = 0;
                if (cur()->k == K_NAME && !at_op("{") && !is_nm(cur(), "as")) ty = postfix_core(0);
                if (eat_nm("as")) { if (cur()->k != K_NAME) perr(line, "A name was expected after 'as'."); var = cur()->u.s; adv(); }
                eat_op(":");
                if (k < 16) nm[k++] = var;
                nl_push(&types, ty); nl_push(&bodies, block());
            }
            if (!types.n) perr(line, "'ask { ... }' needs an 'error_for { ... }' part.");
            opt_semi();
            n->list = types.a; n->n = types.n; n->list2 = bodies.a; n->n2 = bodies.n;
            n->names = as_alloc((unsigned)k * sizeof(Str *)); as_memcpy(n->names, nm, (unsigned)k * sizeof(Str *));
            return n;
        }
        if (!as_strcmp(s, "with")) {
            adv(); Node *n = mk(S_WITH, line); n->a = expr();
            if (eat_nm("as")) { if (cur()->k != K_NAME) perr(line, "A name was expected after 'as'."); n->s = cur()->u.s; adv(); }
            eat_op(":");
            n->b = block(); opt_semi(); return n;
        }
        if (!as_strcmp(s, "import")) {
            adv();
            Node *n = mk(S_IMPORT, line);
            if (cur()->k != K_NAME) perr(line, "Use: import name;");
            n->s = cur()->u.s; adv();
            if (eat_nm("as")) { if (cur()->k != K_NAME) perr(line, "A name was expected after 'as'."); n->d = const_str(cur()->u.s, line); adv(); }
            semi("import"); return n;
        }
        if (!as_strcmp(s, "from")) {
            adv();
            Node *n = mk(S_FROM, line);
            if (cur()->k != K_NAME) perr(line, "Use: from name import Item;");
            n->s = cur()->u.s; adv();
            if (!eat_nm("import")) perr(line, "Use: from name import Item;");
            Str *nm[32]; int k = 0;
            for (;;) {
                if (cur()->k != K_NAME) perr(line, "A name to import was expected.");
                Str *a = cur()->u.s, *b = a; adv();
                if (eat_nm("as")) { if (cur()->k != K_NAME) perr(line, "A name was expected after 'as'."); b = cur()->u.s; adv(); }
                if (k < 15) { nm[2 * k] = a; nm[2 * k + 1] = b; k++; }
                if (!eat_op(",")) break;
            }
            n->names = as_alloc((unsigned)k * 2 * sizeof(Str *)); as_memcpy(n->names, nm, (unsigned)k * 2 * sizeof(Str *)); n->n = k;
            semi("import"); return n;
        }
        if (!as_strcmp(s, "use") && peekn(1)->k == K_NAME) {
            adv(); Node *n = mk(S_USE, line);
            Buf b; buf_init(&b);
            while (cur()->k == K_NAME || at_op("-")) { buf_addc(&b, cur()->u.s->s); adv(); }
            n->s = as_intern(b.s, b.n);
            semi("use"); return n;
        }
        if (!as_strcmp(s, "yield")) {
            adv(); Node *n = mk(S_YIELD, line); yield_flag = 1;
            if (!at_op(";") && !at_op("}") && cur()->k != K_EOF) n->a = exprlist();
            semi("yield"); return n;
        }
    }
    return simple(1);
}

/* ------------------------------------------------------------------ entry points */
Node *as_parse(Token *t, int n, const char *what)
{
    (void)what;
    as_parse_error_line = 0;
    if (__builtin_setjmp(pbuf)) { in_parse = 0; return 0; }
    P.t = t; P.pos = 0; P.n = n; in_parse = 1;
    NL l = {0};
    int line = t[0].line;
    while (cur()->k != K_EOF) {
        if (at_op("}")) perr(cline(), "A '}' without an opening '{'.");
        nl_push(&l, stmt());
    }
    in_parse = 0;
    Node *b = mk(S_BLOCK, line); b->list = l.a; b->n = l.n;
    return b;
}

Node *as_parse_expr_text(const char *s, int len, int line)
{
    int nt = 0;
    as_parse_error_line = 0;
    Token *t = as_lex(s, (unsigned)len, &nt, line);
    if (!t) { as_parse_error_line = as_lex_error_line; as_fmt(as_parse_error, sizeof as_parse_error, "%s", as_lex_error); return 0; }
    if (__builtin_setjmp(pbuf)) return 0;
    P.t = t; P.pos = 0; P.n = nt;
    Node *e = expr();
    if (cur()->k != K_EOF) perr(line, "Unexpected text after the expression.");
    return e;
}
