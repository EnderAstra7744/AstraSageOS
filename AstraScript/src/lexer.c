/* AstraScript engine 2 - lexer.c: source text -> tokens. Comments: "@ ..." to the end of the line, "@** ... **@" blocks. */
#include "as_types.h"

int as_lex_error_line;
char as_lex_error[96];

static int lerr(int line, const char *msg) { as_lex_error_line = line; as_fmt(as_lex_error, sizeof as_lex_error, "%s", msg); return 0; }

static int is_id0(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c >= 0x80; }
static int is_id(unsigned char c) { return is_id0(c) || (c >= '0' && c <= '9'); }
static int is_dig(unsigned char c) { return c >= '0' && c <= '9'; }

static const char *const KW_BEFORE_EXPR[] = { "return", "in", "and", "or", "not", "if", "elif", "else", "yield", "is", "while", "whle", "assert", "lambda", "case", "cse", "mtch", 0 };

static int ends_value(const Token *t)
{
    if (!t) return 0;
    if (t->k == K_NUM_I || t->k == K_NUM_F || t->k == K_STR || t->k == K_BSTR || t->k == K_FSTR) return 1;
    if (t->k == K_NAME) { for (int i = 0; KW_BEFORE_EXPR[i]; i++) if (!as_strcmp(t->u.s->s, KW_BEFORE_EXPR[i])) return 0; return 1; }
    if (t->k == K_OP) { char c = t->u.s->s[0]; return t->u.s->len == 1 && (c == ')' || c == ']' || c == '}'); }
    return 0;
}

static int esc_char(const char *src, unsigned len, unsigned *i, Buf *b)       /* after a backslash */
{
    char c = src[*i];
    (*i)++;
    switch (c) {
    case 'n': buf_putc(b, '\n'); return 1;
    case 't': buf_putc(b, '\t'); return 1;
    case 'r': buf_putc(b, '\r'); return 1;
    case 'e': buf_putc(b, 27); return 1;
    case 'a': buf_putc(b, 7); return 1;
    case 'b': buf_putc(b, 8); return 1;
    case '0': buf_putc(b, 0); return 1;
    case '\\': buf_putc(b, '\\'); return 1;
    case '"': buf_putc(b, '"'); return 1;
    case '\'': buf_putc(b, '\''); return 1;
    case 'x': {
        int v = 0, n = 0;
        while (n < 2 && *i < len) {
            char h = src[*i]; int d = h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : -1;
            if (d < 0) break;
            v = v * 16 + d; (*i)++; n++;
        }
        buf_putc(b, (char)v);
        return 1;
    }
    default: buf_putc(b, '\\'); buf_putc(b, c); return 1;
    }
}

static const char *const OPS3[] = { "**=", "//=", 0 };
static const char *const OPS2[] = { "**", "//", "==", "!=", "<=", ">=", "+=", "-=", "*=", "/=", "%=", "<<", ">>", "->", 0 };

Token *as_lex(const char *src, unsigned len, int *ntok, int line0)
{
    int cap = 256, n = 0, line = line0;
    Token *t = as_alloc((unsigned)cap * sizeof(Token));
    unsigned i = 0;
    as_lex_error_line = 0;

#define PUSH(kind) do { if (n == cap) { Token *nt = as_alloc((unsigned)cap * 2 * sizeof(Token)); as_memcpy(nt, t, (unsigned)n * sizeof(Token)); t = nt; cap *= 2; } \
        t[n].k = (unsigned char)(kind); t[n].aux = 0; t[n].line = (unsigned short)line; n++; } while (0)

    while (i < len) {
        unsigned char c = (unsigned char)src[i];
        if (c == '\n') { line++; i++; continue; }
        if (c == ' ' || c == '\t' || c == '\r') { i++; continue; }
        if (c == '@') {                                              /* comment */
            if (i + 2 < len && src[i + 1] == '*' && src[i + 2] == '*') {
                i += 3;
                while (i < len && !(src[i] == '*' && i + 2 < len + 0 && src[i + 1] == '*' && src[i + 2] == '@')) { if (src[i] == '\n') line++; i++; }
                i += 3;
            } else while (i < len && src[i] != '\n') i++;
            continue;
        }
        if (c == '#') {                                              /* #import / #from lines, otherwise a comment */
            unsigned j = i + 1;
            while (j < len && (src[j] == ' ' || src[j] == '\t')) j++;
            if ((j + 6 <= len && !as_strncmp(src + j, "import", 6) && !is_id((unsigned char)src[j + 6])) ||
                (j + 4 <= len && !as_strncmp(src + j, "from", 4) && !is_id((unsigned char)src[j + 4]))) {
                unsigned e = j;
                while (e < len && src[e] != '\n') e++;
                PUSH(K_DIRECT);
                t[n - 1].u.s = as_intern(src + j, (int)(e - j));
                i = e;
            } else while (i < len && src[i] != '\n') i++;
            continue;
        }
        if (is_dig(c) || (c == '.' && i + 1 < len && is_dig((unsigned char)src[i + 1]))) {
            unsigned j = i;
            if (c == '0' && i + 1 < len && (src[i + 1] == 'x' || src[i + 1] == 'X')) {
                j += 2; unsigned v = 0;
                while (j < len) { char h = src[j]; int d = h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : -1; if (d < 0) break; v = v * 16 + (unsigned)d; j++; }
                PUSH(K_NUM_I); t[n - 1].u.i = (int)v; i = j; continue;
            }
            int isf = 0;
            while (j < len && is_dig((unsigned char)src[j])) j++;
            if (j < len && src[j] == '.' && j + 1 < len && is_dig((unsigned char)src[j + 1])) { isf = 1; j++; while (j < len && is_dig((unsigned char)src[j])) j++; }
            else if (j < len && src[j] == '.' && !(j + 1 < len && is_id0((unsigned char)src[j + 1])) && !(j + 1 < len && src[j + 1] == '.')) { isf = 1; j++; }
            if (j < len && (src[j] == 'e' || src[j] == 'E') && j + 1 < len && (is_dig((unsigned char)src[j + 1]) || ((src[j + 1] == '-' || src[j + 1] == '+') && j + 2 < len && is_dig((unsigned char)src[j + 2])))) {
                isf = 1; j += 2; while (j < len && is_dig((unsigned char)src[j])) j++;
            }
            if (!isf) {
                double v = 0;
                for (unsigned k = i; k < j; k++) v = v * 10 + (src[k] - '0');
                if (v < 2147483648.0) { PUSH(K_NUM_I); t[n - 1].u.i = (int)v; i = j; continue; }
                isf = 1;
            }
            double v = 0, scale = 1; unsigned k = i; int e10 = 0;
            while (k < j && is_dig((unsigned char)src[k])) v = v * 10 + (src[k++] - '0');
            if (k < j && src[k] == '.') { k++; while (k < j && is_dig((unsigned char)src[k])) { v = v * 10 + (src[k++] - '0'); scale *= 10; } }
            if (k < j && (src[k] == 'e' || src[k] == 'E')) {
                k++; int neg = 0; if (src[k] == '-') { neg = 1; k++; } else if (src[k] == '+') k++;
                while (k < j) e10 = e10 * 10 + (src[k++] - '0');
                if (neg) e10 = -e10;
            }
            v /= scale;
            while (e10 > 0) { v *= 10; e10--; }
            while (e10 < 0) { v /= 10; e10++; }
            PUSH(K_NUM_F); t[n - 1].u.d = v; i = j; continue;
        }
        /* strings: "..." '...' b"..." %strt"..." %strt%inter"..." */
        {
            unsigned j = i; int kind = K_STR, aux = 0;
            if (c == 'b' && i + 1 < len && (src[i + 1] == '"' || src[i + 1] == '\'')) { kind = K_BSTR; j = i + 1; }
            else if (c == '%' && !ends_value(n ? &t[n - 1] : 0)) {
                unsigned q = i; int types = 0, bits = 0;
                while (q < len && src[q] == '%') {
                    unsigned s0 = ++q;
                    while (q < len && is_id((unsigned char)src[q])) q++;
                    if (q == s0) { types = 0; break; }
                    int tl = (int)(q - s0);
                    bits |= (tl == 4 && !as_strncmp(src + s0, "strt", 4)) ? 1 : (tl == 5 && !as_strncmp(src + s0, "inter", 5)) ? 2 : (tl == 3 && !as_strncmp(src + s0, "flo", 3)) ? 4 : (tl == 4 && !as_strncmp(src + s0, "bool", 4)) ? 8 : 16;
                    types++;
                }
                if (types && q < len && (src[q] == '"' || src[q] == '\'')) { kind = K_FSTR; aux = bits; j = q; }
            }
            if ((kind != K_STR || c == '"' || c == '\'') && (src[j] == '"' || src[j] == '\'')) {
                char qc = src[j]; unsigned k = j + 1; int depth = 0; int sline = line;
                Buf b; buf_init(&b);
                int closed = 0;
                if (k + 1 < len && src[k] == qc && src[k + 1] == qc && kind != K_FSTR) {         /* triple quoted string */
                    k += 2;
                    while (k < len) {
                        if (src[k] == qc && k + 2 < len + 0 && src[k + 1] == qc && src[k + 2] == qc) { k += 3; closed = 1; break; }
                        if (src[k] == '\n') line++;
                        if (src[k] == '\\' && k + 1 < len) { k++; esc_char(src, len, &k, &b); continue; }
                        buf_putc(&b, src[k++]);
                    }
                } else {
                    while (k < len) {
                        char ch = src[k];
                        if (ch == '\n') break;
                        if (kind == K_FSTR) {
                            if (ch == '{' && k + 1 < len && src[k + 1] == '{') { buf_addc(&b, "{{"); k += 2; continue; }
                            if (ch == '}' && k + 1 < len && src[k + 1] == '}') { buf_addc(&b, "}}"); k += 2; continue; }
                            if (ch == '{') depth++;
                            else if (ch == '}' && depth) depth--;
                            else if (depth && (ch == '"' || ch == '\'') && ch != qc) { buf_putc(&b, ch); k++; while (k < len && src[k] != ch && src[k] != '\n') buf_putc(&b, src[k++]); if (k < len && src[k] == ch) { buf_putc(&b, ch); k++; } continue; }
                            else if (depth && ch == qc) { buf_putc(&b, ch); k++; while (k < len && src[k] != qc && src[k] != '\n') buf_putc(&b, src[k++]); if (k < len && src[k] == qc) { buf_putc(&b, qc); k++; } continue; }
                        }
                        if (ch == qc && !depth) { k++; closed = 1; break; }
                        if (ch == '\\' && k + 1 < len && !depth) { k++; if (kind == K_FSTR && (src[k] == '{' || src[k] == '}')) { buf_putc(&b, src[k++]); continue; } esc_char(src, len, &k, &b); continue; }
                        buf_putc(&b, ch); k++;
                    }
                }
                if (!closed) { lerr(sline, "The string is not closed (missing quote)."); return 0; }
                PUSH(kind); t[n - 1].line = (unsigned short)sline;
                t[n - 1].u.s = as_intern(b.s, b.n); t[n - 1].aux = (unsigned char)aux;
                i = k; continue;
            }
        }
        if (is_id0(c)) {
            unsigned j = i;
            while (j < len && is_id((unsigned char)src[j])) j++;
            if (j - i == 2 && c == 'a' && src[i + 1] == 's' && j + 1 < len && src[j] == ' ' && is_id0((unsigned char)src[j + 1]) &&
                (n == 0 || (as_relaxed && t[n - 1].line < line) || (t[n - 1].k == K_OP && (t[n - 1].u.s->s[0] == ';' || t[n - 1].u.s->s[0] == '{' || t[n - 1].u.s->s[0] == '}')))) {
                unsigned e = j + 1, q = 0;                          /* "as <command>;" - an AST command, kept as plain text */
                while (e < len && src[e] != '\n' && (q || src[e] != ';')) { if (src[e] == '"') q = !q; e++; }
                PUSH(K_DIRECT); t[n - 1].aux = 1;
                t[n - 1].u.s = as_intern(src + j + 1, (int)(e - j - 1));
                i = e; continue;
            }
            PUSH(K_NAME); t[n - 1].u.s = as_intern(src + i, (int)(j - i)); i = j; continue;
        }
        {   /* operators */
            const char *hit = 0; int hl = 0;
            for (int k = 0; OPS3[k]; k++) if (i + 3 <= len && !as_strncmp(src + i, OPS3[k], 3)) { hit = OPS3[k]; hl = 3; break; }
            if (!hit) for (int k = 0; OPS2[k]; k++) if (i + 2 <= len && !as_strncmp(src + i, OPS2[k], 2)) { hit = OPS2[k]; hl = 2; break; }
            if (!hit) {
                const char *single = "+-*/%<>=()[]{},:;.&|^~!";
                for (const char *p = single; *p; p++) if (*p == (char)c) { hit = src + i; hl = 1; break; }
            }
            if (!hit) { char m[40]; as_fmt(m, sizeof m, "Unknown character '%c'.", (char)c); lerr(line, m); return 0; }
            PUSH(K_OP); t[n - 1].u.s = as_intern(hit, hl); i += (unsigned)hl;
        }
    }
    PUSH(K_EOF);
    t[n - 1].u.s = as_intern("", 0);
    *ntok = n;
    return t;
}
