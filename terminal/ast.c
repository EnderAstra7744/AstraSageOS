/*
 * AST - the AstraSageOS terminal.
 *
 * Every command is written as:  as <command> [arguments]
 * Scripts are started with:     asscript <file.as>
 *
 * Prompt:  [User@Model]:~ $                  (nobody logged in)
 *          [name@Model]:S:~/home/: $         (inside the user's folder on S:)
 */
#include "astra.h"
#include "gui.h"

#define LINE_MAX 1023

#define C_RST   "\033[0m"
#define C_G     "\033[38;5;112m"      /* green        - brackets          */
#define C_LG    "\033[38;5;154m"      /* light green  - user and model    */
#define C_W     "\033[97m"            /* white        - colons            */
#define C_DIR   "\033[38;5;70m"       /* dim green    - directory         */
#define C_DOL   "\033[38;5;64m"       /* darkest      - dollar sign       */
#define C_BLUE  "\033[94m"
#define C_ERR   "\033[91m"
#define C_DIM   "\033[90m"

enum { F_LOGIN = 1, F_NOSCRIPT = 2 };

static int cwd = 0;
static int in_script = 0;
static int fail_flag;            /* set when a command reported an error (stops an && chain) */
static int exec_depth;           /* alias / brace recursion guard */
static int at_prompt, typed_len; /* the shell waits for a command line / how much of it is typed */
static void alias_load(void);

/* ---------- argument helpers ---------- */
static const char *skip_ws(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

/* Copies the next whitespace-delimited token (line <= LINE_MAX, so it fits). */
static const char *next_tok(const char *s, char *out)
{
    size_t n = 0;
    s = skip_ws(s);
    while (*s && *s != ' ' && *s != '\t') out[n++] = *s++;
    out[n] = 0;
    return s;
}

static void err(const char *what, const char *arg, const char *why)
{
    fail_flag = 1;
    kprintf(C_ERR "as %s: %s: %s" C_RST "\n", what, arg, why);
}

static void usage(const char *u)
{
    fail_flag = 1;
    kprintf(TT("Usage: as %s\n"), u);
}

/*
 * Accepts "S:~/home/x", "~/x", "/x" and "x"; the visible root of a session is
 * its own user folder, so all of them end up inside it.
 */
static const char *user_path(const char *in, char *out, size_t cap)
{
    const char *p = in;
    if (!kstrncmp(p, "root:", 5) || !kstrncmp(p, "ROOT:", 5)) {      /* root:/bin  -  the real root of the system (outside S:~) */
        ksnprintf(out, cap, "root:%s%s", p[5] == '/' ? "" : "/", p + 5);
        return out;
    }
    if ((p[0] == 'S' || p[0] == 's') && p[1] == ':') p += 2;
    if (p[0] == '~' && (p[1] == '/' || p[1] == 0)) {
        ksnprintf(out, cap, "/%s", p[1] ? p + 2 : p + 1);
        return out;
    }
    if (!*p) { ksnprintf(out, cap, "/"); return out; }
    ksnprintf(out, cap, "%s", p);
    return out;
}

/* The file explorer (or another session) may have removed the folder we stand in. */
static void cwd_check(void)
{
    if (!fs_exists(cwd) || !fs_is_dir(cwd) || !fs_is_inside(cwd, fs_get_jail()))
        cwd = user_name() ? user_home() : 0;
}

/* "S:~/home/" style location of the current directory. */
static void location_text(char *out, size_t cap)
{
    cwd_check();
    if (!user_name()) { ksnprintf(out, cap, "~"); return; }
    char p[160];
    fs_getpath(cwd, p, sizeof p);
    if (!kstrcmp(p, "/")) ksnprintf(out, cap, "S:~/");
    else                  ksnprintf(out, cap, "S:~%s/", p);
}

/* ---------- commands ---------- */
static void cmd_help(const char *a);

static void cmd_version(const char *a)
{
    (void)a;
    kprintf("%s %s  (%s %s, %s %s)\n", AS_OS_NAME, AS_VERSION,
            AS_KERNEL_NAME, AS_VERSION, AS_TERM_NAME, AS_VERSION);
    kprintf("Built %s %s\n", __DATE__, __TIME__);
}

static void cmd_info(const char *a)
{
    (void)a;
    u32 nu, bu, bc;
    fs_stats(&nu, &bu, &bc);
    u32 up = hal_uptime_sec();
    char loc[170];
    location_text(loc, sizeof loc);
    kprintf(C_LG "%s" C_RST " system information\n", AS_OS_NAME);
    kprintf("  OS         : %s %s\n", AS_OS_NAME, AS_VERSION);
#if defined(__x86_64__)
    kprintf("  Kernel     : %s %s (x86_64, 64-bit long mode)\n", AS_KERNEL_NAME, AS_VERSION);
#elif defined(__aarch64__)
    kprintf("  Kernel     : %s %s (ARM64, 64-bit)\n", AS_KERNEL_NAME, AS_VERSION);
#elif defined(__arm__)
    kprintf("  Kernel     : %s %s (ARM32, 32-bit ARMv7)\n", AS_KERNEL_NAME, AS_VERSION);
#else
    kprintf("  Kernel     : %s %s (i386, 32-bit protected mode)\n", AS_KERNEL_NAME, AS_VERSION);
#endif
    kprintf("  Terminal   : %s %s\n", AS_TERM_NAME, AS_VERSION);
    kprintf("  Script     : AstraScript (built in)\n");
    kprintf("  CPU        : %s (%u cores)\n", hal_cpu_name(), hal_cpu_cores());
    kprintf("  GPU        : %s\n", hal_gpu_name());
    kprintf("  Machine    : %s\n", hal_model());
    kprintf("  Memory     : %u MiB total, %u MiB used by kernel\n", hal_mem_total_mib(), hal_mem_used_mib());
    kprintf("  Drive S:   : asfs, %u/%u bytes, %u nodes (%s)\n", bu, bc, nu, persist_status());
    kprintf("  User       : %s\n", user_name() ? user_name() : "Unknown (not logged in)");
    kprintf("  Location   : %s\n", loc);
    kprintf("  Uptime     : %um %us\n", up / 60, up % 60);
}

static void cmd_arch(const char *a)
{
    (void)a;
#if defined(__x86_64__)
    const char *arch = "x86_64", *mode = "64-bit (long mode)"; int bits = 64;
#elif defined(__aarch64__)
    const char *arch = "ARM64 (aarch64)", *mode = "64-bit"; int bits = 64;
#elif defined(__arm__)
    const char *arch = "ARM32 (ARMv7-A)", *mode = "32-bit"; int bits = 32;
#else
    const char *arch = "i386 (x86)", *mode = "32-bit (protected mode)"; int bits = 32;
#endif
    int lm = hal_cpu_long_mode();
    kprintf(C_LG "%s" C_RST " architecture\n", AS_OS_NAME);
    kprintf("  Kernel architecture : %s\n", arch);
    kprintf("  Running mode        : %s, pointers are %d bytes\n", mode, bits / 8);
#if defined(__aarch64__)
    kprintf("  CPU 64-bit support  : yes\n");
#elif defined(__arm__)
    kprintf("  CPU 64-bit support  : not applicable (ARM32 build)\n");
#else
    kprintf("  CPU 64-bit support  : %s\n", lm ? "yes (x86_64 long mode)" : "no (32-bit only)");
    if (bits == 32 && lm) kprintf("  Hint                : this CPU could run the 64-bit kernel (boot menu: 64-bit entry)\n");
#endif
#if defined(__aarch64__)
    kprintf("  Native programs     : supported (ARM64 ELF: FroggyCode, NetSurf)\n");
#elif defined(__arm__)
    kprintf("  Native programs     : supported (ARM32 ELF: FroggyCode, NetSurf)\n");
#elif defined(__i386__)
    kprintf("  Native programs     : supported (32-bit i386 ELF: NetSurf, FroggyCode)\n");
#elif defined(__x86_64__)
    kprintf("  Native programs     : supported (32-bit i386 ELF in compatibility mode)\n");
#else
    kprintf("  Native programs     : not available (they are 32-bit i386 ELF files)\n");
#endif
    kprintf("  Ports               : i386 (stable), x86_64 (experimental), ARM64 (experimental), ARM32 (experimental)\n");
}

static void cmd_clear(const char *a)
{
    (void)a;
    con_clear();
}

static void cmd_location(const char *a)
{
    (void)a;
    char loc[170];
    location_text(loc, sizeof loc);
    kprintf("%s\n", loc);
}

static void date_str(u32 secs, char *o, size_t cap)
{
    hal_time t;
    ktime_unpack(secs, &t);
    ksnprintf(o, cap, "%u-%02u-%02u %02u:%02u", t.year, t.month, t.day, t.hour, t.min);
}

static void cmd_files(const char *a)
{
    char arg[LINE_MAX + 1], pbuf[LINE_MAX + 2];
    next_tok(a, arg);
    int dir = cwd;
    if (arg[0]) {
        dir = fs_resolve(user_path(arg, pbuf, sizeof pbuf), cwd);
        if (dir < 0) { err("files", arg, fs_strerror(dir)); return; }
    }
    char p[160];
    fs_getpath(dir, p, sizeof p);
    char when[24];
    if (!fs_is_dir(dir)) {               /* a single file: show it alone */
        date_str(fs_mtime(dir), when, sizeof when);
        kprintf("  %5u B  %s  %s\n", fs_size_real(dir), when, p);
        return;
    }
    kprintf(C_DIM "Directory of" C_RST " S:~%s%s\n", kstrcmp(p, "/") ? p : "", "/");
    int count = 0;
    for (int c = fs_child(dir, 0); c > 0; c = fs_child(dir, c), count++) {
        date_str(fs_mtime(c), when, sizeof when);
        if (fs_is_dir(c)) kprintf("  " C_BLUE "<DIR>" C_RST "    %s  " C_BLUE "%s/" C_RST "\n", when, fs_name(c));
        else              kprintf("  %5u B  %s  %s\n", fs_size_real(c), when, fs_name(c));
    }
    kprintf(C_DIM "%d item(s)" C_RST "\n", count);
}

static void cmd_goto(const char *a)
{
    char arg[LINE_MAX + 1], pbuf[LINE_MAX + 2];
    next_tok(a, arg);
    if (!arg[0]) { usage("goto <path>"); return; }
    int n = fs_resolve(user_path(arg, pbuf, sizeof pbuf), cwd);
    if (n < 0) { err("goto", arg, fs_strerror(n)); return; }
    if (!fs_is_dir(n)) { err("goto", arg, fs_strerror(FS_ENOTDIR)); return; }
    cwd = n;
}

/* Changes outside the account's own area need the password (acs): see fs_guard_* in fs.c */
static int g_create(const char *path, int cwd_, int is_dir)
{
    int g = fs_guard_create(path, cwd_);
    return g < 0 ? g : fs_create(path, cwd_, is_dir);
}
static int g_write(int node, const char *d, u32 n, int append)
{
    int g = fs_guard_edit(node);
    return g < 0 ? g : fs_write(node, d, n, append);
}
static int g_delete(int node)
{
    int g = fs_guard_delete(node);
    return g < 0 ? g : fs_delete(node);
}

static void cmd_newdir(const char *a)
{
    char arg[LINE_MAX + 1], pbuf[LINE_MAX + 2];
    next_tok(a, arg);
    if (!arg[0]) { usage("newdir <name>"); return; }
    int r = g_create(user_path(arg, pbuf, sizeof pbuf), cwd, 1);
    if (r < 0) err("newdir", arg, fs_strerror(r));
    else kprintf(TT("Directory created: %s\n"), arg);
}

static void cmd_newfile(const char *a)
{
    char arg[LINE_MAX + 1], pbuf[LINE_MAX + 2];
    next_tok(a, arg);
    if (!arg[0]) { usage("newfile <name>"); return; }
    int r = g_create(user_path(arg, pbuf, sizeof pbuf), cwd, 0);
    if (r < 0) { err("newfile", arg, fs_strerror(r)); return; }
    u32 al = kstrlen(arg);
    if (al > 3 && !kstrcmp(arg + al - 3, ".as")) { const char *t = ast_script_template(); fs_write(r, t, kstrlen(t), 0); }   /* a new .as file starts from the template */
    kprintf(TT("File created: %s\n"), arg);
}

static void cmd_read(const char *a)
{
    char arg[LINE_MAX + 1], pbuf[LINE_MAX + 2];
    next_tok(a, arg);
    if (!arg[0]) { usage("read <file>"); return; }
    int n = fs_resolve(user_path(arg, pbuf, sizeof pbuf), cwd);
    if (n < 0) { err("read", arg, fs_strerror(n)); return; }
    if (fs_is_dir(n)) { err("read", arg, fs_strerror(FS_EISDIR)); return; }
    if (fs_is_big(n)) { kprintf("read: %s: %s (%u bytes)\n", arg, "too big to print", fs_size_real(n)); return; }
    const char *d = fs_data(n);
    u32 len = fs_size(n);
    for (u32 i = 0; i < len; i++) {
        char c = d[i];
        con_putc((c == '\n' || c == '\t' || ((u8)c >= 32 && (u8)c < 127)) ? c : '.');
    }
    if (len && d[len - 1] != '\n') con_putc('\n');
}

/* as write [-a] <file> <text>   (supports \n \t \\ escapes) */
static void cmd_write(const char *a)
{
    char tok[LINE_MAX + 1], file[LINE_MAX + 1], pbuf[LINE_MAX + 2];
    int append = 0;
    const char *rest = next_tok(a, tok);
    if (!kstrcmp(tok, "-a")) { append = 1; rest = next_tok(rest, tok); }
    kmemcpy(file, tok, kstrlen(tok) + 1);
    rest = skip_ws(rest);
    if (!file[0] || !*rest) { usage("write [-a] <file> <text>"); return; }

    static char out[FS_FILE_MAX + 1];
    u32 len = 0;
    for (const char *s = rest; *s; s++) {
        char c = *s;
        if (c == '\\' && s[1]) {
            s++;
            c = (*s == 'n') ? '\n' : (*s == 't') ? '\t' : *s;
        }
        if (len >= FS_FILE_MAX) { err("write", file, fs_strerror(FS_ETOOBIG)); return; }
        out[len++] = c;
    }

    const char *path = user_path(file, pbuf, sizeof pbuf);
    int n = fs_resolve(path, cwd);
    if (n == FS_ENOENT) n = g_create(path, cwd, 0);   /* create on demand */
    if (n < 0) { err("write", file, fs_strerror(n)); return; }
    int r = g_write(n, out, len, append);
    if (r < 0) err("write", file, fs_strerror(r));
    else kprintf("%s %u byte(s) to %s\n", append ? "Appended" : "Wrote", len, file);
}

/* ---- as copy <SRC> <DEST>  (cp: files and whole directories) ---- */

static int is_inside(int node, int ancestor)       /* node == ancestor or below it */
{
    for (int n = node; n > 0; n = fs_parent(n))
        if (n == ancestor) return 1;
    return 0;
}

/* Copies the contents of src (file data or all children) into the existing node dst. */
static int copy_into(int src, int dst)
{
    if (!fs_is_dir(src))
        { int g = fs_guard_edit(dst); return g < 0 ? g : fs_copy_data(dst, src); }
    for (int c = fs_child(src, 0); c > 0; c = fs_child(src, c)) {
        int n = fs_resolve(fs_name(c), dst);
        if (n == FS_ENOENT) n = g_create(fs_name(c), dst, fs_is_dir(c));
        if (n < 0) return n;
        if (fs_is_dir(c) != fs_is_dir(n)) return FS_ENOTDIR;
        if (is_inside(dst, c)) continue;           /* never recurse into the copy itself */
        int r = copy_into(c, n);
        if (r < 0) return r;
    }
    return FS_OK;
}

static void cmd_copy(const char *a)
{
    char t[3][LINE_MAX + 1], pb1[LINE_MAX + 2], pb2[LINE_MAX + 2];
    int k = 0;
    for (;;) {                                      /* tokens; a lone -r / -R is accepted and ignored */
        char tok[LINE_MAX + 1];
        a = next_tok(a, tok);
        if (!tok[0]) break;
        if (!kstrcmp(tok, "-r") || !kstrcmp(tok, "-R")) continue;
        if (k < 3) kmemcpy(t[k], tok, kstrlen(tok) + 1);
        k++;
    }
    if (k != 2) { usage("copy <SRC> <DEST>   (directories are copied with everything inside)"); return; }

    int src = fs_resolve(user_path(t[0], pb1, sizeof pb1), cwd);
    if (src < 0) { err("copy", t[0], fs_strerror(src)); return; }
    const char *dpath = user_path(t[1], pb2, sizeof pb2);
    int dst = fs_resolve(dpath, cwd);
    if (dst >= 0 && fs_is_dir(dst)) {              /* into an existing directory, same name */
        int n = fs_resolve(fs_name(src), dst);
        if (src == fs_get_jail()) { err("copy", t[0], "cannot copy the root folder"); return; }
        if (n == FS_ENOENT) n = g_create(fs_name(src), dst, fs_is_dir(src));
        if (n < 0) { err("copy", t[1], fs_strerror(n)); return; }
        dst = n;
    } else if (dst == FS_ENOENT) {
        dst = g_create(dpath, cwd, fs_is_dir(src));
        if (dst < 0) { err("copy", t[1], fs_strerror(dst)); return; }
    } else if (dst < 0) { err("copy", t[1], fs_strerror(dst)); return; }

    if (src == dst) { err("copy", t[0], "source and destination are the same"); return; }
    if (fs_is_dir(src) != fs_is_dir(dst)) { err("copy", t[1], fs_is_dir(src) ? "not a directory" : "is a directory"); return; }
    if (fs_is_dir(src) && is_inside(dst, src)) { err("copy", t[1], "cannot copy a folder into itself"); return; }
    int r = copy_into(src, dst);
    if (r < 0) { err("copy", t[1], fs_strerror(r)); return; }
    kprintf(TT("Copied %s -> %s\n"), t[0], t[1]);
}

/* ---- as rem [-r|-rf] <FILE>  (rm) ---- */
static void cmd_rem(const char *a)
{
    char path[LINE_MAX + 1] = "", pbuf[LINE_MAX + 2];
    int rec = 0, force = 0, files = 0;
    for (;;) {
        char tok[LINE_MAX + 1];
        a = next_tok(a, tok);
        if (!tok[0]) break;
        if (tok[0] == '-' && tok[1]) {
            for (const char *f = tok + 1; *f; f++) {
                if (*f == 'r' || *f == 'R') rec = 1;
                else if (*f == 'f') force = 1;
                else { err("rem", tok, "unknown option (use -r, -f or -rf)"); return; }
            }
            continue;
        }
        if (files++ == 0) kmemcpy(path, tok, kstrlen(tok) + 1);
    }
    if (files != 1) { usage("rem [-r|-rf] <FILE>"); return; }

    int n = fs_resolve(user_path(path, pbuf, sizeof pbuf), cwd);
    if (n < 0) { if (force && n == FS_ENOENT) return; err("rem", path, fs_strerror(n)); return; }
    if (n == fs_get_jail()) { err("rem", path, "refusing to remove the root folder"); return; }
    if (fs_is_dir(n) && !rec) { err("rem", path, "is a directory (use: as rem -r)"); return; }
    int back = is_inside(cwd, n);                  /* we are standing in what gets deleted */
    int parent = fs_parent(n);
    int r = g_delete(n);
    if (r < 0) { err("rem", path, fs_strerror(r)); return; }
    if (back) cwd = parent;
    kprintf(TT("Removed %s\n"), path);
}

static void neofetch_wrap(const char *a) { (void)a; cmd_neofetch(); }

/* as edit <file>   text editor with AstraScript error highlighting */
static void cmd_edit(const char *a)
{
    char arg[LINE_MAX + 1], pbuf[LINE_MAX + 2];
    next_tok(a, arg);
    if (!arg[0]) { usage("edit <file>"); return; }
    const char *path = user_path(arg, pbuf, sizeof pbuf);
    int n = fs_resolve(path, cwd);
    if (n >= 0 && fs_is_dir(n)) { err("edit", arg, fs_strerror(FS_EISDIR)); return; }
    if (n < 0 && n != FS_ENOENT) { err("edit", arg, fs_strerror(n)); return; }
    editor_run(path, cwd, arg);
    persist_flush();
}

/* as login [create|remove] <PASSWORD> <NAME> */
static void need_login(void);
static void cmd_login(const char *a)
{
    char t1[LINE_MAX + 1], t2[LINE_MAX + 1], t3[LINE_MAX + 1];
    const char *r = next_tok(a, t1);
    r = next_tok(r, t2);
    next_tok(r, t3);

    if (!t1[0] || !t2[0]) {
        usage("login [create|remove] <PASSWORD> <NAME>");
        return;
    }
    if (t3[0] && !kstrcmp(t1, "create")) {
        int rc = user_create(t3, t2);
        if (rc < 0) { err("login", t3, user_strerror(rc)); return; }
        kprintf(TT("Account '%s' created. Log in with: as login <PASSWORD> %s\n"), t3, t3);
        return;
    }
    if (t3[0] && !kstrcmp(t1, "remove")) {
        int was = user_name() && !kstrcmp(user_name(), t3);
        int rc = user_remove(t3, t2);
        if (rc < 0) { err("login", t3, user_strerror(rc)); return; }
        if (was) cwd = 0;
        kprintf(TT("Account '%s' and everything in its folder were removed.\n"), t3);
        return;
    }
    int rc = user_login(t2, t1);                    /* as login <PASSWORD> <NAME> */
    if (rc < 0) { err("login", t2, user_strerror(rc)); return; }
    cwd = user_home();
    alias_load();
    char loc[170];
    location_text(loc, sizeof loc);
    kprintf("Welcome, " C_LG "%s" C_RST ". You are in %s\n", t2, loc);
}

/* as account [-change <--NAME|--PASSWORD> <PASSWORD> <NEW_NAME|NEW_PASSWORD>] */
static int flag_is(const char *a, const char *b)       /* case-insensitive */
{
    while (*a && *b && (*a | 32) == (*b | 32)) { a++; b++; }
    return !*a && !*b;
}

static void cmd_account(const char *a)
{
    char t1[LINE_MAX + 1], t2[LINE_MAX + 1], t3[LINE_MAX + 1], t4[LINE_MAX + 1];
    const char *r = next_tok(a, t1);
    r = next_tok(r, t2);
    r = next_tok(r, t3);
    next_tok(r, t4);
    if (!user_name()) { need_login(); return; }
    if (!t1[0]) {
        kprintf("Signed in as " C_LG "%s" C_RST "   (change: as account -change --NAME <PASSWORD> <NEW_NAME>)\n", user_name());
        return;
    }
    if (!flag_is(t1, "-change") || !t3[0] || !t4[0]) {
        usage("account -change <--NAME|--PASSWORD> <PASSWORD> <NEW_NAME|NEW_PASSWORD>");
        return;
    }
    if (flag_is(t2, "--NAME")) {
        int rc = user_change_name(t3, t4);
        if (rc < 0) { err("account", t4, user_strerror(rc)); return; }
        char loc[170];
        location_text(loc, sizeof loc);
        kprintf("Your account is now called " C_LG "%s" C_RST ". You are in %s\n", user_name(), loc);
        return;
    }
    if (flag_is(t2, "--PASSWORD")) {
        int rc = user_change_password(t3, t4);
        if (rc < 0) { err("account", "password", user_strerror(rc)); return; }
        kprintf(TT("Password changed.\n"));
        return;
    }
    usage("account -change <--NAME|--PASSWORD> <PASSWORD> <NEW_NAME|NEW_PASSWORD>");
}

static void cmd_logout(const char *a)
{
    (void)a;
    if (!user_name()) { kprintf(TT("Nobody is logged in.\n")); return; }
    kprintf(TT("Goodbye, %s.\n"), user_name());
    user_logout();
    cwd = 0;
    alias_load();
}

/* as cper [+x|-x] <FILE.as>   grants / revokes execute permission (.asbin) */
static void cmd_cper(const char *a)
{
    char t1[LINE_MAX + 1], t2[LINE_MAX + 1], pbuf[LINE_MAX + 2];
    const char *r = next_tok(a, t1);
    next_tok(r, t2);
    int revoke = 0;
    const char *file = t1;
    if (!kstrcmp(t1, "+x")) file = t2;
    else if (!kstrcmp(t1, "-x")) { revoke = 1; file = t2; }
    if (!file[0]) { usage("cper [+x|-x] <FILE.as>"); return; }

    const char *path = user_path(file, pbuf, sizeof pbuf);
    int rc = revoke ? perm_revoke(path, cwd) : perm_grant(path, cwd);
    if (rc < 0) { err("cper", file, perm_strerror(rc)); return; }
    kprintf(revoke ? "Execute permission removed from %s\n"
                   : "Execute permission granted: %sbin created\n", file);
}

/* asscript <FILE.as>   runs a script that has a valid .asbin permission file */
static void cmd_asscript(const char *a)
{
    char arg[LINE_MAX + 1], pbuf[LINE_MAX + 2];
    next_tok(a, arg);
    if (!arg[0]) { kprintf("Usage: asscript <FILE.as>\n"); return; }
    const char *path = user_path(arg, pbuf, sizeof pbuf);
    int n = fs_resolve(path, cwd);
    if (n < 0) { err("asscript", arg, fs_strerror(n)); return; }
    if (fs_is_dir(n)) { err("asscript", arg, fs_strerror(FS_EISDIR)); return; }
    int rc = perm_check(n, path, cwd);
    if (rc < 0) { err("asscript", arg, perm_strerror(rc)); return; }
    asc_run(fs_data(n), fs_size(n));
}

/* nrun <name|file> [args]   runs a native (C/C++) program: the bundled demo "hello" or an ELF file */
extern const u8 asset_nhello_start[] __attribute__((weak));
extern const u8 asset_nhello_end[] __attribute__((weak));
extern const u8 asset_nlibc_start[] __attribute__((weak));
extern const u8 asset_nlibc_end[] __attribute__((weak));
int native_run(const u8 *img, u32 len, const char *args, const char **why);
int native_run_node(int node, const char *args, const char **why);

/* as proc [<PROC_FILE>] [--sleep <SECONDS>]   runs a trigger file of /proc (Kernel Panic / heavy-load screens) */
static void cmd_proc(const char *a)
{
    char t[LINE_MAX + 1], file[LINE_MAX + 1] = "";
    int sleep_s = 0, bad = 0;
    for (;;) {
        a = next_tok(a, t);
        if (!t[0]) break;
        if (!kstrcmp(t, "--sleep")) {
            a = next_tok(a, t);
            if (!t[0] || t[0] < '0' || t[0] > '9') { bad = 1; break; }
            for (const char *q = t; *q; q++) { if (*q < '0' || *q > '9') { bad = 1; break; } sleep_s = sleep_s * 10 + (*q - '0'); if (sleep_s > 3600) sleep_s = 3600; }
            if (bad) break;
        } else if (!file[0]) kstrncpy(file, t, sizeof file - 1);
        else { bad = 1; break; }
    }
    if (bad) { kprintf("Usage: as proc [<PROC_FILE>] [--sleep <SECONDS>]\n"); return; }
    int rc = proc_run(file, sleep_s);
    if (rc == -1) err("proc", file, "no such file in /proc");
    else if (rc == -2) err("proc", file, "not a trigger file (first line must be 'panic' or 'overload')");
}

/* as seal <FILE.as> [<APP_NAME>]   compile-check, encrypt and seal a script into /user/apps/<NAME>.asx */
static void cmd_seal(const char *a)
{
    char f[LINE_MAX + 1], nm[LINE_MAX + 1], pbuf[LINE_MAX + 2], msg[120];
    const char *rest = next_tok(a, f);
    next_tok(rest, nm);
    if (!f[0]) { kprintf("Usage: as seal <FILE.as> [<APP_NAME>]\n"); return; }
    int n = fs_resolve(user_path(f, pbuf, sizeof pbuf), cwd);
    if (n < 0) { err("seal", f, fs_strerror(n)); return; }
    if (fs_is_dir(n)) { err("seal", f, fs_strerror(FS_EISDIR)); return; }
    if (!nm[0]) {                                         /* default: file name without the extension */
        kstrncpy(nm, fs_name(n), sizeof nm - 1);
        char *dot = kstrchr(nm, '.'); if (dot) *dot = 0;
    }
    for (const char *q = nm; *q; q++)
        if (!((*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z') || (*q >= '0' && *q <= '9') || *q == '_' || *q == '-')) { err("seal", nm, "app names use letters, digits, - and _"); return; }
    int rc = user_seal_app(nm, fs_data(n), fs_size(n), msg, sizeof msg);
    if (rc == -100) { err("seal", f, msg); return; }
    if (rc < 0) { err("seal", f, fs_strerror(rc)); return; }
    kprintf("Sealed (compiled + encrypted): /user/apps/%s\nRun it with: as userapp %s\n", msg, nm);
}

/* as userapp <NAME>   runs a sealed app of /user/apps */
static void cmd_userapp(const char *a)
{
    char nm[LINE_MAX + 1];
    next_tok(a, nm);
    if (!nm[0]) { kprintf("Usage: as userapp <NAME>\n"); return; }
    int rc = user_app_run(nm);
    if (rc == -1) err("userapp", nm, "no such sealed app in /user/apps");
}

/* as tab [--add|--remove] [<TAB_NAME>]   terminal tabs: each one is its own AST session; the others keep running */
static void cmd_tab(const char *a)
{
    char t[LINE_MAX + 1], name[LINE_MAX + 1] = "", msg[300];
    int op = -1;                                          /* -1 not decided, 0 list, 1 add, 2 remove, 3 switch */
    for (;;) {
        a = next_tok(a, t);
        if (!t[0]) break;
        if (!kstrcmp(t, "--add")) op = 1;
        else if (!kstrcmp(t, "--remove")) op = 2;
        else if (t[0] == '-' && t[1] == '-') { kprintf("Usage: as tab [--add | --remove] [<TAB_NAME>]\n"); fail_flag = 1; return; }
        else if (!name[0]) kstrncpy(name, t, sizeof name - 1);
        else { kprintf("Usage: as tab [--add | --remove] [<TAB_NAME>]\n"); fail_flag = 1; return; }
    }
    if (op < 0) op = name[0] ? 3 : 0;
    int rc = ast_tab_op(op, name, msg, sizeof msg);
    if (rc < 0) { fail_flag = 1; kprintf(C_ERR "tab: %s" C_RST "\n", msg); }
    else kprintf("%s%s", msg, op == 0 ? "" : "\n");
}

static const char *col_text(int c, char *b, int cap)
{
    if (c < 0) return "default";
    ksnprintf(b, (size_t)cap, "#%06x", c);
    for (char *q = b; *q; q++) if (*q >= 'a' && *q <= 'f') *q = (char)(*q - 32);
    return b;
}

/*
 * as custom [--tab_color|--line_color|--custom_input_color] [<TAB_COLOR>] [<LINE_COLOR>] [<CUSTOM_INPUT_COLOR>]
 * as custom --neofetch [<C1>,<C2>,<C3>] [<LOGO_PNG_NAME>] [<LOGO_TXT_NAME>] [<INFO_PANEL_COLOR>]
 * Colours: a name (red, orange, ...), #RRGGBB or "default". Without a flag the values are tab, line, input in that order.
 */
static void cmd_custom(const char *a)
{
    char t[LINE_MAX + 1], flags[4] = { 0 }, vals[6][LINE_MAX + 1];
    int nf = 0, nv = 0, neo = 0, bad = 0;
    for (;;) {
        a = next_tok(a, t);
        if (!t[0]) break;
        if (t[0] == '-' && t[1] == '-') {
            if (!kstrcmp(t, "--tab_color")) { if (nf < 3) flags[nf++] = 't'; }
            else if (!kstrcmp(t, "--line_color")) { if (nf < 3) flags[nf++] = 'l'; }
            else if (!kstrcmp(t, "--custom_input_color")) { if (nf < 3) flags[nf++] = 'i'; }
            else if (!kstrcmp(t, "--neofetch")) neo = 1;
            else bad = 1;
        } else if (nv < 6) kstrncpy(vals[nv++], t, LINE_MAX);
        else bad = 1;
    }
    as_settings s = *config_get();
    char b1[16], b2[16], b3[16];
    if (bad || (neo && nf)) {
        kprintf("Usage: as custom [--tab_color] [--line_color] [--custom_input_color] [<TAB_COLOR>] [<LINE_COLOR>] [<CUSTOM_INPUT_COLOR>]\n"
                "       as custom --neofetch [<C1>,<C2>,<C3>] [<LOGO_PNG_NAME>] [<LOGO_TXT_NAME>] [<INFO_PANEL_COLOR>]\n"
                "Colours: red green blue yellow orange purple pink cyan white black gray lime teal brown gold navy magenta, #RRGGBB or default\n");
        fail_flag = 1;
        return;
    }
    if (neo) {
        int k = 0;
        if (nv == 0) {
            kprintf("Neofetch logo colours: %s %s %s   info panel: %s   picture: %s   text logo: %s\n",
                    col_text(s.nf_logo[0], b1, 16), col_text(s.nf_logo[1], b2, 16), col_text(s.nf_logo[2], b3, 16),
                    col_text(s.nf_info, b1, 16), s.nf_png[0] ? s.nf_png : "(AstraSage logo)", s.nf_txt[0] ? s.nf_txt : "(none)");
            return;
        }
        if (nv > k) {                                      /* c1,c2,c3 */
            char *p = vals[k++], *parts[3]; int np = 0;
            if (!kstrcmp(p, "default") || !kstrcmp(p, "-")) { s.nf_logo[0] = s.nf_logo[1] = s.nf_logo[2] = -1; goto colors_done; }
            parts[np++] = p;
            for (char *q = p; *q && np < 4; q++) if (*q == ',') { *q = 0; if (np < 3) parts[np++] = q + 1; else { bad = 1; } }
            if (bad || np != 3) { kprintf(C_ERR "custom: the logo colours are three colours separated by commas, e.g. red,green,blue" C_RST "\n"); fail_flag = 1; return; }
            for (int i = 0; i < 3; i++) if (color_parse(parts[i], &s.nf_logo[i])) { kprintf(C_ERR "custom: '%s' is not a colour" C_RST "\n", parts[i]); fail_flag = 1; return; }
        }
        colors_done:
        if (nv > k) {                                      /* picture name: must exist in /bin/neofetch/custom_logo (or "default") */
            const char *n = vals[k++];
            if (!kstrcmp(n, "default") || !kstrcmp(n, "-")) s.nf_png[0] = 0;
            else {
                char pth[100]; ksnprintf(pth, sizeof pth, "root:/bin/neofetch/custom_logo/%s", n);
                if (fs_resolve(pth, 0) < 0) { kprintf(C_ERR "custom: /bin/neofetch/custom_logo/%s does not exist (put the PNG there with acs)" C_RST "\n", n); fail_flag = 1; return; }
                kstrncpy(s.nf_png, n, sizeof s.nf_png - 1);
            }
        }
        if (nv > k) {                                      /* text logo name */
            const char *n = vals[k++];
            if (!kstrcmp(n, "default") || !kstrcmp(n, "-")) s.nf_txt[0] = 0;
            else {
                char pth[100]; ksnprintf(pth, sizeof pth, "root:/bin/neofetch/custom_logo_txt/%s", n);
                if (fs_resolve(pth, 0) < 0) { kprintf(C_ERR "custom: /bin/neofetch/custom_logo_txt/%s does not exist (put the text file there with acs)" C_RST "\n", n); fail_flag = 1; return; }
                kstrncpy(s.nf_txt, n, sizeof s.nf_txt - 1);
            }
        }
        if (nv > k) { if (color_parse(vals[k++], &s.nf_info)) { kprintf(C_ERR "custom: '%s' is not a colour" C_RST "\n", vals[k - 1]); fail_flag = 1; return; } }
        config_set(&s);
        kprintf(TT("Neofetch look saved. Try: as neofetch\n"));
        return;
    }
    if (nv == 0 && nf == 0) {
        kprintf("Tab colour: %s   line colour: %s   input colour: %s\n", col_text(s.tab_color, b1, 16), col_text(s.line_color, b2, 16), col_text(s.input_color, b3, 16));
        return;
    }
    if (nf == 0) { flags[0] = 't'; flags[1] = 'l'; flags[2] = 'i'; nf = nv > 3 ? 3 : nv; }
    if (nv != nf) { kprintf(C_ERR "custom: %d option(s) need %d colour value(s)" C_RST "\n", nf, nf); fail_flag = 1; return; }
    for (int i = 0; i < nf; i++) {
        int c;
        if (color_parse(vals[i], &c)) { kprintf(C_ERR "custom: '%s' is not a colour" C_RST "\n", vals[i]); fail_flag = 1; return; }
        if (flags[i] == 't') s.tab_color = c; else if (flags[i] == 'l') s.line_color = c; else s.input_color = c;
    }
    config_set(&s);
    kprintf("Saved. Tab colour: %s   line colour: %s   input colour: %s\n", col_text(s.tab_color, b1, 16), col_text(s.line_color, b2, 16), col_text(s.input_color, b3, 16));
}

static void cmd_nrun(const char *a)
{
    char arg[LINE_MAX + 1], pbuf[LINE_MAX + 2];
    const char *rest = next_tok(a, arg);
    if (!arg[0]) { kprintf("Usage: nrun <hello|libctest|FILE> [arguments]\n"); return; }
    const u8 *img = 0; u32 len = 0; const char *why = 0; int fnode = -1;
    if (!kstrcmp(arg, "hello") && asset_nhello_start) { img = asset_nhello_start; len = (u32)(asset_nhello_end - asset_nhello_start); }
    else if (!kstrcmp(arg, "libctest") && asset_nlibc_start) { img = asset_nlibc_start; len = (u32)(asset_nlibc_end - asset_nlibc_start); }
    else {
        int n = fs_resolve(user_path(arg, pbuf, sizeof pbuf), cwd);
        if (n < 0) { err("nrun", arg, fs_strerror(n)); return; }
        if (fs_is_dir(n)) { err("nrun", arg, fs_strerror(FS_EISDIR)); return; }
        fnode = n;
    }
    while (*rest == ' ') rest++;
    int rc = fnode >= 0 ? native_run_node(fnode, rest, &why) : native_run(img, len, rest, &why);
    if (why) kprintf("nrun: %s\n", why);
    else kprintf("nrun: finished, exit code %d\n", rc);
}

/* a native program of an installed ASPI package (native.txt names it), run from its file */
int ast_native_node(int node, const char *args)
{
    const char *why = 0;
    int rc = native_run_node(node, args ? args : "", &why);
    if (why) kprintf("nrun: %s\n", why);
    else kprintf("nrun: finished, exit code %d\n", rc);
    return rc;
}

/* aspi <install|remove|list|info|repo|log|run> ...   package manager (login is checked inside) */
static void cmd_aspi(const char *a) { aspi_command(a); }
static void cmd_net(const char *a) { net_command(a); }
static void cmd_ping(const char *a) { ping_command(a); }
static void cmd_wifi(const char *a) { wifi_command(a); }
static void cmd_weather(const char *a) { weather_command(a); }
static void cmd_bluetooth(const char *a) { bluetooth_command(a); }
void tls_command(const char *a);
static void cmd_tls(const char *a) { tls_command(a); }

/* as get <http-url> [file]   download a page; shows it, or saves the body to a file of the user's folder */
static void cmd_get(const char *a)
{
    char url[LINE_MAX + 1], file[LINE_MAX + 1], pbuf[LINE_MAX + 2];
    const char *rest = next_tok(a, url);
    next_tok(rest, file);
    if (!url[0]) { usage("get <http(s)://host[:port]/path> [file]"); return; }
    if (file[0] && !user_name()) { need_login(); return; }
    const char *body; u32 blen; int status, trunc;
    if (!net_http_get(url, &body, &blen, &status, &trunc, 1)) { fail_flag = 1; kprintf(C_ERR "get: %s" C_RST "\n", net_http_error()); return; }
    kprintf("HTTP %d, %u byte(s)%s\n", status, blen, trunc ? " (answer cut at 32 KiB)" : "");
    if (!file[0]) {
        u32 shown = blen > 1800 ? 1800 : blen;
        for (u32 i = 0; i < shown; i++) { char c = body[i]; if (c == '\n' || c == '\t' || (c >= 32 && c < 127)) { char t[2] = { c, 0 }; kputs(t); } else kputs("."); }
        kputs("\n");
        if (shown < blen) kprintf(C_DIM "... %u more byte(s): save with  as get <url> <file>" C_RST "\n", blen - shown);
        return;
    }
    u32 n = blen > FS_FILE_MAX ? FS_FILE_MAX : blen;
    const char *path = user_path(file, pbuf, sizeof pbuf);
    int node = fs_resolve(path, cwd);
    if (node == FS_ENOENT) node = g_create(path, cwd, 0);
    if (node < 0) { err("get", file, fs_strerror(node)); return; }
    int r = g_write(node, body, n, 0);
    if (r < 0) { err("get", file, fs_strerror(r)); return; }
    kprintf("Saved %u byte(s) to %s%s\n", n, file, n < blen ? "  (files are limited to 4096 bytes: the rest was dropped)" : "");
}

/* ---- as reboot ---- */
static void cmd_reboot(const char *a)
{
    (void)a;
    persist_flush();                                  /* nothing is lost: the file tree and the login are on the disk */
    kprintf(TT("Rebooting AstraSageOS...\n"));
    hal_reboot();
}

/* ---- as shutdown ---- */
static void cmd_shutdown(const char *a)
{
    (void)a;
    persist_flush();                                  /* the file tree and the login are on the disk */
    kprintf(TT("Shutting down AstraSageOS...\n"));
    hal_shutdown();
}

/* ---- as tree [path] [--files|--dir] ---- */
#define TREE_DEPTH 12
static int tree_files, tree_dirs;
static int ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
    }
    return !*a && !*b;
}
static void tree_walk(int dir, int want_dirs, int want_files, char *prefix, int depth)
{
    int last_node = 0;
    for (int c = fs_child(dir, 0); c > 0; c = fs_child(dir, c)) {
        if (fs_is_dir(c) || want_files) last_node = c;   /* folders always stay (they are the structure) */
    }
    for (int c = fs_child(dir, 0); c > 0; c = fs_child(dir, c)) {
        int isd = fs_is_dir(c);
        if (!isd && !want_files) continue;
        int last = c == last_node;
        if (isd) { tree_dirs++; kprintf("%s%s " C_BLUE "%s/" C_RST "\n", prefix, last ? "`--" : "|--", fs_name(c)); }
        else     { tree_files++; kprintf("%s%s %s\n", prefix, last ? "`--" : "|--", fs_name(c)); }
        if (isd && depth < TREE_DEPTH) {
            size_t n = kstrlen(prefix);
            if (n + 5 < 120) {
                ksnprintf(prefix + n, 128 - n, "%s", last ? "    " : "|   ");
                tree_walk(c, want_dirs, want_files, prefix, depth + 1);
                prefix[n] = 0;
            }
        }
    }
}
static void cmd_tree(const char *a)
{
    char tok[LINE_MAX + 1], pbuf[LINE_MAX + 2], path[LINE_MAX + 1] = "";
    int want_dirs = 1, want_files = 1;
    for (;;) {
        a = next_tok(a, tok);
        if (!tok[0]) break;
        if (tok[0] == '-' && tok[1] == '-') {
            if (ieq(tok, "--files")) { want_files = 1; want_dirs = 0; }
            else if (ieq(tok, "--dir") || ieq(tok, "--dirs") || ieq(tok, "--<dir>") || ieq(tok, "--folders")) { want_dirs = 1; want_files = 0; }
            else { usage("tree [path] [--files|--dir]"); return; }
        } else if (!path[0]) kstrncpy(path, tok, sizeof path - 1);
        else { usage("tree [path] [--files|--dir]"); return; }
    }
    int dir = cwd;
    if (path[0]) {
        dir = fs_resolve(user_path(path, pbuf, sizeof pbuf), cwd);
        if (dir < 0) { err("tree", path, fs_strerror(dir)); return; }
        if (!fs_is_dir(dir)) { err("tree", path, fs_strerror(FS_EISDIR)); return; }
    }
    char p[160];
    fs_getpath(dir, p, sizeof p);
    kprintf(C_DIM "Tree of" C_RST " S:~%s%s\n", kstrcmp(p, "/") ? p : "", "/");
    char prefix[128] = "";
    tree_files = tree_dirs = 0;
    tree_walk(dir, want_dirs, want_files, prefix, 1);
    if (want_files && want_dirs) kprintf(C_DIM "%d folder(s), %d file(s)" C_RST "\n", tree_dirs, tree_files);
    else if (want_files)        kprintf(C_DIM "%d file(s)" C_RST "\n", tree_files);
    else                        kprintf(C_DIM "%d folder(s)" C_RST "\n", tree_dirs);
}

/* ---- as alias [NAME] [==|=:] ['COMMAND'] ---- */
#define ALIAS_MAX 16
typedef struct { char name[17]; char cmd[170]; u8 perm; } alias_t;
static alias_t aliases[ALIAS_MAX];
static int naliases;

static int os_enter(void) { int o = fs_get_jail(); fs_set_jail(0); return o; }

static const char *alias_find(const char *name)
{
    for (int i = 0; i < naliases; i++) if (!kstrcmp(aliases[i].name, name)) return aliases[i].cmd;
    return NULL;
}

static int alias_find_n(const char *name, u32 n)
{
    for (int i = 0; i < naliases; i++)
        if (kstrlen(aliases[i].name) == n && !kstrncmp(aliases[i].name, name, n)) return 1;
    return 0;
}

static int alias_valid_name(const char *n)
{
    size_t l = kstrlen(n);
    if (l < 1 || l > 16) return 0;
    for (; *n; n++) {
        char c = *n;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return 0;
    }
    return 1;
}

static const char *alias_owner(void) { return user_name() ? user_name() : "-"; }

/* Saved aliases (=:) live in the OS-root file /.term/aliases: one "user name command" line each. */
static void alias_save(void)
{
    static char out[FS_FILE_MAX + 1];
    u32 w = 0;
    int o = os_enter();
    int f = fs_resolve("/.term/aliases", 0);
    if (f == FS_ENOENT) f = fs_create("/.term/aliases", 0, 0);
    if (f > 0) {
        const char *d = fs_data(f);
        u32 len = fs_size(f), i = 0;
        size_t ol = kstrlen(alias_owner());
        while (i < len) {                              /* keep every other account's lines */
            u32 e = i;
            while (e < len && d[e] != '\n') e++;
            int mine = e - i > ol && !kstrncmp(d + i, alias_owner(), ol) && d[i + ol] == ' ';
            if (!mine && e > i && w + (e - i) + 1 < FS_FILE_MAX) { kmemcpy(out + w, d + i, e - i); w += e - i; out[w++] = '\n'; }
            i = e + 1;
        }
        for (int k = 0; k < naliases; k++) {
            if (!aliases[k].perm) continue;
            int n = ksnprintf(out + w, FS_FILE_MAX - w, "%s %s %s\n", alias_owner(), aliases[k].name, aliases[k].cmd);
            if (n > 0 && w + (u32)n < FS_FILE_MAX) w += (u32)n;
        }
        fs_write(f, out, w, 0);
    }
    fs_set_jail(o);
}

/* Loads the saved aliases of the current account (called at start and after login / logout). */
static void alias_load(void)
{
    int keep = 0;
    for (int i = 0; i < naliases; i++) if (!aliases[i].perm) aliases[keep++] = aliases[i];
    naliases = keep;
    int o = os_enter();
    int f = fs_resolve("/.term/aliases", 0);
    if (f > 0) {
        const char *d = fs_data(f);
        u32 len = fs_size(f), i = 0;
        size_t ol = kstrlen(alias_owner());
        while (i < len) {
            u32 e = i;
            while (e < len && d[e] != '\n') e++;
            if (e - i > ol + 1 && !kstrncmp(d + i, alias_owner(), ol) && d[i + ol] == ' ') {
                const char *nm = d + i + ol + 1;
                u32 nl = 0;
                while (nm + nl < d + e && nm[nl] != ' ') nl++;
                const char *cm = nm + nl + 1;
                if (nl > 0 && nl <= 16 && cm <= d + e && naliases < ALIAS_MAX && !alias_find_n(nm, nl)) {
                    alias_t *a = &aliases[naliases++];
                    kmemcpy(a->name, nm, nl); a->name[nl] = 0;
                    u32 cl = (u32)(d + e - cm);
                    if (cl > sizeof a->cmd - 1) cl = sizeof a->cmd - 1;
                    kmemcpy(a->cmd, cm, cl); a->cmd[cl] = 0;
                    a->perm = 1;
                }
            }
            i = e + 1;
        }
    }
    fs_set_jail(o);
}

static int is_builtin_name(const char *w);

static void alias_list(void)
{
    if (!naliases) { kprintf("No aliases. Define one:  as alias ll == 'as files'   (=: keeps it after a reboot)\n"); return; }
    for (int i = 0; i < naliases; i++)
        kprintf("  " C_LG "%s" C_RST " %s '%s'\n", aliases[i].name, aliases[i].perm ? "=:" : "==", aliases[i].cmd);
}

static void cmd_alias(const char *a)
{
    char name[LINE_MAX + 1], op[LINE_MAX + 1];
    const char *r = next_tok(a, name);
    if (!name[0]) { alias_list(); return; }
    if (!kstrcmp(name, "-r")) {                                   /* as alias -r NAME */
        char t[LINE_MAX + 1];
        next_tok(r, t);
        if (!t[0]) { usage("alias -r <NAME>"); return; }
        for (int i = 0; i < naliases; i++)
            if (!kstrcmp(aliases[i].name, t)) {
                int perm = aliases[i].perm;
                aliases[i] = aliases[--naliases];
                if (perm) alias_save();
                kprintf(TT("Alias removed: %s\n"), t);
                return;
            }
        err("alias", t, "no such alias");
        return;
    }
    r = next_tok(r, op);
    if (!op[0]) {                                                  /* as alias NAME: show it */
        const char *c = alias_find(name);
        if (!c) { err("alias", name, "no such alias"); return; }
        kprintf("%s = '%s'\n", name, c);
        return;
    }
    if (kstrcmp(op, "==") && kstrcmp(op, "=:")) { usage("alias [NAME] [==|=:] ['COMMAND']   (== this session, =: saved)"); return; }
    int perm = op[1] == ':';
    r = skip_ws(r);
    char cmd[LINE_MAX + 1];
    size_t n = kstrlen(r);
    if (n >= 2 && (r[0] == '\'' || r[0] == '"') && r[n - 1] == r[0]) { kmemcpy(cmd, r + 1, n - 2); cmd[n - 2] = 0; }
    else kmemcpy(cmd, r, n + 1);
    while (kstrlen(cmd) && (cmd[kstrlen(cmd) - 1] == ' ')) cmd[kstrlen(cmd) - 1] = 0;
    if (!cmd[0]) { usage("alias [NAME] [==|=:] ['COMMAND']"); return; }
    if (!alias_valid_name(name)) { err("alias", name, "invalid name (1-16 letters, digits, '_' or '-')"); return; }
    if (is_builtin_name(name)) { err("alias", name, "is already a built-in command"); return; }
    if (perm && !user_name()) { err("alias", name, "login required to save an alias (=:); use == for this session"); return; }
    char full[LINE_MAX + 8];
    if (!kstrncmp(cmd, "as ", 3) || !kstrcmp(cmd, "as")) ksnprintf(full, sizeof full, "%s", cmd);
    else ksnprintf(full, sizeof full, "as %s", cmd);
    if (kstrlen(full) >= sizeof aliases[0].cmd) { err("alias", name, "command too long"); return; }
    alias_t *slot = NULL;
    for (int i = 0; i < naliases; i++) if (!kstrcmp(aliases[i].name, name)) slot = &aliases[i];
    if (!slot) {
        if (naliases >= ALIAS_MAX) { err("alias", name, "too many aliases (max 16)"); return; }
        slot = &aliases[naliases++];
    }
    kstrncpy(slot->name, name, 16); slot->name[16] = 0;
    kstrncpy(slot->cmd, full, sizeof slot->cmd - 1); slot->cmd[sizeof slot->cmd - 1] = 0;
    slot->perm = (u8)perm;
    alias_save();
    kprintf("Alias set: as %s  ->  %s%s\n", name, full, perm ? "   (saved)" : "");
}

/* ---------- dispatch ---------- */
typedef struct {
    const char *name;
    const char *args;
    const char *desc;
    void (*fn)(const char *);
    int flags;
} command;

static const command commands[] = {
    { "help",     "",                           "Show this list of commands",             cmd_help,       0 },
    { "version",  "",                           "Show OS, kernel and terminal version",   cmd_version,    0 },
    { "info",     "",                           "Show system information",               cmd_info,       0 },
    { "arch",     "",                           "Show the CPU architecture and 64-bit status", cmd_arch,    0 },
    { "clear",    "",                           "Clear the screen",                       cmd_clear,      0 },
    { "files",    "[path]",                     "List files and directories",             cmd_files,      F_LOGIN },
    { "location", "",                           "Print the current directory",            cmd_location,   0 },
    { "goto",     "<path>",                     "Change the current directory",           cmd_goto,       F_LOGIN },
    { "newdir",   "<name>",                     "Create a directory",                     cmd_newdir,     F_LOGIN },
    { "newfile",  "<name>",                     "Create an empty file",                   cmd_newfile,    F_LOGIN },
    { "read",     "<file>",                     "Print a file's contents",                cmd_read,       F_LOGIN },
    { "write",    "[-a] <file> <text>",         "Write (-a append) text; \\n = newline",  cmd_write,      F_LOGIN },
    { "copy",     "<SRC> <DEST>",               "Copy a file or folder (like cp)",        cmd_copy,       F_LOGIN },
    { "rem",      "[-r|-rf] <FILE>",            "Remove a file or folder (like rm)",      cmd_rem,        F_LOGIN },
    { "edit",     "<file>",                     "Edit a file (AstraScript errors shown live)", cmd_edit,  F_LOGIN | F_NOSCRIPT },
    { "nrun",     "<hello|FILE> [args]",        "Run a native C/C++ program (ELF)",       cmd_nrun,       F_LOGIN | F_NOSCRIPT },
    { "neofetch", "",                           "Show the AstraSage system banner",       neofetch_wrap,  0 },
    { "login",    "[create|remove] <PASS> <NAME>", "Log in, create or remove an account", cmd_login,      F_NOSCRIPT },
    { "account",  "[-change --NAME|--PASSWORD <PASS> <NEW>]", "Show or change your account name / password", cmd_account, F_NOSCRIPT },
    { "logout",   "",                           "Log out of the current account",         cmd_logout,     F_NOSCRIPT },
    { "cper",     "[+x|-x] <FILE.as>",          "Grant/revoke execute permission (.asbin)", cmd_cper,     F_LOGIN | F_NOSCRIPT },
    { "aspi",     "<install|remove|list|info|repo|log|run> ...", "Package installer (also written: aspi ...)", cmd_aspi, F_NOSCRIPT },
    { "net",      "[ip <ip> [mask] [gw]]",      "Network card, link and address",         cmd_net,        F_NOSCRIPT },
    { "weather",  "",                              "Current weather (needs the internet)",  cmd_weather,    F_NOSCRIPT },
    { "wifi",     "[status|scan|connect <ssid> <pass>]", "Wi-Fi manager (adapter status)",       cmd_wifi,       F_NOSCRIPT },
    { "bluetooth", "[status|scan|pair <name>]",   "Bluetooth manager (controller status)",  cmd_bluetooth,  F_NOSCRIPT },
    { "ping",     "<a.b.c.d> [count]",          "Send ICMP echo requests",                cmd_ping,       F_NOSCRIPT },
    { "get",      "<http(s)-url> [file]",       "Download a web page over HTTP or HTTPS", cmd_get,        F_NOSCRIPT },
    { "tls",      "[strict|insecure]",          "TLS status / certificate checking",      cmd_tls,        F_NOSCRIPT },
    { "reboot",   "",                           "Restart the computer",                   cmd_reboot,     F_NOSCRIPT },
    { "shutdown", "",                           "Power the computer off",                 cmd_shutdown,   F_NOSCRIPT },
    { "tree",     "[path] [--files|--dir]",     "List a folder as a tree",                cmd_tree,       F_LOGIN },
    { "alias",    "[NAME] [==|=:] ['COMMAND']", "Make a short name for a command",        cmd_alias,      0 },
    { "tab",      "[--add|--remove] [<TAB_NAME>]", "Terminal tabs (each its own AST session)", cmd_tab, F_NOSCRIPT },
    { "custom",   "[--tab_color|--line_color|--custom_input_color|--neofetch] <values>", "Colours of tabs, lines, typed text, neofetch", cmd_custom, 0 },
    { "proc",     "[<PROC_FILE>] [--sleep <SECONDS>]", "Run a /proc trigger file (error screens)", cmd_proc, F_LOGIN | F_NOSCRIPT },
    { "seal",     "<FILE.as> [<APP_NAME>]",     "Compile + encrypt a script into /user/apps", cmd_seal,  F_LOGIN | F_NOSCRIPT },
    { "userapp",  "<NAME>",                     "Run a sealed app of /user/apps",         cmd_userapp,    F_LOGIN | F_NOSCRIPT },
    { "script",   "<FILE.as>",                  "Run a script (same as: asscript)",       cmd_asscript,   F_LOGIN | F_NOSCRIPT },
};
#define NCOMMANDS (sizeof commands / sizeof commands[0])

static int is_builtin_name(const char *w)
{
    for (size_t i = 0; i < NCOMMANDS; i++) if (!kstrcmp(w, commands[i].name)) return 1;
    return !kstrcmp(w, "asscript") || !kstrcmp(w, "as") || !kstrcmp(w, "aspi");
}

int ast_is_builtin_name(const char *w) { return is_builtin_name(w); }

int ast_command_count(void) { return (int)NCOMMANDS + 1; }   /* + asscript */

/*
 * Validates a command line written inside an AstraScript file ("as newfile x")
 * without running it. Returns NULL when it is fine, otherwise a short error name.
 */
static const char *check_one(const char *line)
{
    char word[LINE_MAX + 1];
    const char *p = next_tok(line, word);

    if (!kstrcmp(word, "asscript"))
        return "Scripts cannot start other scripts.";
    if (kstrcmp(word, "as"))
        return "Unknown syntax.";
    p = next_tok(p, word);
    if (!word[0])
        return "Missing command after 'as'.";

    for (size_t i = 0; i < NCOMMANDS; i++) {
        if (kstrcmp(word, commands[i].name)) continue;
        if (commands[i].flags & F_NOSCRIPT)
            return "Command not allowed inside a script.";
        int need = 0, depth = 0;                       /* <required> arguments outside [...] */
        for (const char *a = commands[i].args; *a; a++) {
            if (*a == '[') depth++;
            else if (*a == ']') depth--;
            else if (*a == '<' && depth == 0) need++;
        }
        int have = 0;
        for (;;) {
            char t[LINE_MAX + 1];
            p = next_tok(p, t);
            if (!t[0]) break;
            if (t[0] != '-' || !t[1]) have++;
        }
        if (have < need) return "Missing argument.";
        return NULL;
    }
    return "Unknown AstraSage command.";
}



/* Validates a script line; "a && b" is checked piece by piece. */
const char *ast_check_command(const char *line)
{
    char seg[LINE_MAX + 1];
    const char *p = line;
    for (;;) {
        const char *q = p, *sep = NULL;
        for (; *q; q++)
            if (q[0] == '&' && q[1] == '&' && (q == line || q[-1] == ' ' || q[-1] == '\t') && (!q[2] || q[2] == ' ' || q[2] == '\t')) { sep = q; break; }
        size_t n = (size_t)((sep ? sep : q) - p);
        if (n > LINE_MAX) n = LINE_MAX;
        kmemcpy(seg, p, n);
        seg[n] = 0;
        if (!*skip_ws(seg)) return "A command is missing next to &&.";
        const char *e = check_one(seg);
        if (e) return e;
        if (!sep) return NULL;
        p = sep + 2;
    }
}

static void cmd_help(const char *a)
{
    (void)a;
    kprintf(C_LG "%s" C_RST "\n", TT("AST - AstraSageOS Terminal. Usage: as <command> [arguments]"));
    for (size_t i = 0; i < NCOMMANDS; i++) {
        char left[48];
        ksnprintf(left, sizeof left, "as %s %s", commands[i].name, commands[i].args);
        kprintf("  %-34s %s\n", left, TT(commands[i].desc));
    }
    kprintf("  %-34s %s\n", "asscript <FILE.as>", TT("Run an AstraScript file"));
    kprintf(C_DIM "%s" C_RST "\n", TT("Files need a login. Scripts need: as cper +x <FILE.as>"));
}

static void need_login(void)
{
    fail_flag = 1;
    kprintf(C_ERR "%s" C_RST "\n", TT("AST: login required - as login <PASSWORD> <NAME>   (new account: as login create <PASSWORD> <NAME>)"));
}

static void run_line(const char *line, int from_script);

/* Password prompt of acs: typed characters show as '*'. Returns 0 when cancelled (Ctrl+C). */
static int read_masked(char *buf, size_t cap)
{
    size_t n = 0;
    for (;;) {
        int c = kbd_getkey();
        if (c == '\n') { con_putc('\n'); buf[n] = 0; return 1; }
        if (c == 3) { kputs("^C\n"); buf[0] = 0; return 0; }
        if (c == '\b') { if (n) { n--; con_putc('\b'); } }
        else if (c >= 32 && c < 127 && n + 1 < cap) { buf[n++] = (char)c; con_putc('*'); }
    }
}

static void cmd_acs(const char *rest)
{
    rest = skip_ws(rest);
    if (!*rest) { kprintf("Usage: acs <command>   e.g. acs as newdir root:/mydir   (acs aspi install <pkg>)\n"); return; }
    char pw[64];
    kprintf("[acs] Pls Enter your Password: ");
    if (!read_masked(pw, sizeof pw)) return;
    int ok = user_verify_password(pw);
    kmemset(pw, 0, sizeof pw);
    if (!ok) { fail_flag = 1; kprintf(C_ERR "[acs] %s" C_RST "\n", config_tr("Sorry, wrong password.", "Yanlis parola.")); return; }
    char line[LINE_MAX + 1];
    ksnprintf(line, sizeof line, "%s", rest);
    fs_acs_set(1);
    run_line(line, 0);
    fs_acs_set(0);
}

/* The editor, the script engine, ASPI and native programs exist once: with several terminal tabs only one tab
 * uses them at a time (the others get a short message and can try again). */
int ast_cur_tab;                                  /* set by the tab manager (0 without tabs) */
static int engine_owner = -1;
static int (*tab_alive_fn)(int);
void ast_set_tab_alive(int (*fn)(int)) { tab_alive_fn = fn; }
void ast_engine_release(int tab) { if (engine_owner == tab) engine_owner = -1; }
static int engine_take(const char *what)
{
    if (engine_owner >= 0 && engine_owner != ast_cur_tab && (!tab_alive_fn || tab_alive_fn(engine_owner))) {
        fail_flag = 1;
        kprintf(C_ERR "AST: %s: %s" C_RST "\n", what, config_tr("another tab is using the editor / script engine - wait for it or close that tab",
                                                              "baska bir sekme duzenleyiciyi / betik motorunu kullaniyor - bekleyin ya da o sekmeyi kapatin"));
        return 0;
    }
    engine_owner = ast_cur_tab;
    return 1;
}
static int word_needs_engine(const char *w)
{
    static const char *const n[] = { "edit", "script", "userapp", "seal", "nrun", "check", 0 };
    for (int i = 0; n[i]; i++) if (!kstrcmp(w, n[i])) return 1;
    return 0;
}

static void exec_single(const char *line, int from_script)
{
    char word[LINE_MAX + 1];
    const char *p = next_tok(line, word);
    if (!word[0]) return;
    int saved = in_script;
    in_script = from_script;

    if (!kstrcmp(word, "asscript")) {                       /* asscript <file> */
        if (from_script) kprintf(C_ERR "%s" C_RST "\n", TT("AST: scripts cannot start other scripts"));
        else if (!user_name()) need_login();
        else if (engine_take("asscript")) cmd_asscript(p);
        in_script = saved;
        return;
    }
    if (!kstrcmp(word, "aspi")) {                           /* aspi <...>  =  as aspi <...> */
        if (from_script) kprintf(C_ERR "%s" C_RST "\n", TT("AST: 'aspi' is not allowed inside a script"));
        else if (engine_take("aspi")) cmd_aspi(p);
        in_script = saved;
        return;
    }
    if (!kstrcmp(word, "acs")) {                            /* acs <command>: run it with the password's authority */
        if (from_script) kprintf(C_ERR "%s" C_RST "\n", TT("AST: 'acs' is not allowed inside a script"));
        else if (!user_name()) need_login();
        else cmd_acs(p);
        in_script = saved;
        return;
    }
    if (kstrcmp(word, "as")) {
        fail_flag = 1;
        { kprintf(C_ERR); kprintf(TT("AST: unknown command '%s' - commands start with 'as' (only aspi stands alone), try: as help"), word); kprintf(C_RST "\n"); }
        in_script = saved;
        return;
    }
    p = next_tok(p, word);
    if (!word[0]) { kprintf("%s\n", TT("Usage: as <command>   (try: as help)")); in_script = saved; return; }

    for (size_t i = 0; i < NCOMMANDS; i++) {
        if (kstrcmp(word, commands[i].name)) continue;
        if (from_script && (commands[i].flags & F_NOSCRIPT))
            { kprintf(C_ERR); kprintf(TT("AST: 'as %s' is not allowed inside a script"), word); kprintf(C_RST "\n"); }
        else if ((commands[i].flags & F_LOGIN) && !user_name())
            need_login();
        else if (word_needs_engine(word) && !engine_take(word))
            ;
        else
            commands[i].fn(p);
        in_script = saved;
        return;
    }
    {
        const char *ex = alias_find(word);                  /* as <alias> [more arguments] */
        if (ex && exec_depth < 5) {
            char nl[LINE_MAX + 1];
            const char *rest = skip_ws(p);
            ksnprintf(nl, sizeof nl, "%s%s%s", ex, *rest ? " " : "", rest);
            in_script = saved;
            run_line(nl, from_script);
            return;
        }
    }
    if (!from_script && engine_take(word) && aspi_package_command(word, p)) { in_script = saved; return; }   /* command of an installed package */
    fail_flag = 1;
    { kprintf(C_ERR); kprintf(TT("AST: unknown command 'as %s' - try: as help"), word); kprintf(C_RST "\n"); }
    in_script = saved;
}

/* ---- brace groups: as rem -rf {a.as b.as}  ==  as rem -rf a.as  +  as rem -rf b.as ---- */
static void run_segment(const char *seg, int from_script)
{
    char w1[LINE_MAX + 1], w2[LINE_MAX + 1];
    const char *p = next_tok(seg, w1);
    next_tok(p, w2);
    int raw = !kstrcmp(w1, "as") && (!kstrcmp(w2, "write") || !kstrcmp(w2, "alias"));   /* free text: braces are literal */
    const char *lb = raw ? NULL : kstrchr(seg, '{');
    const char *rb = lb ? kstrchr(lb, '}') : NULL;
    if (!lb || !rb) { exec_single(seg, from_script); return; }

    const char *ts = lb;                                       /* the word the group is part of */
    while (ts > seg && ts[-1] != ' ' && ts[-1] != '\t') ts--;
    const char *te = rb + 1;
    while (*te && *te != ' ' && *te != '\t') te++;

    char grp[LINE_MAX + 1];
    size_t gl = (size_t)(rb - lb - 1);
    kmemcpy(grp, lb + 1, gl);
    grp[gl] = 0;
    int items = 0;
    for (const char *g = grp; *g;) {
        while (*g == ' ' || *g == '\t' || *g == ',') g++;
        if (!*g) break;
        const char *e = g;
        while (*e && *e != ' ' && *e != '\t' && *e != ',') e++;
        if (++items > 32) { err("brace", "{}", "too many items in one {} group (max 32)"); return; }
        char line[LINE_MAX + 1];
        size_t o = 0, pre = (size_t)(lb - ts), item = (size_t)(e - g), suf = (size_t)(te - (rb + 1)), head = (size_t)(ts - seg);
        size_t tail = kstrlen(te);
        if (head + pre + item + suf + tail >= LINE_MAX) { err("brace", "{}", "line too long after expanding {}"); return; }
        kmemcpy(line + o, seg, head); o += head;
        kmemcpy(line + o, ts, pre); o += pre;
        kmemcpy(line + o, g, item); o += item;
        kmemcpy(line + o, rb + 1, suf); o += suf;
        kmemcpy(line + o, te, tail + 1);
        run_segment(line, from_script);                         /* more groups in the same line */
        g = e;
    }
    if (!items) err("brace", "{}", "empty {} group");
}

/* ---- && : the commands run left to right, the chain stops at the first one that fails ---- */
static void run_line(const char *line, int from_script)
{
    if (++exec_depth > 6) { exec_depth--; err("alias", "...", "aliases call each other too deeply"); return; }
    char seg[LINE_MAX + 1];
    const char *p = line;
    for (;;) {
        size_t n = 0;
        const char *q = p;
        const char *sep = NULL;
        for (; *q; q++) {                                       /* a lone "&&" token separates commands */
            if (q[0] == '&' && q[1] == '&' && (q == line || q[-1] == ' ' || q[-1] == '\t') && (!q[2] || q[2] == ' ' || q[2] == '\t')) { sep = q; break; }
        }
        n = (size_t)((sep ? sep : q) - p);
        if (n > LINE_MAX) n = LINE_MAX;
        kmemcpy(seg, p, n);
        seg[n] = 0;
        const char *t = skip_ws(seg);
        if (!*t) {
            if (sep || p != line) { err("&&", "", "a command is missing next to &&"); }
        } else {
            fail_flag = 0;
            run_segment(seg, from_script);
        }
        if (fail_flag || !sep) break;
        p = sep + 2;
    }
    exec_depth--;
}

static char pbuf_big[FS_FILE_MAX + 8];

int ast_capture_command(const char *cmd, char *out, int cap)
{
    char word[LINE_MAX + 1], line[LINE_MAX + 8];
    out[0] = 0;
    while (*cmd == ' ') cmd++;
    const char *p = next_tok(cmd, word);
    const char *body = kstrcmp(word, "as") ? cmd : p;
    char w2[LINE_MAX + 1];
    next_tok(body, w2);
    static const char *const blocked[] = { "edit", "login", "logout", "reboot", "shutdown", "asscript", "aspi", "clear" };
    for (unsigned i = 0; i < sizeof blocked / sizeof blocked[0]; i++)
        if (!kstrcmp(w2, blocked[i])) return -1;
    ksnprintf(line, sizeof line, "as %s", body);
    con_capture_begin(out, (u32)cap);
    ast_exec(line, (kstrcmp(w2, "script") && kstrcmp(w2, "cper")) ? 1 : 0);   /* "script FILE" and "cper" are the commands a program may start (the caller checks the file first) */
    con_capture_end();
    return 0;
}

int ast_bg_exec(const char *cmd)
{
    if (!user_name()) return -1;
    if (!cwd) { cwd = user_home(); alias_load(); }
    ast_exec(cmd, 0);
    persist_flush();
    return 0;
}

int ast_file_put(const char *path, const char *text, int append)
{
    char pbuf[LINE_MAX + 2];
    if (!user_name()) return -1;
    int node = fs_resolve(user_path(path, pbuf, sizeof pbuf), cwd);
    if (node == FS_ENOENT) node = g_create(user_path(path, pbuf, sizeof pbuf), cwd, 0);
    if (node < 0) return node;
    u32 n = (u32)kstrlen(text), old = append ? fs_size(node) : 0;
    char *buf = pbuf_big;
    if (old + n > FS_FILE_MAX) return -2;
    if (old) kmemcpy(buf, fs_data(node), old);
    kmemcpy(buf + old, text, n);
    return g_write(node, buf, old + n, 0) < 0 ? -3 : 0;
}

int ast_file_write(const char *path, const void *data, unsigned len)
{
    char pbuf[LINE_MAX + 2];
    if (!user_name()) return -1;
    if (len > FS_FILE_MAX) return -2;
    int node = fs_resolve(user_path(path, pbuf, sizeof pbuf), cwd);
    if (node == FS_ENOENT) node = g_create(user_path(path, pbuf, sizeof pbuf), cwd, 0);
    if (node < 0) return node;
    if (fs_is_dir(node)) return -4;
    int r = g_write(node, data, len, 0) < 0 ? -3 : 0;
    persist_flush();
    return r;
}

int ast_file_read(const char *path, char *out, int cap)
{
    char pbuf[LINE_MAX + 2];
    out[0] = 0;
    if (!user_name()) return -1;
    int node = fs_resolve(user_path(path, pbuf, sizeof pbuf), cwd);
    if (node < 0 || fs_is_dir(node)) return -2;
    u32 n = fs_size(node);
    if (n > (u32)cap - 1) n = (u32)cap - 1;
    kmemcpy(out, fs_data(node), n);
    out[n] = 0;
    return (int)n;
}

void ast_exec(const char *line, int from_script)
{
    exec_depth = 0;
    fail_flag = 0;
    run_line(line, from_script);
}

static void prompt(void)
{
    char loc[170];
    const char *u = user_name() ? user_name() : "Unknown";
    location_text(loc, sizeof loc);
    kprintf(C_G "[" C_LG "%s@%s" C_G "]" C_W ":" C_DIR "%s" C_W "%s" C_DOL "$" C_RST " ",
            u, hal_model(), loc, user_name() ? ": " : " ");
}

/* Line input with echo and backspace. */
void ast_input(char *buf, size_t cap)
{
    size_t n = 0;
    if (cap > LINE_MAX + 1) cap = LINE_MAX + 1;
    for (;;) {
        int c = kbd_getc();
        if (c == '\n') { con_putc('\n'); break; }
        if (c == '\b') {
            if (n) { n--; con_putc('\b'); }
        } else if (c >= 32 && c < 127 && n + 1 < cap) {
            buf[n++] = (char)c;
            con_putc((char)c);
        }
    }
    buf[n] = 0;
}

/*
 * Command-line input for the prompt. Same as ast_input, except that the password in
 *     as login [create|remove] <PASSWORD> <NAME>
 * is echoed as '*'. While the first argument is being typed it is masked too; once
 * it is complete and turns out to be "create" or "remove" it is shown as a word.
 */
static void render_line(const char *b, size_t n, char *shown)
{
    size_t ts[6] = {0}, te[6] = {0};
    int tcount = 0;
    size_t i = 0;
    while (i < n && tcount < 6) {
        while (i < n && (b[i] == ' ' || b[i] == '\t')) i++;
        if (i >= n) break;
        ts[tcount] = i;
        while (i < n && b[i] != ' ' && b[i] != '\t') i++;
        te[tcount++] = i;
    }
    int pw = -1;                                           /* index of the password token */
    if (tcount >= 2 && te[1] < n &&
        te[0] - ts[0] == 2 && !kstrncmp(b + ts[0], "as", 2) &&
        te[1] - ts[1] == 5 && !kstrncmp(b + ts[1], "login", 5)) {
        pw = 2;
        if (tcount >= 3 && te[2] < n) {
            size_t l = te[2] - ts[2];
            if ((l == 6 && (!kstrncmp(b + ts[2], "create", 6) || !kstrncmp(b + ts[2], "remove", 6))))
                pw = 3;
        }
    }
    int pw2 = -1;                                          /* "as account -change --PASSWORD <PASS> <NEW>": both are secret */
    if (tcount >= 5 && te[1] < n && te[0] - ts[0] == 2 && !kstrncmp(b + ts[0], "as", 2) &&
        te[1] - ts[1] == 7 && !kstrncmp(b + ts[1], "account", 7)) {
        pw = 4;
        if (te[3] - ts[3] == 10 && !kstrncmp(b + ts[3], "--PASSWORD", 10)) pw2 = 5;
        else if (te[3] - ts[3] == 10 && !kstrncmp(b + ts[3], "--password", 10)) pw2 = 5;
    }
    for (size_t k = 0; k < n; k++) shown[k] = b[k];
    if (pw >= 0 && pw < tcount)
        for (size_t k = ts[pw]; k < te[pw]; k++) shown[k] = '*';
    if (pw2 >= 0 && pw2 < tcount)
        for (size_t k = ts[pw2]; k < te[pw2]; k++) shown[k] = '*';
}

/* ---- command history (Up / Down) ---- */
#define HIST_N 32
static char hist[HIST_N][LINE_MAX + 2];
static int  hist_count, hist_head;                 /* entries stored / next slot */

static void hist_add(const char *line)
{
    if (!line[0]) return;
    if (!kstrncmp(line, "as login", 8) || !kstrncmp(line, "as account", 10)) return;     /* passwords never enter the history */
    int last = (hist_head + HIST_N - 1) % HIST_N;
    if (hist_count && !kstrcmp(hist[last], line)) return;
    kstrncpy(hist[hist_head], line, LINE_MAX + 1);
    hist[hist_head][LINE_MAX + 1] = 0;
    hist_head = (hist_head + 1) % HIST_N;
    if (hist_count < HIST_N) hist_count++;
}
static const char *hist_get(int back)               /* 1 = newest */
{
    if (back < 1 || back > hist_count) return 0;
    return hist[(hist_head + HIST_N - back) % HIST_N];
}

static void ast_input_cmd(char *buf, size_t cap)
{
    size_t n = 0, sn = 0;
    char shown[LINE_MAX + 2], now[LINE_MAX + 2], undo[LINE_MAX + 2], draft[LINE_MAX + 2];
    int browse = 0, undo_n = -1;
    if (cap > LINE_MAX + 1) cap = LINE_MAX + 1;
    for (;;) {
        int c = kbd_getkey();
        int changed = 1;
        if (c == '\n') { con_putc('\n'); break; }
        if (c == '\b') { if (!n) continue; n--; browse = 0; }
        else if (c >= 32 && c < 127 && n + 1 < cap) { buf[n++] = (char)c; browse = 0; }
        else if (c == KEY_UP || c == KEY_DOWN) {                         /* older / newer commands */
            int want = browse + (c == KEY_UP ? 1 : -1);
            if (want < 0 || want > hist_count) continue;
            if (browse == 0) { kmemcpy(draft, buf, n); draft[n] = 0; }
            kmemcpy(undo, buf, n); undo[n] = 0; undo_n = (int)n;
            browse = want;
            const char *h = browse ? hist_get(browse) : draft;
            n = kstrlen(h); if (n >= cap) n = cap - 1;
            kmemcpy(buf, h, n);
        }
        else if (c == 3) {                                               /* Ctrl+C: drop the line */
            kputs("^C\n"); n = 0; typed_len = 0; sn = 0; buf[0] = 0; return;
        }
        else if (c == 21) { kmemcpy(undo, buf, n); undo[n] = 0; undo_n = (int)n; n = 0; browse = 0; }    /* Ctrl+U: clear the line */
        else if (c == 23) {                                                                             /* Ctrl+W: delete the last word */
            kmemcpy(undo, buf, n); undo[n] = 0; undo_n = (int)n;
            while (n && buf[n - 1] == ' ') n--;
            while (n && buf[n - 1] != ' ') n--;
            browse = 0;
        }
        else if (c == 26) {                                              /* Ctrl+Z: undo the last big change of the line */
            if (undo_n < 0) continue;
            char t[LINE_MAX + 2]; kmemcpy(t, buf, n); size_t tn = n;
            n = (size_t)undo_n; kmemcpy(buf, undo, n);
            kmemcpy(undo, t, tn); undo_n = (int)tn; browse = 0;
        }
        else if (c == 22) {                                              /* Ctrl+V: paste the clipboard (first line) */
            int cl; const char *cp = ast_clip(&cl);
            for (int i = 0; i < cl && cp[i] != '\n' && n + 1 < cap; i++) if (cp[i] >= 32 && cp[i] < 127) buf[n++] = cp[i];
            browse = 0;
        }
        else if (c == 12) { kputs("\033[2J\033[H"); sn = 0; shown[0] = 0; changed = 1; prompt(); }   /* Ctrl+L: clear the screen */
        else changed = 0;
        if (!changed) continue;
        typed_len = (int)n;
        render_line(buf, n, now);
        size_t k = 0;
        while (k < sn && k < n && shown[k] == now[k]) k++;
        while (sn > k) { con_putc('\b'); sn--; }
        for (; k < n; k++) con_putc(now[k]);
        for (k = 0; k < n; k++) shown[k] = now[k];
        sn = n;
    }
    buf[n] = 0;
    typed_len = 0;
    hist_add(buf);
}

/* The desktop (file explorer) wants to run a command in AST: only possible while the prompt waits. */
int ast_prompt_state(int *typed)
{
    if (typed) *typed = typed_len;
    return at_prompt;
}

/* One-time system start: disk, file tree, font. */
void ast_system_init(void)
{
    int restored = persist_init();                     /* mount the ATA disk, if there is one */
    if (restored > 0) { fs_seed_system(); bl_add(BL_OK, "System files refreshed (saved files kept)"); }   /* keep the saved files, refresh system ones */
    else { fs_init(); bl_add(BL_INFO, "Filesystem created in memory (fs_init): /bin /user /proc /.term /.aspi /.config"); }
    config_init();                                     /* /config/settings: language, theme, login mode ... */
    if (fs_resolve("/.config/settings", 0) > 0) bl_add(BL_OK, "Settings read: /.config/settings");
    else bl_add(BL_INFO, "Settings: /.config/settings not found, defaults used");
    int font = fs_resolve("/.term/ast8x16.fnt", 0);
    if (font > 0) { con_set_font((const u8 *)fs_data(font), fs_size(font)); bl_add(BL_OK, "Font loaded: /.term/ast8x16.fnt (%u bytes)", fs_size(font)); }
    else bl_add(BL_WARN, "Font /.term/ast8x16.fnt missing: built-in font used");
    if (user_session_restore()) bl_add(BL_OK, "Saved login found: %s", user_name());   /* the login of the last session comes back (saved, encrypted) */
    else bl_add(BL_INFO, "No saved login in /.config");
    persist_flush();
}

/* The shell: banner, then prompt / read / run forever. Runs on the text console or in the AST window. */
void ast_session(void)
{
    char line[LINE_MAX + 1];

    kprintf(C_LG "%s %s" C_RST " - kernel %s\n", AS_OS_NAME, AS_VERSION, AS_KERNEL_NAME);
    kprintf(TT("Welcome to AST. Type %s for the command list."), C_LG "as help" C_RST); kprintf("\n");
    if (persist_active())
        kprintf(C_DIM "Disk: %s - files are kept across reboots." C_RST "\n", persist_status());
    else
        kprintf(C_ERR "Disk: %s - files are lost on shutdown (use: make run)." C_RST "\n",
                persist_status());
    {
        char note[100];
        while (ui_take_note(note, sizeof note)) kprintf(C_ERR "GUI: %s" C_RST "\n", note);
    }
    cwd = user_name() ? user_home() : 0;
    alias_load();
    if (user_name())
        { kprintf(C_DIM); kprintf(TT("Signed in as %s (saved login). Sign out with: as logout"), user_name()); kprintf(C_RST "\n\n"); }
    else
        kprintf(C_DIM "%s" C_RST "\n\n", TT("New here? as login create <PASSWORD> <NAME>, then: as login <PASSWORD> <NAME>"));
    for (;;) {
        prompt();
        { int ic = config_get()->input_color; if (ic >= 0) kprintf("\x1b[38;5;%dm", con_rgb256(ic)); }   /* as custom --custom_input_color */
        at_prompt = 1;
        ast_input_cmd(line, sizeof line);
        at_prompt = 0;
        kprintf(C_RST);
        ast_exec(line, 0);
        ast_engine_release(ast_cur_tab);
        persist_flush();
    }
}

void ast_main(void)
{
    ast_system_init();
    ast_session();
}

/* ---- terminal tabs: what belongs to one AST session (the folder, the history, the state of the prompt) ---- */
typedef struct {
    int cwd, in_script, fail_flag, exec_depth, at_prompt, typed_len, hist_count, hist_head;
    char hist[HIST_N][LINE_MAX + 2];
} ast_tab_state;

_Static_assert(sizeof(ast_tab_state) <= AST_STATE_BYTES, "AST_STATE_BYTES too small");

void ast_state_save(void *dst)
{
    ast_tab_state *t = (ast_tab_state *)dst;
    t->cwd = cwd; t->in_script = in_script; t->fail_flag = fail_flag; t->exec_depth = exec_depth;
    t->at_prompt = at_prompt; t->typed_len = typed_len; t->hist_count = hist_count; t->hist_head = hist_head;
    kmemcpy(t->hist, hist, sizeof hist);
}

void ast_state_load(const void *src)
{
    const ast_tab_state *t = (const ast_tab_state *)src;
    cwd = t->cwd; in_script = t->in_script; fail_flag = t->fail_flag; exec_depth = t->exec_depth;
    at_prompt = t->at_prompt; typed_len = t->typed_len; hist_count = t->hist_count; hist_head = t->hist_head;
    kmemcpy(hist, t->hist, sizeof hist);
}

/* a new tab starts in the home folder with an empty history */
void ast_state_blank(void *dst)
{
    ast_tab_state *t = (ast_tab_state *)dst;
    kmemset(t, 0, sizeof *t);
    t->cwd = user_name() ? user_home() : 0;
}
