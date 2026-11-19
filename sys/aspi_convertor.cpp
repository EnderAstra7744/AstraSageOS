/*
 * aspi_convertor.cpp - the ASPI archive converter, built into as_kernel (C++17, freestanding: no libstdc++, no exceptions,
 * no RTTI, no heap, no global constructors - it only uses the kernel's own functions).
 *
 *   .tar.gz / .tar.xz  ->  .aspi
 *
 * A ".aspi" file is a zip archive (only the name differs). The converter
 *   1. decompresses the archive (gzip: inflate_raw, xz: xz_decode - the kernel's own decoders, C),
 *   2. walks the tar stream (ustar, GNU long names; pax headers are skipped; links and devices are skipped),
 *   3. refuses unsafe paths (absolute, "..", control characters, backslashes),
 *   4. removes one common top folder ("Repo-1.0/..."), as GitHub archives have one,
 *   5. writes a real zip (stored entries with CRC-32, local headers, central directory, end record) into an output buffer.
 * Nothing is run and nothing outside the buffers is touched. The result is exactly what `unzip` would accept.
 *
 * C interface (extern "C"): aspi_convert(), aspi_convert_error() - see kernel/astra.h.
 */
extern "C" {
#include "astra.h"
int inflate_raw(const u8 *src, u32 srclen, u8 *dst, u32 dstcap, u32 *outlen, u32 *used);
int xz_decode(const u8 *src, u32 n, u8 *dst, u32 cap, u32 *outlen);
u8 *arc_scratch(u32 *cap);
}

namespace {

constexpr u32 kOutCap     = 4u << 20;          // largest .aspi the converter can produce
constexpr int kMaxEntries = ARC_MAX_FILES;
constexpr size_t kNameMax = 96;                // longest name the installer accepts (after the top folder is removed)
constexpr size_t kEntryMax = 256;              // longest name read from the tar

u8 g_out[kOutCap];                              // the converted archive (stays valid until the next conversion)

// ---------------------------------------------------------------- CRC-32 (zip)
struct Crc32 {
    u32 table[256];
    bool ready;

    void init()
    {
        if (ready) return;
        for (u32 i = 0; i < 256; i++) {
            u32 c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    u32 of(const u8 *p, u32 n)
    {
        init();
        u32 c = 0xFFFFFFFFu;
        for (u32 i = 0; i < n; i++) c = table[(c ^ p[i]) & 255] ^ (c >> 8);
        return ~c;
    }
};
Crc32 g_crc;

// ---------------------------------------------------------------- one file of the package
struct Entry {
    char        name[kEntryMax];
    size_t      nameLen;
    const u8   *data;
    u32         size;
    bool        isDir;
};

// ---------------------------------------------------------------- tar reader
class TarReader {
public:
    TarReader(const u8 *t, u32 n) : tar_(t), n_(n), pos_(0), haveLong_(false) {}

    enum class Status { Ok, End, Bad };

    // Next regular file or folder (links, devices, pax headers are skipped). Entry.name is NOT yet checked.
    Status next(Entry &e, const char **why)
    {
        while (pos_ + 512 <= n_) {
            const u8 *h = tar_ + pos_;
            if (isZero(h)) return Status::End;
            u32 size = octal(h + 124, 12);
            u8 type = h[156];

            char name[256];
            size_t nl = 0;
            if (!kmemcmp(h + 257, "ustar", 5) && h[345]) {          // ustar prefix field
                for (int i = 0; i < 155 && h[345 + i]; i++) name[nl++] = static_cast<char>(h[345 + i]);
                name[nl++] = '/';
            }
            for (int i = 0; i < 100 && h[i]; i++) name[nl++] = static_cast<char>(h[i]);

            pos_ += 512;
            if (pos_ + size > n_) { *why = "truncated tar archive"; return Status::Bad; }
            const u8 *data = tar_ + pos_;
            pos_ += (size + 511u) & ~511u;

            if (type == 'x' || type == 'g') continue;               // pax headers: ignored
            if (type == 'L') {                                     // GNU long name for the next entry
                size_t k = size < sizeof longName_ - 1 ? size : sizeof longName_ - 1;
                kmemcpy(longName_, data, k);
                longName_[k] = 0;
                haveLong_ = true;
                continue;
            }
            const char *nm = name;
            if (haveLong_) { nm = longName_; nl = kstrlen(longName_); haveLong_ = false; }
            while (nl && nm[nl - 1] == '/') nl--;

            const bool dir = type == '5';
            if (!dir && type != '0' && type != 0 && type != '7') continue;   // symlinks, devices ...: skipped
            if (nl == 0) continue;
            if (nl >= kEntryMax) { *why = "file name too long in archive"; return Status::Bad; }
            kmemcpy(e.name, nm, nl);
            e.name[nl] = 0;
            e.nameLen = nl;
            e.data = dir ? nullptr : data;
            e.size = dir ? 0 : size;
            e.isDir = dir;
            return Status::Ok;
        }
        return Status::End;
    }

private:
    static bool isZero(const u8 *h)
    {
        for (int i = 0; i < 512; i++) if (h[i]) return false;
        return true;
    }
    static u32 octal(const u8 *p, int n)
    {
        u32 v = 0;
        while (n-- && (*p == ' ' || *p == 0)) p++;
        for (; n >= 0 && *p >= '0' && *p <= '7'; p++, n--) v = v * 8 + static_cast<u32>(*p - '0');
        return v;
    }

    const u8 *tar_;
    u32       n_, pos_;
    char      longName_[kEntryMax];
    bool      haveLong_;
};

// ---------------------------------------------------------------- zip writer (stored entries)
struct Meta { char name[kNameMax + 1]; size_t nameLen; u32 off, size, crc; bool isDir; };
Meta g_meta[kMaxEntries];                       // kept out of the (small) stack

class ZipWriter {
public:
    ZipWriter(u8 *buf, u32 cap, Meta *meta) : buf_(buf), cap_(cap), pos_(0), count_(0), meta_(meta) {}

    bool add(const char *name, size_t nameLen, bool isDir, const u8 *data, u32 size, const char **why)
    {
        if (count_ >= kMaxEntries) { *why = "too many files in the package"; return fail(); }
        const size_t zl = nameLen + (isDir ? 1 : 0);
        if (zl >= kNameMax) { *why = "file name too long in archive"; return fail(); }
        if (!room(30 + zl + size)) { *why = "converted package too large (4 MiB limit)"; return fail(); }

        Meta &m = meta_[count_++];
        m.off = pos_;
        m.size = isDir ? 0 : size;
        m.crc = isDir ? 0 : g_crc.of(data, size);
        m.isDir = isDir;
        m.nameLen = zl;
        kmemcpy(m.name, name, nameLen);
        if (isDir) m.name[nameLen] = '/';

        put32(0x04034b50u); put16(20); put16(0); put16(0);          // signature, version, flags, stored
        put16(0); put16(0x21);                                       // time 00:00, date 1980-01-01
        put32(m.crc); put32(m.size); put32(m.size);
        put16(static_cast<u16>(zl)); put16(0);
        raw(m.name, zl);
        if (!isDir && size) raw(data, size);
        return true;
    }

    // central directory + end record; returns the archive size, 0 on failure
    u32 finish(const char **why)
    {
        const u32 cdStart = pos_;
        for (int i = 0; i < count_; i++) {
            const Meta &m = meta_[i];
            if (!room(46 + m.nameLen)) { *why = "converted package too large (4 MiB limit)"; return 0; }
            put32(0x02014b50u); put16(20); put16(20); put16(0); put16(0);
            put16(0); put16(0x21);
            put32(m.crc); put32(m.size); put32(m.size);
            put16(static_cast<u16>(m.nameLen)); put16(0); put16(0); put16(0); put16(0);
            put32(m.isDir ? 0x10u : 0u);                              // external attributes
            put32(m.off);
            raw(m.name, m.nameLen);
        }
        if (!room(22)) { *why = "converted package too large (4 MiB limit)"; return 0; }
        const u32 cdSize = pos_ - cdStart;
        put32(0x06054b50u); put16(0); put16(0);
        put16(static_cast<u16>(count_)); put16(static_cast<u16>(count_));
        put32(cdSize); put32(cdStart); put16(0);
        return pos_;
    }

private:
    bool fail() { return false; }
    bool room(size_t n) const { return pos_ + n <= cap_; }
    void put16(u32 v) { buf_[pos_++] = static_cast<u8>(v); buf_[pos_++] = static_cast<u8>(v >> 8); }
    void put32(u32 v) { put16(v & 0xFFFF); put16(v >> 16); }
    void raw(const void *p, size_t n) { kmemcpy(buf_ + pos_, p, n); pos_ += static_cast<u32>(n); }

    u8 *buf_;
    u32 cap_, pos_;
    int count_;
    Meta *meta_;
};

// ---------------------------------------------------------------- path rules
bool safeName(const char *nm, const char **why)
{
    const size_t n = kstrlen(nm);
    const bool bad = nm[0] == '/' || !kstrcmp(nm, "..") || !kstrncmp(nm, "../", 3) || kstrstr(nm, "/../") ||
                     (n >= 3 && !kstrcmp(nm + n - 3, "/.."));
    if (bad) { *why = "archive contains an unsafe path (refused)"; return false; }
    for (const char *c = nm; *c; c++)
        if (static_cast<u8>(*c) < 32 || static_cast<u8>(*c) >= 127 || *c == '\\') { *why = "archive contains an invalid file name"; return false; }
    return true;
}

// "./a/b" -> "a/b"
void dropDotSlash(Entry &e)
{
    while (e.name[0] == '.' && e.name[1] == '/') {
        kmemmove(e.name, e.name + 2, kstrlen(e.name + 2) + 1);
        e.nameLen = kstrlen(e.name);
    }
}

// Length of the single common top folder of all entries ("Repo-1.0"), 0 when there is none.
size_t commonTop(const u8 *tar, u32 n, char *top, const char **why)
{
    TarReader r(tar, n);
    Entry e;
    bool first = true;
    size_t k = 0;
    int seen = 0;
    for (;;) {
        TarReader::Status s = r.next(e, why);
        if (s == TarReader::Status::Bad) return static_cast<size_t>(-1);
        if (s == TarReader::Status::End) break;
        dropDotSlash(e);
        if (e.nameLen == 0) continue;
        seen++;
        if (first) {
            first = false;
            while (e.name[k] && e.name[k] != '/') k++;
            if (!e.name[k] && !e.isDir) return 0;              // a plain file in the top level: nothing to strip
            kmemcpy(top, e.name, k);
            top[k] = 0;
            continue;
        }
        if (kstrncmp(e.name, top, k) || (e.name[k] != '/' && !(e.name[k] == 0 && e.isDir))) return 0;
    }
    return seen ? k : 0;
}

}  // namespace

// ------------------------------------------------------------------------------------------------ C interface

extern "C" int aspi_convert(int kind, const u8 *src, u32 len, const u8 **out, u32 *outLen, const char **why)
{
    static const char *dummy;
    if (!why) why = &dummy;
    *why = "";

    // 1. decompress into the shared scratch area
    u32 cap = 0;
    u8 *tar = arc_scratch(&cap);
    u32 tl = 0;
    if (kind == ARC_TGZ) {
        if (len < 18 || src[0] != 0x1f || src[1] != 0x8b || src[2] != 8) { *why = "not a gzip file"; return -1; }
        const u8 flags = src[3];
        u32 p = 10;
        if (flags & 4)  { if (p + 2 > len) { *why = "corrupt gzip header"; return -1; } p += 2 + (src[p] | static_cast<u32>(src[p + 1]) << 8); }
        if (flags & 8)  { while (p < len && src[p]) p++; p++; }
        if (flags & 16) { while (p < len && src[p]) p++; p++; }
        if (flags & 2)  p += 2;
        if (p >= len) { *why = "corrupt gzip header"; return -1; }
        const int r = inflate_raw(src + p, len - p, tar, cap, &tl, nullptr);
        if (r == -2) { *why = "package too large to unpack (4 MiB limit)"; return -1; }
        if (r < 0)   { *why = "corrupt gzip data"; return -1; }
    } else if (kind == ARC_TXZ) {
        const int x = xz_decode(src, len, tar, cap, &tl);
        if (x == -2)      { *why = "package too large to unpack (4 MiB limit)"; return -1; }
        else if (x == -3) { *why = "unsupported .xz filter (only LZMA2 is supported)"; return -1; }
        else if (x < 0)   { *why = "corrupt .xz data"; return -1; }
    } else {
        *why = "the converter takes .tar.gz and .tar.xz";
        return -1;
    }

    // 2. one common top folder is removed
    char top[kEntryMax];
    const size_t k = commonTop(tar, tl, top, why);
    if (k == static_cast<size_t>(-1)) return -1;

    // 3. write the zip
    ZipWriter zip(g_out, kOutCap, g_meta);
    TarReader rd(tar, tl);
    Entry e;
    int files = 0;
    for (;;) {
        const TarReader::Status s = rd.next(e, why);
        if (s == TarReader::Status::Bad) return -1;
        if (s == TarReader::Status::End) break;
        dropDotSlash(e);
        if (e.nameLen == 0) continue;
        if (!safeName(e.name, why)) return -1;
        const char *nm = e.name;
        size_t nl = e.nameLen;
        if (k) {
            if (nl == k) continue;                              // the top folder itself
            nm += k + 1;
            nl -= k + 1;
        }
        if (!zip.add(nm, nl, e.isDir, e.data, e.size, why)) return -1;
        if (!e.isDir) files++;
    }
    if (files == 0) { *why = "the archive contains no files"; return -1; }
    const u32 total = zip.finish(why);
    if (!total) return -1;
    *out = g_out;
    *outLen = total;
    return 0;
}
