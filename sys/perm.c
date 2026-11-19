/*
 * Execute permission for AstraScript files.
 *
 *   as cper +x prog.as   ->  creates prog.asbin
 *
 * The .asbin file is encrypted and authenticated with a key that lives only
 * inside as_kernel: header "ASBIN1", 8-byte nonce, payload XOR a SHA-256
 * keystream, and a 32-byte SHA-256 MAC. The payload binds the permission to
 * the owner and to the exact SHA-256 of the script, so editing the script (or
 * copying someone else's .asbin) invalidates it.
 */
#include "astra.h"

static const char SECRET[] = "AstraSageOS/as_kernel/asbin/v1/7c1f0e5ad92b46f3";
#define HDR   6
#define NONCE 8
#define MACLEN 32

static u32 counter;

static int has_as_ext(const char *p)
{
    size_t l = kstrlen(p);
    return l > 3 && p[l - 3] == '.' && (p[l - 2] | 32) == 'a' && (p[l - 1] | 32) == 's';
}

static void keystream(const u8 *nonce, u32 block, u8 out[32])
{
    u8 buf[sizeof SECRET + NONCE + 4];
    size_t n = 0;
    kmemcpy(buf, SECRET, sizeof SECRET - 1); n += sizeof SECRET - 1;
    kmemcpy(buf + n, nonce, NONCE);          n += NONCE;
    buf[n++] = (u8)(block >> 24); buf[n++] = (u8)(block >> 16);
    buf[n++] = (u8)(block >> 8);  buf[n++] = (u8)block;
    sha256(buf, n, out);
}

static void crypt(const u8 *nonce, u8 *data, u32 len)
{
    u8 ks[32];
    for (u32 i = 0; i < len; i++) {
        if (i % 32 == 0) keystream(nonce, i / 32, ks);
        data[i] ^= ks[i % 32];
    }
}

static void mac(const u8 *nonce, const u8 *cipher, u32 len, u8 out[32])
{
    u8 buf[HDR + NONCE + 256 + sizeof SECRET + 4];
    size_t n = 0;
    kmemcpy(buf, "MAC", 3);                    n += 3;
    kmemcpy(buf + n, SECRET, sizeof SECRET - 1); n += sizeof SECRET - 1;
    kmemcpy(buf + n, "ASBIN1", HDR);           n += HDR;
    kmemcpy(buf + n, nonce, NONCE);            n += NONCE;
    kmemcpy(buf + n, cipher, len);             n += len;
    sha256(buf, n, out);
}

static int bin_path(const char *path, char *out, size_t cap)
{
    if (kstrlen(path) + 4 > cap) return PM_EFS;
    ksnprintf(out, cap, "%sbin", path);
    return PM_OK;
}

static int grant_as(const char *path, int cwd, const char *owner)
{
    if (!has_as_ext(path)) return PM_EEXT;
    int s = fs_resolve(path, cwd);
    if (s == FS_ENOENT) return PM_ENOENT;
    if (s < 0 || fs_is_dir(s)) return PM_EFS;

    char hex[65], payload[200], bp[160];
    sha256_hex(fs_data(s), fs_size(s), hex);
    int pl = ksnprintf(payload, sizeof payload, "perm=x\nowner=%s\nsha=%s\n", owner, hex);

    u8 nonce[NONCE], seed[200], h[32];
    int sn = ksnprintf((char *)seed, sizeof seed, "%s|%u|%u|%u|%s", SECRET, hal_ticks(),
                       hal_uptime_sec(), ++counter, path);
    if (sn > (int)sizeof seed - 1) sn = (int)sizeof seed - 1;
    sha256(seed, (size_t)sn, h);
    kmemcpy(nonce, h, NONCE);

    u8 file[HDR + NONCE + 256 + MACLEN];
    kmemcpy(file, "ASBIN1", HDR);
    kmemcpy(file + HDR, nonce, NONCE);
    kmemcpy(file + HDR + NONCE, payload, (size_t)pl);
    crypt(nonce, file + HDR + NONCE, (u32)pl);
    mac(nonce, file + HDR + NONCE, (u32)pl, file + HDR + NONCE + pl);

    if (bin_path(path, bp, sizeof bp) < 0) return PM_EFS;
    int b = fs_resolve(bp, cwd);
    if (b == FS_ENOENT) b = fs_create(bp, cwd, 0);
    if (b < 0 || fs_write(b, (const char *)file, (u32)(HDR + NONCE + pl + MACLEN), 0) < 0) return PM_EFS;
    return PM_OK;
}

int perm_grant(const char *path, int cwd)
{
    const char *owner = user_name();
    if (!owner) return PM_ENOLOGIN;
    return grant_as(path, cwd, owner);
}

/* ASPI: seals an installed package script (owner "aspi"). Call with the OS root visible. */
int perm_seal_system(const char *path) { return grant_as(path, 0, "aspi"); }

int perm_revoke(const char *path, int cwd)
{
    if (!user_name()) return PM_ENOLOGIN;
    if (!has_as_ext(path)) return PM_EEXT;
    char bp[160];
    if (bin_path(path, bp, sizeof bp) < 0) return PM_EFS;
    int b = fs_resolve(bp, cwd);
    if (b < 0) return PM_EMISSING;
    return fs_delete(b) == FS_OK ? PM_OK : PM_EFS;
}

static int check_as(int script, const char *path, int cwd, const char *owner)
{
    if (!has_as_ext(path)) return PM_EEXT;
    char bp[160];
    if (bin_path(path, bp, sizeof bp) < 0) return PM_EFS;
    int b = fs_resolve(bp, cwd);
    if (b < 0 || fs_is_dir(b)) return PM_EMISSING;

    u32 len = fs_size(b);
    if (len < HDR + NONCE + 1 + MACLEN || len > HDR + NONCE + 256 + MACLEN) return PM_EBAD;
    const u8 *f = (const u8 *)fs_data(b);
    if (kmemcmp(f, "ASBIN1", HDR)) return PM_EBAD;

    u32 cl = len - HDR - NONCE - MACLEN;
    u8 m[32];
    mac(f + HDR, f + HDR + NONCE, cl, m);
    int diff = 0;
    for (int i = 0; i < 32; i++) diff |= m[i] ^ f[HDR + NONCE + cl + i];
    if (diff) return PM_EBAD;

    char plain[257];
    kmemcpy(plain, f + HDR + NONCE, cl);
    crypt(f + HDR, (u8 *)plain, cl);
    plain[cl] = 0;

    char want[160];
    char hex[65];
    sha256_hex(fs_data(script), fs_size(script), hex);
    ksnprintf(want, sizeof want, "perm=x\nowner=%s\nsha=%s\n", owner, hex);
    if (!kstrcmp(plain, want)) return PM_OK;

    /* Decide between "someone else's" and "script changed" for a clearer message. */
    char who[40];
    ksnprintf(who, sizeof who, "perm=x\nowner=%s\n", owner);
    return kstrncmp(plain, who, kstrlen(who)) ? PM_EOWNER : PM_ECHANGED;
}

int perm_check(int script, const char *path, int cwd)
{
    const char *owner = user_name();
    if (!owner) return PM_ENOLOGIN;
    return check_as(script, path, cwd, owner);
}

/* ASPI: verifies the "aspi" seal of an installed package script (any user may run it). */
int perm_check_system(int script, const char *path) { return check_as(script, path, 0, "aspi"); }

const char *perm_strerror(int e)
{
    switch (e) {
    case PM_ENOLOGIN: return "not logged in";
    case PM_ENOENT:   return "no such file";
    case PM_EFS:      return "file error";
    case PM_EEXT:     return "not an AstraScript file (.as required)";
    case PM_EMISSING: return "no execute permission (.asbin missing) - run: as cper +x <file>";
    case PM_EBAD:     return "permission file is corrupt or forged - run: as cper +x <file>";
    case PM_EOWNER:   return "permission belongs to another user";
    case PM_ECHANGED: return "script changed since permission was granted - run: as cper +x <file>";
    default:          return "unknown error";
    }
}
