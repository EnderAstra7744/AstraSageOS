/*
 * ASPI - AstraSage Package Installer.
 *
 *   aspi install <Name> [ver]   aspi remove [-f] <Name>   aspi list
 *   aspi info <Name>            aspi repo                 aspi log <Name>
 *   aspi run <Name> [script]
 *
 * Where things live (OS root, outside every user's S: folder):
 *   /.aspi/lib/<Name>/   the package's files (AstraScript files are checked and sealed)
 *   /.aspi/app/<Name>.pkg  install record      /.term/log/<Name>.log  install log
 *
 * Packages come from GitHub (github.com/EnderAstra7744/aspi-package-sage, downloaded over the built-in
 * TLS client; archives: .aspi, .tar.gz, .tar.xz), from the packages built into the OS image, or from an
 * optional repo disk (second ATA disk filled by sys/tools/aspi_sync.py, for offline use). Archive
 * conversion, manifest and installer.txt, dependency resolution, compiling .as files, copying and
 * logging happen here, and every line printed is something that really ran.
 */
#include "astra.h"

#define GH_REPO       "EnderAstra7744/aspi-package-sage"
#define REPO_SOURCE   "github.com/" GH_REPO
#define REPO_DRIVE    (config_get()->repo_drive > 1 ? 1 : config_get()->repo_drive)
#define MAX_REPO      64
#define ARC_IN_MAX    (3u << 20)
#define LOG_MAX       8192
#define ASC_CTX_MAX   40000

/* ------------------------------------------------------------------ repo disk */

typedef struct { char name[32], version[24], kind[8]; u32 lba, size; u8 sha[32]; u8 net; u8 tagged; char path[96]; } repo_ent;   /* net=1: download from GitHub */

static repo_ent repo[MAX_REPO];
static int repo_n = -1;              /* -1 = not probed, 0 = no repo disk */
static char repo_source[64];

/* packages carried inside the kernel image (no repo disk or network needed); absent in the host test */
#ifdef AS_NO_IO      /* test harness only: two packages that stand in for GitHub (the real OS has none built in) */
extern const u8 asset_froggy_start[], asset_froggy_end[], asset_nspkg_start[], asset_nspkg_end[];
#define NETSURF_VERSION "3.11.0"
#define FROGGY_VERSION "2.0.0"
#endif

static int repo_open_disk(void)
{
    if (repo_n >= 0) return repo_n > 0 || 0;
    repo_n = 0;
    static u8 sec[512];
    if (!ata_init_drive(REPO_DRIVE)) return 0;
    if (ata_read_drive(REPO_DRIVE, 0, sec) < 0 || kmemcmp(sec, "ASPIREPO", 8)) return 0;
    u32 ver = sec[8] | sec[9] << 8 | sec[10] << 16 | (u32)sec[11] << 24;
    u32 count = sec[12] | sec[13] << 8 | sec[14] << 16 | (u32)sec[15] << 24;
    if (ver != 1 || count > MAX_REPO) return 0;
    kmemcpy(repo_source, sec + 16, 63);
    repo_source[63] = 0;
    for (u32 i = 0; i < count; i++) {
        if (i % 4 == 0 && ata_read_drive(REPO_DRIVE, 1 + i / 4, sec) < 0) return 0;
        const u8 *e = sec + (i % 4) * 128;
        repo_ent *r = &repo[i];
        kmemcpy(r->name, e, 32); r->name[31] = 0;
        kmemcpy(r->version, e + 32, 24); r->version[23] = 0;
        kmemcpy(r->kind, e + 56, 8); r->kind[7] = 0;
        r->lba = e[64] | e[65] << 8 | e[66] << 16 | (u32)e[67] << 24;
        r->size = e[68] | e[69] << 8 | e[70] << 16 | (u32)e[71] << 24;
        kmemcpy(r->sha, e + 72, 32);
    }
    repo_n = (int)count;
    return repo_n > 0;
}

static int repo_open(void)
{
    if (repo_n >= 0) return repo_n > 0;
    repo_open_disk();
    if (repo_n < 0) repo_n = 0;
#ifdef AS_NO_IO
    if (asset_froggy_start && repo_n < MAX_REPO) {
        repo_ent *r = &repo[repo_n];
        kmemset(r, 0, sizeof *r);
        kstrncpy(r->name, "FroggyCode", 31);
        kstrncpy(r->version, FROGGY_VERSION, 23);
        kstrncpy(r->kind, "aspi", 7);
        r->size = (u32)(asset_froggy_end - asset_froggy_start);
        r->net = 2;
        sha256(asset_froggy_start, r->size, r->sha);
        repo_n++;
    }
    if (asset_nspkg_start && repo_n < MAX_REPO) {
        repo_ent *r = &repo[repo_n];
        kmemset(r, 0, sizeof *r);
        kstrncpy(r->name, "NetSurf", 31);
        kstrncpy(r->version, NETSURF_VERSION, 23);
        kstrncpy(r->kind, "aspi", 7);
        r->size = (u32)(asset_nspkg_end - asset_nspkg_start);
        r->net = 3;
        sha256(asset_nspkg_start, r->size, r->sha);
        repo_n++;
    }
#endif
    return repo_n > 0;
}

static int ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a;
        char y = *b >= 'A' && *b <= 'Z' ? (char)(*b + 32) : *b;
        if (x != y) return 0;
    }
    return *a == *b;
}

/* dotted version compare: 1.10 > 1.9; non-numeric characters separate parts */
static int vcmp(const char *a, const char *b)
{
    for (;;) {
        while (*a && (*a < '0' || *a > '9')) a++;
        while (*b && (*b < '0' || *b > '9')) b++;
        if (!*a && !*b) return 0;
        u32 x = 0, y = 0;
        while (*a >= '0' && *a <= '9') x = x * 10 + (u32)(*a++ - '0');
        while (*b >= '0' && *b <= '9') y = y * 10 + (u32)(*b++ - '0');
        if (x != y) return x < y ? -1 : 1;
    }
}

static int repo_find(const char *name, const char *ver)
{
    int best = -1;
    int latest = !ver || !*ver || ieq(ver, "lastver") || ieq(ver, "latest");
    for (int i = 0; i < repo_n; i++) {
        if (!ieq(repo[i].name, name)) continue;
        if (!latest) { if (vcmp(repo[i].version, ver) == 0) return i; continue; }
        if (best < 0 || vcmp(repo[i].version, repo[best].version) > 0 || (vcmp(repo[i].version, repo[best].version) == 0 && repo[i].tagged && !repo[best].tagged)) best = i;
    }
    return best;
}

/* ------------------------------------------------------------------ output + log */

static char logbuf[LOG_MAX];
static u32  loglen;
static int  bar_pct = -1;            /* -1: no progress bar being shown */
static int  nest;                    /* >0 while installing a dependency */

#define C_OK   "\033[38;5;112m"
#define C_BAD  "\033[91m"
#define C_WARN "\033[93m"
#define C_DIM  "\033[90m"
#define C_HI   "\033[38;5;154m"
#define C_RST  "\033[0m"

static void log_add(const char *s)
{
    size_t n = kstrlen(s);
    if (loglen + n + 1 >= LOG_MAX) {                         /* drop the oldest half */
        u32 cut = LOG_MAX / 2;
        while (cut < loglen && logbuf[cut] != '\n') cut++;
        cut++;
        if (cut > loglen) cut = loglen;
        kmemmove(logbuf, logbuf + cut, loglen - cut);
        loglen -= cut;
    }
    if (n + 1 >= LOG_MAX) n = LOG_MAX - 2;
    kmemcpy(logbuf + loglen, s, n);
    loglen += (u32)n;
    logbuf[loglen++] = '\n';
}

static void bar_clear(void) { if (bar_pct >= 0) kputs("\r\033[K"); }

static void bar_draw(void)
{
    if (bar_pct < 0) return;
    int hashes = bar_pct * 20 / 100;
    char b[24];
    for (int i = 0; i < 20; i++) b[i] = i < hashes ? '#' : '.';
    b[20] = 0;
    kprintf("\r\033[K" C_HI "%%%d" C_RST " [%s]", bar_pct, b);
}

static void progress(int pct)
{
    if (nest || bar_pct < 0) return;
    if (pct > 100) pct = 100;
    if (pct < bar_pct) pct = bar_pct;
    bar_pct = pct;
    bar_draw();
}

/* kind: 0 plain line, 1 "... Done", 2 "... FAILED", 3 warning */
static void say(int kind, const char *fmt, ...)
{
    char msg[200], line[240], plain[240];
    va_list ap;
    va_start(ap, fmt);
    kvsnprintf(msg, sizeof msg, TT(fmt), ap);
    va_end(ap);
    const char *pad = nest ? "    " : "";
    switch (kind) {
    case 1: ksnprintf(plain, sizeof plain, "%s%s. %s", pad, msg, TT("Done"));
            ksnprintf(line, sizeof line, "%s%s. " C_OK "%s" C_RST, pad, msg, TT("Done")); break;
    case 2: ksnprintf(plain, sizeof plain, "%s%s. %s", pad, msg, TT("FAILED"));
            ksnprintf(line, sizeof line, "%s%s. " C_BAD "%s" C_RST, pad, msg, TT("FAILED")); break;
    case 3: ksnprintf(plain, sizeof plain, "%s%s%s", pad, TT("WARNING: "), msg);
            ksnprintf(line, sizeof line, "%s" C_WARN "%s%s" C_RST, pad, TT("WARNING: "), msg); break;
    default: ksnprintf(plain, sizeof plain, "%s%s", pad, msg);
             ksnprintf(line, sizeof line, "%s%s", pad, msg); break;
    }
    log_add(plain);
    bar_clear();
    kputs(line);
    kputs("\n");
    bar_draw();
}

/* ------------------------------------------------------------------ OS-root file helpers */

static int os_enter(void) { int o = fs_get_jail(); fs_set_jail(0); return o; }
static void os_leave(int o) { fs_set_jail(o); }

static int mkdir_p(const char *path)
{
    char p[160];
    size_t n = kstrlen(path);
    if (n >= sizeof p) return FS_ENAME;
    kmemcpy(p, path, n + 1);
    for (size_t i = 1; i <= n; i++) {
        if (p[i] != '/' && p[i] != 0) continue;
        char c = p[i]; p[i] = 0;
        int r = fs_create(p, 0, 1);
        if (r < 0 && r != FS_EEXIST) return r;
        p[i] = c;
    }
    return FS_OK;
}

static int put_file(const char *path, const u8 *data, u32 size)
{
    char dir[160];
    size_t n = kstrlen(path);
    if (n >= sizeof dir) return FS_ENAME;
    kmemcpy(dir, path, n + 1);
    char *slash = kstrrchr(dir, '/');
    if (slash && slash != dir) { *slash = 0; int r = mkdir_p(dir); if (r < 0) return r; }
    int node = fs_resolve(path, 0);
    if (node == FS_ENOENT) node = fs_create(path, 0, 0);
    if (node < 0) return node;
    return fs_write(node, (const char *)data, size, 0);
}

static int has_ext(const char *name, const char *ext)
{
    size_t a = kstrlen(name), b = kstrlen(ext);
    return a > b && ieq(name + a - b, ext);
}

/* ------------------------------------------------------------------ manifest / installer.txt */

static u8  arc_in[ARC_IN_MAX];
static arc_list files;

static const arc_entry *arc_find(const char *name)
{
    for (int i = 0; i < files.n; i++) if (!kstrcmp(files.e[i].name, name)) return &files.e[i];
    return 0;
}

/* tiny JSON string reader: "key": "value" (handles \" \\ \n \t) */
static int json_str(const char *t, u32 len, const char *key, char *out, size_t cap)
{
    char pat[48];
    ksnprintf(pat, sizeof pat, "\"%s\"", key);
    size_t pl = kstrlen(pat);
    for (u32 i = 0; i + pl < len; i++) {
        if (kmemcmp(t + i, pat, pl)) continue;
        u32 j = i + (u32)pl;
        while (j < len && (t[j] == ' ' || t[j] == '\t' || t[j] == '\r' || t[j] == '\n')) j++;
        if (j >= len || t[j] != ':') continue;
        j++;
        while (j < len && (t[j] == ' ' || t[j] == '\t' || t[j] == '\r' || t[j] == '\n')) j++;
        if (j >= len || t[j] != '"') return 0;
        j++;
        size_t o = 0;
        while (j < len && t[j] != '"') {
            char c = t[j++];
            if (c == '\\' && j < len) {
                c = t[j++];
                c = c == 'n' ? '\n' : c == 't' ? '\t' : c;
            }
            if (o + 1 < cap) out[o++] = c;
        }
        out[o] = 0;
        return 1;
    }
    return 0;
}

/* "key": ["a", "b"]  ->  "a,b" (strings only; used for "commands") */
static int json_arr(const char *t, u32 len, const char *key, char *out, size_t cap)
{
    char pat[48];
    ksnprintf(pat, sizeof pat, "\"%s\"", key);
    size_t pl = kstrlen(pat);
    out[0] = 0;
    for (u32 i = 0; i + pl < len; i++) {
        if (kmemcmp(t + i, pat, pl)) continue;
        u32 j = i + (u32)pl;
        while (j < len && (t[j] == ' ' || t[j] == '\t' || t[j] == '\r' || t[j] == '\n')) j++;
        if (j >= len || t[j] != ':') continue;
        j++;
        while (j < len && (t[j] == ' ' || t[j] == '\t' || t[j] == '\r' || t[j] == '\n')) j++;
        if (j >= len || t[j] != '[') return 0;
        j++;
        size_t o = 0;
        while (j < len && t[j] != ']') {
            if (t[j] == '"') {
                j++;
                if (o && o + 1 < cap) out[o++] = ',';
                while (j < len && t[j] != '"') { if (o + 1 < cap) out[o++] = t[j]; j++; }
            }
            j++;
        }
        out[o] = 0;
        return 1;
    }
    return 0;
}

typedef struct { char name[32], url[96], ver[24]; } section;
#define MAX_SEC 12
static section secs[MAX_SEC];
static int nsec;
static char eap[MAX_SEC][32];
static int neap;

static void trim_end(char *s) { size_t n = kstrlen(s); while (n && (s[n - 1] == ' ' || s[n - 1] == '\r' || s[n - 1] == '\t')) s[--n] = 0; }

static void parse_installer(const char *t, u32 len)
{
    nsec = neap = 0;
    int in_eap = 0;
    u32 i = 0;
    while (i < len) {
        char line[160]; size_t n = 0;
        while (i < len && t[i] != '\n') { if (n + 1 < sizeof line) line[n++] = t[i]; i++; }
        i++;
        line[n] = 0; trim_end(line);
        char *l = line;
        while (*l == ' ' || *l == '\t') l++;
        if (!*l) { in_eap = 0; continue; }
        if (l[0] == '#' && l[1] == '\'') {                          /* #' Name   : a section */
            in_eap = 0;
            l += 2; while (*l == ' ') l++;
            if (nsec < MAX_SEC) { kmemset(&secs[nsec], 0, sizeof secs[0]); kstrncpy(secs[nsec].name, l, 31); nsec++; }
            continue;
        }
        if (l[0] == '#' && l[1] == '*' && nsec) {                   /* #* url   /  #* ver='x' */
            l += 2; while (*l == ' ') l++;
            section *s = &secs[nsec - 1];
            if (!kstrncmp(l, "ver", 3)) {
                char *q = kstrchr(l, '\'');
                if (!q) q = kstrchr(l, '"');
                if (q) { char *e = kstrchr(q + 1, *q); if (e) { *e = 0; kstrncpy(s->ver, q + 1, 23); } }
            } else kstrncpy(s->url, l, 95);
            continue;
        }
        if (!kstrncmp(l, "#EAP", 4)) { in_eap = 1; continue; }
        if (l[0] == '#') { in_eap = 0; continue; }
        if (in_eap) {
            if (!kstrncmp(l, "\xE2\x80\xA2", 3)) l += 3;            /* bullet */
            else if (*l == '-' || *l == '*') l++;
            while (*l == ' ') l++;
            if (*l && neap < MAX_SEC) kstrncpy(eap[neap++], l, 31);
        }
    }
}

static const section *find_section(const char *name)
{
    for (int i = 1; i < nsec; i++) if (ieq(secs[i].name, name)) return &secs[i];
    return 0;
}

/* ------------------------------------------------------------------ install records */

typedef struct { char name[32], version[24], deps[160], type[12], cmds[84]; } rec;

static int read_rec(const char *name, rec *r, const char **raw, u32 *rawlen)
{
    char p[80];
    ksnprintf(p, sizeof p, "/.aspi/app/%s.pkg", name);
    int n = fs_resolve(p, 0);
    if (n < 0 || fs_is_dir(n)) return 0;
    const char *d = fs_data(n);
    u32 len = fs_size(n);
    kmemset(r, 0, sizeof *r);
    for (u32 i = 0; i < len;) {
        u32 e = i;
        while (e < len && d[e] != '\n') e++;
        if (!kstrncmp(d + i, "name=", 5))    { u32 k = e - i - 5; if (k > 31) k = 31; kmemcpy(r->name, d + i + 5, k); }
        if (!kstrncmp(d + i, "version=", 8)) { u32 k = e - i - 8; if (k > 23) k = 23; kmemcpy(r->version, d + i + 8, k); }
        if (!kstrncmp(d + i, "deps=", 5))    { u32 k = e - i - 5; if (k > 159) k = 159; kmemcpy(r->deps, d + i + 5, k); }
        if (!kstrncmp(d + i, "type=", 5))    { u32 k = e - i - 5; if (k > 11) k = 11; kmemcpy(r->type, d + i + 5, k); }
        if (!kstrncmp(d + i, "commands=", 9)) { u32 k = e - i - 9; if (k > 83) k = 83; kmemcpy(r->cmds, d + i + 9, k); }
        i = e + 1;
    }
    if (raw) *raw = d;
    if (rawlen) *rawlen = len;
    return 1;
}

static int is_installed(const char *name) { rec r; int o = os_enter(); int ok = read_rec(name, &r, 0, 0); os_leave(o); return ok; }

static void do_run_cmd(const char *name, const char *cmd);
static char native_args[160];                    /* what the user typed after the command: "as netsurf http://..." */

/* ------------------------------------------------------------------ package type and commands */

static int type_ok(const char *t)
{
    return !kstrcmp(t, "app") || !kstrcmp(t, "tool") || !kstrcmp(t, "library") || !kstrcmp(t, "data") || !kstrcmp(t, "system");
}

static int cmd_name_ok(const char *c)
{
    size_t n = kstrlen(c);
    if (n < 1 || n > 16) return 0;
    for (size_t i = 0; i < n; i++)
        if (!((c[i] >= 'a' && c[i] <= 'z') || (c[i] >= '0' && c[i] <= '9') || c[i] == '_' || c[i] == '-')) return 0;
    return 1;
}

/* does the comma list contain exactly this word? */
static int list_has(const char *list, const char *w)
{
    size_t wl = kstrlen(w);
    for (const char *p = list; *p;) {
        const char *e = p; while (*e && *e != ',') e++;
        if ((size_t)(e - p) == wl && !kstrncmp(p, w, wl)) return 1;
        p = *e ? e + 1 : e;
    }
    return 0;
}

/* installed package (other than `except`) that already owns command `cmd`; 1 + its name in out */
static int cmd_owner(const char *cmd, const char *except, char *out)
{
    int o = os_enter(), found = 0;
    int bin = fs_resolve("/.aspi/app", 0);
    if (bin > 0)
        for (int c = fs_child(bin, 0); c > 0 && !found; c = fs_child(bin, c)) {
            const char *nm = fs_name(c);
            if (!has_ext(nm, ".pkg")) continue;
            char pn[32]; size_t l = kstrlen(nm) - 4; if (l > 31) l = 31;
            kmemcpy(pn, nm, l); pn[l] = 0;
            if (except && !kstrcmp(pn, except)) continue;
            rec r;
            if (read_rec(pn, &r, 0, 0) && list_has(r.cmds, cmd)) { kstrncpy(out, pn, 31); out[31] = 0; found = 1; }
        }
    os_leave(o);
    return found;
}

static int valid_pkg_name(const char *n);
int aspi_app_packages(char (*names)[32], int max)
{
    int o = os_enter(), n = 0;
    int bin = fs_resolve("/.aspi/app", 0);
    if (bin > 0)
        for (int c = fs_child(bin, 0); c > 0 && n < max; c = fs_child(bin, c)) {
            const char *nm = fs_name(c);
            if (!has_ext(nm, ".pkg")) continue;
            char pn[32]; size_t l = kstrlen(nm) - 4; if (l > 31) l = 31;
            kmemcpy(pn, nm, l); pn[l] = 0;
            rec r;
            if (read_rec(pn, &r, 0, 0) && !kstrcmp(r.type, "app")) { kstrncpy(names[n], pn, 31); names[n][31] = 0; n++; }
        }
    os_leave(o);
    return n;
}

int aspi_app_command(const char *name, char *out, int cap)
{
    int o = os_enter(), ok = 0;
    rec r;
    out[0] = 0;
    if (valid_pkg_name(name) && read_rec(name, &r, 0, 0)) {
        int i = 0;
        while (r.cmds[i] && r.cmds[i] != ',' && i < cap - 1) { out[i] = r.cmds[i]; i++; }
        out[i] = 0;
        ok = 1;
    }
    os_leave(o);
    return ok;
}

/*
 * Checks the manifest's "commands" list: valid names, not an OS command, not owned by another package.
 * Returns 0 when fine, -1 (after saying why) otherwise.
 */
static int check_commands(const char *cmds, const char *self)
{
    char w[20], owner[32];
    for (const char *p = cmds; *p;) {
        const char *e = p; while (*e && *e != ',') e++;
        size_t l = (size_t)(e - p); if (l > 19) l = 19;
        kmemcpy(w, p, l); w[l] = 0;
        p = *e ? e + 1 : e;
        if (!cmd_name_ok(w)) { say(2, "Command '%s': use 1-16 characters a-z 0-9 _ -", w); return -1; }
        if (ast_is_builtin_name(w)) { say(2, "Command '%s' is an AST command and cannot be taken by a package", w); return -1; }
        if (cmd_owner(w, self, owner)) { say(2, "Command '%s' already belongs to package %s", w, owner); return -1; }
    }
    return 0;
}

/*
 * "as <word>" where <word> is not an AST command: runs the installed package that declared it in its
 * manifest ("commands"): script <word>.as, else main.as, else <Package>.as. Returns 1 when handled.
 */
int aspi_package_command(const char *word, const char *args)
{
    char owner[32];
    if (!cmd_name_ok(word) || !cmd_owner(word, 0, owner)) return 0;
    if (!user_name()) { kprintf(C_BAD "AST: login required" C_RST " - as login <PASSWORD> <NAME>\n"); return 1; }
    kstrncpy(native_args, args ? args : "", sizeof native_args - 1); native_args[sizeof native_args - 1] = 0;
    do_run_cmd(owner, word);
    native_args[0] = 0;
    return 1;
}

int aspi_installed(const char *name) { return is_installed(name); }

static int valid_pkg_name(const char *n)
{
    size_t l = kstrlen(n);
    if (l < 1 || l > 23) return 0;
    for (; *n; n++) {
        char c = *n;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return 0;
    }
    return 1;
}

static int in_deps(const char *deps, const char *name)
{
    size_t nl = kstrlen(name);
    for (const char *p = deps; *p;) {
        const char *e = p;
        while (*e && *e != ',') e++;
        if ((size_t)(e - p) == nl) { char t[32]; kmemcpy(t, p, nl); t[nl] = 0; if (ieq(t, name)) return 1; }
        p = *e ? e + 1 : e;
    }
    return 0;
}

static void dependents_of(const char *name, char *out, size_t cap)
{
    out[0] = 0;
    int bin = fs_resolve("/.aspi/app", 0);
    if (bin < 0) return;
    for (int c = fs_child(bin, 0); c > 0; c = fs_child(bin, c)) {
        const char *nm = fs_name(c);
        if (!has_ext(nm, ".pkg")) continue;
        char pn[32]; size_t l = kstrlen(nm) - 4; if (l > 31) l = 31;
        kmemcpy(pn, nm, l); pn[l] = 0;
        rec r;
        if (read_rec(pn, &r, 0, 0) && in_deps(r.deps, name) && !ieq(pn, name)) {
            size_t o = kstrlen(out);
            ksnprintf(out + o, cap - o, "%s%s", o ? ", " : "", pn);
        }
    }
}

/* ------------------------------------------------------------------ install */

typedef struct { char name[48], ver[32], desc[160], desc2[700], type[12], cmds[84]; } manifest;
typedef struct { int n; char name[MAX_SEC][32], ver[MAX_SEC][24], url[MAX_SEC][96]; } plan;

static int  stack_n;
static char stack_name[6][32];

static void write_log(const char *name)
{
    char p[80];
    ksnprintf(p, sizeof p, "/.term/log/%s.log", name);
    u32 off = loglen > 4000 ? loglen - 4000 : 0;
    while (off && off < loglen && logbuf[off - 1] != '\n') off++;
    int o = os_enter();
    put_file(p, (const u8 *)logbuf + off, loglen - off);
    os_leave(o);
}

static void rollback(const char *name)
{
    char p[80];
    ksnprintf(p, sizeof p, "/.aspi/lib/%s", name);
    int o = os_enter();
    int n = fs_resolve(p, 0);
    if (n > 0) fs_delete(n);
    os_leave(o);
}

static void fmt_size(char *o, size_t cap, u32 b)
{
    if (b < 1024) ksnprintf(o, cap, "%u B", b);
    else ksnprintf(o, cap, "%u.%u KiB", b / 1024, (b % 1024) * 10 / 1024);
}

/*
 * Reads repo entry `ri` into arc_in / files and parses manifest and installer.txt.
 * `loud` = print the steps (the first pass); the second pass after dependencies is silent.
 */
/* ------------------------------------------------------------------ GitHub repository (network) */

static int  net_index_done;

static int json_num(const char *t, u32 len, const char *key, u32 *out)
{
    char pat[48];
    ksnprintf(pat, sizeof pat, "\"%s\"", key);
    size_t pl = kstrlen(pat);
    for (u32 i = 0; i + pl < len; i++) {
        if (kmemcmp(t + i, pat, pl)) continue;
        u32 j = i + (u32)pl;
        while (j < len && (t[j] == ' ' || t[j] == ':')) j++;
        if (j >= len || t[j] < '0' || t[j] > '9') return 0;
        u32 v = 0;
        while (j < len && t[j] >= '0' && t[j] <= '9') v = v * 10 + (u32)(t[j++] - '0');
        *out = v;
        return 1;
    }
    return 0;
}

static int valid_pkg_name(const char *n);

/*
 * "Name-1.2.3" / "Name_1.2.3" / "Name-v1.2" -> name "Name", version "1.2.3"; no version in the file name -> "0".
 * (The real version is the one in the archive's ASPIManifest.json; the file name is only used to find the package.)
 */
static void split_name_ver(const char *base, char *name, size_t ncap, char *ver, size_t vcap)
{
    int cut = -1;
    for (int i = 1; base[i]; i++) {
        if ((base[i] == '-' || base[i] == '_') && ((base[i + 1] >= '0' && base[i + 1] <= '9') ||
            ((base[i + 1] == 'v' || base[i + 1] == 'V') && base[i + 2] >= '0' && base[i + 2] <= '9'))) cut = i;
    }
    if (cut < 0) { kstrncpy(name, base, ncap - 1); name[ncap - 1] = 0; kstrncpy(ver, "0", vcap); return; }
    size_t n = (size_t)cut < ncap - 1 ? (size_t)cut : ncap - 1;
    kmemcpy(name, base, n); name[n] = 0;
    const char *v = base + cut + 1;
    if (*v == 'v' || *v == 'V') v++;
    kstrncpy(ver, v, vcap - 1); ver[vcap - 1] = 0;
}

/* repo path -> URL text (spaces and a few unsafe characters are percent-encoded) */
static void url_path(const char *in, char *out, size_t cap)
{
    size_t n = 0;
    for (; *in && n + 4 < cap; in++) {
        unsigned char c = (unsigned char)*in;
        if (c <= ' ' || c == '#' || c == '?' || c == '%' || c >= 127) { out[n++] = '%'; out[n++] = "0123456789ABCDEF"[c >> 4]; out[n++] = "0123456789ABCDEF"[c & 15]; }
        else out[n++] = (char)c;
    }
    out[n] = 0;
}

static int cache_index(void);

/* reads the file list of the GitHub repository once and appends every archive to repo[] */
static int net_index_load(void)
{
    if (net_index_done) return 1;
    repo_open();
    const char *url = "https://api.github.com/repos/" GH_REPO "/git/trees/HEAD?recursive=1";
    const char *body; u32 bl; int st, tr;
    say(0, "Asking GitHub for the package list (%s)", REPO_SOURCE);
    if (!net_http_get(url, &body, &bl, &st, &tr, 0)) { say(2, "GitHub: %s", net_http_error()); return cache_index(); }
    if (st != 200) { say(2, "GitHub answered HTTP %d%s", st, st == 404 ? " (repository or branch not found)" : st == 403 ? " (rate limit reached? try again later)" : ""); return cache_index(); }
    if (tr) say(3, "The file list is longer than 32 KiB - only the first part is used");
    if (repo_n < 0) repo_n = 0;
    int added = 0;
    for (u32 i = 0; i < bl; i++) {
        if (body[i] != '{') continue;
        u32 e = i;
        while (e < bl && body[e] != '}') e++;
        if (e >= bl) break;
        const char *o = body + i; u32 ol = e - i;
        i = e;
        char path[96], type[12], base[64], file[64];
        u32 size = 0;
        if (!json_str(o, ol, "path", path, sizeof path) || !json_str(o, ol, "type", type, sizeof type) || kstrcmp(type, "blob")) continue;
        json_num(o, ol, "size", &size);
        const char *fn = path;
        for (const char *q = path; *q; q++) if (*q == '/') fn = q + 1;
        kstrncpy(file, fn, 63); file[63] = 0;
        int kind = -1; size_t fl = kstrlen(file), cutl = 0;
        static const struct { const char *ext; const char *kind; } ex[] = { { ".aspi", "aspi" }, { ".tar.gz", "tar.gz" }, { ".tgz", "tar.gz" }, { ".tar.xz", "tar.xz" }, { ".txz", "tar.xz" } };
        repo_ent *r = &repo[repo_n < MAX_REPO ? repo_n : MAX_REPO - 1];
        if (repo_n >= MAX_REPO) { say(3, "Too many packages in the repository - the rest is ignored"); break; }
        for (unsigned k = 0; k < sizeof ex / sizeof ex[0]; k++) {
            size_t el = kstrlen(ex[k].ext);
            if (fl > el && !kstrcmp(file + fl - el, ex[k].ext)) { kind = (int)k; cutl = el; kstrncpy(r->kind, ex[k].kind, 7); break; }
        }
        if (kind < 0) continue;
        if (size == 0) continue;
        if (size > ARC_IN_MAX) { say(3, "%s is %u bytes - larger than the 3 MiB archive limit, skipped", file, size); continue; }
        kmemcpy(base, file, fl - cutl); base[fl - cutl] = 0;
        r->tagged = 0;
        {   /* "Name-1.0-arm64.aspi": an archive for one processor; other processors do not list it */
            static const char *const tags[] = { "-arm64", "-arm32", "-x86" };
            size_t bl = kstrlen(base);
            for (unsigned t = 0; t < 3; t++) {
                size_t tl = kstrlen(tags[t]);
                if (bl > tl && !kstrcmp(base + bl - tl, tags[t])) {
#if defined(__aarch64__)
                    int mine = t == 0;
#elif defined(__arm__)
                    int mine = t == 1;
#else
                    int mine = t == 2;
#endif
                    if (!mine) goto skip_entry;
                    base[bl - tl] = 0; r->tagged = 1;
                    break;
                }
            }
        }
        char nm[32], vr[24];
        split_name_ver(base, nm, sizeof nm, vr, sizeof vr);
        if (!valid_pkg_name(nm)) continue;
        kstrncpy(r->name, nm, 31); r->name[31] = 0;
        kstrncpy(r->version, vr, 23); r->version[23] = 0;
        r->size = size; r->net = 1;
        kstrncpy(r->path, path, 95); r->path[95] = 0;
        kmemset(r->sha, 0, 32);
        repo_n++; added++;
        continue;
skip_entry:;
    }
    net_index_done = 1;
    say(1, "GitHub lists %d package archive(s)", added);
    return 1;
}

/* ---- package cache: every archive downloaded from GitHub is kept in /.aspi/cache, so it installs again without the network ---- */
#define CACHE_DIR "/.aspi/cache"

static const char *base_name(const char *path)
{
    const char *fn = path;
    for (const char *q = path; *q; q++) if (*q == '/') fn = q + 1;
    return fn;
}

static int cache_read(const char *file, u32 want_size)          /* into arc_in; returns the size or -1 */
{
    char p[160];
    ksnprintf(p, sizeof p, CACHE_DIR "/%s", file);
    int o = os_enter();
    int n = fs_resolve(p, 0);
    u32 sz = n > 0 && !fs_is_dir(n) ? fs_size_real(n) : 0;
    int ok = sz && sz <= ARC_IN_MAX && (!want_size || sz == want_size) && fs_pread(n, 0, arc_in, sz) == (int)sz;
    os_leave(o);
    return ok ? (int)sz : -1;
}

static void cache_store(const char *file, const u8 *data, u32 size)
{
    char p[160];
    ksnprintf(p, sizeof p, CACHE_DIR "/%s", file);
    int o = os_enter();
    if (persist_active()) put_file(p, data, size);             /* without a disk the cache would only eat RAM */
    os_leave(o);
}

/* no network / GitHub unreachable: offer what the cache holds */
static int cache_index(void)
{
    int o = os_enter();
    int d = fs_resolve(CACHE_DIR, 0), added = 0;
    for (int c = d > 0 ? fs_child(d, 0) : -1; c > 0 && repo_n < MAX_REPO; c = fs_child(d, c)) {
        if (fs_is_dir(c)) continue;
        const char *file = fs_name(c);
        static const struct { const char *ext, *kind; } ex[] = { { ".aspi", "aspi" }, { ".tar.gz", "tar.gz" }, { ".tar.xz", "tar.xz" } };
        size_t fl = kstrlen(file);
        for (unsigned k = 0; k < sizeof ex / sizeof ex[0]; k++) {
            size_t el = kstrlen(ex[k].ext);
            if (fl <= el || kstrcmp(file + fl - el, ex[k].ext)) continue;
            char base[64], nm[32], vr[24];
            if (fl - el >= sizeof base) break;
            kmemcpy(base, file, fl - el); base[fl - el] = 0;
            split_name_ver(base, nm, sizeof nm, vr, sizeof vr);
            if (!valid_pkg_name(nm)) break;
            repo_ent *r = &repo[repo_n];
            kmemset(r, 0, sizeof *r);
            kstrncpy(r->name, nm, 31); kstrncpy(r->version, vr, 23); kstrncpy(r->kind, ex[k].kind, 7);
            r->size = fs_size_real(c); r->net = 4; kstrncpy(r->path, file, 95);
            repo_n++; added++;
            break;
        }
    }
    os_leave(o);
    if (added) { net_index_done = 1; say(1, "Offline: %d package archive(s) from the cache", added); }
    return added > 0;
}

#ifdef AS_NO_IO
int aspi_cache_test(void) { repo_n = 0; net_index_done = 0; return cache_index(); }     /* harness: pretend GitHub is unreachable */
#endif

static int load_pkg(int ri, int loud, manifest *m, plan *pl)
{
    repo_ent *re = &repo[ri];
    int kind = arc_kind_from_text(re->kind);
    char sz[24];
    if (kind < 0) { say(2, "Unknown archive type '%s' in repo", re->kind); return -1; }
    if (re->size == 0 || re->size > ARC_IN_MAX) { say(2, "Archive size %u is not supported (max 3 MiB)", re->size); return -1; }
#ifdef AS_NO_IO
    if (re->net == 2 || re->net == 3) {
        kmemcpy(arc_in, re->net == 3 ? asset_nspkg_start : asset_froggy_start, re->size);
        if (loud) progress(26);
    } else
#endif
    if (re->net == 4 || (re->net == 1 && cache_read(base_name(re->path), re->size) > 0)) {
        if (re->net == 4 && cache_read(base_name(re->path), 0) < 0) { say(2, "Download: %s", "cached archive is unreadable"); return -1; }
        if (loud) progress(26);
    } else if (re->net) {
        char url[260], ep[300]; u32 got = 0; int st = 0;
        url_path(re->path, ep, sizeof ep);
        ksnprintf(url, sizeof url, "https://raw.githubusercontent.com/%s/HEAD/%s", GH_REPO, ep);
        if (loud) say(0, "Downloading %s from GitHub (%s)", re->name, re->path);
        if (!net_http_download(url, arc_in, ARC_IN_MAX, &got, &st)) { say(2, "Download: %s", net_http_error()); return -1; }
        if (st != 200) { say(2, "Download: GitHub answered HTTP %d", st); return -1; }
        if (got != re->size) { say(2, "Download: got %u bytes, the file list says %u", got, re->size); return -1; }
        cache_store(base_name(re->path), arc_in, got);
        if (loud) progress(26);
    } else {
        static u8 sec[512];
        u32 secs_n = (re->size + 511) / 512;
        for (u32 s = 0; s < secs_n; s++) {
            if (ata_read_drive(REPO_DRIVE, re->lba + s, sec) < 0) { say(2, "Reading %s archive (disk error at sector %u)", arc_kind_name(kind), re->lba + s); return -1; }
            u32 off = s * 512, n = re->size - off < 512 ? re->size - off : 512;
            kmemcpy(arc_in + off, sec, n);
            if (loud && (s & 31) == 0) progress(8 + (int)(s * 20 / secs_n));
        }
    }
    fmt_size(sz, sizeof sz, re->size);
    if (loud) { say(1, "Got %s archive %s (%s)", arc_kind_name(kind), re->net == 4 ? "from the cache" : re->net >= 2 ? "built into the OS" : re->net ? "from GitHub" : "from repo disk", sz); progress(28); }

    u8 h[32];
    sha256(arc_in, re->size, h);
    if (re->net == 1 || re->net == 4) {                       /* GitHub's file list carries no SHA-256: the TLS connection is the protection */
        if (loud) { say(1, "Archive SHA-256 %02x%02x%02x%02x%02x%02x%02x%02x... (not pinned by GitHub's list; the connection is verified TLS)", h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7]); progress(34); }
    } else if (kmemcmp(h, re->sha, 32)) { say(2, "Verifying SHA-256 (archive does not match the repo index)"); return -1; }
    else if (loud) { say(1, "Verifying SHA-256"); progress(34); }

    const char *why;
    if (arc_load(kind, arc_in, re->size, &files, &why) < 0) { say(2, "Opening archive: %s", why); return -1; }
    if (loud) {
        if (kind == ARC_ASPI) say(1, "Opening .aspi archive (%d entries)", files.n);
        else say(1, "Converting %s to .aspi (%d entries, done in memory)", arc_kind_name(kind), files.n);
        progress(44);
    }

    const arc_entry *mf = arc_find("ASPIManifest.json");
    if (!mf) { say(2, "Reading ASPIManifest.json (not found in the package)"); return -1; }
    if (loud) say(0, "Reading ASPIManifest.json");
    kmemset(m, 0, sizeof *m);
    json_str((const char *)mf->data, mf->size, "name", m->name, sizeof m->name);
    json_str((const char *)mf->data, mf->size, "version", m->ver, sizeof m->ver);
    json_str((const char *)mf->data, mf->size, "description", m->desc, sizeof m->desc);
    {
        char d2[160];
        if (json_str((const char *)mf->data, mf->size, "description2", d2, sizeof d2)) {
            char *dollar = kstrchr(d2, '$');
            if (dollar) {                                    /* "file $README.md": use that file's text */
                const arc_entry *f = arc_find(dollar + 1);
                if (f) { u32 k = f->size < sizeof m->desc2 - 1 ? f->size : (u32)sizeof m->desc2 - 1; kmemcpy(m->desc2, f->data, k); m->desc2[k] = 0; }
                else   ksnprintf(m->desc2, sizeof m->desc2, "(%s is not in the package)", dollar + 1);
            } else kstrncpy(m->desc2, d2, sizeof m->desc2 - 1);
        }
    }
    if (!m->name[0] || !m->ver[0]) { say(2, "ASPIManifest.json needs \"name\" and \"version\""); return -1; }
    /* package type (app | tool | library | data; missing = library) and the commands it adds to AST */
    {
        char t[16];
        t[0] = 0;
        json_str((const char *)mf->data, mf->size, "type", t, sizeof t);
        for (char *c = t; *c; c++) if (*c >= 'A' && *c <= 'Z') *c = (char)(*c + 32);
        if (!t[0]) kstrncpy(m->type, "library", 11);
        else if (type_ok(t)) kstrncpy(m->type, t, 11);
        else { say(3, "Unknown package type '%s' (app, tool, library, data) - treated as library", t); kstrncpy(m->type, "library", 11); }
        if (!kstrcmp(m->type, "system") && !fs_acs_get()) {      /* changes the system: only with the password */
            say(2, "%s", config_tr("Permission denied - 'system' packages change the system: acs aspi install <name>",
                                   "Izin Reddedildi - 'system' paketleri sistemi degistirir: acs aspi install <ad>"));
            return -1;
        }
        json_arr((const char *)mf->data, mf->size, "commands", m->cmds, sizeof m->cmds);
        if (check_commands(m->cmds, m->name) < 0) return -1;
        if (loud && m->cmds[0]) say(0, "The package adds AST command(s): %s", m->cmds);
    }
    if (loud) {
        if (!ieq(m->name, re->name)) say(3, "manifest name '%s' differs from repo name '%s'", m->name, re->name);
        say(1, "Package: %s %s (%s) - %s", m->name, m->ver, m->type, m->desc[0] ? m->desc : "(no description)");
        progress(52);
    }

    pl->n = 0;
    const arc_entry *inst = arc_find("installer.txt");
    if (inst) {
        if (loud) say(0, "Reading installer.txt");
        parse_installer((const char *)inst->data, inst->size);
        for (int i = 0; i < neap && pl->n < MAX_SEC; i++) {
            int k = pl->n++;
            kstrncpy(pl->name[k], eap[i], 31);
            pl->ver[k][0] = pl->url[k][0] = 0;
            const section *sc = find_section(eap[i]);
            if (sc) { kstrncpy(pl->ver[k], sc->ver, 23); kstrncpy(pl->url[k], sc->url, 95); }
        }
        if (loud) say(1, "installer.txt lists %d third-party package(s)", pl->n);
    } else if (loud) say(0, "No installer.txt: the package has no third-party requirements");
    return 0;
}

static int is_src_code(const char *n)
{
    return has_ext(n, ".c") || has_ext(n, ".cpp") || has_ext(n, ".cc") || has_ext(n, ".h") || has_ext(n, ".hpp");
}

static int install_one(const char *name, const char *want_ver)
{
    for (int i = 0; i < stack_n; i++)
        if (ieq(stack_name[i], name)) { say(2, "Dependency cycle through %s", name); return -1; }
    if (stack_n >= 6) { say(2, "Dependencies are nested too deeply"); return -1; }
    kstrncpy(stack_name[stack_n++], name, 31);
    int rc = -1, wrote = 0;
    char pkg[32]; kstrncpy(pkg, name, 31);
    static manifest mm[6];
    manifest *m = &mm[stack_n - 1];
    plan pl;

    say(0, "Checking package '%s' in repo (%s)", name, repo_source[0] ? repo_source : REPO_SOURCE);
    int ri = repo_find(name, want_ver);
    if (ri < 0 && !net_index_done && net_index_load()) ri = repo_find(name, want_ver);   /* not built in / on the disk: ask GitHub */
    if (ri < 0) {
        say(2, "Package '%s'%s%s is not built in, on the repo disk or on GitHub", name, want_ver && *want_ver && !ieq(want_ver, "lastver") ? " version " : "",
            want_ver && !ieq(want_ver, "lastver") ? want_ver : "");
        say(0, "Check the name (aspi online lists what GitHub has) and that the network works: as net dhcp");
        goto out;
    }
    repo_ent *re = &repo[ri];
    kstrncpy(pkg, re->name, 31);
    say(1, "Found %s %s (%s archive)%s", re->name, re->version, re->kind, re->net == 4 ? " in the cache" : re->net >= 2 ? " built into the OS" : re->net ? " on GitHub" : "");
    progress(8);

    {   /* already there? */
        rec ex;
        int o = os_enter();
        int have = read_rec(re->name, &ex, 0, 0);
        os_leave(o);
        if (have && !vcmp(ex.version, re->version)) { say(0, "%s %s is already installed", re->name, ex.version); rc = 0; goto out; }
    }

    if (load_pkg(ri, 1, m, &pl) < 0) goto out;

    /* dependencies first */
    {
        int installed_dep = 0;
        char deps[160]; deps[0] = 0;
        for (int i = 0; i < pl.n; i++) {
            size_t o = kstrlen(deps);
            ksnprintf(deps + o, sizeof deps - o, "%s%s", o ? "," : "", pl.name[i]);
            int have;
            { int j = os_enter(); rec r; have = read_rec(pl.name[i], &r, 0, 0); os_leave(j); }
            if (have) { say(0, "Dependency %s is already installed", pl.name[i]); continue; }
            say(0, "Dependency %s (version %s)%s%s", pl.name[i], pl.ver[i][0] ? pl.ver[i] : "lastver", pl.url[i][0] ? ", source " : "", pl.url[i]);
            nest++;
            int dr = install_one(pl.name[i], pl.ver[i]);
            nest--;
            if (dr < 0) { say(2, "Dependency %s could not be installed", pl.name[i]); goto out; }
            installed_dep = 1;
        }
        if (installed_dep) {                                 /* the dependency used the buffers: reload ours */
            say(0, "Back to %s: re-reading its archive", re->name);
            plan again;
            if (load_pkg(ri, 0, m, &again) < 0) goto out;
        }
        progress(58);

        /* compile .as files */
        say(0, "The files in the package are being compiled (AstraScript: checked and sealed)");
        int n_as = 0, errors = 0, skipped = 0;
        static char ctx[ASC_CTX_MAX];
        u32 ctx_deps = 0;                                    /* the dependencies' code, shared by every file */
        aspi_lib_reset();
        for (int i = 0; i < pl.n; i++) {
            int g = aspi_lib_source(pl.name[i], ctx + ctx_deps, (int)sizeof ctx - (int)ctx_deps);
            if (g > 0) ctx_deps += (u32)g;
        }
        for (int i = 0; i < files.n; i++) {
            const arc_entry *f = &files.e[i];
            if (f->is_dir) continue;
            if (has_ext(f->name, ".as")) {
                asc_diag d[6];
                u32 cl = ctx_deps;                           /* + the package's other .as files */
                for (int j = 0; j < files.n; j++) {
                    const arc_entry *g = &files.e[j];
                    if (j == i || g->is_dir || !has_ext(g->name, ".as") || cl + g->size + 2 > sizeof ctx) continue;
                    kmemcpy(ctx + cl, g->data, g->size);
                    cl += g->size;
                    ctx[cl++] = '\n';
                }
                int ne = asc_check_ctx((const char *)f->data, f->size, ctx, cl, d, 6);
                if (ne == 0) { say(1, "Compiling %s", f->name); n_as++; }
                else {
                    errors++;
                    say(2, "Compiling %s", f->name);
                    for (int k = 0; k < ne; k++) say(0, "    %s: line %d: %s", f->name, d[k].line, d[k].msg);
                }
            } else if (is_src_code(f->name)) {
                say(3, "%s not compiled: as_kernel has no C/C++ compiler (kept as source)", f->name);
                skipped++;
            }
        }
        progress(70);
        if (errors) { say(2, "%d AstraScript file(s) have errors - nothing was installed", errors); goto out; }
        say(1, "Compiled %d AstraScript file(s)%s", n_as, skipped ? ", C/C++ sources kept as is" : "");

        /* replace an older version */
        {
            int o = os_enter();
            rec ex;
            if (read_rec(re->name, &ex, 0, 0)) {
                say(0, "Replacing %s %s with %s", re->name, ex.version, re->version);
                char p[80]; int n;
                ksnprintf(p, sizeof p, "/.aspi/lib/%s", re->name); n = fs_resolve(p, 0); if (n > 0) fs_delete(n);
                ksnprintf(p, sizeof p, "/.aspi/app/%s.pkg", re->name); n = fs_resolve(p, 0); if (n > 0) fs_delete(n);
            }
            os_leave(o);
        }

        /* copy */
        int o = os_enter();
        char base[64], path[160];
        ksnprintf(base, sizeof base, "/.aspi/lib/%s", re->name);
        wrote = 1;
        int mk = mkdir_p(base);
        if (mk < 0) { os_leave(o); say(2, "Package Archive Copying to /.aspi/lib (%s)", fs_strerror(mk)); goto out; }
        int copied = 0, sealed = 0, big = 0;
        for (int i = 0; i < files.n; i++) {
            const arc_entry *f = &files.e[i];
            ksnprintf(path, sizeof path, "%s/%s", base, f->name);
            if (f->is_dir) { int r = mkdir_p(path); if (r < 0) { os_leave(o); say(2, "Creating folder %s (%s)", f->name, fs_strerror(r)); goto out; } continue; }
            int r = put_file(path, f->data, f->size);
            if (r < 0) {
                if (has_ext(f->name, ".as")) { os_leave(o); say(2, "Copying %s (%s)", f->name, fs_strerror(r)); goto out; }
                say(3, "%s not copied (%s)", f->name, fs_strerror(r));
                big++;
                continue;
            }
            copied++;
            if (has_ext(f->name, ".as")) {
                int sr = perm_seal_system(path);
                if (sr < 0) { os_leave(o); say(2, "Sealing %s (%s)", f->name, perm_strerror(sr)); goto out; }
                sealed++;
            }
            if ((i & 3) == 0) { os_leave(o); progress(70 + (i * 22) / (files.n ? files.n : 1)); o = os_enter(); }
        }
        os_leave(o);
        say(1, "Package Archive Copying to /.aspi/lib/%s (%d files, %d sealed%s)", re->name, copied, sealed, big ? ", some skipped" : "");
        progress(94);

        /* install record */
        {
            static char recbuf[1560], d2esc[720];
            size_t k = 0;
            for (const char *c = m->desc2; *c && k + 3 < sizeof d2esc; c++) {
                if (*c == '\n') { d2esc[k++] = '\\'; d2esc[k++] = 'n'; }
                else if (*c != '\r') d2esc[k++] = *c;
            }
            d2esc[k] = 0;
            hal_time t; hal_datetime(&t);
            int n = ksnprintf(recbuf, sizeof recbuf,
                "name=%s\nversion=%s\ndescription=%s\ndeps=%s\ninstalled=%u-%02u-%02u %02u:%02u\narchive=%s\nfiles=%d\nscripts=%d\ntype=%s\ncommands=%s\ndescription2=%s\n",
                re->name, re->version, m->desc, deps, t.year, t.month, t.day, t.hour, t.min, re->kind, copied, sealed, m->type, m->cmds, d2esc);
            if (n > (int)sizeof recbuf - 1) n = (int)sizeof recbuf - 1;
            char rp[80];
            ksnprintf(rp, sizeof rp, "/.aspi/app/%s.pkg", re->name);
            o = os_enter();
            int r = put_file(rp, (const u8 *)recbuf, (u32)n);
            os_leave(o);
            if (r < 0) { say(2, "Registering package in /.aspi/app (%s)", fs_strerror(r)); goto out; }
            say(1, "Registering package in /.aspi/app");
        }
    }
    rc = 0;
out:
    if (rc < 0 && wrote) rollback(pkg);
    stack_n--;
    return rc;
}

/* ------------------------------------------------------------------ use Name; (library source) */

static int lib_collect(int dir, const char *skip, char *out, int cap, int pos)
{
    for (int c = fs_child(dir, 0); c > 0; c = fs_child(dir, c)) {
        if (fs_is_dir(c)) { pos = lib_collect(c, skip, out, cap, pos); continue; }
        if (!has_ext(fs_name(c), ".as")) continue;
        if (skip && !kstrcmp(fs_name(c), skip)) continue;
        char path[160];
        fs_getpath(c, path, sizeof path);
        if (perm_check_system(c, path) < 0) continue;            /* not sealed by aspi: never used */
        u32 n = fs_size(c);
        if (pos + (int)n + 2 > cap) continue;
        kmemcpy(out + pos, fs_data(c), n);
        {   /* strict files: the top-level start; and the final end; are file markers, not code to run inside another program */
            char *f = out + pos; int a = 0, z = (int)n;
            while (a < z && (f[a] == ' ' || f[a] == '\t' || f[a] == '\n' || f[a] == '\r')) a++;
            if (a + 6 <= z && !kmemcmp(f + a, "start;", 6)) for (int k = 0; k < 6; k++) f[a + k] = ' ';
            while (z > 0 && (f[z - 1] == ' ' || f[z - 1] == '\t' || f[z - 1] == '\n' || f[z - 1] == '\r')) z--;
            if (z >= 4 && !kmemcmp(f + z - 4, "end;", 4) && (z == 4 || f[z - 5] == '\n')) for (int k = 0; k < 4; k++) f[z - 4 + k] = ' ';
        }
        pos += (int)n;
        out[pos++] = '\n';
    }
    return pos;
}

static char lib_seen[12][32];
static int  lib_nseen;
void aspi_lib_reset(void) { lib_nseen = 0; }

/* Dependencies (from the install record) first, then the package's own sealed .as files,
 * leaving out the file called `skip`. Each package is added once per reset. */
static int lib_ctx(const char *pkg, const char *skip, char *out, int cap, int depth)
{
    if (!valid_pkg_name(pkg)) return -1;
    for (int i = 0; i < lib_nseen; i++) if (ieq(lib_seen[i], pkg)) return 0;
    if (lib_nseen >= 12 || depth > 6) return 0;
    kstrncpy(lib_seen[lib_nseen++], pkg, 31);
    char p[64];
    ksnprintf(p, sizeof p, "/.aspi/lib/%s", pkg);
    int o = os_enter();
    int n = fs_resolve(p, 0);
    rec r;
    int have = n > 0 && fs_is_dir(n) && read_rec(pkg, &r, 0, 0);
    os_leave(o);
    if (!have) return -1;
    int pos = 0;
    char deps[160]; kstrncpy(deps, r.deps, 159);
    for (char *d = deps; *d;) {
        char *e = d; while (*e && *e != ',') e++;
        int last = !*e; *e = 0;
        if (*d) { int g = lib_ctx(d, 0, out + pos, cap - pos, depth + 1); if (g > 0) pos += g; }
        if (last) break;
        d = e + 1;
    }
    o = os_enter();
    n = fs_resolve(p, 0);
    if (n > 0) pos = lib_collect(n, skip, out, cap, pos);
    os_leave(o);
    return pos;
}

/* Appends the sealed .as files of package `pkg` (and its dependencies) to out; returns the byte
 * count, -1 if it is not installed. */
int aspi_lib_source(const char *pkg, char *out, int cap) { return lib_ctx(pkg, 0, out, cap, 0); }

/* ------------------------------------------------------------------ remove / list / info / repo / log / run */

static void print_buf(const char *d, u32 n)
{
    char b[129];
    while (n) {
        u32 k = n > 128 ? 128 : n;
        kmemcpy(b, d, k); b[k] = 0;
        kputs(b);
        d += k; n -= k;
    }
}

static int do_remove(const char *name, int force)
{
    if (!valid_pkg_name(name)) { kprintf(C_BAD "aspi: bad package name" C_RST "\n"); return -1; }
    int o = os_enter();
    rec r;
    int have = read_rec(name, &r, 0, 0);
    char deps[160];
    dependents_of(name, deps, sizeof deps);
    os_leave(o);
    if (!have) { kprintf(C_BAD "aspi: '%s' is not installed" C_RST "\n", name); return -1; }
    if (deps[0] && !force) {
        kprintf(C_BAD "aspi: cannot remove %s" C_RST " - needed by: %s   (force: aspi remove -f %s)\n", name, deps, name);
        return -1;
    }
    if (!kstrcmp(r.type, "system") && !fs_acs_get()) {
        kprintf(C_BAD "aspi: %s" C_RST "\n", config_tr("Permission denied - removing a 'system' package needs acs: acs aspi remove <name>",
                                                      "Izin Reddedildi - 'system' paketini kaldirmak icin acs gerekir: acs aspi remove <ad>"));
        return -1;
    }
    say(0, "Removing %s %s", r.name, r.version);
    o = os_enter();
    char p[80];
    ksnprintf(p, sizeof p, "/.aspi/lib/%s", r.name);
    int n = fs_resolve(p, 0);
    if (n > 0) { fs_delete(n); }
    ksnprintf(p, sizeof p, "/.aspi/app/%s.pkg", r.name);
    n = fs_resolve(p, 0);
    if (n > 0) fs_delete(n);
    os_leave(o);
    say(1, "Package files removed from /.aspi/lib/%s and /.aspi/app", r.name);
    return 0;
}

static void do_list(void)
{
    int o = os_enter();
    int bin = fs_resolve("/.aspi/app", 0), cnt = 0;
    if (bin > 0)
        for (int c = fs_child(bin, 0); c > 0; c = fs_child(bin, c)) {
            const char *nm = fs_name(c);
            if (!has_ext(nm, ".pkg")) continue;
            char pn[32]; size_t l = kstrlen(nm) - 4; if (l > 31) l = 31;
            kmemcpy(pn, nm, l); pn[l] = 0;
            rec r;
            if (!read_rec(pn, &r, 0, 0)) continue;
            kprintf("  " C_HI "%-16s" C_RST " %-10s %-8s %s%s%s%s\n", r.name, r.version, r.type[0] ? r.type : "library",
                    r.cmds[0] ? "cmd: " : "", r.cmds, r.deps[0] ? (r.cmds[0] ? "  needs: " : "needs: ") : "", r.deps);
            cnt++;
        }
    os_leave(o);
    if (!cnt) kprintf(TT("No packages installed. Try: aspi repo   /   aspi install <Name>\n"));
}

static void do_info(const char *name)
{
    if (!valid_pkg_name(name)) { kprintf(C_BAD "aspi: bad package name" C_RST "\n"); return; }
    int o = os_enter();
    rec r; const char *raw; u32 len;
    int have = read_rec(name, &r, &raw, &len);
    if (!have) { os_leave(o); kprintf(C_BAD "aspi: '%s' is not installed" C_RST "\n", name); return; }
    static char buf[1700];
    if (len > sizeof buf - 1) len = sizeof buf - 1;
    kmemcpy(buf, raw, len); buf[len] = 0;
    os_leave(o);
    static const char *keys[][2] = {
        {"name=", "Name"}, {"version=", "Version"}, {"description=", "Description"}, {"description2=", "More"},
        {"type=", "Type"}, {"commands=", "Commands"}, {"deps=", "Needs"}, {"installed=", "Installed"}, {"archive=", "Archive"}, {"files=", "Files"}, {"scripts=", "Sealed .as"} };
    for (unsigned k = 0; k < sizeof keys / sizeof keys[0]; k++) {
        size_t kl = kstrlen(keys[k][0]);
        for (const char *p = buf; *p;) {
            const char *e = p; while (*e && *e != '\n') e++;
            if (!kstrncmp(p, keys[k][0], kl) && e > p + kl) {
                kprintf("  " C_HI "%-11s" C_RST " ", keys[k][1]);
                for (const char *c = p + kl; c < e; c++) {
                    if (c[0] == '\\' && c + 1 < e && c[1] == 'n') { kputs("\n              "); c++; }
                    else { char t[2] = { *c, 0 }; kputs(t); }
                }
                kputs("\n");
            }
            if (!*e) break;
            p = e + 1;
        }
    }
}

static void do_repo(void)
{
    kprintf("Repo: %s\n", repo_source[0] ? repo_source : REPO_SOURCE);
    for (int i = 0; i < repo_n; i++) {
        char sz[24]; fmt_size(sz, sizeof sz, repo[i].size);
        kprintf("  " C_HI "%-16s" C_RST " %-10s %-7s %-10s%s\n", repo[i].name, repo[i].version, repo[i].kind, sz,
                is_installed(repo[i].name) ? C_OK "  installed" C_RST : "");
    }
}

static void do_log(const char *name)
{
    if (!valid_pkg_name(name)) { kprintf(C_BAD "aspi: bad package name" C_RST "\n"); return; }
    char p[80];
    ksnprintf(p, sizeof p, "/.term/log/%s.log", name);
    int o = os_enter();
    int n = fs_resolve(p, 0);
    if (n < 0) { os_leave(o); kprintf("No log for '%s'.\n", name); return; }
    static char buf[4200];
    u32 len = fs_size(n); if (len > sizeof buf - 1) len = sizeof buf - 1;
    kmemcpy(buf, fs_data(n), len);
    os_leave(o);
    print_buf(buf, len);
}

static int find_script(int dir, const char *want, int base)
{
    for (int c = fs_child(dir, 0); c > 0; c = fs_child(dir, c)) {
        if (fs_is_dir(c)) { int r = find_script(c, want, base); if (r > 0) return r; continue; }
        if (!kstrcmp(fs_name(c), want)) return c;
    }
    (void)base;
    return -1;
}

static void do_run(const char *name, const char *script)
{
    if (!valid_pkg_name(name)) { kprintf(C_BAD "aspi: bad package name" C_RST "\n"); return; }
    char p[64], want[48];
    ksnprintf(p, sizeof p, "/.aspi/lib/%s", name);
    if (script && *script) ksnprintf(want, sizeof want, "%s%s", script, has_ext(script, ".as") ? "" : ".as");
    int o = os_enter();
    int dir = fs_resolve(p, 0), n = -1;
    if (dir > 0 && fs_is_dir(dir)) {
        if (script && *script) n = find_script(dir, want, dir);
        else {
            char t[48];
            n = find_script(dir, "main.as", dir);
            if (n < 0) { ksnprintf(t, sizeof t, "%s.as", name); n = find_script(dir, t, dir); }
        }
    }
    if (dir <= 0) { os_leave(o); kprintf(C_BAD "aspi: '%s' is not installed" C_RST "\n", name); return; }
    if (n < 0 && !(script && *script)) {                    /* a native (C/C++) package: native.txt names the program */
        int nf = find_script(dir, "native.txt", dir);
        if (nf > 0) {
            char prog[48]; u32 pl = fs_size(nf); if (pl > 47) pl = 47;
            kmemcpy(prog, fs_data(nf), pl); prog[pl] = 0;
            while (pl && (prog[pl - 1] == '\n' || prog[pl - 1] == ' ' || prog[pl - 1] == '\r')) prog[--pl] = 0;
            const char *bn = prog;                          /* native.txt names the program inside the package (e.g. bin/netsurf.elf) */
            for (const char *q = prog; *q; q++) if (*q == '/') bn = q + 1;
#if defined(__aarch64__)
            static const char *const arch_suf = "-arm64";
#elif defined(__arm__)
            static const char *const arch_suf = "-arm32";
#else
            static const char *const arch_suf = "";
#endif
            char abn[64];
            kstrncpy(abn, bn, 63); abn[63] = 0;
            if (*arch_suf) {                                /* ARM: the package ships  name-arm64.elf / name-arm32.elf  next to the i386 name.elf */
                u32 bl = kstrlen(abn);
                if (bl > 4 && !kstrcmp(abn + bl - 4, ".elf")) abn[bl - 4] = 0;
                ksnprintf(abn + kstrlen(abn), (int)sizeof abn - (int)kstrlen(abn), "%s.elf", arch_suf);
            }
            int pn = find_script(dir, abn, dir);
            if (pn < 0 && *arch_suf) { os_leave(o); kprintf(C_BAD "aspi: %s has no build for this processor (%s)" C_RST "\n", name, abn); return; }
            if (pn < 0) { os_leave(o); kprintf(C_BAD "aspi: %s: program %s is missing in the package (reinstall it)" C_RST "\n", name, prog); return; }
            os_leave(o);
            ast_native_node(pn, native_args);
            return;
        }
    }
    if (n < 0) { os_leave(o); kprintf(C_BAD "aspi: no script %s in %s" C_RST " (try: aspi run %s <script>)\n", script && *script ? want : "main.as", name, name); return; }
    char path[160];
    fs_getpath(n, path, sizeof path);
    int rc = perm_check_system(n, path);
    char fs_name_skip[48]; kstrncpy(fs_name_skip, fs_name(n), 47);
    static char src[FS_FILE_MAX + 2];
    u32 len = fs_size(n); if (len > FS_FILE_MAX) len = FS_FILE_MAX;
    kmemcpy(src, fs_data(n), len);
    os_leave(o);
    if (rc < 0) { kprintf(C_BAD "aspi: %s" C_RST "\n", perm_strerror(rc)); return; }
    static char ctx[ASC_CTX_MAX];
    aspi_lib_reset();
    int cl = lib_ctx(name, fs_name_skip, ctx, (int)sizeof ctx, 0);
    asc_run_ctx(src, len, ctx, cl > 0 ? (u32)cl : 0);
}

static void do_run_cmd(const char *name, const char *cmd)
{
    char want[48], p[64];
    ksnprintf(p, sizeof p, "/.aspi/lib/%s", name);
    ksnprintf(want, sizeof want, "%s.as", cmd);
    int o = os_enter();
    int dir = fs_resolve(p, 0);
    int have = dir > 0 && fs_is_dir(dir) && find_script(dir, want, dir) > 0;
    os_leave(o);
    do_run(name, have ? cmd : 0);
}

/* ------------------------------------------------------------------ command entry */

static const char *tok(const char *s, char *out, size_t cap)
{
    while (*s == ' ') s++;
    size_t n = 0;
    while (*s && *s != ' ') { if (n + 1 < cap) out[n++] = *s; s++; }
    out[n] = 0;
    return s;
}

int aspi_wake;                                   /* set while a download runs: the graphical shell is resumed every frame */

static void usage(void)
{
    kprintf("Usage: aspi <install|remove|update|upgrade|list|info|repo|online|log|run> ...   (also works as: as aspi ...)\n"
            "  install <Name> [version]   fetch (built in, GitHub or repo disk), compile, install (+ dependencies)\n"
            "  online                     list the packages of the GitHub repository\n"
            "  update                     refresh the GitHub list and show which installed packages have a newer version\n"
            "  upgrade                    install the newer versions of the installed packages\n"
            "  remove [-f] <Name>         remove a package (-f: even if others need it)\n"
            "  list                       installed packages        info <Name>   package details\n"
            "  repo                       packages on the repo disk  log <Name>    last install log\n"
            "  run <Name> [script]        run a package script       (AstraScript: use <Name>;)\n");
}

void aspi_command(const char *args)
{
    char sub[16], a1[48], a2[48];
    const char *p = tok(args, sub, sizeof sub);
    p = tok(p, a1, sizeof a1);
    p = tok(p, a2, sizeof a2);
    if (!sub[0]) { usage(); return; }

    int write_cmd = !kstrcmp(sub, "install") || !kstrcmp(sub, "remove") || !kstrcmp(sub, "run") || !kstrcmp(sub, "upgrade");
    if (write_cmd && !user_name()) {
        kprintf(C_BAD "AST: login required" C_RST " - as login <PASSWORD> <NAME>\n");
        return;
    }
    loglen = 0; bar_pct = -1; nest = 0; stack_n = 0;

    if (!kstrcmp(sub, "list")) { do_list(); return; }
    if (!kstrcmp(sub, "info")) { if (!a1[0]) usage(); else do_info(a1); return; }
    if (!kstrcmp(sub, "log"))  { if (!a1[0]) usage(); else do_log(a1); return; }
    if (!kstrcmp(sub, "run"))  { if (!a1[0]) usage(); else do_run(a1, a2); return; }

    if (!kstrcmp(sub, "remove")) {
        int force = !kstrcmp(a1, "-f");
        const char *name = force ? a2 : a1;
        if (!name[0]) { usage(); return; }
        bar_pct = -1;
        do_remove(name, force);
        return;
    }

    repo_open();
    if (!kstrcmp(sub, "update") || !kstrcmp(sub, "upgrade")) {
        int up = !kstrcmp(sub, "upgrade");
        kprintf(C_HI "ASPI" C_RST " - %s\n", up ? "upgrading installed packages" : "refreshing the package list");
        bar_pct = -1;
        net_index_done = 0;
        for (int i = repo_n - 1; i >= 0; i--) if (repo[i].net == 1) repo_n = i;
        if (!net_index_load()) { kprintf(C_BAD "aspi: GitHub could not be reached" C_RST " (%s). Is the network up?  as net dhcp\n", net_http_error()); return; }
        static char names[24][32]; int nn = 0, newer = 0;
        int o = os_enter();
        int bin = fs_resolve("/.aspi/app", 0);
        if (bin > 0)
            for (int c = fs_child(bin, 0); c > 0 && nn < 24; c = fs_child(bin, c)) {
                const char *nm = fs_name(c);
                if (!has_ext(nm, ".pkg")) continue;
                size_t l = kstrlen(nm) - 4; if (l > 31) l = 31;
                kmemcpy(names[nn], nm, l); names[nn][l] = 0; nn++;
            }
        os_leave(o);
        static char todo[24][32]; int nt = 0;
        for (int i = 0; i < nn; i++) {
            rec r; o = os_enter(); int have = read_rec(names[i], &r, 0, 0); os_leave(o);
            if (!have) continue;
            int ri = repo_find(names[i], 0);
            if (ri < 0 || repo[ri].net != 1) continue;
            if (vcmp(r.version, repo[ri].version) < 0) {
                kprintf("  %-16s %s  ->  %s\n", r.name, r.version, repo[ri].version);
                kstrncpy(todo[nt++], names[i], 31); newer++;
            }
        }
        if (!newer) { kprintf(C_OK "Everything is up to date." C_RST " (%d installed, %d archives on GitHub)\n", nn, repo_n); return; }
        if (!up) { kprintf("%d package(s) can be upgraded: aspi upgrade\n", newer); return; }
        for (int i = 0; i < nt; i++) {
            bar_pct = 0; loglen = 0; nest = 0; stack_n = 0;
            int rc = install_one(todo[i], "");
            progress(100); bar_clear(); bar_pct = -1;
            if (rc < 0) kprintf(C_BAD "Upgrade of %s failed." C_RST " See: aspi log %s\n", todo[i], todo[i]);
            else kprintf(C_OK "%s upgraded." C_RST "\n", todo[i]);
            write_log(todo[i]);
        }
        return;
    }
    if (!kstrcmp(sub, "online")) {
        bar_pct = -1;
        net_index_done = 0;
        for (int i = repo_n - 1; i >= 0; i--) if (repo[i].net == 1) repo_n = i;
        if (!net_index_load()) { kprintf(C_BAD "aspi: GitHub could not be reached" C_RST " (%s). Is the network up?  as net dhcp\n", net_http_error()); return; }
        for (int i = 0; i < repo_n; i++) if (repo[i].net == 1) kprintf("  %-20s %-10s %u bytes\n", repo[i].name, repo[i].version, repo[i].size);
        return;
    }
    if (!kstrcmp(sub, "repo") || !kstrcmp(sub, "install")) {
        if (!kstrcmp(sub, "repo") && repo_n <= 0) {
            kprintf(C_BAD "aspi: no repo disk" C_RST " - the list of the repo disk is empty (packages come from GitHub: aspi online)\n");
            return;
        }
        if (!kstrcmp(sub, "repo")) { do_repo(); return; }
        if (!a1[0]) { usage(); return; }
        if (!valid_pkg_name(a1)) { kprintf(C_BAD "aspi: bad package name" C_RST "\n"); return; }
        kprintf(C_HI "ASPI" C_RST " - AstraSage Package Installer\n");
        bar_pct = 0;
        int rc = install_one(a1, a2);
        progress(100);
        bar_clear();
        bar_pct = -1;
        if (rc < 0) kprintf(C_BAD "Installation of %s failed." C_RST " See: aspi log %s\n", a1, a1);
        else        kprintf(C_OK "%s is ready." C_RST " Use it with: aspi run %s   or   use %s;  in AstraScript\n", a1, a1, a1);
        write_log(a1);
        return;
    }
    usage();
}
