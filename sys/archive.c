/*
 * Package archives. ASPI accepts .aspi (zip based), .jar (zip), .tar.gz and .tar.xz and
 * turns every one of them into the same in-memory file list - the ".aspi form" - before
 * installing. Decompressed data lives in one 4 MiB scratch area.
 */
#include "astra.h"

int inflate_raw(const u8 *src, u32 srclen, u8 *dst, u32 dstcap, u32 *outlen, u32 *used);
int xz_decode(const u8 *src, u32 n, u8 *dst, u32 cap, u32 *outlen);

#define SCRATCH_SIZE (4u << 20)
static u8  scratch[SCRATCH_SIZE];
static u32 scratch_used;

/* the shared decompression area (the C++ converter, sys/aspi_convertor.cpp, unpacks tar archives into it) */
u8 *arc_scratch(u32 *cap) { *cap = SCRATCH_SIZE; return scratch; }
int aspi_convert(int kind, const u8 *src, u32 len, const u8 **out, u32 *outLen, const char **why);

static u32 rd16(const u8 *p) { return p[0] | (u32)p[1] << 8; }
static u32 rd32(const u8 *p) { return p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }

static int add_entry(arc_list *l, const char *name, size_t nlen, const u8 *data, u32 size, int is_dir, const char **why)
{
    if (l->n >= ARC_MAX_FILES) { *why = "too many files in the package"; return -1; }
    if (nlen == 0 || nlen >= sizeof l->e[0].name) { *why = "file name too long in archive"; return -1; }
    arc_entry *e = &l->e[l->n++];
    kmemcpy(e->name, name, nlen);
    e->name[nlen] = 0;
    e->data = data; e->size = size; e->is_dir = (u8)is_dir;
    return 0;
}

/* ---- zip ---- */
static int load_zip(const u8 *z, u32 n, arc_list *l, const char **why)
{
    if (n < 22) { *why = "not a zip archive"; return -1; }
    u32 eocd = n - 22;
    for (;; eocd--) {
        if (rd32(z + eocd) == 0x06054b50u) break;
        if (eocd == 0 || n - eocd > 65557) { *why = "not a zip archive (no end record)"; return -1; }
    }
    u32 count = rd16(z + eocd + 10), cdoff = rd32(z + eocd + 16);
    u32 p = cdoff;
    for (u32 i = 0; i < count; i++) {
        if (p + 46 > n || rd32(z + p) != 0x02014b50u) { *why = "corrupt zip directory"; return -1; }
        u32 flags = rd16(z + p + 8), method = rd16(z + p + 10), csize = rd32(z + p + 20), usize = rd32(z + p + 24);
        u32 nl = rd16(z + p + 28), el = rd16(z + p + 30), cl = rd16(z + p + 32), lo = rd32(z + p + 42);
        if (p + 46 + nl > n) { *why = "corrupt zip directory"; return -1; }
        const char *name = (const char *)z + p + 46;
        p += 46 + nl + el + cl;
        if (flags & 1) { *why = "encrypted zip entries are not supported"; return -1; }
        int is_dir = nl && name[nl - 1] == '/';
        if (is_dir) { if (add_entry(l, name, nl - 1, 0, 0, 1, why) < 0) return -1; continue; }
        if (lo + 30 > n || rd32(z + lo) != 0x04034b50u) { *why = "corrupt zip entry"; return -1; }
        u32 ds = lo + 30 + rd16(z + lo + 26) + rd16(z + lo + 28);
        if (ds + csize > n) { *why = "corrupt zip entry"; return -1; }
        const u8 *data;
        if (method == 0) {
            if (csize != usize) { *why = "corrupt zip entry"; return -1; }
            data = z + ds;
        } else if (method == 8) {
            if (scratch_used + usize > SCRATCH_SIZE) { *why = "package too large to unpack (4 MiB limit)"; return -1; }
            u32 got = 0;
            if (inflate_raw(z + ds, csize, scratch + scratch_used, usize, &got, 0) < 0 || got != usize) {
                *why = "corrupt compressed data in zip"; return -1;
            }
            data = scratch + scratch_used; scratch_used += usize;
        } else { *why = "unsupported zip compression method"; return -1; }
        if (add_entry(l, name, nl, data, usize, 0, why) < 0) return -1;
    }
    return 0;
}

/* A single top-level folder (GitHub archives: "Repo-1.0/...") is removed. */
static void strip_top(arc_list *l)
{
    if (l->n == 0) return;
    const char *first = l->e[0].name;
    size_t k = 0;
    while (first[k] && first[k] != '/') k++;
    if (!first[k] && !l->e[0].is_dir) return;
    for (int i = 0; i < l->n; i++) {
        const char *nm = l->e[i].name;
        if (kstrncmp(nm, first, k) || (nm[k] != '/' && !(nm[k] == 0 && l->e[i].is_dir))) return;
    }
    int w = 0;
    for (int i = 0; i < l->n; i++) {
        arc_entry *e = &l->e[i];
        if (e->name[k] == 0) continue;                                 /* the folder itself */
        kmemmove(e->name, e->name + k + 1, kstrlen(e->name + k + 1) + 1);
        l->e[w++] = *e;
    }
    l->n = w;
}

static int safe_names(arc_list *l, const char **why)
{
    for (int i = 0; i < l->n; i++) {
        char *nm = l->e[i].name;
        while (nm[0] == '.' && nm[1] == '/') kmemmove(nm, nm + 2, kstrlen(nm + 2) + 1);
        if (nm[0] == '/' || !kstrcmp(nm, "..") || !kstrncmp(nm, "../", 3) || kstrstr(nm, "/../") ||
            (kstrlen(nm) >= 3 && !kstrcmp(nm + kstrlen(nm) - 3, "/.."))) {
            *why = "archive contains an unsafe path (refused)";
            return -1;
        }
        for (char *c = nm; *c; c++)
            if ((u8)*c < 32 || (u8)*c >= 127 || *c == '\\') { *why = "archive contains an invalid file name"; return -1; }
    }
    return 0;
}

const char *arc_kind_name(int k)
{
    switch (k) { case ARC_ASPI: return ".aspi"; case ARC_JAR: return ".jar";
                 case ARC_TGZ: return ".tar.gz"; case ARC_TXZ: return ".tar.xz"; }
    return "?";
}

int arc_kind_from_text(const char *s)
{
    if (!kstrcmp(s, "aspi")) return ARC_ASPI;
    if (!kstrcmp(s, "jar")) return ARC_JAR;
    if (!kstrcmp(s, "tar.gz") || !kstrcmp(s, "tgz")) return ARC_TGZ;
    if (!kstrcmp(s, "tar.xz") || !kstrcmp(s, "txz")) return ARC_TXZ;
    return -1;
}

/* Unpacks an archive of the given kind into the list (tar archives are converted to .aspi on the way). Returns 0 or -1 with *why set. */
int arc_load(int kind, const u8 *src, u32 len, arc_list *l, const char **why)
{
    scratch_used = 0;
    l->n = 0;
    *why = "";
    int r;
    if (kind == ARC_ASPI || kind == ARC_JAR) r = load_zip(src, len, l, why);
    else {                                       /* .tar.gz / .tar.xz: converted to a real .aspi (zip) by aspi_convertor.cpp, then read like one */
        const u8 *zip = 0; u32 zl = 0;
        r = aspi_convert(kind, src, len, &zip, &zl, why);
        if (r == 0) r = load_zip(zip, zl, l, why);
    }
    if (r < 0) return -1;
    if (safe_names(l, why) < 0) return -1;
    if (kind == ARC_ASPI || kind == ARC_JAR) strip_top(l);        /* tar archives: the converter already removed the top folder */
    return 0;
}
