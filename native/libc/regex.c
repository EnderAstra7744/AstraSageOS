/* POSIX-style regular expressions for native programs: compiles to a small program run by a backtracking matcher. */
#include <regex.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

enum { I_CHAR, I_ANY, I_SET, I_SPLIT, I_JMP, I_SAVE, I_BOL, I_EOL, I_MATCH, I_WORDB };
typedef struct { unsigned char op; unsigned char c; int x, y; } inst;
typedef struct { inst *code; int n, cap; unsigned char (*sets)[32]; int nsets; int nsave; int icase, nl; } prog;

typedef struct { const char *p; prog *g; int ere, err, ngroups; } parser;

static int emit(parser *ps, int op, int c, int x, int y)
{
    prog *g = ps->g;
    if (g->n == g->cap) {
        int nc = g->cap ? g->cap * 2 : 64;
        inst *nb = realloc(g->code, (size_t)nc * sizeof(inst));
        if (!nb) { ps->err = REG_ESPACE; return 0; }
        g->code = nb; g->cap = nc;
    }
    g->code[g->n] = (inst){ (unsigned char)op, (unsigned char)c, x, y };
    return g->n++;
}

static int new_set(parser *ps)
{
    prog *g = ps->g;
    unsigned char (*ns)[32] = realloc(g->sets, (size_t)(g->nsets + 1) * 32);
    if (!ns) { ps->err = REG_ESPACE; return 0; }
    g->sets = ns;
    memset(g->sets[g->nsets], 0, 32);
    return g->nsets++;
}
#define SETBIT(s, c) ((s)[(unsigned char)(c) >> 3] |= (unsigned char)(1u << ((c) & 7)))

static int class_match(const char *name, int len, int c)
{
    if (len == 5 && !strncmp(name, "alpha", 5)) return isalpha(c);
    if (len == 5 && !strncmp(name, "digit", 5)) return isdigit(c);
    if (len == 5 && !strncmp(name, "alnum", 5)) return isalnum(c);
    if (len == 5 && !strncmp(name, "upper", 5)) return isupper(c);
    if (len == 5 && !strncmp(name, "lower", 5)) return islower(c);
    if (len == 5 && !strncmp(name, "space", 5)) return isspace(c);
    if (len == 5 && !strncmp(name, "punct", 5)) return ispunct(c);
    if (len == 6 && !strncmp(name, "xdigit", 6)) return isxdigit(c);
    if (len == 5 && !strncmp(name, "print", 5)) return isprint(c);
    if (len == 5 && !strncmp(name, "graph", 5)) return isgraph(c);
    if (len == 5 && !strncmp(name, "blank", 5)) return c == ' ' || c == '\t';
    if (len == 5 && !strncmp(name, "cntrl", 5)) return iscntrl(c);
    return 0;
}

static int parse_set(parser *ps)     /* after '[' */
{
    int si = new_set(ps), neg = 0, first = 1;
    unsigned char *s;
    if (ps->err) return 0;
    s = ps->g->sets[si];
    if (*ps->p == '^') { neg = 1; ps->p++; }
    for (;;) {
        int c = (unsigned char)*ps->p;
        if (!c) { ps->err = REG_BADPAT; return 0; }
        if (c == ']' && !first) { ps->p++; break; }
        first = 0;
        if (c == '[' && ps->p[1] == ':') {
            const char *e = strstr(ps->p + 2, ":]");
            if (e) {
                for (int k = 0; k < 256; k++) if (class_match(ps->p + 2, (int)(e - ps->p - 2), k)) SETBIT(s, k);
                ps->p = e + 2;
                continue;
            }
        }
        ps->p++;
        if (*ps->p == '-' && ps->p[1] && ps->p[1] != ']') {
            int hi = (unsigned char)ps->p[1];
            ps->p += 2;
            for (int k = c; k <= hi; k++) SETBIT(s, k);
        } else SETBIT(s, c);
    }
    if (ps->g->icase) for (int k = 0; k < 256; k++) if (s[k >> 3] & (1 << (k & 7))) { SETBIT(s, tolower(k)); SETBIT(s, toupper(k)); }
    if (neg) { for (int k = 0; k < 32; k++) s[k] = (unsigned char)~s[k]; if (ps->g->nl) s[10 >> 3] &= (unsigned char)~(1 << (10 & 7)); }
    return si;
}

static void parse_alt(parser *ps, int depth);

/* one atom; returns start pc of the atom, code appended */
static void parse_atom(parser *ps, int depth)
{
    int c = (unsigned char)*ps->p;
    if (ps->err) return;
    if (c == '(' && ps->ere) {
        int gi = ++ps->ngroups;
        ps->p++;
        emit(ps, I_SAVE, 0, 2 * gi, 0);
        parse_alt(ps, depth + 1);
        if (*ps->p != ')') { ps->err = REG_BADPAT; return; }
        ps->p++;
        emit(ps, I_SAVE, 0, 2 * gi + 1, 0);
        return;
    }
    if (c == '\\' && !ps->ere && ps->p[1] == '(') {
        int gi = ++ps->ngroups;
        ps->p += 2;
        emit(ps, I_SAVE, 0, 2 * gi, 0);
        parse_alt(ps, depth + 1);
        if (!(ps->p[0] == '\\' && ps->p[1] == ')')) { ps->err = REG_BADPAT; return; }
        ps->p += 2;
        emit(ps, I_SAVE, 0, 2 * gi + 1, 0);
        return;
    }
    if (c == '[') { ps->p++; int si = parse_set(ps); emit(ps, I_SET, 0, si, 0); return; }
    if (c == '.') { ps->p++; emit(ps, I_ANY, 0, 0, 0); return; }
    if (c == '^') { ps->p++; emit(ps, I_BOL, 0, 0, 0); return; }
    if (c == '$') { ps->p++; emit(ps, I_EOL, 0, 0, 0); return; }
    if (c == '\\' && ps->p[1]) {
        int e = (unsigned char)ps->p[1];
        ps->p += 2;
        int si;
        switch (e) {
        case 'd': si = new_set(ps); for (int k = '0'; k <= '9'; k++) SETBIT(ps->g->sets[si], k); emit(ps, I_SET, 0, si, 0); return;
        case 'w': si = new_set(ps); for (int k = 0; k < 256; k++) if (isalnum(k) || k == '_') SETBIT(ps->g->sets[si], k); emit(ps, I_SET, 0, si, 0); return;
        case 's': si = new_set(ps); for (int k = 0; k < 256; k++) if (isspace(k)) SETBIT(ps->g->sets[si], k); emit(ps, I_SET, 0, si, 0); return;
        case 'n': e = '\n'; break;
        case 't': e = '\t'; break;
        case 'b': emit(ps, I_WORDB, 0, 0, 0); return;
        }
        emit(ps, I_CHAR, ps->g->icase ? tolower(e) : e, 0, 0);
        return;
    }
    ps->p++;
    emit(ps, I_CHAR, ps->g->icase ? tolower(c) : c, 0, 0);
}

/* append a copy of tmp[0..len) (originally at pc 'oldbase') at the end of the program, fixing jump targets */
static void reloc_emit(parser *ps, const inst *tmp, int len, int oldbase)
{
    int delta = ps->g->n - oldbase;
    for (int k = 0; k < len; k++) {
        inst in = tmp[k];
        if (in.op == I_SPLIT) { in.x += delta; in.y += delta; }
        else if (in.op == I_JMP) in.x += delta;
        emit(ps, in.op, in.c, in.x, in.y);
    }
}

/* take code[start..n) out, put a SPLIT in front of it and put the code back; returns the SPLIT's pc */
static int wrap_split(parser *ps, int start)
{
    prog *g = ps->g;
    int len = g->n - start, sp;
    inst *tmp = malloc((size_t)(len ? len : 1) * sizeof(inst));
    if (!tmp) { ps->err = REG_ESPACE; return start; }
    memcpy(tmp, &g->code[start], (size_t)len * sizeof(inst));
    g->n = start;
    sp = emit(ps, I_SPLIT, 0, start + 1, 0);
    reloc_emit(ps, tmp, len, start);
    free(tmp);
    return sp;
}

static void parse_piece(parser *ps, int depth)
{
    prog *g;
    int start, c, lo, hi, len;
    inst *tmp;
    start = ps->g->n;
    parse_atom(ps, depth);
    for (;;) {
        if (ps->err) return;
        c = *ps->p; lo = -1; hi = -1;
        if (c == '*') { lo = 0; ps->p++; }
        else if (ps->ere && c == '+') { lo = 1; ps->p++; }
        else if (ps->ere && c == '?') { lo = 0; hi = 1; ps->p++; }
        else if ((ps->ere && c == '{') || (!ps->ere && c == '\\' && ps->p[1] == '{')) {
            const char *q = ps->p + (c == '\\' ? 2 : 1);
            char *e;
            if (!isdigit((unsigned char)*q)) return;             /* a lone '{' is an ordinary character */
            lo = (int)strtol(q, &e, 10);
            hi = lo;
            if (*e == ',') { q = e + 1; if (isdigit((unsigned char)*q)) hi = (int)strtol(q, &e, 10); else { hi = -1; e = (char *)q; } }
            if (c == '\\') { if (!(e[0] == '\\' && e[1] == '}')) { ps->err = REG_BADPAT; return; } e += 2; }
            else { if (*e != '}') { ps->err = REG_BADPAT; return; } e++; }
            ps->p = e;
        } else return;
        if (lo > 30 || hi > 30 || (hi != -1 && hi < lo)) { ps->err = REG_BADPAT; return; }
        g = ps->g;
        len = g->n - start;
        tmp = malloc((size_t)(len ? len : 1) * sizeof(inst));
        if (!tmp) { ps->err = REG_ESPACE; return; }
        memcpy(tmp, &g->code[start], (size_t)len * sizeof(inst));
        g->n = start;
        for (int i = 0; i < lo; i++) reloc_emit(ps, tmp, len, start);
        if (hi == -1) {                                           /* L: SPLIT body, out ; body ; JMP L */
            int l = emit(ps, I_SPLIT, 0, 0, 0);
            g->code[l].x = l + 1;
            reloc_emit(ps, tmp, len, start);
            emit(ps, I_JMP, 0, l, 0);
            g->code[l].y = g->n;
        } else {
            int nopt = hi - lo, sps[32];
            for (int i = 0; i < nopt; i++) {
                sps[i] = emit(ps, I_SPLIT, 0, 0, 0);
                g->code[sps[i]].x = sps[i] + 1;
                reloc_emit(ps, tmp, len, start);
            }
            for (int i = 0; i < nopt; i++) g->code[sps[i]].y = g->n;
        }
        free(tmp);
        /* the next quantifier (x*? is not supported) applies to the whole expansion */
    }
}

static void parse_seq(parser *ps, int depth)
{
    while (*ps->p && !ps->err) {
        if (*ps->p == '|' && ps->ere) break;
        if (*ps->p == ')' && ps->ere && depth > 0) break;
        if (*ps->p == '\\' && !ps->ere && (ps->p[1] == ')' || ps->p[1] == '|') && depth > 0) break;
        parse_piece(ps, depth);
    }
}

static void parse_alt(parser *ps, int depth)
{
    int start = ps->g->n, jumps[64], nj = 0, split = -1;
    parse_seq(ps, depth);
    while (!ps->err && ((ps->ere && *ps->p == '|') || (!ps->ere && ps->p[0] == '\\' && ps->p[1] == '|'))) {
        int sp = wrap_split(ps, start);                    /* SPLIT start+1 , next branch */
        if (split >= 0) { /* previous split already points here: patched below */ }
        if (nj >= 63) { ps->err = REG_BADPAT; return; }
        ps->p += ps->ere ? 1 : 2;
        jumps[nj++] = emit(ps, I_JMP, 0, 0, 0);
        ps->g->code[sp].y = ps->g->n;
        start = ps->g->n;
        parse_seq(ps, depth);
        split = sp;
    }
    for (int i = 0; i < nj; i++) ps->g->code[jumps[i]].x = ps->g->n;
}

int regcomp(regex_t *r, const char *pat, int flags)
{
    parser ps;
    prog *g = calloc(1, sizeof *g);
    if (!g) return REG_ESPACE;
    g->icase = (flags & REG_ICASE) != 0;
    g->nl = (flags & REG_NEWLINE) != 0;
    ps.p = pat; ps.g = g; ps.ere = (flags & REG_EXTENDED) != 0; ps.err = 0; ps.ngroups = 0;
    emit(&ps, I_SAVE, 0, 0, 0);
    parse_alt(&ps, 0);
    if (!ps.err && *ps.p) ps.err = REG_BADPAT;
    emit(&ps, I_SAVE, 0, 1, 0);
    emit(&ps, I_MATCH, 0, 0, 0);
    if (ps.err) { free(g->code); free(g->sets); free(g); return ps.err; }
    g->nsave = 2 * (ps.ngroups + 1);
    r->prog = g; r->flags = flags; r->re_nsub = (size_t)ps.ngroups;
    return 0;
}

typedef struct { const char *s, *base, *end; int notbol, steps; const char **save; prog *g; } mstate;

static int word(int c) { return isalnum(c) || c == '_'; }

static int run(mstate *m, int pc, const char *sp, int depth)
{
    prog *g = m->g;
    for (;;) {
        const inst *in = &g->code[pc];
        if (++m->steps > 2000000 || depth > 4000) return 0;
        switch (in->op) {
        case I_CHAR:
            if (sp >= m->end) return 0;
            if ((g->icase ? tolower((unsigned char)*sp) : (unsigned char)*sp) != in->c) return 0;
            sp++; pc++; break;
        case I_ANY: if (sp >= m->end || (g->nl && *sp == '\n')) return 0; sp++; pc++; break;
        case I_SET:
            if (sp >= m->end) return 0;
            if (!(g->sets[in->x][(unsigned char)*sp >> 3] & (1 << ((unsigned char)*sp & 7)))) return 0;
            sp++; pc++; break;
        case I_JMP: pc = in->x; break;
        case I_SPLIT:
            if (run(m, in->x, sp, depth + 1)) return 1;
            pc = in->y; break;
        case I_SAVE: {
            const char *old = m->save[in->x];
            m->save[in->x] = sp;
            if (run(m, pc + 1, sp, depth + 1)) return 1;
            m->save[in->x] = old;
            return 0;
        }
        case I_BOL: if (!((sp == m->base && !m->notbol) || (g->nl && sp > m->base && sp[-1] == '\n'))) return 0; pc++; break;
        case I_EOL: if (!(sp == m->end || (g->nl && *sp == '\n'))) return 0; pc++; break;
        case I_WORDB: {
            int a = sp > m->base && word((unsigned char)sp[-1]), b = sp < m->end && word((unsigned char)*sp);
            if (a == b) return 0;
            pc++; break;
        }
        case I_MATCH: m->save[1] = sp; return 1;
        }
    }
}

int regexec(const regex_t *r, const char *s, size_t n, regmatch_t *pm, int flags)
{
    prog *g = r->prog;
    const char *save[2 * 10 + 2];
    int ns = g->nsave;
    mstate m;
    if (!g || ns > (int)(sizeof save / sizeof save[0])) return REG_ESPACE;
    m.base = s; m.end = s + strlen(s); m.notbol = (flags & REG_NOTBOL) != 0; m.g = g; m.save = save;
    for (const char *sp = s; ; sp++) {
        for (int i = 0; i < ns; i++) save[i] = 0;
        m.steps = 0;
        if (run(&m, 0, sp, 0)) {
            if (!(r->flags & REG_NOSUB)) for (size_t i = 0; i < n; i++) {
                if ((int)i < ns / 2 && save[2 * i] && save[2 * i + 1]) { pm[i].rm_so = (regoff_t)(save[2 * i] - s); pm[i].rm_eo = (regoff_t)(save[2 * i + 1] - s); }
                else { pm[i].rm_so = -1; pm[i].rm_eo = -1; }
            }
            return 0;
        }
        if (sp >= m.end) break;
    }
    return REG_NOMATCH;
}

size_t regerror(int code, const regex_t *r, char *buf, size_t n)
{
    const char *t = code == REG_NOMATCH ? "no match" : code == REG_BADPAT ? "invalid regular expression" : "out of memory";
    size_t l = strlen(t) + 1;
    (void)r;
    if (n) { size_t c = l < n ? l : n - 1; memcpy(buf, t, c); buf[c] = 0; }
    return l;
}

void regfree(regex_t *r)
{
    prog *g = r->prog;
    if (g) { free(g->code); free(g->sets); free(g); }
    r->prog = 0;
}
