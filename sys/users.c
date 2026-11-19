/*
 * Accounts and sessions.
 *   - account records live in the OS-root system file /user/accounts ("name:sha256")
 *   - every account owns S:/usrs/<name>/ (= /drives/S/usrs/<name>) with a "home" folder
 *   - a login jails the session to that folder (see fs.c)
 */
#include "astra.h"

static char cur_name[20];
void (*user_rename_hook)(const char *oldname, const char *newname);      /* the desktop moves its file shortcuts along */
static int  cur_home = -1;

static int sys_enter(void) { int o = fs_get_jail(); fs_set_jail(0); return o; }
static void sys_leave(int o) { fs_set_jail(o); }

static int valid_user(const char *n)
{
    size_t l = kstrlen(n);
    if (l < 1 || l > 16) return 0;
    for (; *n; n++) {
        char c = *n;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) return 0;
    }
    return 1;
}

static void pass_hash(const char *name, const char *pass, char out[65])
{
    char buf[96];
    int n = ksnprintf(buf, sizeof buf, "AS-PASS:%s:%s", name, pass);
    if (n > (int)sizeof buf - 1) n = (int)sizeof buf - 1;
    sha256_hex(buf, (size_t)n, out);
}

/* Find "name:" in the accounts file. Returns the hash text or NULL. */
static const char *find_account(const char *data, u32 len, const char *name)
{
    size_t nl = kstrlen(name);
    u32 i = 0;
    while (i < len) {
        u32 e = i;
        while (e < len && data[e] != '\n') e++;
        if (e - i > nl + 1 && !kstrncmp(data + i, name, nl) && data[i + nl] == ':')
            return data + i + nl + 1;
        i = e + 1;
    }
    return NULL;
}

/* How many accounts the machine has (lines in /user/accounts). */
int user_account_count(void)
{
    int acc = fs_resolve("/user/accounts", 0);
    if (acc < 0) return 0;
    const char *d = fs_data(acc);
    u32 len = fs_size(acc), n = 0, i = 0;
    while (i < len) {
        u32 e = i;
        while (e < len && d[e] != '\n') e++;
        if (e - i > 2) n++;
        i = e + 1;
    }
    return (int)n;
}

static int verify(const char *name, const char *pass)
{
    int acc = fs_resolve("/user/accounts", 0);
    if (acc < 0) return US_ENOUSER;
    const char *h = find_account(fs_data(acc), fs_size(acc), name);
    if (!h) return US_ENOUSER;
    char want[65];
    pass_hash(name, pass, want);
    int diff = 0;
    for (int i = 0; i < 64; i++) diff |= want[i] ^ h[i];
    return diff ? US_EPASS : US_OK;
}

int user_verify_password(const char *pass)
{
    const char *n = user_name();
    if (!n) return 0;
    int o = sys_enter();
    int rc = verify(n, pass);
    sys_leave(o);
    return rc == US_OK;
}

static int ensure_folders(const char *name)
{
    char p[64];
    ksnprintf(p, sizeof p, "/drives/S/usrs/%s", name);
    int r = fs_create(p, 0, 1);
    if (r < 0 && r != FS_EEXIST) return US_EFULL;
    ksnprintf(p, sizeof p, "/drives/S/usrs/%s/home", name);
    r = fs_create(p, 0, 1);
    if (r < 0 && r != FS_EEXIST) return US_EFULL;
    return US_OK;
}

int user_create(const char *name, const char *pass)
{
    if (!valid_user(name)) return US_ENAME;
    if (!*pass || kstrlen(pass) > 32) return US_EPASSFMT;
    int o = sys_enter(), rc = US_OK;
    int acc = fs_resolve("/user/accounts", 0);
    if (find_account(fs_data(acc), fs_size(acc), name)) { rc = US_EEXIST; goto out; }

    char line[96], hash[65];
    pass_hash(name, pass, hash);
    int n = ksnprintf(line, sizeof line, "%s:%s\n", name, hash);
    if (fs_size(acc) + (u32)n > FS_FILE_MAX) { rc = US_EFULL; goto out; }
    rc = ensure_folders(name);
    if (rc == US_OK && fs_write(acc, line, (u32)n, 1) < 0) rc = US_EFULL;
out:
    sys_leave(o);
    return rc;
}

static void session_forget(const char *name);

int user_remove(const char *name, const char *pass)
{
    if (!valid_user(name)) return US_ENAME;
    int o = sys_enter();
    int rc = verify(name, pass);
    if (rc == US_OK) {
        int acc = fs_resolve("/user/accounts", 0);
        const char *d = fs_data(acc);
        u32 len = fs_size(acc), i = 0, w = 0;
        static char out[FS_FILE_MAX];
        size_t nl = kstrlen(name);
        while (i < len) {                       /* copy every line except this user's */
            u32 e = i;
            while (e < len && d[e] != '\n') e++;
            int mine = (e - i > nl && !kstrncmp(d + i, name, nl) && d[i + nl] == ':');
            if (!mine) { kmemcpy(out + w, d + i, e - i + 1); w += e - i + 1; }
            i = e + 1;
        }
        fs_write(acc, out, w, 0);
        session_forget(name);
        char p[64];
        ksnprintf(p, sizeof p, "/drives/S/usrs/%s", name);
        int home = fs_resolve(p, 0);
        if (home > 0) fs_delete(home);
        if (!kstrcmp(cur_name, name)) { sys_leave(0); user_logout(); return US_OK; }
    }
    sys_leave(o);
    return rc;
}

/* Starts the session of an account that has been verified (password, or the saved session). */
static int enter_session(const char *name)
{
    int rc = ensure_folders(name);
    if (rc != US_OK) return rc;
    char p[64];
    ksnprintf(p, sizeof p, "/drives/S/usrs/%s", name);
    int root = fs_resolve(p, 0);
    ksnprintf(p, sizeof p, "/drives/S/usrs/%s/home", name);
    cur_home = fs_resolve(p, 0);
    kstrncpy(cur_name, name, sizeof cur_name - 1);
    fs_set_jail(root);                           /* jail the session in its own folder */
    return US_OK;
}

/*
 * The login survives closing the terminal and rebooting: for every saved user the OS root has
 *     /config/<name>.config
 * holding the user name and the password (its salted SHA-256 hash, as in /user/accounts - never the plain
 * password), both encrypted, plus a MAC:
 *     AS2
 *     nonce=<8 hex>
 *     name=<name XOR keystream, hex>
 *     pass=<hash XOR keystream, hex>
 *     mac=<SHA-256 over all of it>
 * The keystream is SHA-256(key, nonce, counter). The key is a constant built into the OS: this hides the data
 * from a casual look at the drive image and detects edits, but it is a home-made scheme on the kernel's SHA-256,
 * NOT an audited cipher, and anyone with the kernel binary could decrypt it. On start the system looks into
 * /config, decrypts the files and signs in a user only if name and hash agree with /user/accounts.
 */
static const char HEXD[] = "0123456789abcdef";
#define CFG_KEY "AstraSageOS-config-key-v1"

static void cfg_stream(const char *label, const char *nonce, u32 n, u8 *out)      /* n <= 64 bytes */
{
    for (u32 blk = 0; blk * 32 < n; blk++) {
        char b[96];
        int l = ksnprintf(b, sizeof b, "%s:%s:%s:%u", CFG_KEY, label, nonce, blk);
        u8 d[32];
        sha256(b, (size_t)l, d);
        for (u32 i = 0; i < 32 && blk * 32 + i < n; i++) out[blk * 32 + i] = d[i];
    }
}

static void hex_of(const u8 *in, u32 n, char *out)
{
    for (u32 i = 0; i < n; i++) { out[i * 2] = HEXD[in[i] >> 4]; out[i * 2 + 1] = HEXD[in[i] & 15]; }
    out[n * 2] = 0;
}

static int unhex(const char *in, u32 n, u8 *out)                  /* 2n hex chars -> n bytes; 0 = bad */
{
    for (u32 i = 0; i < n; i++) {
        const char *h = kstrchr(HEXD, in[i * 2]), *l = kstrchr(HEXD, in[i * 2 + 1]);
        if (!in[i * 2] || !in[i * 2 + 1] || !h || !l) return 0;
        out[i] = (u8)(((h - HEXD) << 4) | (l - HEXD));
    }
    return 1;
}

static void cfg_mac(const char *nonce, const char *nh, const char *ph, char out[65])
{
    char b[400];
    int n = ksnprintf(b, sizeof b, "%s:MAC:%s:%s:%s", CFG_KEY, nonce, nh, ph);
    sha256_hex(b, (size_t)n, out);
}

static int config_dir(void)
{
    int d = fs_resolve("/.config", 0);
    if (d == FS_ENOENT) d = fs_create("/.config", 0, 1);
    return d;
}

static void session_path(const char *name, char *out, size_t cap) { ksnprintf(out, cap, "/.config/%s.config", name); }

static void session_write(const char *name)
{
    int o = sys_enter();
    int acc = fs_resolve("/user/accounts", 0);
    const char *h = acc >= 0 ? find_account(fs_data(acc), fs_size(acc), name) : NULL;
    if (h && config_dir() > 0) {
        char path[64];
        session_path(name, path, sizeof path);
        int sf = fs_resolve(path, 0);
        if (sf == FS_ENOENT) sf = fs_create(path, 0, 0);
        if (sf > 0) {
            hal_time t;
            hal_datetime(&t);
            u32 seed = ktime_pack(&t) * 2654435761u ^ hal_uptime_sec() * 40503u ^ (u32)kstrlen(name) * 977u;
            char nonce[9];
            for (int i = 0; i < 8; i++) nonce[i] = HEXD[(seed >> (i * 4)) & 15];
            nonce[8] = 0;
            u8 kn[16], kp[64], nm[16] = { 0 }, pw[64];
            cfg_stream("n", nonce, 16, kn);
            cfg_stream("p", nonce, 64, kp);
            kmemcpy(nm, name, kstrlen(name));
            kmemcpy(pw, h, 64);
            for (int i = 0; i < 16; i++) nm[i] ^= kn[i];
            for (int i = 0; i < 64; i++) pw[i] ^= kp[i];
            char nh[33], ph[129], mac[65], line[400];
            hex_of(nm, 16, nh); hex_of(pw, 64, ph);
            cfg_mac(nonce, nh, ph, mac);
            int n = ksnprintf(line, sizeof line, "AS2\nnonce=%s\nname=%s\npass=%s\nmac=%s\n", nonce, nh, ph, mac);
            fs_write(sf, line, (u32)n, 0);
        }
    }
    sys_leave(o);
}

static void session_forget(const char *name)
{
    if (!name || !*name) return;
    int o = sys_enter();
    char path[64];
    session_path(name, path, sizeof path);
    int sf = fs_resolve(path, 0);
    if (sf > 0) fs_delete(sf);
    sys_leave(o);
}

/* One /config/<x>.config file: decrypted and checked against /user/accounts. 1 = valid, name filled in. */
static int session_check(int f, char name[17])
{
    const char *d = fs_data(f);
    u32 len = fs_size(f);
    if (len < 20 || kstrncmp(d, "AS2\n", 4)) return 0;
    char nonce[9], nh[33], ph[129], mac[65], want[65];
    const char *p = d + 4, *end = d + len;
    struct { const char *key; char *dst; u32 n; } f4[] = { { "nonce=", nonce, 8 }, { "name=", nh, 32 }, { "pass=", ph, 128 }, { "mac=", mac, 64 } };
    for (int i = 0; i < 4; i++) {
        size_t kl = kstrlen(f4[i].key);
        if ((u32)(end - p) < kl + f4[i].n + 1 || kstrncmp(p, f4[i].key, kl)) return 0;
        kmemcpy(f4[i].dst, p + kl, f4[i].n); f4[i].dst[f4[i].n] = 0;
        p += kl + f4[i].n;
        if (*p != '\n') return 0;
        p++;
    }
    cfg_mac(nonce, nh, ph, want);
    int diff = 0;
    for (int k = 0; k < 64; k++) diff |= want[k] ^ mac[k];
    if (diff) return 0;
    u8 kn[16], kp[64], nm[16], pw[64];
    cfg_stream("n", nonce, 16, kn);
    cfg_stream("p", nonce, 64, kp);
    if (!unhex(nh, 16, nm) || !unhex(ph, 64, pw)) return 0;
    for (int i = 0; i < 16; i++) nm[i] ^= kn[i];
    for (int i = 0; i < 64; i++) pw[i] ^= kp[i];
    kmemcpy(name, nm, 16); name[16] = 0;
    if (!valid_user(name)) return 0;
    int acc = fs_resolve("/user/accounts", 0);
    if (acc < 0) return 0;
    const char *h = find_account(fs_data(acc), fs_size(acc), name);
    if (!h) return 0;
    for (int i = 0; i < 64; i++) if ((u8)h[i] != pw[i]) return 0;      /* the saved password must still be the account's */
    return 1;
}

/* Boot / new terminal: look into /config and sign the saved user in again. 1 = restored. */
int user_session_restore(void)
{
    if (cur_name[0]) return 1;
    int o = sys_enter();
    int ok = 0, best = -1;
    u32 best_t = 0;
    int dir = fs_resolve("/.config", 0);
    if (dir > 0) {
        for (int c = fs_child(dir, 0); c > 0; c = fs_child(dir, c)) {         /* the most recently saved login wins */
            const char *n = fs_name(c);
            size_t l = kstrlen(n);
            if (fs_is_dir(c) || l < 8 || kstrcmp(n + l - 7, ".config")) continue;
            char who[17];
            if (!session_check(c, who)) continue;
            char want[64];
            ksnprintf(want, sizeof want, "%s.config", who);
            if (kstrcmp(want, n)) continue;                                   /* file name and name inside must agree */
            if (best < 0 || fs_mtime(c) >= best_t) { best = c; best_t = fs_mtime(c); }
        }
        if (best > 0) {
            char who[17];
            if (session_check(best, who)) {
                char why[120];
                if (user_integrity_check(why, sizeof why)) { sys_leave(o); err_overload(why); return 0; }   /* /user is checked at every sign-in */
                if (enter_session(who) == US_OK) ok = 1;
            }
        }
    }
    if (!ok) sys_leave(o);
    return ok;
}

/* Start of the graphical session: the saved login is not simply taken over, the password is asked first.
 * The account stays saved; only the session is put aside. Returns 1 and the name when there was one. */
static char locked_name[17];
int user_lock_session(void)
{
    if (!cur_name[0]) return 0;
    kstrncpy(locked_name, cur_name, 16); locked_name[16] = 0;
    cur_name[0] = 0;
    cur_home = -1;
    fs_set_jail(0);
    return 1;
}
const char *user_locked_name(void) { return locked_name[0] ? locked_name : NULL; }
void user_unlock_clear(void) { locked_name[0] = 0; }


/* Replace the account line of `old` by "<newname>:<hash>" (the hash is salted with the name, so a rename needs the
 * password again). Returns 0 or US_EFULL. Root context. */
static int account_replace(const char *old, const char *newname, const char *pass)
{
    int acc = fs_resolve("/user/accounts", 0);
    const char *d = fs_data(acc);
    u32 len = fs_size(acc), i = 0, w = 0;
    static char out[FS_FILE_MAX];
    size_t nl = kstrlen(old);
    char hash[65];
    pass_hash(newname, pass, hash);
    while (i < len) {
        u32 e = i;
        while (e < len && d[e] != '\n') e++;
        int mine = (e - i > nl && !kstrncmp(d + i, old, nl) && d[i + nl] == ':');
        if (mine) {
            int n = ksnprintf(out + w, sizeof out - w, "%s:%s\n", newname, hash);
            if (n < 0 || w + (u32)n >= sizeof out) return US_EFULL;
            w += (u32)n;
        } else if (e < len || e - i > 0) {
            if (w + (e - i) + 1 >= sizeof out) return US_EFULL;
            kmemcpy(out + w, d + i, e - i); w += e - i; out[w++] = '\n';
        }
        i = e + 1;
    }
    return fs_write(acc, out, w, 0) < 0 ? US_EFULL : US_OK;
}

/* as account -change --NAME <PASSWORD> <NEW_NAME>: the signed-in account gets a new name (its folder and its
 * saved login follow; the password stays). */
int user_change_name(const char *pass, const char *newname)
{
    const char *old = user_name();
    if (!old) return US_ENOUSER;
    if (!valid_user(newname)) return US_ENAME;
    char oldc[20]; kstrncpy(oldc, old, 19); oldc[19] = 0;
    int o = sys_enter();
    int rc = verify(oldc, pass);
    if (rc != US_OK) { sys_leave(o); return rc; }
    if (!kstrcmp(oldc, newname)) { sys_leave(o); return US_OK; }
    int acc = fs_resolve("/user/accounts", 0);
    if (find_account(fs_data(acc), fs_size(acc), newname)) { sys_leave(o); return US_EEXIST; }
    char p[64];
    ksnprintf(p, sizeof p, "/drives/S/usrs/%s", oldc);
    int root = fs_resolve(p, 0);
    if (root <= 0) { sys_leave(o); return US_ENOUSER; }
    rc = account_replace(oldc, newname, pass);
    if (rc == US_OK && fs_rename(root, newname) < 0) {            /* could not rename the folder: put the account back */
        account_replace(newname, oldc, pass);
        rc = US_EFULL;
    }
    if (rc == US_OK) {
        session_forget(oldc);
        kstrncpy(cur_name, newname, sizeof cur_name - 1);
        session_write(newname);
    }
    sys_leave(o);
    if (rc == US_OK) { fs_set_jail(root); if (user_rename_hook) user_rename_hook(oldc, newname); persist_flush(); }
    return rc;
}

/* as account -change --PASSWORD <PASSWORD> <NEW_PASSWORD> */
int user_change_password(const char *pass, const char *newpass)
{
    const char *cn = user_name();
    if (!cn) return US_ENOUSER;
    if (!*newpass || kstrlen(newpass) > 32) return US_EPASSFMT;
    char name[20]; kstrncpy(name, cn, 19); name[19] = 0;
    int o = sys_enter();
    int rc = verify(name, pass);
    if (rc == US_OK) rc = account_replace(name, name, newpass);
    if (rc == US_OK) session_write(name);
    sys_leave(o);
    if (rc == US_OK) persist_flush();
    return rc;
}

int user_login(const char *name, const char *pass)
{
    if (!valid_user(name)) return US_ENAME;
    int o = sys_enter();
    int rc = verify(name, pass);
    if (rc == US_OK) {                                   /* /user is checked at every login: tampering -> yellow screen + restart */
        char why[120];
        if (user_integrity_check(why, sizeof why)) { sys_leave(o); err_overload(why); return US_ENOUSER; }
    }
    if (rc == US_OK) rc = enter_session(name);
    if (rc == US_OK) {
        fs_set_jail(0);
        session_write(name);
        char p[64];
        ksnprintf(p, sizeof p, "/drives/S/usrs/%s", name);
        fs_set_jail(fs_resolve(p, 0));
        return US_OK;
    }
    sys_leave(o);
    return rc;
}

void user_logout(void)
{
    char gone[17];
    kstrncpy(gone, cur_name, 16); gone[16] = 0;
    cur_name[0] = 0;
    cur_home = -1;
    fs_set_jail(0);
    session_forget(gone);
}

const char *user_name(void) { return cur_name[0] ? cur_name : NULL; }
int         user_home(void) { return cur_home; }

const char *user_strerror(int e)
{
    switch (e) {
    case US_EEXIST:   return "user already exists";
    case US_ENOUSER:  return "no such user";
    case US_EPASS:    return "wrong password";
    case US_ENAME:    return "invalid name (1-16 letters, digits, '_' or '-')";
    case US_EFULL:    return "drive S: is full";
    case US_EPASSFMT: return "invalid password (1-32 characters)";
    default:          return "unknown error";
    }
}
