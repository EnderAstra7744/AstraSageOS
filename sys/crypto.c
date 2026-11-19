/* See crypto.h. Everything here is public-input verification or ephemeral-key work; the code is not constant-time. */
#include "crypto.h"
#include "crypto_tables.h"

static void cmemcpy(void *d, const void *s, size_t n) { cu8 *a = d; const cu8 *b = s; while (n--) *a++ = *b++; }
static void cmemset(void *d, int c, size_t n) { cu8 *a = d; while (n--) *a++ = (cu8)c; }
static int  cmemeq(const cu8 *a, const cu8 *b, size_t n) { cu8 x = 0; while (n--) x |= *a++ ^ *b++; return x == 0; }

/* ============================================================ SHA-256 */

static const cu32 K256[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};
#define ROR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_block(cu32 h[8], const cu8 *p)
{
    cu32 w[64];
    for (int i = 0; i < 16; i++) w[i] = (cu32)p[i*4] << 24 | (cu32)p[i*4+1] << 16 | (cu32)p[i*4+2] << 8 | p[i*4+3];
    for (int i = 16; i < 64; i++) {
        cu32 s0 = ROR32(w[i-15], 7) ^ ROR32(w[i-15], 18) ^ (w[i-15] >> 3);
        cu32 s1 = ROR32(w[i-2], 17) ^ ROR32(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    cu32 a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; i++) {
        cu32 S1 = ROR32(e, 6) ^ ROR32(e, 11) ^ ROR32(e, 25);
        cu32 ch = (e & f) ^ (~e & g);
        cu32 t1 = hh + S1 + ch + K256[i] + w[i];
        cu32 S0 = ROR32(a, 2) ^ ROR32(a, 13) ^ ROR32(a, 22);
        cu32 mj = (a & b) ^ (a & c) ^ (b & c);
        cu32 t2 = S0 + mj;
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

static void sha256_init(cr_sha256_ctx *c)
{
    static const cu32 iv[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    for (int i = 0; i < 8; i++) c->h[i] = iv[i];
    c->len = 0; c->n = 0;
}

static void sha256_update(cr_sha256_ctx *c, const cu8 *d, size_t len)
{
    c->len += len;
    while (len) {
        cu32 take = 64 - c->n; if (take > len) take = (cu32)len;
        cmemcpy(c->buf + c->n, d, take);
        c->n += take; d += take; len -= take;
        if (c->n == 64) { sha256_block(c->h, c->buf); c->n = 0; }
    }
}

static void sha256_final(cr_sha256_ctx *c, cu8 *out)
{
    cu64 bits = c->len * 8;
    cu8 pad = 0x80;
    sha256_update(c, &pad, 1);
    pad = 0;
    while (c->n != 56) sha256_update(c, &pad, 1);
    cu8 lb[8];
    for (int i = 0; i < 8; i++) lb[i] = (cu8)(bits >> (56 - 8 * i));
    sha256_update(c, lb, 8);
    for (int i = 0; i < 8; i++) { out[i*4] = (cu8)(c->h[i] >> 24); out[i*4+1] = (cu8)(c->h[i] >> 16); out[i*4+2] = (cu8)(c->h[i] >> 8); out[i*4+3] = (cu8)c->h[i]; }
}

/* ============================================================ SHA-512 / SHA-384 */

#define ROR64(x, n) (((x) >> (n)) | ((x) << (64 - (n))))

static void sha512_block(cu64 h[8], const cu8 *p)
{
    cu64 w[80];
    for (int i = 0; i < 16; i++) { cu64 v = 0; for (int j = 0; j < 8; j++) v = v << 8 | p[i*8+j]; w[i] = v; }
    for (int i = 16; i < 80; i++) {
        cu64 s0 = ROR64(w[i-15], 1) ^ ROR64(w[i-15], 8) ^ (w[i-15] >> 7);
        cu64 s1 = ROR64(w[i-2], 19) ^ ROR64(w[i-2], 61) ^ (w[i-2] >> 6);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    cu64 a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 80; i++) {
        cu64 S1 = ROR64(e, 14) ^ ROR64(e, 18) ^ ROR64(e, 41);
        cu64 ch = (e & f) ^ (~e & g);
        cu64 t1 = hh + S1 + ch + SHA512_K[i] + w[i];
        cu64 S0 = ROR64(a, 28) ^ ROR64(a, 34) ^ ROR64(a, 39);
        cu64 mj = (a & b) ^ (a & c) ^ (b & c);
        cu64 t2 = S0 + mj;
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

static void sha512_init(cr_sha512_ctx *c, int is384)
{
    static const cu64 iv512[8] = {
        0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
        0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL };
    for (int i = 0; i < 8; i++) c->h[i] = is384 ? SHA384_H0[i] : iv512[i];
    c->len = 0; c->n = 0; c->is384 = is384;
}

static void sha512_update(cr_sha512_ctx *c, const cu8 *d, size_t len)
{
    c->len += len;
    while (len) {
        cu32 take = 128 - c->n; if (take > len) take = (cu32)len;
        cmemcpy(c->buf + c->n, d, take);
        c->n += take; d += take; len -= take;
        if (c->n == 128) { sha512_block(c->h, c->buf); c->n = 0; }
    }
}

static void sha512_final(cr_sha512_ctx *c, cu8 *out)
{
    cu64 bits = c->len * 8;
    cu8 pad = 0x80;
    sha512_update(c, &pad, 1);
    pad = 0;
    while (c->n != 112) sha512_update(c, &pad, 1);
    cu8 lb[16];
    for (int i = 0; i < 8; i++) lb[i] = 0;
    for (int i = 0; i < 8; i++) lb[8 + i] = (cu8)(bits >> (56 - 8 * i));
    sha512_update(c, lb, 16);
    int words = c->is384 ? 6 : 8;
    for (int i = 0; i < words; i++) for (int j = 0; j < 8; j++) out[i*8+j] = (cu8)(c->h[i] >> (56 - 8 * j));
}

/* ---- one interface ---- */

void cr_hash_init(cr_hash *c, int alg)
{
    c->alg = alg;
    if (alg == CR_SHA256) sha256_init(&c->u.a); else sha512_init(&c->u.b, alg == CR_SHA384);
}
void cr_hash_update(cr_hash *c, const void *d, size_t n)
{
    if (c->alg == CR_SHA256) sha256_update(&c->u.a, d, n); else sha512_update(&c->u.b, d, n);
}
void cr_hash_final(cr_hash *c, cu8 *out)
{
    if (c->alg == CR_SHA256) sha256_final(&c->u.a, out); else sha512_final(&c->u.b, out);
}
int cr_hash_len(int alg) { return alg == CR_SHA256 ? 32 : alg == CR_SHA384 ? 48 : 64; }
int cr_hash_block(int alg) { return alg == CR_SHA256 ? 64 : 128; }
void cr_hash_once(int alg, const void *d, size_t n, cu8 *out)
{
    cr_hash c; cr_hash_init(&c, alg); cr_hash_update(&c, d, n); cr_hash_final(&c, out);
}

void cr_hmac(int alg, const cu8 *key, size_t klen, const cu8 *const *parts, const size_t *lens, int np, cu8 *out)
{
    cu8 k[128], pad[128], inner[64];
    int bl = cr_hash_block(alg), hl = cr_hash_len(alg);
    cmemset(k, 0, sizeof k);
    if (klen > (size_t)bl) cr_hash_once(alg, key, klen, k); else cmemcpy(k, key, klen);
    cr_hash c;
    for (int i = 0; i < bl; i++) pad[i] = k[i] ^ 0x36;
    cr_hash_init(&c, alg); cr_hash_update(&c, pad, (size_t)bl);
    for (int i = 0; i < np; i++) cr_hash_update(&c, parts[i], lens[i]);
    cr_hash_final(&c, inner);
    for (int i = 0; i < bl; i++) pad[i] = k[i] ^ 0x5c;
    cr_hash_init(&c, alg); cr_hash_update(&c, pad, (size_t)bl); cr_hash_update(&c, inner, (size_t)hl);
    cr_hash_final(&c, out);
}

/* ============================================================ AES + GCM */

static cu8 xtime(cu8 a) { return (cu8)((a << 1) ^ ((a & 0x80) ? 0x1b : 0)); }

static void aes_expand(cr_gcm *g, const cu8 *key, int klen)
{
    int nk = klen / 4;
    g->rounds = nk + 6;
    int total = 4 * (g->rounds + 1);
    for (int i = 0; i < nk; i++) g->rk[i] = (cu32)key[4*i] << 24 | (cu32)key[4*i+1] << 16 | (cu32)key[4*i+2] << 8 | key[4*i+3];
    cu8 rcon = 1;
    for (int i = nk; i < total; i++) {
        cu32 t = g->rk[i - 1];
        if (i % nk == 0) {
            t = (t << 8) | (t >> 24);
            t = (cu32)AES_SBOX[t >> 24] << 24 | (cu32)AES_SBOX[(t >> 16) & 255] << 16 | (cu32)AES_SBOX[(t >> 8) & 255] << 8 | AES_SBOX[t & 255];
            t ^= (cu32)rcon << 24;
            rcon = xtime(rcon);
        } else if (nk > 6 && i % nk == 4) {
            t = (cu32)AES_SBOX[t >> 24] << 24 | (cu32)AES_SBOX[(t >> 16) & 255] << 16 | (cu32)AES_SBOX[(t >> 8) & 255] << 8 | AES_SBOX[t & 255];
        }
        g->rk[i] = g->rk[i - nk] ^ t;
    }
}

static cu32 TE[4][256];
static int te_ready;

static void te_init(void)
{
    for (int i = 0; i < 256; i++) {
        cu8 sb = AES_SBOX[i], s2 = xtime(sb), s3 = (cu8)(s2 ^ sb);
        cu32 w = (cu32)s2 << 24 | (cu32)sb << 16 | (cu32)sb << 8 | s3;
        TE[0][i] = w; TE[1][i] = ROR32(w, 8); TE[2][i] = ROR32(w, 16); TE[3][i] = ROR32(w, 24);
    }
    te_ready = 1;
}

static void aes_encrypt(const cr_gcm *g, const cu8 in[16], cu8 out[16])
{
    if (!te_ready) te_init();
    const cu32 *rk = g->rk;
    cu32 s0 = ((cu32)in[0] << 24 | (cu32)in[1] << 16 | (cu32)in[2] << 8 | in[3]) ^ rk[0];
    cu32 s1 = ((cu32)in[4] << 24 | (cu32)in[5] << 16 | (cu32)in[6] << 8 | in[7]) ^ rk[1];
    cu32 s2 = ((cu32)in[8] << 24 | (cu32)in[9] << 16 | (cu32)in[10] << 8 | in[11]) ^ rk[2];
    cu32 s3 = ((cu32)in[12] << 24 | (cu32)in[13] << 16 | (cu32)in[14] << 8 | in[15]) ^ rk[3];
    for (int r = 1; r < g->rounds; r++) {
        rk += 4;
        cu32 t0 = TE[0][s0 >> 24] ^ TE[1][(s1 >> 16) & 255] ^ TE[2][(s2 >> 8) & 255] ^ TE[3][s3 & 255] ^ rk[0];
        cu32 t1 = TE[0][s1 >> 24] ^ TE[1][(s2 >> 16) & 255] ^ TE[2][(s3 >> 8) & 255] ^ TE[3][s0 & 255] ^ rk[1];
        cu32 t2 = TE[0][s2 >> 24] ^ TE[1][(s3 >> 16) & 255] ^ TE[2][(s0 >> 8) & 255] ^ TE[3][s1 & 255] ^ rk[2];
        cu32 t3 = TE[0][s3 >> 24] ^ TE[1][(s0 >> 16) & 255] ^ TE[2][(s1 >> 8) & 255] ^ TE[3][s2 & 255] ^ rk[3];
        s0 = t0; s1 = t1; s2 = t2; s3 = t3;
    }
    rk += 4;
    cu32 r0 = ((cu32)AES_SBOX[s0 >> 24] << 24 | (cu32)AES_SBOX[(s1 >> 16) & 255] << 16 | (cu32)AES_SBOX[(s2 >> 8) & 255] << 8 | AES_SBOX[s3 & 255]) ^ rk[0];
    cu32 r1 = ((cu32)AES_SBOX[s1 >> 24] << 24 | (cu32)AES_SBOX[(s2 >> 16) & 255] << 16 | (cu32)AES_SBOX[(s3 >> 8) & 255] << 8 | AES_SBOX[s0 & 255]) ^ rk[1];
    cu32 r2 = ((cu32)AES_SBOX[s2 >> 24] << 24 | (cu32)AES_SBOX[(s3 >> 16) & 255] << 16 | (cu32)AES_SBOX[(s0 >> 8) & 255] << 8 | AES_SBOX[s1 & 255]) ^ rk[2];
    cu32 r3 = ((cu32)AES_SBOX[s3 >> 24] << 24 | (cu32)AES_SBOX[(s0 >> 16) & 255] << 16 | (cu32)AES_SBOX[(s1 >> 8) & 255] << 8 | AES_SBOX[s2 & 255]) ^ rk[3];
    cu32 rr[4] = { r0, r1, r2, r3 };
    for (int i = 0; i < 16; i++) out[i] = (cu8)(rr[i / 4] >> (24 - 8 * (i % 4)));
}

static const cu64 LAST4[16] = { 0x0000, 0x1c20, 0x3840, 0x2460, 0x7080, 0x6ca0, 0x48c0, 0x54e0,
                                0xe100, 0xfd20, 0xd940, 0xc560, 0x9180, 0x8da0, 0xa9c0, 0xb5e0 };

int cr_gcm_init(cr_gcm *g, const cu8 *key, int keylen)
{
    if (keylen != 16 && keylen != 32) return -1;
    aes_expand(g, key, keylen);
    cu8 zero[16], h[16];
    cmemset(zero, 0, 16);
    aes_encrypt(g, zero, h);
    cu64 vh = 0, vl = 0;
    for (int i = 0; i < 8; i++) { vh = vh << 8 | h[i]; vl = vl << 8 | h[8 + i]; }
    g->HL[8] = vl; g->HH[8] = vh; g->HH[0] = 0; g->HL[0] = 0;
    for (int i = 4; i > 0; i >>= 1) {
        cu32 T = (cu32)(vl & 1) * 0xe1000000u;
        vl = (vh << 63) | (vl >> 1);
        vh = (vh >> 1) ^ ((cu64)T << 32);
        g->HL[i] = vl; g->HH[i] = vh;
    }
    for (int i = 2; i <= 8; i *= 2) {
        cu64 *HiL = g->HL + i, *HiH = g->HH + i;
        vh = *HiH; vl = *HiL;
        for (int j = 1; j < i; j++) { HiH[j] = vh ^ g->HH[j]; HiL[j] = vl ^ g->HL[j]; }
    }
    return 0;
}

static void ghash_mult(const cr_gcm *g, cu8 x[16])
{
    cu8 lo = x[15] & 0xf;
    cu64 zh = g->HH[lo], zl = g->HL[lo];
    for (int i = 15; i >= 0; i--) {
        lo = x[i] & 0xf;
        cu8 hi = (x[i] >> 4) & 0xf;
        cu8 rem;
        if (i != 15) {
            rem = (cu8)(zl & 0xf);
            zl = (zh << 60) | (zl >> 4); zh = zh >> 4;
            zh ^= LAST4[rem] << 48; zh ^= g->HH[lo]; zl ^= g->HL[lo];
        }
        rem = (cu8)(zl & 0xf);
        zl = (zh << 60) | (zl >> 4); zh = zh >> 4;
        zh ^= LAST4[rem] << 48; zh ^= g->HH[hi]; zl ^= g->HL[hi];
    }
    for (int i = 0; i < 8; i++) { x[i] = (cu8)(zh >> (56 - 8 * i)); x[8 + i] = (cu8)(zl >> (56 - 8 * i)); }
}

static void ghash_data(const cr_gcm *g, cu8 y[16], const cu8 *d, size_t n)
{
    while (n) {
        size_t take = n < 16 ? n : 16;
        for (size_t i = 0; i < take; i++) y[i] ^= d[i];
        ghash_mult(g, y);
        d += take; n -= take;
    }
}

static void gcm_core(cr_gcm *g, int enc, const cu8 iv[12], const cu8 *aad, size_t alen, const cu8 *in, size_t len, cu8 *out, cu8 tag[16])
{
    cu8 j0[16], ctr[16], ks[16], y[16];
    cmemcpy(j0, iv, 12); j0[12] = 0; j0[13] = 0; j0[14] = 0; j0[15] = 1;
    cmemset(y, 0, 16);
    ghash_data(g, y, aad, alen);
    if (!enc) ghash_data(g, y, in, len);
    cmemcpy(ctr, j0, 16);
    for (size_t off = 0; off < len; off += 16) {
        for (int i = 15; i >= 12; i--) if (++ctr[i]) break;
        aes_encrypt(g, ctr, ks);
        size_t take = len - off < 16 ? len - off : 16;
        for (size_t i = 0; i < take; i++) out[off + i] = in[off + i] ^ ks[i];
    }
    if (enc) ghash_data(g, y, out, len);
    cu8 lb[16];
    cu64 ab = (cu64)alen * 8, cb = (cu64)len * 8;
    for (int i = 0; i < 8; i++) { lb[i] = (cu8)(ab >> (56 - 8 * i)); lb[8 + i] = (cu8)(cb >> (56 - 8 * i)); }
    ghash_data(g, y, lb, 16);
    aes_encrypt(g, j0, ks);
    for (int i = 0; i < 16; i++) tag[i] = y[i] ^ ks[i];
}

void cr_gcm_seal(cr_gcm *g, const cu8 iv[12], const cu8 *aad, size_t alen, const cu8 *in, size_t len, cu8 *out, cu8 tag[16])
{
    gcm_core(g, 1, iv, aad, alen, in, len, out, tag);
}

int cr_gcm_open(cr_gcm *g, const cu8 iv[12], const cu8 *aad, size_t alen, const cu8 *in, size_t len, cu8 *out, const cu8 tag[16])
{
    cu8 t[16];
    gcm_core(g, 0, iv, aad, alen, in, len, out, t);
    if (!cmemeq(t, tag, 16)) { cmemset(out, 0, len); return -1; }
    return 0;
}

/* ============================================================ big numbers (little-endian 32-bit limbs) + Montgomery */

#define BN_MAX 128

typedef struct { int n; cu32 m[BN_MAX]; cu32 n0; cu32 rr[BN_MAX]; cu32 one[BN_MAX]; } mont;

static int bn_cmp(const cu32 *a, const cu32 *b, int n)
{
    for (int i = n - 1; i >= 0; i--) { if (a[i] > b[i]) return 1; if (a[i] < b[i]) return -1; }
    return 0;
}
static cu32 bn_sub(cu32 *r, const cu32 *a, const cu32 *b, int n)       /* r = a - b, returns borrow */
{
    cu64 br = 0;
    for (int i = 0; i < n; i++) { cu64 d = (cu64)a[i] - b[i] - br; r[i] = (cu32)d; br = (d >> 32) & 1; }
    return (cu32)br;
}
static cu32 bn_add(cu32 *r, const cu32 *a, const cu32 *b, int n)       /* returns carry */
{
    cu64 c = 0;
    for (int i = 0; i < n; i++) { cu64 s = (cu64)a[i] + b[i] + c; r[i] = (cu32)s; c = s >> 32; }
    return (cu32)c;
}
static int bn_is_zero(const cu32 *a, int n) { cu32 x = 0; for (int i = 0; i < n; i++) x |= a[i]; return x == 0; }
static void bn_from_bytes(cu32 *r, int n, const cu8 *b, size_t len)
{
    for (int i = 0; i < n; i++) r[i] = 0;
    for (size_t i = 0; i < len; i++) {
        size_t pos = len - 1 - i;                                  /* byte index from the least significant end */
        if (pos / 4 < (size_t)n) r[pos / 4] |= (cu32)b[i] << (8 * (pos % 4)) ;
    }
}
static void bn_to_bytes(cu8 *out, size_t len, const cu32 *a, int n)
{
    for (size_t i = 0; i < len; i++) {
        size_t pos = len - 1 - i;
        out[i] = pos / 4 < (size_t)n ? (cu8)(a[pos / 4] >> (8 * (pos % 4))) : 0;
    }
}
static int bn_bits(const cu32 *a, int n)
{
    for (int i = n - 1; i >= 0; i--) if (a[i]) { int b = 32; cu32 v = a[i]; while (!(v & 0x80000000u)) { v <<= 1; b--; } return i * 32 + b; }
    return 0;
}

static void dbl_mod(cu32 *x, const cu32 *m, int n)                     /* x = 2x mod m (x < m) */
{
    cu32 carry = 0;
    for (int i = 0; i < n; i++) { cu32 nc = x[i] >> 31; x[i] = x[i] << 1 | carry; carry = nc; }
    if (carry || bn_cmp(x, m, n) >= 0) bn_sub(x, x, m, n);
}

static void mont_init(mont *c, const cu32 *m, int n)
{
    c->n = n;
    for (int i = 0; i < n; i++) c->m[i] = m[i];
    cu32 inv = m[0];                                               /* Newton: inverse of m[0] mod 2^32 */
    for (int i = 0; i < 5; i++) inv *= 2 - m[0] * inv;
    c->n0 = (cu32)(0u - inv);
    for (int i = 0; i < n; i++) c->one[i] = 0;
    c->one[0] = 1;
    if (n == 1 && m[0] == 1) { c->one[0] = 0; }
    for (int i = 0; i < n * 32; i++) dbl_mod(c->one, c->m, n);     /* R mod m */
    for (int i = 0; i < n; i++) c->rr[i] = c->one[i];
    for (int i = 0; i < n * 32; i++) dbl_mod(c->rr, c->m, n);      /* R^2 mod m */
}

static void mont_mul(const mont *c, cu32 *r, const cu32 *a, const cu32 *b)
{
    int n = c->n;
    cu32 t[BN_MAX + 2];
    for (int i = 0; i < n + 2; i++) t[i] = 0;
    for (int i = 0; i < n; i++) {
        cu64 carry = 0;
        for (int j = 0; j < n; j++) { cu64 s = (cu64)t[j] + (cu64)a[j] * b[i] + carry; t[j] = (cu32)s; carry = s >> 32; }
        cu64 s = (cu64)t[n] + carry; t[n] = (cu32)s; t[n + 1] = (cu32)(s >> 32);
        cu32 mm = t[0] * c->n0;
        s = (cu64)t[0] + (cu64)mm * c->m[0]; carry = s >> 32;
        for (int j = 1; j < n; j++) { s = (cu64)t[j] + (cu64)mm * c->m[j] + carry; t[j - 1] = (cu32)s; carry = s >> 32; }
        s = (cu64)t[n] + carry; t[n - 1] = (cu32)s; carry = s >> 32;
        t[n] = t[n + 1] + (cu32)carry;
    }
    if (t[n] || bn_cmp(t, c->m, n) >= 0) bn_sub(t, t, c->m, n);
    for (int i = 0; i < n; i++) r[i] = t[i];
}

static void mont_to(const mont *c, cu32 *r, const cu32 *a) { mont_mul(c, r, a, c->rr); }
static void mont_from(const mont *c, cu32 *r, const cu32 *a)
{
    cu32 one[BN_MAX];
    for (int i = 0; i < c->n; i++) one[i] = 0;
    one[0] = 1;
    mont_mul(c, r, a, one);
}

/* r = a^e mod m (a < m, normal domain in and out); e given as limbs */
static void mont_exp(const mont *c, cu32 *r, const cu32 *a, const cu32 *e, int en)
{
    int n = c->n;
    cu32 base[BN_MAX], acc[BN_MAX];
    mont_to(c, base, a);
    for (int i = 0; i < n; i++) acc[i] = c->one[i];
    int bits = bn_bits(e, en);
    for (int i = bits - 1; i >= 0; i--) {
        mont_mul(c, acc, acc, acc);
        if ((e[i / 32] >> (i % 32)) & 1) mont_mul(c, acc, acc, base);
    }
    mont_from(c, r, acc);
}

/* modular add / sub for the field and the group order (values < m) */
static void mod_add(const mont *c, cu32 *r, const cu32 *a, const cu32 *b)
{
    cu32 carry = bn_add(r, a, b, c->n);
    if (carry || bn_cmp(r, c->m, c->n) >= 0) bn_sub(r, r, c->m, c->n);
}
static void mod_sub(const mont *c, cu32 *r, const cu32 *a, const cu32 *b)
{
    if (bn_sub(r, a, b, c->n)) bn_add(r, r, c->m, c->n);
}
static void mont_inv(const mont *c, cu32 *r, const cu32 *a)           /* a^-1 mod prime m, normal domain */
{
    cu32 e[BN_MAX], two[BN_MAX];
    for (int i = 0; i < BN_MAX; i++) { e[i] = 0; two[i] = 0; }
    two[0] = 2;
    bn_sub(e, c->m, two, c->n);
    mont_exp(c, r, a, e, c->n);
}

/* ============================================================ RSA PKCS#1 v1.5 verification */

int cr_rsa_verify(const cu8 *nb, size_t nlen, const cu8 *eb, size_t elen, int hashalg, const cu8 *hash, const cu8 *sig, size_t siglen)
{
    while (nlen && *nb == 0) { nb++; nlen--; }
    if (nlen < 256 || nlen > BN_MAX * 4 || siglen != nlen) return -1;                   /* modulus of at least 2048 bits */
    if (elen == 0 || elen > 4 || !(eb[elen - 1] & 1)) return -1;
    static mont mc;
    cu32 n[BN_MAX], s[BN_MAX], m[BN_MAX], e[1];
    int limbs = (int)((nlen + 3) / 4);
    bn_from_bytes(n, limbs, nb, nlen);
    if (!(n[0] & 1)) return -1;
    bn_from_bytes(s, limbs, sig, siglen);
    if (bn_cmp(s, n, limbs) >= 0) return -1;
    bn_from_bytes(e, 1, eb, elen);
    if (e[0] < 3) return -1;
    mont_init(&mc, n, limbs);
    mont_exp(&mc, m, s, e, 1);
    cu8 em[BN_MAX * 4];
    bn_to_bytes(em, nlen, m, limbs);
    static const cu8 p256[19] = { 0x30,0x31,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x01,0x05,0x00,0x04,0x20 };
    static const cu8 p384[19] = { 0x30,0x41,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x02,0x05,0x00,0x04,0x30 };
    static const cu8 p512[19] = { 0x30,0x51,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x03,0x05,0x00,0x04,0x40 };
    const cu8 *pre = hashalg == CR_SHA256 ? p256 : hashalg == CR_SHA384 ? p384 : p512;
    size_t hl = (size_t)cr_hash_len(hashalg), tl = 19 + hl;
    if (nlen < tl + 11) return -1;
    size_t pslen = nlen - tl - 3;
    if (em[0] != 0 || em[1] != 1) return -1;
    for (size_t i = 0; i < pslen; i++) if (em[2 + i] != 0xff) return -1;
    if (em[2 + pslen] != 0) return -1;
    if (!cmemeq(em + 3 + pslen, pre, 19)) return -1;
    if (!cmemeq(em + 3 + pslen + 19, hash, hl)) return -1;
    return 0;
}

/* ============================================================ elliptic curves y^2 = x^3 - 3x + b (P-256, P-384) */

typedef struct { int ok, n, bytes; mont fp, fn; cu32 b[12], gx[12], gy[12], three[12]; } curve;
typedef struct { cu32 x[12], y[12], z[12]; } jpt;                      /* Jacobian, Montgomery domain; z = 0 is infinity */

static curve curves[2];

static const curve *get_curve(int id)
{
    curve *c = &curves[id - 1];
    if (c->ok) return c;
    const cu8 *P, *B, *GX, *GY, *N;
    int bytes;
    if (id == CR_P256) { P = P256_P; B = P256_B; GX = P256_GX; GY = P256_GY; N = P256_N; bytes = 32; }
    else { P = P384_P; B = P384_B; GX = P384_GX; GY = P384_GY; N = P384_N; bytes = 48; }
    int n = bytes / 4;
    c->n = n; c->bytes = bytes;
    cu32 t[12];
    bn_from_bytes(t, n, P, (size_t)bytes); mont_init(&c->fp, t, n);
    bn_from_bytes(t, n, N, (size_t)bytes); mont_init(&c->fn, t, n);
    bn_from_bytes(t, n, B, (size_t)bytes); mont_to(&c->fp, c->b, t);
    bn_from_bytes(c->gx, n, GX, (size_t)bytes);
    bn_from_bytes(c->gy, n, GY, (size_t)bytes);
    for (int i = 0; i < n; i++) t[i] = 0;
    t[0] = 3; mont_to(&c->fp, c->three, t);
    c->ok = 1;
    return c;
}

static void jset_inf(const curve *c, jpt *p)
{
    for (int i = 0; i < c->n; i++) { p->x[i] = c->fp.one[i]; p->y[i] = c->fp.one[i]; p->z[i] = 0; }
}

static void jdbl(const curve *c, jpt *r, const jpt *p)
{
    const mont *f = &c->fp;
    int n = c->n;
    if (bn_is_zero(p->z, n)) { *r = *p; return; }
    cu32 delta[12], gamma[12], beta[12], alpha[12], t1[12], t2[12], x3[12], y3[12], z3[12];
    mont_mul(f, delta, p->z, p->z);
    mont_mul(f, gamma, p->y, p->y);
    mont_mul(f, beta, p->x, gamma);
    mod_sub(f, t1, p->x, delta); mod_add(f, t2, p->x, delta);
    mont_mul(f, t1, t1, t2);
    mod_add(f, alpha, t1, t1); mod_add(f, alpha, alpha, t1);                 /* 3*(x-d)(x+d) */
    mont_mul(f, x3, alpha, alpha);
    mod_add(f, t1, beta, beta); mod_add(f, t1, t1, t1); mod_add(f, t2, t1, t1);   /* t1 = 4beta, t2 = 8beta */
    mod_sub(f, x3, x3, t2);
    mod_add(f, z3, p->y, p->z); mont_mul(f, z3, z3, z3);
    mod_sub(f, z3, z3, gamma); mod_sub(f, z3, z3, delta);
    mod_sub(f, t1, t1, x3);
    mont_mul(f, y3, alpha, t1);
    mont_mul(f, t2, gamma, gamma);
    mod_add(f, t2, t2, t2); mod_add(f, t2, t2, t2); mod_add(f, t2, t2, t2);       /* 8 gamma^2 */
    mod_sub(f, y3, y3, t2);
    for (int i = 0; i < n; i++) { r->x[i] = x3[i]; r->y[i] = y3[i]; r->z[i] = z3[i]; }
}

static void jadd(const curve *c, jpt *r, const jpt *p, const jpt *q)
{
    const mont *f = &c->fp;
    int n = c->n;
    if (bn_is_zero(p->z, n)) { *r = *q; return; }
    if (bn_is_zero(q->z, n)) { *r = *p; return; }
    cu32 z1z1[12], z2z2[12], u1[12], u2[12], s1[12], s2[12], h[12], i2[12], j[12], rr[12], v[12], t[12], x3[12], y3[12], z3[12];
    mont_mul(f, z1z1, p->z, p->z);
    mont_mul(f, z2z2, q->z, q->z);
    mont_mul(f, u1, p->x, z2z2);
    mont_mul(f, u2, q->x, z1z1);
    mont_mul(f, t, q->z, z2z2); mont_mul(f, s1, p->y, t);
    mont_mul(f, t, p->z, z1z1); mont_mul(f, s2, q->y, t);
    mod_sub(f, h, u2, u1);
    mod_sub(f, rr, s2, s1);
    if (bn_is_zero(h, n)) {
        if (bn_is_zero(rr, n)) { jdbl(c, r, p); return; }
        jset_inf(c, r); return;
    }
    mod_add(f, rr, rr, rr);
    mod_add(f, t, h, h); mont_mul(f, i2, t, t);
    mont_mul(f, j, h, i2);
    mont_mul(f, v, u1, i2);
    mont_mul(f, x3, rr, rr);
    mod_sub(f, x3, x3, j);
    mod_add(f, t, v, v); mod_sub(f, x3, x3, t);
    mod_sub(f, t, v, x3); mont_mul(f, y3, rr, t);
    mont_mul(f, t, s1, j); mod_add(f, t, t, t); mod_sub(f, y3, y3, t);
    mod_add(f, t, p->z, q->z); mont_mul(f, t, t, t);
    mod_sub(f, t, t, z1z1); mod_sub(f, t, t, z2z2);
    mont_mul(f, z3, t, h);
    for (int i = 0; i < n; i++) { r->x[i] = x3[i]; r->y[i] = y3[i]; r->z[i] = z3[i]; }
}

static void jmul(const curve *c, jpt *r, const jpt *p, const cu32 *k)       /* k: n limbs */
{
    jpt acc;
    jset_inf(c, &acc);
    for (int i = bn_bits(k, c->n) - 1; i >= 0; i--) {
        jdbl(c, &acc, &acc);
        if ((k[i / 32] >> (i % 32)) & 1) jadd(c, &acc, &acc, p);
    }
    *r = acc;
}

static int jto_affine(const curve *c, const jpt *p, cu32 *x, cu32 *y)         /* normal domain; -1 for infinity */
{
    if (bn_is_zero(p->z, c->n)) return -1;
    cu32 zn[12], zi[12], z2[12], z3[12], t[12];
    mont_from(&c->fp, zn, p->z);
    mont_inv(&c->fp, zi, zn);                                                /* normal domain */
    mont_to(&c->fp, zi, zi);
    mont_mul(&c->fp, z2, zi, zi);
    mont_mul(&c->fp, z3, z2, zi);
    mont_mul(&c->fp, t, p->x, z2); mont_from(&c->fp, x, t);
    mont_mul(&c->fp, t, p->y, z3); mont_from(&c->fp, y, t);
    return 0;
}

static void jfrom_affine(const curve *c, jpt *p, const cu32 *x, const cu32 *y)
{
    mont_to(&c->fp, p->x, x); mont_to(&c->fp, p->y, y);
    for (int i = 0; i < c->n; i++) p->z[i] = c->fp.one[i];
}

static int on_curve(const curve *c, const cu32 *x, const cu32 *y)             /* x, y normal domain */
{
    const mont *f = &c->fp;
    if (bn_cmp(x, f->m, c->n) >= 0 || bn_cmp(y, f->m, c->n) >= 0) return 0;
    cu32 xm[12], ym[12], l[12], r[12], t[12];
    mont_to(f, xm, x); mont_to(f, ym, y);
    mont_mul(f, l, ym, ym);
    mont_mul(f, r, xm, xm); mont_mul(f, r, r, xm);
    mont_mul(f, t, c->three, xm);
    mod_sub(f, r, r, t);
    mod_add(f, r, r, c->b);
    return bn_cmp(l, r, c->n) == 0;
}

int cr_p256_keygen(const cu8 rnd[32], cu8 priv[32], cu8 pub[65])
{
    const curve *c = get_curve(CR_P256);
    cu32 k[8];
    bn_from_bytes(k, 8, rnd, 32);
    while (bn_cmp(k, c->fn.m, 8) >= 0) bn_sub(k, k, c->fn.m, 8);            /* reduce into [0, n) (rnd < 2^256 < 2n) */
    if (bn_is_zero(k, 8)) k[0] = 1;
    jpt g, q;
    jfrom_affine(c, &g, c->gx, c->gy);
    jmul(c, &q, &g, k);
    cu32 x[12], y[12];
    if (jto_affine(c, &q, x, y)) return -1;
    bn_to_bytes(priv, 32, k, 8);
    pub[0] = 4;
    bn_to_bytes(pub + 1, 32, x, 8); bn_to_bytes(pub + 33, 32, y, 8);
    return 0;
}

int cr_p256_shared(const cu8 priv[32], const cu8 *peer, size_t peerlen, cu8 out[32])
{
    const curve *c = get_curve(CR_P256);
    if (peerlen != 65 || peer[0] != 4) return -1;
    cu32 x[12], y[12], k[8];
    bn_from_bytes(x, 8, peer + 1, 32); bn_from_bytes(y, 8, peer + 33, 32);
    if (!on_curve(c, x, y)) return -1;
    bn_from_bytes(k, 8, priv, 32);
    jpt p, q;
    jfrom_affine(c, &p, x, y);
    jmul(c, &q, &p, k);
    if (jto_affine(c, &q, x, y)) return -1;
    bn_to_bytes(out, 32, x, 8);
    return 0;
}

/* DER: SEQUENCE { INTEGER r, INTEGER s } -> fixed-width big-endian values; 0 = ok */
static int der_len(const cu8 *p, size_t avail, size_t *hdr, size_t *len)
{
    if (avail < 2) return -1;
    if (p[1] < 0x80) { *hdr = 2; *len = p[1]; }
    else if (p[1] == 0x81) { if (avail < 3) return -1; *hdr = 3; *len = p[2]; }
    else if (p[1] == 0x82) { if (avail < 4) return -1; *hdr = 4; *len = (size_t)p[2] << 8 | p[3]; }
    else return -1;
    return *hdr + *len <= avail ? 0 : -1;
}

static int der_int(const cu8 **pp, size_t *avail, cu32 *out, int n)
{
    const cu8 *p = *pp; size_t h, l;
    if (*avail < 2 || p[0] != 2 || der_len(p, *avail, &h, &l)) return -1;
    const cu8 *v = p + h; size_t vl = l;
    if (vl == 0 || (v[0] & 0x80)) return -1;
    while (vl > 1 && v[0] == 0) { v++; vl--; }
    if (vl > (size_t)n * 4) return -1;
    bn_from_bytes(out, n, v, vl);
    *pp = p + h + l; *avail -= h + l;
    return 0;
}

int cr_ecdsa_verify(int curve_id, const cu8 *pub, size_t publen, const cu8 *hash, size_t hlen, const cu8 *sig, size_t siglen)
{
    if (curve_id != CR_P256 && curve_id != CR_P384) return -1;
    const curve *c = get_curve(curve_id);
    int n = c->n;
    if (publen != 1 + 2 * (size_t)c->bytes || pub[0] != 4) return -1;
    cu32 qx[12], qy[12], r[12], s[12];
    bn_from_bytes(qx, n, pub + 1, (size_t)c->bytes);
    bn_from_bytes(qy, n, pub + 1 + c->bytes, (size_t)c->bytes);
    if (!on_curve(c, qx, qy)) return -1;
    size_t h, l;
    if (siglen < 2 || sig[0] != 0x30 || der_len(sig, siglen, &h, &l) || h + l != siglen) return -1;
    const cu8 *p = sig + h; size_t avail = l;
    if (der_int(&p, &avail, r, n) || der_int(&p, &avail, s, n) || avail != 0) return -1;
    if (bn_is_zero(r, n) || bn_is_zero(s, n) || bn_cmp(r, c->fn.m, n) >= 0 || bn_cmp(s, c->fn.m, n) >= 0) return -1;
    /* z = leftmost bits of the hash */
    cu32 z[12];
    size_t use = hlen > (size_t)c->bytes ? (size_t)c->bytes : hlen;
    bn_from_bytes(z, n, hash, use);
    while (bn_cmp(z, c->fn.m, n) >= 0) bn_sub(z, z, c->fn.m, n);
    const mont *fn = &c->fn;
    cu32 w[12], u1[12], u2[12], t[12], zm[12], rm[12], wm[12];
    mont_inv(fn, w, s);
    mont_to(fn, wm, w);
    mont_to(fn, zm, z); mont_mul(fn, u1, zm, wm); mont_from(fn, u1, u1);        /* u1 = z*w mod n */
    mont_to(fn, rm, r); mont_mul(fn, u2, rm, wm); mont_from(fn, u2, u2);        /* u2 = r*w mod n */
    jpt g, q, a, b, sum;
    jfrom_affine(c, &g, c->gx, c->gy);
    jfrom_affine(c, &q, qx, qy);
    jmul(c, &a, &g, u1);
    jmul(c, &b, &q, u2);
    jadd(c, &sum, &a, &b);
    cu32 x[12], y[12];
    if (jto_affine(c, &sum, x, y)) return -1;
    while (bn_cmp(x, fn->m, n) >= 0) bn_sub(x, x, fn->m, n);
    (void)t;
    return bn_cmp(x, r, n) == 0 ? 0 : -1;
}

/* ============================================================ X25519 (RFC 7748) on the generic Montgomery field code */

static mont m25519;
static int m25519_ok;

static void x25519_setup(void)
{
    if (m25519_ok) return;
    cu8 pb[32];
    for (int i = 0; i < 32; i++) pb[i] = 0xff;
    pb[0] = 0x7f; pb[31] = 0xed;                                              /* 2^255 - 19, big-endian */
    cu32 p[8];
    bn_from_bytes(p, 8, pb, 32);
    mont_init(&m25519, p, 8);
    m25519_ok = 1;
}

static void cswap(cu32 *a, cu32 *b, cu32 swap)
{
    cu32 mask = 0u - swap;
    for (int i = 0; i < 8; i++) { cu32 t = mask & (a[i] ^ b[i]); a[i] ^= t; b[i] ^= t; }
}

int cr_x25519(cu8 out[32], const cu8 scalar[32], const cu8 point[32])
{
    x25519_setup();
    const mont *f = &m25519;
    cu8 k[32], u[32];
    for (int i = 0; i < 32; i++) { k[i] = scalar[i]; u[i] = point[i]; }
    k[0] &= 248; k[31] &= 127; k[31] |= 64;
    u[31] &= 127;
    cu32 x1n[8], x1[8], x2[8], z2[8], x3[8], z3[8], a24[8], tmp[8];
    cu8 be[32];
    for (int i = 0; i < 32; i++) be[i] = u[31 - i];
    bn_from_bytes(tmp, 8, be, 32);
    while (bn_cmp(tmp, f->m, 8) >= 0) bn_sub(tmp, tmp, f->m, 8);
    for (int i = 0; i < 8; i++) x1n[i] = tmp[i];
    mont_to(f, x1, x1n);
    for (int i = 0; i < 8; i++) { x2[i] = f->one[i]; z2[i] = 0; x3[i] = x1[i]; z3[i] = f->one[i]; tmp[i] = 0; }
    tmp[0] = 121665; mont_to(f, a24, tmp);
    cu32 swap = 0;
    for (int t = 254; t >= 0; t--) {
        cu32 kt = (k[t / 8] >> (t % 8)) & 1;
        swap ^= kt;
        cswap(x2, x3, swap); cswap(z2, z3, swap);
        swap = kt;
        cu32 A[8], AA[8], B[8], BB[8], E[8], C[8], D[8], DA[8], CB[8], s1[8], s2[8];
        mod_add(f, A, x2, z2); mont_mul(f, AA, A, A);
        mod_sub(f, B, x2, z2); mont_mul(f, BB, B, B);
        mod_sub(f, E, AA, BB);
        mod_add(f, C, x3, z3);
        mod_sub(f, D, x3, z3);
        mont_mul(f, DA, D, A);
        mont_mul(f, CB, C, B);
        mod_add(f, s1, DA, CB); mont_mul(f, x3, s1, s1);
        mod_sub(f, s2, DA, CB); mont_mul(f, s2, s2, s2); mont_mul(f, z3, x1, s2);
        mont_mul(f, x2, AA, BB);
        mont_mul(f, s1, a24, E); mod_add(f, s1, s1, AA); mont_mul(f, z2, E, s1);
    }
    cswap(x2, x3, swap); cswap(z2, z3, swap);
    cu32 zn[8], zi[8], res[8], resn[8];
    mont_from(f, zn, z2);
    mont_inv(f, zi, zn);
    mont_to(f, zi, zi);
    mont_mul(f, res, x2, zi);
    mont_from(f, resn, res);
    bn_to_bytes(be, 32, resn, 8);
    cu8 acc = 0;
    for (int i = 0; i < 32; i++) { out[i] = be[31 - i]; acc |= out[i]; }
    return acc ? 0 : -1;
}

void cr_x25519_base(cu8 out[32], const cu8 scalar[32])
{
    cu8 nine[32];
    for (int i = 0; i < 32; i++) nine[i] = 0;
    nine[0] = 9;
    cr_x25519(out, scalar, nine);
}
