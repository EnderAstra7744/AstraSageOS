/*
 * AstraSageOS file tree.
 *
 *   /                         the OS root
 *     bin                     command data (neofetch/custom_logo, neofetch/custom_logo_txt, version)
 *     .aspi/app  .aspi/lib    installed ASPI apps and libraries (hidden)
 *     .term                   AST settings, font and error log (hidden)
 *     user                    account data (hidden); proc = process / test trigger files
 *     .config                 settings and saved logins
 *     drives/S                drive S:  (only usrs)
 *       usrs/<name>/home      one folder per user
 *
 * A logged-in session is "jailed": its visible root is S:/usrs/<name>, so absolute
 * paths and ".." can never reach the OS root or other users.
 *
 * The whole tree lives in RAM and is mirrored to the ATA disk by persist.c
 * (the fs_rec / fs_put_rec / dirty interface below).
 */
#include "astra.h"
#include "../sys/seed_logo.h"
#include "font8x16.h"

typedef struct {
    char name[FS_NAME_MAX];
    u8   used;
    u8   is_dir;
    int  parent;
    u32  size;
    u32  mtime;
    u32  ctime;
    u32  off, cap;                 /* where the contents are in the arena, and how much room they have there */
    u8   big;                      /* contents live on the disk only (bigger than FS_STORE_MAX): see fs_pread / fs_pwrite */
} fsnode;

static fsnode nodes[FS_MAX_NODES];
/* File contents live in one arena. Every file has a contiguous extent (so fs_data() is still a plain pointer); a file that outgrows its
 * extent moves to the end, and when the end is reached the arena is compacted. */
#define FS_ARENA (FS_STORE_MAX * 16)
static char   arena[FS_ARENA];
static u32    arena_top;
static const char empty_data[4] = "";
static u8     ddirty[FS_MAX_NODES];    /* contents changed since the last save */
static int    any_dirty;               /* anything (table or contents) changed */
static int    jail;                    /* visible root of the current session */
static u32    gen;
static u8     sysf[FS_MAX_NODES];      /* created by the OS itself (seed): never deleted by a user */
static int    acs_on;                  /* the session proved its password (acs): root items may be added / removed */                     /* bumped on every change (the file explorer follows it live) */

void fs_set_jail(int node) { jail = node; }
int  fs_get_jail(void)     { return jail; }

static int top_of(int n);

static void arena_compact(void)
{
    u32 pos = 0;
    for (;;) {                                           /* lowest extent at or after pos, moved down to pos */
        int best = -1;
        for (int i = 1; i < FS_MAX_NODES; i++)
            if (nodes[i].used && nodes[i].cap && nodes[i].off >= pos && (best < 0 || nodes[i].off < nodes[best].off)) best = i;
        if (best < 0) break;
        if (nodes[best].off != pos) kmemmove(arena + pos, arena + nodes[best].off, nodes[best].cap);
        nodes[best].off = pos;
        pos += nodes[best].cap;
    }
    arena_top = pos;
}

/* Makes room for 'need' bytes in the file's extent. 0 = ok. */
static int arena_room(int n, u32 need)
{
    if (need <= nodes[n].cap) return FS_OK;
    if (need > FS_STORE_MAX) return FS_ETOOBIG;
    u32 cap = (need + need / 4 + 511) & ~511u;
    if (cap > FS_STORE_MAX + 512) cap = (need + 511) & ~511u;
    for (int pass = 0; pass < 2; pass++) {
        if (arena_top + cap <= FS_ARENA) {
            u32 o = arena_top;
            if (nodes[n].size) kmemcpy(arena + o, arena + nodes[n].off, nodes[n].size);
            kmemset(arena + o + nodes[n].size, 0, cap - nodes[n].size);
            nodes[n].off = o; nodes[n].cap = cap;       /* the old extent becomes a hole until the next compaction */
            arena_top += cap;
            return FS_OK;
        }
        if (pass == 0) arena_compact();
    }
    return FS_EFULL;
}

static void arena_free(int n)
{
    if (nodes[n].cap && nodes[n].off + nodes[n].cap == arena_top) arena_top = nodes[n].off;
    nodes[n].off = nodes[n].cap = 0;
}

static void touch(int n)
{
    hal_time t;
    hal_datetime(&t);
    nodes[n].mtime = ktime_pack(&t);
    any_dirty = 1;
    gen++;
}

int  fs_dirty(void)       { return any_dirty; }
void fs_clear_dirty(void) { kmemset(ddirty, 0, sizeof ddirty); any_dirty = 0; }
int  fs_data_dirty(int n) { return n > 0 && n < FS_MAX_NODES && ddirty[n]; }
u32  fs_mtime(int n)      { return nodes[n].mtime; }
u32  fs_ctime(int n)      { return nodes[n].ctime; }
u32  fs_generation(void)  { return gen; }
int  fs_exists(int n)     { return n >= 0 && n < FS_MAX_NODES && nodes[n].used; }

static int find_child(int dir, const char *name)
{
    for (int i = 1; i < FS_MAX_NODES; i++)
        if (nodes[i].used && nodes[i].parent == dir && !kstrcmp(nodes[i].name, name))
            return i;
    return FS_ENOENT;
}

int fs_resolve(const char *path, int cwd)
{
    int cur = (path[0] == '/') ? jail : cwd;
    int top = jail;                                  /* ".." stops here */
    const char *p = path;
    char comp[FS_NAME_MAX];
    if (!kstrncmp(path, "root:", 5)) { cur = 0; top = 0; p = path + 5; }   /* root:/bin - the real root, outside the user's jail */

    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        size_t n = 0;
        while (p[n] && p[n] != '/') n++;
        if (n >= FS_NAME_MAX) return FS_ENOENT;
        kmemcpy(comp, p, n);
        comp[n] = 0;
        if (!nodes[cur].is_dir) return FS_ENOTDIR;
        if (!kstrcmp(comp, ".")) { /* stay */ }
        else if (!kstrcmp(comp, "..")) { if (cur != top) cur = nodes[cur].parent; }
        else {
            int c = find_child(cur, comp);
            if (c < 0) return FS_ENOENT;
            cur = c;
        }
        p += n;
    }
    return cur;
}

static int valid_name(const char *n)
{
    if (!*n || !kstrcmp(n, ".") || !kstrcmp(n, "..")) return 0;
    for (; *n; n++)
        if ((u8)*n <= 32 || (u8)*n >= 127) return 0;
    return 1;
}

/* Split "a/b/c" into parent directory node and final component. */
static int split(const char *path, int cwd, int *parent, char *name)
{
    size_t len = kstrlen(path);
    while (len > 1 && path[len - 1] == '/') len--;
    size_t i = len;
    while (i > 0 && path[i - 1] != '/') i--;
    size_t nlen = len - i;
    if (nlen == 0) return FS_EINVAL;
    if (nlen >= FS_NAME_MAX) return FS_ENAME;
    kmemcpy(name, path + i, nlen);
    name[nlen] = 0;
    if (!valid_name(name)) return FS_ENAME;

    if (i == 0) {
        *parent = cwd;
    } else {
        char dir[160];
        if (i >= sizeof dir) return FS_ENAME;
        kmemcpy(dir, path, i);
        dir[i] = 0;
        int r = fs_resolve(dir, cwd);
        if (r < 0) return r;
        *parent = r;
    }
    return nodes[*parent].is_dir ? FS_OK : FS_ENOTDIR;
}

int fs_create(const char *path, int cwd, int is_dir)
{
    int parent;
    char name[FS_NAME_MAX];
    int r = split(path, cwd, &parent, name);
    if (r < 0) return r;
    if (find_child(parent, name) >= 0) return FS_EEXIST;
    for (int i = 1; i < FS_MAX_NODES; i++) {
        if (nodes[i].used) continue;
        kmemset(&nodes[i], 0, sizeof nodes[i]);
        kmemcpy(nodes[i].name, name, kstrlen(name) + 1);
        nodes[i].used = 1;
        nodes[i].is_dir = (u8)(is_dir != 0);
        nodes[i].parent = parent;
        ddirty[i] = 1;
        touch(i);
        nodes[i].ctime = nodes[i].mtime;
        return i;
    }
    return FS_EFULL;
}

/* ---- big files: contents on the disk in one contiguous extent (persist.c), read and written in pieces ---- */

static int big_ok(void) { return persist_active(); }

/* turns a file that lives in the arena into a big one */
static int make_big(int n, u32 want)
{
    if (!big_ok()) return FS_ETOOBIG;
    u32 sz = nodes[n].size;
    int rc = persist_big_reserve(n, want > sz ? want : sz);
    if (rc) return rc;
    if (sz) { rc = persist_big_rw(n, 0, arena + nodes[n].off, sz, 1); if (rc) return rc; }
    arena_free(n);
    nodes[n].big = 1;
    return FS_OK;
}

static int small_pwrite(int n, u32 off, const char *data, u32 len)
{
    u32 end = off + len;
    int so = -1; u32 delta = 0;
    if (len && data >= arena && data < arena + FS_ARENA) {
        u32 at = (u32)(data - arena);
        for (int i = 1; i < FS_MAX_NODES; i++) if (nodes[i].used && nodes[i].cap && at >= nodes[i].off && at < nodes[i].off + nodes[i].cap) { so = i; delta = at - nodes[i].off; break; }
    }
    int rc = arena_room(n, end);
    if (rc) return rc;
    if (so >= 0) data = arena + nodes[so].off + delta;
    if (off > nodes[n].size) kmemset(arena + nodes[n].off + nodes[n].size, 0, off - nodes[n].size);
    kmemmove(arena + nodes[n].off + off, data, len);
    if (end > nodes[n].size) nodes[n].size = end;
    return FS_OK;
}

/* writes len bytes at byte offset off (the file grows, a gap is zero); files beyond FS_STORE_MAX become big (need the disk) */
int fs_pwrite(int n, u32 off, const void *data, u32 len)
{
    if (n <= 0 || n >= FS_MAX_NODES || !nodes[n].used) return FS_ENOENT;
    if (nodes[n].is_dir) return FS_EISDIR;
    if (off + len < off || off + len > FS_BIG_MAX) return FS_ETOOBIG;
    u32 end = off + len;
    int rc;
    if (!nodes[n].big && end <= FS_STORE_MAX) rc = small_pwrite(n, off, (const char *)data, len);
    else {
        if (!nodes[n].big && (rc = make_big(n, end)) != FS_OK) return rc;
        if (nodes[n].big) {
            if ((rc = persist_big_reserve(n, end)) != FS_OK) return rc;
            if (off > nodes[n].size) {                                   /* zero the gap */
                static char z[512];
                for (u32 o = nodes[n].size; o < off; ) { u32 k = off - o < sizeof z ? off - o : (u32)sizeof z; if ((rc = persist_big_rw(n, o, z, k, 1)) != FS_OK) return rc; o += k; }
            }
            rc = len ? persist_big_rw(n, off, (void *)(size_t)data, len, 1) : FS_OK;
            if (rc == FS_OK && end > nodes[n].size) nodes[n].size = end;
        }
    }
    if (rc != FS_OK) return rc;
    ddirty[n] = 1;
    touch(n);
    return FS_OK;
}

/* reads up to len bytes at offset off; returns the number of bytes read (0 at the end) or an error */
int fs_pread(int n, u32 off, void *buf, u32 len)
{
    if (n <= 0 || n >= FS_MAX_NODES || !nodes[n].used) return FS_ENOENT;
    if (nodes[n].is_dir) return FS_EISDIR;
    if (off >= nodes[n].size) return 0;
    if (len > nodes[n].size - off) len = nodes[n].size - off;
    if (nodes[n].big) { int rc = persist_big_rw(n, off, buf, len, 0); return rc ? rc : (int)len; }
    if (len) kmemcpy(buf, arena + nodes[n].off + off, len);
    return (int)len;
}

u32 fs_size_real(int n) { return nodes[n].size; }
int fs_is_big(int n)    { return nodes[n].big; }

/* contents of src into dst (works for big files too); dst is replaced */
int fs_copy_data(int dst, int src)
{
    if (nodes[src].is_dir || nodes[dst].is_dir) return FS_EISDIR;
    if (!nodes[src].big) return fs_write(dst, fs_data(src), fs_size(src), 0);
    static char chunk[4096];
    u32 total = nodes[src].size;
    int rc = fs_write(dst, "", 0, 0);
    if (rc) return rc;
    for (u32 o = 0; o < total; ) {
        int k = fs_pread(src, o, chunk, sizeof chunk);
        if (k <= 0) return k < 0 ? k : FS_EINVAL;
        if ((rc = fs_pwrite(dst, o, chunk, (u32)k)) != FS_OK) return rc;
        o += (u32)k;
    }
    return FS_OK;
}

int fs_write(int node, const char *data, u32 len, int append)
{
    if (nodes[node].is_dir) return FS_EISDIR;
    if (nodes[node].big || (append ? nodes[node].size : 0) + len > FS_STORE_MAX) {
        if (nodes[node].big && !append && len <= FS_STORE_MAX) {          /* a big file rewritten small: back into the arena */
            persist_big_free(node);
            nodes[node].big = 0; nodes[node].size = 0; nodes[node].off = nodes[node].cap = 0;
        } else {
            u32 at = append ? nodes[node].size : 0;
            int rc = fs_pwrite(node, at, data, len);
            if (rc == FS_OK && !append && nodes[node].size > len) nodes[node].size = len;       /* shorter than before: cut */
            return rc;
        }
    }
    u32 base = append ? nodes[node].size : 0;
    int so = -1; u32 delta = 0;                          /* the source may be another file's contents: it can move while we make room */
    if (len && data >= arena && data < arena + FS_ARENA) {
        u32 at = (u32)(data - arena);
        for (int i = 1; i < FS_MAX_NODES; i++) if (nodes[i].used && nodes[i].cap && at >= nodes[i].off && at < nodes[i].off + nodes[i].cap) { so = i; delta = at - nodes[i].off; break; }
    }
    int rc = arena_room(node, base + len);
    if (rc) return rc;
    if (so >= 0) data = arena + nodes[so].off + delta;
    kmemmove(arena + nodes[node].off + base, data, len);
    if (base + len < nodes[node].size)           /* shrinking: do not leave old bytes behind */
        kmemset(arena + nodes[node].off + base + len, 0, nodes[node].size - (base + len));
    nodes[node].size = base + len;
    ddirty[node] = 1;
    touch(node);
    return FS_OK;
}

int fs_delete(int node)
{
    if (node <= 0 || node >= FS_MAX_NODES || !nodes[node].used) return FS_ENOENT;
    for (int c = 1; c < FS_MAX_NODES; c++)
        if (nodes[c].used && nodes[c].parent == node) fs_delete(c);
    if (nodes[node].big) persist_big_free(node);
    arena_free(node);
    kmemset(&nodes[node], 0, sizeof nodes[node]);
    ddirty[node] = 0;
    any_dirty = 1;
    gen++;
    return FS_OK;
}

int fs_rename(int node, const char *newname)
{
    if (node <= 0 || node >= FS_MAX_NODES || !nodes[node].used) return FS_ENOENT;
    if (kstrlen(newname) >= FS_NAME_MAX) return FS_ENAME;
    if (!valid_name(newname)) return FS_ENAME;
    int other = find_child(nodes[node].parent, newname);
    if (other >= 0 && other != node) return FS_EEXIST;
    kmemset(nodes[node].name, 0, FS_NAME_MAX);
    kmemcpy(nodes[node].name, newname, kstrlen(newname) + 1);
    touch(node);
    return FS_OK;
}

/* Moves a node (with everything below it) into another folder; a name that is taken there gets "~2", "~3" ... */
int fs_move(int node, int newdir)
{
    if (node <= 0 || node >= FS_MAX_NODES || !nodes[node].used) return FS_ENOENT;
    if (!fs_exists(newdir) || !nodes[newdir].is_dir) return FS_ENOTDIR;
    for (int a = newdir; a > 0; a = nodes[a].parent) if (a == node) return FS_EEXIST;      /* not into itself */
    if (nodes[node].parent == newdir) return FS_OK;
    char nm[FS_NAME_MAX];
    kmemcpy(nm, nodes[node].name, FS_NAME_MAX);
    for (int k = 2; find_child(newdir, nm) >= 0 && k < 1000; k++) {
        size_t base = kstrlen(nodes[node].name);
        char suf[8]; int sl = 0, v = k;
        char tmp[8]; int tl = 0;
        while (v) { tmp[tl++] = (char)('0' + v % 10); v /= 10; }
        suf[sl++] = '~'; while (tl) suf[sl++] = tmp[--tl];
        if (base + (size_t)sl >= FS_NAME_MAX) base = FS_NAME_MAX - 1 - (size_t)sl;
        kmemset(nm, 0, FS_NAME_MAX);
        kmemcpy(nm, nodes[node].name, base);
        kmemcpy(nm + base, suf, (size_t)sl);
    }
    kmemcpy(nodes[node].name, nm, FS_NAME_MAX);
    nodes[node].parent = newdir;
    touch(node);
    return FS_OK;
}

int fs_is_inside(int node, int ancestor)
{
    for (int n = node, guard = 0; n >= 0 && guard < FS_MAX_NODES; n = nodes[n].parent, guard++) {
        if (n == ancestor) return 1;
        if (n == 0) break;
    }
    return 0;
}

/* The one place that decides what a logged-in user may change. Everything outside the user's own folder on S:
 * is read-only: the OS root files (boot, kernel, ...), the drive, its system folders and other users' folders. */
static int own_dir(void)
{
    int o = jail;
    jail = 0;
    char p[64];
    ksnprintf(p, sizeof p, "/drives/S/usrs/%s", user_name() ? user_name() : "-");
    int n = fs_resolve(p, 0);
    jail = o;
    return n;
}

int fs_guard_modify(int node)
{
    if (!fs_exists(node)) return FS_ENOENT;
    if (!user_name()) return FS_ELOGIN;
    int me = own_dir();
    int s = fs_mount_s();
    if (me < 0 || s < 0) return FS_EPROTECT;
    if (node == 0 || !fs_is_inside(node, s) || node == s) return FS_EPROTECT;     /* OS root files and the drive itself */
    if (!fs_is_inside(node, me)) return nodes[node].parent == s ? FS_EPROTECT : FS_EOTHER;
    if (node == me || node == user_home()) return FS_EPROTECT;                      /* the account's own folders */
    return FS_OK;
}

int fs_guard_write_dir(int dir)
{
    if (!fs_exists(dir) || !nodes[dir].is_dir) return FS_ENOTDIR;
    if (!user_name()) return FS_ELOGIN;
    int me = own_dir();
    if (me < 0) return FS_EPROTECT;
    if (fs_is_inside(dir, me)) return FS_OK;
    int s = fs_mount_s();
    if (s >= 0 && fs_is_inside(dir, s)) return dir == s || nodes[dir].parent == s ? FS_EPROTECT : FS_EOTHER;   /* S:, usrs, other users */
    if (dir != 0 && top_of(dir) > 0 && (!kstrcmp(nodes[top_of(dir)].name, ".aspi") || !kstrcmp(nodes[top_of(dir)].name, ".term") ||
                                         !kstrcmp(nodes[top_of(dir)].name, "user") || !kstrcmp(nodes[top_of(dir)].name, ".config"))) return FS_EPROTECT;
    return acs_on ? FS_OK : FS_EACS;                         /* the root and the other root folders: with the password */
}

int         fs_is_dir(int n) { return nodes[n].is_dir; }
int         fs_parent(int n) { return n > 0 ? nodes[n].parent : -1; }
u32         fs_size(int n)   { return nodes[n].big ? 0 : nodes[n].size; }      /* big files read as empty for code that works on whole-file buffers; see fs_size_real */
const char *fs_name(int n)   { return nodes[n].name; }
const char *fs_data(int n)   { return nodes[n].cap && !nodes[n].big ? arena + nodes[n].off : empty_data; }

int fs_child(int dir, int after)
{
    for (int i = after + 1; i < FS_MAX_NODES; i++)
        if (i > 0 && nodes[i].used && nodes[i].parent == dir) return i;
    return -1;
}

/* Path relative to the session root: "/" for the root itself. */
void fs_getpath(int node, char *buf, size_t cap)
{
    int chain[FS_MAX_NODES], depth = 0;
    for (int n = node; n != jail && n != 0 && depth < FS_MAX_NODES; n = nodes[n].parent)
        chain[depth++] = n;
    if (depth == 0) { ksnprintf(buf, cap, "/"); return; }
    size_t o = 0;
    for (int i = depth - 1; i >= 0; i--)
        o += (size_t)ksnprintf(buf + o, o < cap ? cap - o : 0, "/%s", nodes[chain[i]].name);
}

void fs_stats(u32 *nu, u32 *bu, u32 *bc)
{
    u32 n = 0, b = 0;
    for (int i = 0; i < FS_MAX_NODES; i++)
        if (nodes[i].used) { n++; b += nodes[i].size; }
    *nu = n; *bu = b; *bc = FS_ARENA;
    if (persist_active()) { *bc = persist_capacity_bytes(); *bu = persist_used_bytes(); }
}

const char *fs_strerror(int e)
{
    switch (e) {
    case FS_ENOENT:  return "no such file or directory";
    case FS_EEXIST:  return "already exists";
    case FS_ENOTDIR: return "not a directory";
    case FS_EISDIR:  return "is a directory";
    case FS_EFULL:   return "drive S: is full";
    case FS_EINVAL:  return "invalid path";
    case FS_ENAME:   return "invalid or too long name";
    case FS_ETOOBIG: return "file too large (max 4096 bytes)";
    case FS_EPROTECT: return "protected system item - root files can never be removed or changed";
    case FS_EOTHER:  return "belongs to another user";
    case FS_ELOGIN:  return "login required";
    case FS_EACS:    return config_tr("Permission denied - use acs (it asks for your password)", "Izin Reddedildi - acs kullanin (parolanizi sorar)");
    default:         return "unknown error";
    }
}

/* ---- persistence interface ---- */

void fs_clear_all(void)
{
    kmemset(nodes, 0, sizeof nodes);
    arena_top = 0;
    kmemset(ddirty, 0, sizeof ddirty);
    nodes[0].used = 1;
    nodes[0].is_dir = 1;
    jail = 0;
    any_dirty = 1;
    gen++;
}

void fs_get_rec(int n, fs_rec *r)
{
    kmemset(r, 0, sizeof *r);
    r->used = nodes[n].used;
    r->is_dir = nodes[n].is_dir;
    r->parent = nodes[n].parent;
    r->size = nodes[n].size;
    r->mtime = nodes[n].mtime;
    r->ctime = nodes[n].ctime;
    kmemcpy(r->name, nodes[n].name, FS_NAME_MAX);
}

/* Restores one node; data may be NULL for directories. Returns 0 or FS_EINVAL. */
int fs_put_rec(int n, const fs_rec *r, const char *data)
{
    if (n < 0 || n >= FS_MAX_NODES) return FS_EINVAL;
    if (!r->used) return FS_OK;
    if (n > 0 && (r->parent < 0 || r->parent >= FS_MAX_NODES)) return FS_EINVAL;
    if (r->size > FS_BIG_MAX) return FS_EINVAL;
    kmemcpy(nodes[n].name, r->name, FS_NAME_MAX);
    nodes[n].name[FS_NAME_MAX - 1] = 0;
    nodes[n].used = 1;
    nodes[n].is_dir = r->is_dir ? 1 : 0;
    nodes[n].parent = r->parent;
    nodes[n].size = r->is_dir ? 0 : r->size;
    nodes[n].mtime = r->mtime;
    nodes[n].ctime = r->ctime;
    nodes[n].off = nodes[n].cap = 0;
    nodes[n].big = 0;
    if (!r->is_dir && r->size > FS_STORE_MAX) { nodes[n].big = 1; return FS_OK; }       /* contents stay on the disk */
    if (!r->is_dir && r->size) {
        if (arena_room(n, r->size) != FS_OK) { nodes[n].used = 0; return FS_EFULL; }
        if (data) kmemcpy(arena + nodes[n].off, data, r->size);
    }
    return FS_OK;
}

/* the writable extent of a node restored by fs_put_rec(n, r, NULL): persist.c reads the disk straight into it */
char *fs_rec_buf(int n) { return nodes[n].cap ? arena + nodes[n].off : 0; }

/* ---- creating the OS layout ---- */

static void mark_sys(const char *path)
{
    int o = jail; jail = 0;
    int n = fs_resolve(path, 0);
    jail = o;
    if (n > 0) sysf[n] = 1;
}

static void mkdir_p(const char *path)
{
    int r = fs_create(path, 0, 1);
    (void)r;                                   /* FS_EEXIST is fine */
    mark_sys(path);
}

/* Creates the file if needed and rewrites it only when the content differs. */
static void seed(const char *path, const char *data, u32 len)
{
    int n = fs_resolve(path, 0);
    if (n == FS_ENOENT) n = fs_create(path, 0, 0);
    if (n <= 0) return;
    sysf[n] = 1;
    if (fs_size(n) == len && !kmemcmp(fs_data(n), data, len)) return;
    fs_write(n, data, len, 0);
}

static void seed_text(const char *path, const char *text)
{
    seed(path, text, (u32)kstrlen(text));
}

int fs_mount_s(void)
{
    int o = jail;
    jail = 0;
    int n = fs_resolve("/drives/S", 0);
    jail = o;
    return n;
}

static int top_of(int n)                          /* the root-level ancestor of n (n itself when it is at the root) */
{
    for (int g = 0; n > 0 && g < FS_MAX_NODES; g++) { if (nodes[n].parent == 0) return n; n = nodes[n].parent; }
    return 0;
}

/* A disk saved by an older version: move the data to the new places and drop the old folders. */
static void migrate_legacy(void)
{
    static const char *const old_dirs[] = { "/boot", "/kernel", "/terminal", "/fs", "/sys", "/font", "/gui", "/assets", "/aspi/log" };
    char *buf;
    int n;
    if ((n = fs_resolve("/sys/accounts", 0)) > 0 && fs_resolve("/user/accounts", 0) == FS_ENOENT) {
        int d = fs_create("/user/accounts", 0, 0);
        if (d > 0) fs_write(d, fs_data(n), fs_size(n), 0);
    }
    if ((n = fs_resolve("/sys/aliases", 0)) > 0 && fs_resolve("/.term/aliases", 0) == FS_ENOENT) {
        int d = fs_create("/.term/aliases", 0, 0);
        if (d > 0) fs_write(d, fs_data(n), fs_size(n), 0);
    }
    (void)buf;
    if ((n = fs_resolve("/config", 0)) > 0 && fs_resolve("/.config", 0) == FS_ENOENT) fs_rename(n, ".config");
    if ((n = fs_resolve("/aspi", 0)) > 0 && fs_resolve("/.aspi", 0) == FS_ENOENT) {
        fs_rename(n, ".aspi");
        if ((n = fs_resolve("/.aspi/bin", 0)) > 0) fs_rename(n, "app");
    }
    for (unsigned i = 0; i < sizeof old_dirs / sizeof old_dirs[0]; i++) if ((n = fs_resolve(old_dirs[i], 0)) > 0) fs_delete(n);
    if ((n = fs_resolve("/drives/S.drv", 0)) > 0) fs_delete(n);
    static const char *const s_old[] = { "boot", "kernel", "terminal", "fs", "drives", "sys", "font" };
    for (unsigned i = 0; i < sizeof s_old / sizeof s_old[0]; i++) {
        char p[40]; ksnprintf(p, sizeof p, "/drives/S/%s", s_old[i]);
        if ((n = fs_resolve(p, 0)) > 0) fs_delete(n);
    }
}

void fs_seed_system(void)
{
    int saved = jail;
    jail = 0;

    kmemset(sysf, 0, sizeof sysf);
    static const char *const root_dirs[] = {
        "/drives", "/drives/S", "/drives/S/usrs", "/bin", "/bin/neofetch", "/bin/neofetch/custom_logo", "/bin/neofetch/custom_logo_txt",
        "/.aspi", "/.aspi/app", "/.aspi/lib", "/.term", "/.term/log", "/user", "/proc", "/.config"
    };
    migrate_legacy();
    for (unsigned i = 0; i < sizeof root_dirs / sizeof root_dirs[0]; i++) mkdir_p(root_dirs[i]);

    seed_text("/bin/version",        "AstraSageOS " AS_VERSION "\n");
    seed("/bin/neofetch/custom_logo/astrasage.png", (const char *)seed_logo_png, (u32)sizeof seed_logo_png);   /* the default logo as a picture */
    seed_text("/bin/neofetch/custom_logo_txt/astrasage.txt", seed_logo_txt);                                   /* ... and as text */
    seed_text("/.term/term.cfg",     "name=AST\nprompt=[user@model]:path $\n");
    seed("/.term/ast8x16.fnt", (const char *)as_font8x16, (u32)sizeof as_font8x16);
    seed_text("/proc/README",        "Test trigger files: run one with  as proc <FILE> [--sleep <SECONDS>]\n");
    seed_text("/proc/kernel_panic",  "panic\nreason=Test: kernel panic trigger (/proc/kernel_panic)\n");
    seed_text("/proc/heavy_load",    "overload\nreason=Test: heavy load trigger (/proc/heavy_load)\n");
    seed_text("/proc/cpu_fault",     "fault\nreason=Test: real CPU exception (ud2) - goes through the IDT\n");
    if (fs_resolve("/user/accounts", 0) == FS_ENOENT) seed_text("/user/accounts", "");
    mark_sys("/user/accounts");

    jail = saved;
}

/* ---- who may change what (see the file explorer and AST: root items need the password, `acs`) ---- */

void fs_acs_set(int on) { acs_on = on; }
int  fs_acs_get(void)   { return acs_on; }

int fs_in_own_area(int node)
{
    int me = own_dir();
    return me >= 0 && fs_is_inside(node, me);
}

int fs_is_system(int node);
/* 1 = a system-managed item: created by the OS, or inside the folders the OS keeps for itself */
static int is_system_item(int node)
{
    if (node <= 0) return 1;
    if (sysf[node]) return 1;
    static const char *const managed[] = { ".aspi", ".term", "user", ".config" };
    int t = top_of(node);
    if (t > 0) for (unsigned i = 0; i < sizeof managed / sizeof managed[0]; i++) if (!kstrcmp(nodes[t].name, managed[i])) return 1;
    return 0;
}

int fs_is_system(int node) { return is_system_item(node); }

/* Deleting: own files yes; root folders and items put into the root by the user only with the password; system files never. */
int fs_guard_remove(int node)
{
    if (!fs_exists(node)) return FS_ENOENT;
    if (!user_name()) return FS_ELOGIN;
    int s = fs_mount_s();
    if (s >= 0 && fs_is_inside(node, s)) return fs_guard_modify(node);                   /* inside drive S: the old rules */
    if (node == 0) return FS_EPROTECT;
    int top = nodes[node].parent == 0;
    if (top && nodes[node].is_dir && sysf[node]) return acs_on ? FS_OK : FS_EACS;        /* a root folder: deletable with the password */
    if (is_system_item(node)) return FS_EPROTECT;                                        /* system files are never deleted */
    return acs_on ? FS_OK : FS_EACS;
}

int fs_guard_rename(int node)
{
    if (!fs_exists(node)) return FS_ENOENT;
    int s = fs_mount_s();
    if (s >= 0 && !fs_is_inside(node, s)) return is_system_item(node) ? FS_EPROTECT : (acs_on ? FS_OK : FS_EACS);
    return fs_guard_modify(node);
}

/* ---- guards for AST commands: the account's own area is free, everything else asks for acs ---- */
int fs_guard_edit(int node)                         /* change the content of an existing item */
{
    if (!fs_exists(node)) return FS_OK;
    if (fs_in_own_area(node)) return FS_OK;
    return fs_guard_rename(node);
}
int fs_guard_delete(int node)
{
    if (!fs_exists(node)) return FS_OK;
    if (fs_in_own_area(node)) return FS_OK;
    return fs_guard_remove(node);
}
int fs_guard_create(const char *path, int cwd)       /* make a new item at path */
{
    int parent; char name[FS_NAME_MAX];
    int r = split(path, cwd, &parent, name);
    if (r < 0) return FS_OK;                          /* the real call reports the error */
    if (fs_in_own_area(parent)) return FS_OK;
    return fs_guard_write_dir(parent);
}

void fs_init(void)
{
    fs_clear_all();
    fs_seed_system();
}
