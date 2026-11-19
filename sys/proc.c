/*
 * /proc triggers, the error log and the /user integrity check.
 *
 *  - as proc [<FILE>] [--sleep <N>]   runs a trigger file of /proc: first line "panic" (green Kernel Panic screen)
 *                                     or "overload" (yellow screen + restart); "reason=..." is shown on the screen.
 *  - /user/apps/<name>.asx            a compiled + encrypted + sealed user app:
 *                                       "ASX1" 'E'  <body xor keystream>  <32 byte seal>
 *                                     checked at every login; a plain (uncompiled) file, an unencrypted one or one whose
 *                                     seal does not match is tampering -> yellow screen, the item is quarantined so the
 *                                     next start is clean (no restart loop).
 */
#include "astra.h"

extern u32 ui_ms(void);

#define APPS_DIR "/user/apps"
#define QUAR_DIR "/.term/log/quarantine"
#define SEAL_MAX 4096

/* ---- helpers that look at the real tree (the session's jail is lifted for a moment) ---- */
static int root_enter(void) { int o = fs_get_jail(); fs_set_jail(0); return o; }

static void rd_line(const char *d, u32 n, u32 *i, char *out, int cap)
{
    int k = 0;
    while (*i < n && d[*i] != '\n') { if (k < cap - 1 && d[*i] != '\r') out[k++] = d[*i]; (*i)++; }
    if (*i < n) (*i)++;
    out[k] = 0;
}

void err_log(const char *kind, const char *reason)
{
    int o = root_enter();
    int acs = fs_acs_get();
    fs_acs_set(1);
    char line[200];
    ksnprintf(line, sizeof line, "%s: %s\n", kind, reason ? reason : "-");
    int n = fs_resolve("/.term/log/errors.log", 0);
    if (n < 0) n = fs_create("/.term/log/errors.log", 0, 0);
    if (n >= 0) fs_write(n, line, (u32)kstrlen(line), 1);
    fs_acs_set(acs);
    fs_set_jail(o);
    persist_flush();
}

/* ---- as proc ---- */
static void list_proc(void)
{
    int o = root_enter();
    int d = fs_resolve("/proc", 0);
    kprintf("Trigger files in /proc  (as proc <FILE> [--sleep <SECONDS>]):\n");
    for (int c = d >= 0 ? fs_child(d, -1) : -1; c >= 0; c = fs_child(d, c)) {
        if (fs_is_dir(c)) continue;
        char kind[40], reason[100] = "";
        u32 i = 0; const char *data = fs_data(c); u32 n = fs_size(c);
        rd_line(data, n, &i, kind, sizeof kind);
        while (i < n) { char l[120]; rd_line(data, n, &i, l, sizeof l); if (!kstrncmp(l, "reason=", 7)) { kstrncpy(reason, l + 7, sizeof reason - 1); break; } }
        kprintf("  %-14s %-9s %s\n", fs_name(c), kind, reason);
    }
    fs_set_jail(o);
}

int proc_run(const char *name, int sleep_s)
{
    if (!name || !*name) { list_proc(); return 0; }
    int o = root_enter();
    char path[64];
    ksnprintf(path, sizeof path, "/proc/%s", name);
    int n = fs_resolve(path, 0);
    if (n < 0 || fs_is_dir(n)) { fs_set_jail(o); return -1; }
    char kind[40], reason[120] = "";
    const char *data = fs_data(n); u32 sz = fs_size(n), i = 0;
    rd_line(data, sz, &i, kind, sizeof kind);
    while (i < sz) { char l[140]; rd_line(data, sz, &i, l, sizeof l); if (!kstrncmp(l, "reason=", 7)) kstrncpy(reason, l + 7, sizeof reason - 1); }
    fs_set_jail(o);
    int panic = !kstrcmp(kind, "panic"), over = !kstrcmp(kind, "overload");
    int fault = !kstrcmp(kind, "fault");                 /* a real CPU exception, to test the exception -> Kernel Panic path */
    if (!panic && !over && !fault) return -2;
    if (!reason[0]) ksnprintf(reason, sizeof reason, "/proc/%s", name);
    if (sleep_s > 0) {
        kprintf("proc: %s starts in %d second(s)...\n", name, sleep_s);
        for (int s = sleep_s; s > 0; s--) {
            u32 t0 = ui_ms(), spins = 0;
            while (ui_ms() - t0 < 1000 && ++spins < 30000000u) { }
        }
    }
    if (fault) {
#ifdef HOST_TEST
        err_kernel_panic("CPU exception 6 (Invalid opcode) - simulated on the host");
#else
#if defined(__i386__) || defined(__x86_64__)
        __asm__ volatile("ud2");
#else
        __builtin_trap();
#endif
#endif
    }
    if (panic) err_kernel_panic(reason);
    err_overload(reason);
    return 0;
}

/* ---- sealed user apps ---- */
static void keystream(const char *name, u32 block, u8 out[32])
{
    char k[80];
    int n = ksnprintf(k, sizeof k, "AS-ENC:%s:%u", name, block);
    sha256(k, (size_t)n, out);
}

static void xor_body(const char *name, u8 *b, u32 len)
{
    u8 ks[32];
    for (u32 i = 0; i < len; i++) {
        if ((i & 31) == 0) keystream(name, i >> 5, ks);
        b[i] ^= ks[i & 31];
    }
}

static void make_seal(const char *name, const u8 *enc, u32 len, u8 out[32])
{
    static u8 tmp[SEAL_MAX + 96];
    int n = ksnprintf((char *)tmp, 64, "AS-SEAL:%s:", name);
    kmemcpy(tmp + n, enc, len);
    sha256(tmp, (size_t)n + len, out);
}

/* Compile-check + encrypt + seal a script into /user/apps/<name>.asx. Returns 0 or a negative FS error / -100 bad source. */
int user_seal_app(const char *name, const char *src, u32 len, char *msg, int cap)
{
    static u8 buf[SEAL_MAX + 64];
    asc_diag dg[1];
    if (len == 0 || len > SEAL_MAX - 40) { ksnprintf(msg, (size_t)cap, "source must be 1..%d bytes", SEAL_MAX - 40); return -100; }
    if (asc_check(src, len, dg, 1) > 0) { ksnprintf(msg, (size_t)cap, "line %d: %s (not compiled)", dg[0].line, dg[0].msg); return -100; }
    kmemcpy(buf, "ASX1E", 5);
    kmemcpy(buf + 5, src, len);
    xor_body(name, buf + 5, len);
    make_seal(name, buf + 5, len, buf + 5 + len);
    int o = root_enter(), acs = fs_acs_get();
    fs_acs_set(1);
    int d = fs_resolve(APPS_DIR, 0);
    if (d < 0) d = fs_create(APPS_DIR, 0, 1);
    char path[96];
    ksnprintf(path, sizeof path, APPS_DIR "/%s.asx", name);
    int n = fs_resolve(path, 0);
    if (n < 0) n = fs_create(path, 0, 0);
    int rc = n < 0 ? n : fs_write(n, (const char *)buf, 5 + len + 32, 0);
    fs_acs_set(acs); fs_set_jail(o);
    persist_flush();
    ksnprintf(msg, (size_t)cap, "%s.asx", name);
    return rc < 0 ? rc : 0;
}

/* returns 0 when intact (and decrypts the source into out), else a text in why */
static int open_sealed(int node, char *out, u32 *outlen, const char **why)
{
    const u8 *d = (const u8 *)fs_data(node); u32 n = fs_size(node);
    char name[FS_NAME_MAX]; kstrncpy(name, fs_name(node), sizeof name - 1); name[sizeof name - 1] = 0;
    char *dot = kstrchr(name, '.'); if (dot) *dot = 0;
    if (n < 5 + 32 || kmemcmp(d, "ASX1", 4)) { *why = "not compiled (plain file)"; return 1; }
    if (d[4] != 'E') { *why = "not encrypted"; return 1; }
    u32 len = n - 5 - 32;
    u8 want[32];
    make_seal(name, d + 5, len, want);
    if (kmemcmp(want, d + 5 + len, 32)) { *why = "modified (seal mismatch)"; return 1; }
    if (out) {
        kmemcpy(out, d + 5, len);
        xor_body(name, (u8 *)out, len);
        out[len] = 0; *outlen = len;
    }
    return 0;
}

int user_app_run(const char *name)
{
    static char src[SEAL_MAX + 1];
    char path[96]; u32 len = 0; const char *why = "";
    int o = root_enter();
    ksnprintf(path, sizeof path, APPS_DIR "/%s.asx", name);
    int n = fs_resolve(path, 0);
    int rc = n < 0 ? -1 : open_sealed(n, src, &len, &why);
    fs_set_jail(o);
    if (n < 0) return -1;
    if (rc) { kprintf("userapp: %s.asx is %s\n", name, why); return -2; }
    return asc_run(src, len);
}

/* Every login: look at /user/apps. The first offender is moved to the quarantine so the restart that follows is clean. */
int user_integrity_check(char *why_out, int cap)
{
    int o = root_enter(), acs = fs_acs_get();
    int d = fs_resolve(APPS_DIR, 0), bad = 0;
    for (int c = d >= 0 ? fs_child(d, -1) : -1; c >= 0 && !bad; c = fs_child(d, c)) {
        const char *why = "";
        if (fs_is_dir(c)) { ksnprintf(why_out, (size_t)cap, "%s/%s: unexpected folder", APPS_DIR, fs_name(c)); bad = 1; }
        else if (open_sealed(c, 0, 0, &why)) { ksnprintf(why_out, (size_t)cap, "%s/%s: %s", APPS_DIR, fs_name(c), why); bad = 1; }
        if (bad) {
            fs_acs_set(1);
            int q = fs_resolve(QUAR_DIR, 0);
            if (q < 0) q = fs_create(QUAR_DIR, 0, 1);
            if (q >= 0 && !fs_is_dir(c)) {
                char qp[110];
                ksnprintf(qp, sizeof qp, QUAR_DIR "/%s.%u", fs_name(c), (unsigned)hal_ticks() & 0xFFFF);
                int qn = fs_create(qp, 0, 0);
                if (qn >= 0) fs_write(qn, fs_data(c), fs_size(c), 0);
            }
            fs_delete(c);
            fs_acs_set(acs);
        }
    }
    fs_set_jail(o);
    return bad;
}
