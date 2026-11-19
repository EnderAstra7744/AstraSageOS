/*
 * as edit - the AST text editor (nano-like).
 *
 *   top bar     green: file name, extension, size, date, state
 *   text area   AstraScript syntax colours (bash-style roles, AstraSage greens)
 *   hint line   keys and cursor position
 *   bottom line AstraScript errors: dark-red background on the faulty line and
 *               the error name + line number here (live, while typing)
 *
 * Keys: arrows, Home/End, PgUp/PgDn, Del, Backspace, Enter (auto-indent), Tab,
 *       Ctrl+S save, Ctrl+X exit, Ctrl+K cut line, Ctrl+U paste, Ctrl+A/E home/end.
 */
#include "astra.h"

#define MAX_DIAG 16

/* xterm-256 colour numbers */
#define COL_BAR_BG   112     /* green bar                 */
#define COL_BAR_FG   16      /* black text on it          */
#define COL_ERR_BG   52      /* faint dark red            */
#define COL_KEY      154
#define COL_DIM      245
#define COL_OK       112

enum { H_DEF, H_KW, H_TYPE, H_BUILTIN, H_STR, H_NUM, H_COMMENT, H_OP, H_FUNC, H_ASCMD, H_COUNT };

/* bash-like roles, AstraSage-style: keywords light green, strings warm, numbers violet,
 * comments grey, function names yellow, operators white. */
static const int h_color[H_COUNT] = { -1, 154, 112, 70, 215, 141, 245, 255, 221, 112 };

static char  buf[FS_FILE_MAX + 1];
static u32   len, cur;
static int   top, left, want_col;
static int   modified;
static char  clip[1024];
static int   cliplen;
static char  msg[96];
static int   msg_kind;               /* 0 none, 1 ok (green), 2 error (red) */
static asc_diag diags[MAX_DIAG];
static int   ndiag;
static int   is_script;
static int   lang;                   /* L_* of the file, from its extension */
static int   rows, cols;
static u32   mtime;
static int   has_node;
static char  fname[FS_NAME_MAX + 1], fext[FS_NAME_MAX + 1];

/* ------------------------------------------------------------------ output */

static void out(const char *s) { kputs(s); }

static void outf(const char *fmt, ...)
{
    char t[200];
    va_list ap;
    va_start(ap, fmt);
    kvsnprintf(t, sizeof t, fmt, ap);
    va_end(ap);
    kputs(t);
}

static void goto_rc(int r, int c) { outf("\033[%d;%dH", r, c); }

/* ------------------------------------------------------------------ buffer */

static u32 line_begin(u32 p) { while (p > 0 && buf[p - 1] != '\n') p--; return p; }
static u32 line_end(u32 p)   { while (p < len && buf[p] != '\n') p++; return p; }

static int count_lines(void)
{
    int n = 1;
    for (u32 i = 0; i < len; i++) if (buf[i] == '\n') n++;
    return n;
}

static void cursor_pos(int *ln, int *col)
{
    int l = 0;
    u32 b = 0;
    for (u32 i = 0; i < cur; i++) if (buf[i] == '\n') { l++; b = i + 1; }
    *ln = l;
    *col = (int)(cur - b);
}

static int insert(const char *s, u32 n)
{
    if (len + n > FS_FILE_MAX) return 0;
    kmemmove(buf + cur + n, buf + cur, len - cur);
    kmemcpy(buf + cur, s, n);
    len += n; cur += n;
    modified = 1;
    return 1;
}

/* ---- undo / redo (Ctrl+Z / Ctrl+Y): snapshots of the whole buffer, taken when the kind of edit changes ---- */
#define UNDO_N 12
typedef struct { char d[FS_FILE_MAX + 1]; u32 len, cur; } snap_t;
static snap_t us[UNDO_N], rs[UNDO_N];
static int    un, rn, last_class, class_run;

static void snap_take(snap_t *s)  { kmemcpy(s->d, buf, len); s->len = len; s->cur = cur; }
static void snap_put(const snap_t *s) { kmemcpy(buf, s->d, s->len); len = s->len; cur = s->cur; modified = 1; }
static void undo_push(void)
{
    if (un == UNDO_N) { for (int i = 1; i < UNDO_N; i++) us[i - 1] = us[i]; un--; }
    snap_take(&us[un++]);
    rn = 0;
}
static void undo_note(int cls)                       /* called before an edit: 1 typing, 2 deleting, 3 anything else */
{
    if (cls != last_class || cls == 3 || ++class_run >= 12) { undo_push(); class_run = 0; }
    last_class = cls;
}
static void undo_do(void)
{
    if (!un) return;
    if (rn == UNDO_N) { for (int i = 1; i < UNDO_N; i++) rs[i - 1] = rs[i]; rn--; }
    snap_take(&rs[rn++]);
    snap_put(&us[--un]);
    last_class = 0;
}
static void redo_do(void)
{
    if (!rn) return;
    if (un < UNDO_N) snap_take(&us[un++]);
    snap_put(&rs[--rn]);
    last_class = 0;
}

const char *ast_clip(int *l) { if (l) *l = cliplen; return clip; }
void ast_clip_set(const char *t, int l) { if (l > (int)sizeof clip) l = (int)sizeof clip; kmemcpy(clip, t, (u32)l); cliplen = l; }

static void remove_range(u32 from, u32 n)
{
    kmemmove(buf + from, buf + from + n, len - from - n);
    len -= n;
    if (cur > from) cur = cur >= from + n ? cur - n : from;
    modified = 1;
}

/* ------------------------------------------------------------- highlighting */

static int is_idc(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

static int word_is(const char *w, int n, const char *k)
{
    int kl = (int)kstrlen(k);
    return kl == n && !kstrncmp(w, k, (size_t)n);
}

static int is_keyword_w(const char *w, int n)
{
    static const char *const k[] = { "vr", "use", "if", "else", "whle", "fr", "func", "end", "start",
                                     "true", "false", "TRUE", "FALSE" };
    for (unsigned i = 0; i < sizeof k / sizeof k[0]; i++) if (word_is(w, n, k[i])) return 1;
    return 0;
}

static int is_type_w(const char *w, int n)
{
    return word_is(w, n, "inter") || word_is(w, n, "flo") || word_is(w, n, "strt");
}

static int is_builtin_w(const char *w, int n)
{
    return word_is(w, n, "printc") || word_is(w, n, "inputc") || word_is(w, n, "asscript");
}

/* Colours one line; *in_cmt carries the state of @** ... **@ block comments. */
static void highlight(const char *l, int n, int *in_cmt, u8 *col)
{
    int i = 0;
    kmemset(col, H_DEF, (size_t)n);

    if (*in_cmt) {
        for (; i < n; i++) {
            col[i] = H_COMMENT;
            if (l[i] == '*' && i + 2 < n && l[i + 1] == '*' && l[i + 2] == '@') {
                col[i + 1] = col[i + 2] = H_COMMENT;
                i += 3; *in_cmt = 0;
                break;
            }
        }
        if (*in_cmt) return;
    }

    int first = 1, after_func = 0;
    while (i < n) {
        char c = l[i];
        if (c == ' ') { i++; continue; }

        if (first && c == '@') {                          /* comment line */
            int block = (i + 2 < n && l[i + 1] == '*' && l[i + 2] == '*');
            for (int k = i; k < n; k++) col[k] = H_COMMENT;
            if (block) {
                int closed = 0;
                for (int k = i + 3; k + 2 < n; k++)
                    if (l[k] == '*' && l[k + 1] == '*' && l[k + 2] == '@') closed = 1;
                if (!closed) *in_cmt = 1;
            }
            return;
        }
        if (c == '"') {                                    /* string */
            int j = i + 1;
            while (j < n && l[j] != '"') { if (l[j] == '\\' && j + 1 < n) j++; j++; }
            if (j < n) j++;
            for (int k = i; k < j && k < n; k++) col[k] = H_STR;
            i = j; first = 0;
            continue;
        }
        if ((c >= '0' && c <= '9') || (c == '.' && i + 1 < n && l[i + 1] >= '0' && l[i + 1] <= '9')) {
            int j = i;
            while (j < n && ((l[j] >= '0' && l[j] <= '9') || l[j] == '.')) j++;
            for (int k = i; k < j; k++) col[k] = H_NUM;
            i = j; first = 0;
            continue;
        }
        if (is_idc(c)) {
            int j = i;
            while (j < n && is_idc(l[j])) j++;
            int wl = j - i, kind = H_DEF;
            int k2 = j;
            while (k2 < n && l[k2] == ' ') k2++;
            if (first && (word_is(l + i, wl, "as") || word_is(l + i, wl, "asscript"))) {
                for (int k = i; k < j; k++) col[k] = H_KW;
                int s = j;
                while (s < n && l[s] == ' ') s++;
                int e = s;
                while (e < n && is_idc(l[e])) e++;
                for (int k = s; k < e; k++) col[k] = H_ASCMD;
                i = e; first = 0;
                continue;
            }
            if (is_keyword_w(l + i, wl)) kind = H_KW;
            else if (is_type_w(l + i, wl)) kind = H_TYPE;
            else if (is_builtin_w(l + i, wl)) kind = H_BUILTIN;
            else if (after_func || (k2 < n && l[k2] == '(')) kind = H_FUNC;
            for (int k = i; k < j; k++) col[k] = (u8)kind;
            after_func = word_is(l + i, wl, "func");
            i = j; first = 0;
            continue;
        }
        col[i] = H_OP;
        i++; first = 0;
    }
}

/* ------------------------------------------- other languages (colours only: they are written here, never run) */

enum { L_TXT, L_AS, L_PY, L_C, L_CPP, L_ASM };

static const char *lang_name(int l)
{
    switch (l) {
    case L_AS:  return "AstraScript";
    case L_PY:  return "Python";
    case L_C:   return "C";
    case L_CPP: return "C++";
    case L_ASM: return "Assembly";
    default:    return "plain text";
    }
}

static int lang_of(const char *ext)
{
    static const struct { const char *e; int l; } t[] = {
        { "as", L_AS }, { "py", L_PY }, { "pyw", L_PY }, { "c", L_C }, { "h", L_C },
        { "cpp", L_CPP }, { "cc", L_CPP }, { "cxx", L_CPP }, { "hpp", L_CPP }, { "hh", L_CPP },
        { "asm", L_ASM }, { "s", L_ASM },
    };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) if (!kstrcmp(ext, t[i].e)) return t[i].l;
    return L_TXT;
}

static int in_list(const char *w, int n, const char *const *list)
{
    for (; *list; list++) if (word_is(w, n, *list)) return 1;
    return 0;
}

static const char *const kw_c[] = { "if", "else", "for", "while", "do", "switch", "case", "default", "break", "continue",
    "return", "goto", "sizeof", "typedef", "struct", "union", "enum", "static", "extern", "const", "volatile",
    "inline", "register", "auto", "restrict", 0 };
static const char *const ty_c[] = { "int", "char", "short", "long", "float", "double", "void", "unsigned", "signed",
    "size_t", "bool", "_Bool", "uint8_t", "uint16_t", "uint32_t", "uint64_t", "int8_t", "int16_t", "int32_t", "int64_t",
    "FILE", 0 };
const char *ast_script_template(void) { return "start;\nfunc main(void) {\n   printc(\"Templete of Script\");\n   end;\n};\nend"; }

static const char *const kw_cpp[] = { "class", "public", "private", "protected", "namespace", "using", "template",
    "typename", "new", "delete", "this", "virtual", "override", "final", "try", "catch", "throw", "operator",
    "constexpr", "nullptr", "true", "false", "explicit", "friend", "mutable", "noexcept", 0 };
static const char *const ty_cpp[] = { "string", "vector", "map", "set", "wchar_t", "auto", "std", 0 };
static const char *const kw_py[] = { "def", "class", "if", "elif", "else", "for", "while", "return", "import", "from",
    "as", "with", "try", "except", "finally", "raise", "pass", "break", "continue", "lambda", "yield", "global",
    "nonlocal", "in", "is", "not", "and", "or", "None", "True", "False", "del", "assert", "async", "await", 0 };
static const char *const bi_py[] = { "print", "len", "range", "int", "str", "float", "list", "dict", "set", "tuple",
    "open", "input", "type", "isinstance", "enumerate", "zip", "map", "filter", "sum", "min", "max", "abs",
    "sorted", "self", 0 };
static const char *const kw_asm[] = { "mov", "push", "pop", "call", "ret", "jmp", "je", "jne", "jz", "jnz", "ja", "jb",
    "jae", "jbe", "jg", "jl", "add", "sub", "mul", "div", "inc", "dec", "cmp", "test", "and", "or", "xor", "not",
    "shl", "shr", "lea", "int", "nop", "cli", "sti", "hlt", "in", "out", "iret", "leave", "movl", "movb", "pushl",
    "popl", "addl", "subl", "cmpl", "xorl", "orl", "andl", "db", "dw", "dd", "resb", "times", "section", "global", 0 };
static const char *const reg_asm[] = { "eax", "ebx", "ecx", "edx", "esi", "edi", "esp", "ebp", "ax", "bx", "cx", "dx",
    "al", "bl", "cl", "dl", "ah", "bh", "ch", "dh", "si", "di", "sp", "bp", "cs", "ds", "es", "ss", "cr0", "cr3", 0 };

/*
 * One line of C, C++, Python or assembly. *state carries what spans lines: 1 = inside a C block comment,
 * 2 / 3 = inside a Python triple-quoted string (" / ').
 */
static void highlight_other(const char *l, int n, int *state, u8 *col)
{
    int i = 0;
    kmemset(col, H_DEF, (size_t)n);
    int c_like = lang == L_C || lang == L_CPP;

    if (*state == 1) {
        for (; i < n; i++) {
            col[i] = H_COMMENT;
            if (l[i] == '*' && i + 1 < n && l[i + 1] == '/') { col[i + 1] = H_COMMENT; i += 2; *state = 0; break; }
        }
        if (*state) return;
    } else if (*state >= 2) {
        char q = *state == 2 ? '"' : '\'';
        for (; i < n; i++) {
            col[i] = H_STR;
            if (i + 2 < n && l[i] == q && l[i + 1] == q && l[i + 2] == q) { col[i + 1] = col[i + 2] = H_STR; i += 3; *state = 0; break; }
        }
        if (*state) return;
    }

    int first = 1;
    while (i < n) {
        char c = l[i];
        if (c == ' ') { i++; continue; }
        if (c_like && first && c == '#') {                         /* preprocessor */
            for (int k = i; k < n; k++) col[k] = H_BUILTIN;
            return;
        }
        if ((c_like && c == '/' && i + 1 < n && l[i + 1] == '/') ||
            (lang == L_PY && c == '#') || (lang == L_ASM && (c == ';' || (c == '#' && first)))) {
            for (int k = i; k < n; k++) col[k] = H_COMMENT;
            return;
        }
        if (c_like && c == '/' && i + 1 < n && l[i + 1] == '*') {
            int k = i + 2, closed = 0;
            while (k + 1 < n) { if (l[k] == '*' && l[k + 1] == '/') { closed = 1; break; } k++; }
            int end = closed ? k + 2 : n;
            for (int q = i; q < end; q++) col[q] = H_COMMENT;
            if (!closed) { *state = 1; return; }
            i = end; first = 0;
            continue;
        }
        if (lang == L_PY && (c == '"' || c == '\'') && i + 2 < n && l[i + 1] == c && l[i + 2] == c) {
            int k = i + 3, closed = 0;
            for (; k + 2 < n; k++) if (l[k] == c && l[k + 1] == c && l[k + 2] == c) { closed = 1; break; }
            int end = closed ? k + 3 : n;
            for (int q = i; q < end; q++) col[q] = H_STR;
            if (!closed) { *state = c == '"' ? 2 : 3; return; }
            i = end; first = 0;
            continue;
        }
        if (c == '"' || c == '\'') {
            int j = i + 1;
            while (j < n && l[j] != c) { if (l[j] == '\\' && j + 1 < n) j++; j++; }
            if (j < n) j++;
            for (int k = i; k < j && k < n; k++) col[k] = H_STR;
            i = j; first = 0;
            continue;
        }
        if ((c >= '0' && c <= '9') || (c == '.' && i + 1 < n && l[i + 1] >= '0' && l[i + 1] <= '9')) {
            int j = i;
            while (j < n && (is_idc(l[j]) || l[j] == '.')) j++;
            for (int k = i; k < j; k++) col[k] = H_NUM;
            i = j; first = 0;
            continue;
        }
        if (lang == L_ASM && c == '.' && i + 1 < n && is_idc(l[i + 1])) {      /* directive */
            int j = i + 1;
            while (j < n && is_idc(l[j])) j++;
            for (int k = i; k < j; k++) col[k] = H_BUILTIN;
            i = j; first = 0;
            continue;
        }
        if (is_idc(c)) {
            int j = i;
            while (j < n && is_idc(l[j])) j++;
            int wl = j - i, kind = H_DEF;
            int k2 = j;
            while (k2 < n && l[k2] == ' ') k2++;
            const char *w = l + i;
            if (lang == L_PY) {
                if (in_list(w, wl, kw_py)) kind = H_KW;
                else if (in_list(w, wl, bi_py)) kind = H_BUILTIN;
            } else if (lang == L_ASM) {
                if (in_list(w, wl, kw_asm)) kind = H_KW;
                else if (in_list(w, wl, reg_asm)) kind = H_TYPE;
            } else {
                if (in_list(w, wl, kw_c) || (lang == L_CPP && in_list(w, wl, kw_cpp))) kind = H_KW;
                else if (in_list(w, wl, ty_c) || (lang == L_CPP && in_list(w, wl, ty_cpp))) kind = H_TYPE;
            }
            if (kind == H_DEF && k2 < n && l[k2] == '(') kind = H_FUNC;
            if (kind == H_DEF && lang == L_ASM && j < n && l[j] == ':') kind = H_FUNC;       /* label */
            for (int k = i; k < j; k++) col[k] = (u8)kind;
            i = j; first = 0;
            continue;
        }
        col[i] = H_OP;
        i++; first = 0;
    }
}

/* One line in the language of the file. */
static void highlight_line(const char *l, int n, int *state, u8 *col)
{
    if (lang == L_AS) highlight(l, n, state, col);
    else if (lang == L_TXT) { kmemset(col, H_DEF, (size_t)n); }
    else highlight_other(l, n, state, col);
}

/* -------------------------------------------------------------- diagnostics */

static void recheck(void)
{
    ndiag = 0;
    if (is_script && len) ndiag = asc_check(buf, len, diags, MAX_DIAG);
}

static int diag_for_line(int ln)           /* ln is 0-based */
{
    for (int i = 0; i < ndiag; i++) if (diags[i].line == ln + 1) return i;
    return -1;
}

/* ------------------------------------------------------------------ drawing */

static void sgr_line(int errbg, int fg)
{
    char t[48];
    int o = 0;
    o += ksnprintf(t + o, sizeof t - (size_t)o, "\033[0");
    if (errbg) o += ksnprintf(t + o, sizeof t - (size_t)o, ";48;5;%d", COL_ERR_BG);
    if (fg >= 0) o += ksnprintf(t + o, sizeof t - (size_t)o, ";38;5;%d", fg);
    else if (errbg) o += ksnprintf(t + o, sizeof t - (size_t)o, ";97");
    ksnprintf(t + o, sizeof t - (size_t)o, "m");
    out(t);
}

static void date_text(char *o, size_t cap, u32 secs)
{
    if (!secs) { ksnprintf(o, cap, "new file"); return; }
    hal_time t;
    ktime_unpack(secs, &t);
    ksnprintf(o, cap, "%u-%02u-%02u %02u:%02u", t.year, t.month, t.day, t.hour, t.min);
}

static void draw_top(void)
{
    char date[24], line[160];
    date_text(date, sizeof date, mtime);
    ksnprintf(line, sizeof line, " AST Edit | File: %s%s%s | Ext: %s | Size: %u B | Date: %s | %s",
              fname, fext[0] ? "." : "", fext, fext[0] ? fext : "none", len, date,
              modified ? "Modified" : "Saved");
    if ((int)kstrlen(line) > cols - 1) line[cols - 1] = 0;
    goto_rc(1, 1);
    outf("\033[0;48;5;%d;38;5;%dm", COL_BAR_BG, COL_BAR_FG);
    out(line);
    out("\033[K\033[0m");
}

static void draw_text(int text_rows)
{
    static u8 colors[FS_FILE_MAX + 2];
    int in_cmt = 0;
    int ln = 0;
    u32 p = 0;

    /* comment state at the first visible line */
    while (ln < top && p <= len) {
        u32 e = line_end(p);
        highlight_line(buf + p, (int)(e - p), &in_cmt, colors);
        ln++;
        if (e >= len) { p = len + 1; break; }
        p = e + 1;
    }

    for (int r = 0; r < text_rows; r++) {
        goto_rc(r + 2, 1);
        int exists = (p <= len);
        if (!exists) { out("\033[0m\033[K"); continue; }

        u32 e = line_end(p);
        int n = (int)(e - p);
        highlight_line(buf + p, n, &in_cmt, colors);
        int errbg = (is_script && diag_for_line(top + r) >= 0);
        int prev = -2;
        sgr_line(errbg, -1);
        for (int j = left; j < n && j < left + cols; j++) {
            unsigned char ch = (unsigned char)buf[p + (u32)j];
            if (colors[j] != prev) {
                prev = colors[j];
                sgr_line(errbg, h_color[prev]);
            }
            con_putc(ch >= 32 && ch < 127 ? (char)ch : '.');
        }
        if (errbg) sgr_line(errbg, -1);
        out("\033[K\033[0m");

        if (e >= len) p = len + 1; else p = e + 1;
    }
}

static void draw_hint(int ln, int col, int nlines)
{
    char right[40];
    ksnprintf(right, sizeof right, "Ln %d/%d Col %d ", ln + 1, nlines, col + 1);
    goto_rc(rows - 1, 1);
    out("\033[0m");
    static const char *const k[] = { "^S", "Save", "^X", "Exit", "^K", "Cut", "^U", "Paste" };
    int used = 0;
    for (int i = 0; i < 8; i += 2) {
        outf("\033[38;5;%dm%s\033[38;5;%dm %s  ", COL_KEY, k[i], COL_DIM, k[i + 1]);
        used += (int)kstrlen(k[i]) + 1 + (int)kstrlen(k[i + 1]) + 2;
    }
    out("\033[K");
    int rl = (int)kstrlen(right);
    if (cols - rl > used + 1) {
        outf("\033[%dG\033[38;5;%dm%s", cols - rl + 1, COL_DIM, right);
    }
    out("\033[0m");
}

static void draw_bottom(int ln)
{
    char line[160];
    goto_rc(rows, 1);
    int d = is_script ? diag_for_line(ln) : -1;
    if (msg[0] && msg_kind == 1) {                      /* a fresh "Saved" note wins for one key press */
        ksnprintf(line, sizeof line, " %s", msg);
        if ((int)kstrlen(line) > cols - 1) line[cols - 1] = 0;
        outf("\033[0;38;5;%dm", COL_OK);
        out(line);
        out("\033[K\033[0m");
        return;
    }
    if (is_script && ndiag > 0) {
        if (d < 0) d = 0;
        int more = ndiag - 1;
        if (more > 0)
            ksnprintf(line, sizeof line, " Error: %s - line %d  (+%d more)", diags[d].msg, diags[d].line, more);
        else
            ksnprintf(line, sizeof line, " Error: %s - line %d", diags[d].msg, diags[d].line);
        if ((int)kstrlen(line) > cols - 1) line[cols - 1] = 0;
        outf("\033[0;48;5;%d;38;5;255m", COL_ERR_BG);
        out(line);
        out("\033[K\033[0m");
        return;
    }
    if (msg[0]) {
        ksnprintf(line, sizeof line, " %s", msg);
        if ((int)kstrlen(line) > cols - 1) line[cols - 1] = 0;
        outf("\033[0;38;5;%dm", msg_kind == 2 ? 203 : COL_OK);
        out(line);
        out("\033[K\033[0m");
        return;
    }
    if (is_script) outf("\033[0;38;5;%dm AstraScript: no errors\033[K\033[0m", COL_OK);
    else if (lang == L_PY || lang == L_C || lang == L_CPP || lang == L_ASM)
        outf("\033[0;38;5;%dm %s - can be written here, not run (colours only)\033[K\033[0m",
             COL_DIM, lang_name(lang));
    else out("\033[0m\033[K");
}

static void redraw(void)
{
    int text_rows = rows - 3;
    int ln, col;
    cursor_pos(&ln, &col);

    if (ln < top) top = ln;
    if (ln >= top + text_rows) top = ln - text_rows + 1;
    if (col < left) left = col;
    if (col >= left + cols) left = col - cols + 1;

    out("\033[?25l");
    draw_top();
    draw_text(text_rows);
    draw_hint(ln, col, count_lines());
    draw_bottom(ln);
    goto_rc(ln - top + 2, col - left + 1);
    out("\033[?25h");
}

/* --------------------------------------------------------------- operations */

static void set_msg(int kind, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    kvsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    msg_kind = kind;
}

static int save_file(const char *path, int cwd)
{
    int n = fs_resolve(path, cwd);
    { int g = (n == FS_ENOENT) ? fs_guard_create(path, cwd) : fs_guard_edit(n); if (g < 0) { set_msg(2, "Cannot save: %s", fs_strerror(g)); return -1; } }
    if (n == FS_ENOENT) n = fs_create(path, cwd, 0);
    if (n < 0) { set_msg(2, "Cannot save: %s", fs_strerror(n)); return -1; }
    int r = fs_write(n, buf, len, 0);
    if (r < 0) { set_msg(2, "Cannot save: %s", fs_strerror(r)); return -1; }
    mtime = fs_mtime(n);
    has_node = 1;
    modified = 0;
    persist_flush();
    set_msg(1, "Saved %u bytes%s", len, persist_active() ? " (disk)" : " (RAM only - no disk)");
    return 0;
}

static void indent_of_line(u32 b, int *ind)
{
    int n = 0;
    while (b + (u32)n < len && buf[b + (u32)n] == ' ') n++;
    *ind = n;
}

static void key_enter(void)
{
    u32 b = line_begin(cur);
    int ind;
    indent_of_line(b, &ind);
    if ((u32)ind > cur - b) ind = (int)(cur - b);
    u32 p = cur;
    while (p > b && buf[p - 1] == ' ') p--;
    int opens = (p > b && buf[p - 1] == '{');
    int next_close = (cur < len && buf[cur] == '}');
    int add = opens ? ind + 4 : ind;
    if (add > 60) add = 60;

    char tmp[80];
    int n = 0;
    tmp[n++] = '\n';
    for (int i = 0; i < add; i++) tmp[n++] = ' ';
    u32 start = cur;
    if (!insert(tmp, (u32)n)) return;
    if (opens && next_close) {                          /* {|}  ->  { / indented / } */
        u32 mid = cur;
        n = 0;
        tmp[n++] = '\n';
        for (int i = 0; i < ind; i++) tmp[n++] = ' ';
        insert(tmp, (u32)n);
        cur = mid;
    }
    (void)start;
}

static void key_char(char c)
{
    if (c == '}') {                                      /* dedent a lone closing brace */
        u32 b = line_begin(cur);
        int all_space = 1;
        for (u32 i = b; i < cur; i++) if (buf[i] != ' ') all_space = 0;
        if (all_space && cur - b >= 4) remove_range(cur - 4, 4);
    }
    insert(&c, 1);
}

static void key_tab(void)
{
    int ln, col;
    cursor_pos(&ln, &col);
    int n = 4 - (col % 4);
    char sp[4] = { ' ', ' ', ' ', ' ' };
    insert(sp, (u32)n);
}

static void move_vert(int dl)
{
    u32 b = line_begin(cur);
    int col = want_col >= 0 ? want_col : (int)(cur - b);
    want_col = col;
    while (dl < 0) { if (b == 0) break; b = line_begin(b - 1); dl++; }
    while (dl > 0) { u32 e = line_end(b); if (e >= len) break; b = e + 1; dl--; }
    u32 e = line_end(b);
    cur = b + ((u32)col < e - b ? (u32)col : e - b);
}

static void cut_line(void)
{
    u32 b = line_begin(cur), e = line_end(cur);
    if (e < len) e++;                                    /* take the newline too */
    u32 n = e - b;
    if (n == 0) return;
    if (n > sizeof clip) n = sizeof clip;
    kmemcpy(clip, buf + b, n);
    cliplen = (int)n;
    remove_range(b, e - b);
    cur = b;
}

/* Asks a question on the bottom line; returns the key pressed. */
static int ask(const char *q)
{
    goto_rc(rows, 1);
    out("\033[0;48;5;22;38;5;255m");
    char line[120];
    ksnprintf(line, sizeof line, " %s", q);
    if ((int)kstrlen(line) > cols - 1) line[cols - 1] = 0;
    out(line);
    out("\033[K\033[0m");
    return kbd_getkey();
}

void editor_run(const char *path, int cwd, const char *shown)
{
    { int big = fs_resolve(path, cwd);
      if (big >= 0 && !fs_is_dir(big) && fs_size_real(big) > FS_FILE_MAX) {      /* the editor holds the whole file: bigger ones are refused, never cut */
          kprintf("\x1b[91medit: %s: %s (%u bytes, limit %u)\x1b[0m\n", shown, config_tr("too large to edit", "duzenlemek icin cok buyuk"), fs_size_real(big), (u32)FS_FILE_MAX);
          return;
      } }
    u32 tc = 80, tr = 25;
    hal_term_size(&tc, &tr);
    rows = (int)tr; cols = (int)tc;
    if (rows < 8) rows = 8;
    if (cols < 40) cols = 40;

    /* split "dir/name.ext" into name and extension for the top bar */
    const char *base = shown;
    for (const char *s = shown; *s; s++) if (*s == '/') base = s + 1;
    kstrncpy(fname, base, FS_NAME_MAX);
    fname[FS_NAME_MAX] = 0;
    fext[0] = 0;
    char *dot = kstrrchr(fname, '.');
    if (dot && dot != fname && dot[1]) {
        kstrncpy(fext, dot + 1, FS_NAME_MAX);
        fext[FS_NAME_MAX] = 0;
        *dot = 0;
    }
    lang = lang_of(fext);
    is_script = lang == L_AS;

    len = cur = 0; top = left = 0; want_col = -1; modified = 0; msg[0] = 0; msg_kind = 0;
    mtime = 0; has_node = 0; un = rn = 0; last_class = 0; class_run = 0;

    int n = fs_resolve(path, cwd);
    if (n >= 0 && !fs_is_dir(n)) {                       /* load; tabs become 4 spaces */
        const char *d = fs_data(n);
        u32 sz = fs_size(n);
        for (u32 i = 0; i < sz && len < FS_FILE_MAX; i++) {
            if (d[i] == '\t' && len + 4 <= FS_FILE_MAX) { for (int k = 0; k < 4; k++) buf[len++] = ' '; }
            else buf[len++] = d[i];
        }
        mtime = fs_mtime(n);
        has_node = 1;
    } else if (n >= 0) {
        return;
    } else {
        set_msg(1, "New file: %s", shown);
        if (is_script) {                                 /* a new AstraScript file starts from the template */
            const char *tpl = ast_script_template();
            for (u32 i = 0; tpl[i]; i++) buf[len++] = tpl[i];
            cur = 0; int nl = 0;
            while (cur < len && nl < 3) { if (buf[cur] == '\n') nl++; cur++; }   /* the cursor waits before "end;" of main() */
        }
    }

    out("\033[0m\033[2J");
    recheck();
    for (;;) {
        redraw();
        int k = kbd_getkey();
        int keep_want = 0;
        if (msg[0] && k != 0) { msg[0] = 0; msg_kind = 0; }

        if (k == '\b' || k == KEY_DEL) undo_note(2);
        else if (k == '\n' || k == '\t' || k == 11 || k == 22) undo_note(3);
        else if (k >= 32 && k < 127) undo_note(1);

        switch (k) {
        case 26:        undo_do(); break;                    /* Ctrl+Z */
        case 25:        redo_do(); break;                    /* Ctrl+Y */
        case 3: {                                            /* Ctrl+C: copy the current line */
            u32 b = line_begin(cur), e = line_end(cur);
            if (e < len) e++;
            if (e > b) ast_clip_set(buf + b, (int)(e - b));
            break; }
        case 22:        if (cliplen) insert(clip, (u32)cliplen); break;   /* Ctrl+V: paste */
        case KEY_UP:    move_vert(-1); keep_want = 1; break;
        case KEY_DOWN:  move_vert(1);  keep_want = 1; break;
        case KEY_PGUP:  move_vert(-(rows - 4)); keep_want = 1; break;
        case KEY_PGDN:  move_vert(rows - 4);    keep_want = 1; break;
        case KEY_LEFT:  if (cur > 0) cur--; break;
        case KEY_RIGHT: if (cur < len) cur++; break;
        case KEY_HOME: case 1:  cur = line_begin(cur); break;
        case KEY_END:  case 5:  cur = line_end(cur); break;
        case KEY_DEL:   if (cur < len) remove_range(cur, 1); break;
        case '\b':      if (cur > 0) remove_range(cur - 1, 1); break;
        case '\n':      key_enter(); break;
        case '\t':      key_tab(); break;
        case 11:        cut_line(); break;               /* Ctrl+K */
        case 21:        if (cliplen) insert(clip, (u32)cliplen); break;   /* Ctrl+U (older paste key) */
        case 19:        save_file(path, cwd); break;     /* Ctrl+S */
        case 24:                                         /* Ctrl+X */
            if (modified) {
                int a = ask("Save modified buffer?  Y = save   N = discard   Ctrl+C = cancel");
                if (a == 'y' || a == 'Y') { if (save_file(path, cwd) == 0) goto done; }
                else if (a == 'n' || a == 'N') goto done;
                break;
            }
            goto done;
        default:
            if (k >= 32 && k < 127) key_char((char)k);
            break;
        }
        if (!keep_want) want_col = -1;
        recheck();
    }
done:
    out("\033[0m");
    con_clear();
    (void)has_node;
}
