/*
 * .xz (LZMA2) decoder for .tar.xz packages. Single-block streams with the LZMA2 filter
 * only (what `xz`/`tar -J` produce). The output buffer doubles as the dictionary.
 * CRC fields are skipped: package integrity is checked with the SHA-256 in the repo index.
 */
#include "astra.h"

struct rc { const u8 *p, *end; u32 range, code; int bad; };

static u32 rc_byte(struct rc *r)
{
    if (r->p >= r->end) { r->bad = 1; return 0; }
    return *r->p++;
}

static int rc_init(struct rc *r, const u8 *p, u32 n)
{
    r->p = p; r->end = p + n; r->bad = 0;
    if (n < 5 || rc_byte(r) != 0) return -1;
    r->range = 0xFFFFFFFFu; r->code = 0;
    for (int i = 0; i < 4; i++) r->code = (r->code << 8) | rc_byte(r);
    return 0;
}

static inline void rc_norm(struct rc *r)
{
    if (r->range < (1u << 24)) { r->range <<= 8; r->code = (r->code << 8) | rc_byte(r); }
}

static int rc_bit(struct rc *r, u16 *prob)
{
    rc_norm(r);
    u32 bound = (r->range >> 11) * *prob;
    if (r->code < bound) { r->range = bound; *prob += (2048 - *prob) >> 5; return 0; }
    r->range -= bound; r->code -= bound; *prob -= *prob >> 5; return 1;
}

static u32 rc_direct(struct rc *r, int n)
{
    u32 res = 0;
    while (n--) {
        rc_norm(r);
        r->range >>= 1;
        u32 b = 0;
        if (r->code >= r->range) { r->code -= r->range; b = 1; }
        res = (res << 1) | b;
    }
    return res;
}

static u32 tree(struct rc *r, u16 *probs, int nbits)
{
    u32 m = 1;
    for (int i = 0; i < nbits; i++) m = (m << 1) | (u32)rc_bit(r, &probs[m]);
    return m - (1u << nbits);
}

static u32 rtree(struct rc *r, u16 *probs, int nbits)
{
    u32 m = 1, sym = 0;
    for (int i = 0; i < nbits; i++) {
        u32 b = (u32)rc_bit(r, &probs[m]);
        m = (m << 1) | b; sym |= b << i;
    }
    return sym;
}

struct lencoder { u16 choice, choice2, low[16][8], mid[16][8], high[256]; };

static u32 len_decode(struct rc *r, struct lencoder *l, u32 ps)
{
    if (!rc_bit(r, &l->choice)) return tree(r, l->low[ps], 3);
    if (!rc_bit(r, &l->choice2)) return 8 + tree(r, l->mid[ps], 3);
    return 16 + tree(r, l->high, 8);
}

struct lz {
    u16 is_match[12][16], is_rep[12], g0[12], g1[12], g2[12], rep0long[12][16];
    u16 slot[4][64], pos[115], align[16];
    struct lencoder len, replen;
    u16 lit[0x300 << 4];
    int lc, lp, pb;
    u32 state, rep[4];
};

static struct lz lz;

static void lz_reset_state(void)
{
    u16 *p = (u16 *)&lz;
    u16 *e = (u16 *)((u8 *)&lz + offsetof(struct lz, lc));
    /* everything before lc is a probability array */
    for (; p < e; p++) *p = 1024;
    lz.state = 0; lz.rep[0] = lz.rep[1] = lz.rep[2] = lz.rep[3] = 0;
}

/* decodes one LZMA chunk producing exactly `usize` bytes at out[*pos]; dict_base = start of dictionary */
static int lz_chunk(struct rc *r, u8 *out, u32 *pos, u32 usize, u32 dict_base)
{
    u32 end = *pos + usize, o = *pos;
    u32 pbmask = (1u << lz.pb) - 1, lpmask = (1u << lz.lp) - 1;
    while (o < end) {
        if (r->bad) return -1;
        u32 p = o - dict_base, ps = p & pbmask;
        u32 st = lz.state;
        if (!rc_bit(r, &lz.is_match[st][ps])) {
            u32 prev = p ? out[o - 1] : 0;
            u16 *pr = lz.lit + 0x300 * (((p & lpmask) << lz.lc) + (prev >> (8 - lz.lc)));
            u32 sym = 1;
            if (st >= 7) {
                if (lz.rep[0] + 1 > p) return -1;
                u32 mb = out[o - lz.rep[0] - 1];
                do {
                    u32 mbit = (mb >> 7) & 1; mb <<= 1;
                    u32 b = (u32)rc_bit(r, &pr[((1 + mbit) << 8) + sym]);
                    sym = (sym << 1) | b;
                    if (mbit != b) break;
                } while (sym < 0x100);
            }
            while (sym < 0x100) sym = (sym << 1) | (u32)rc_bit(r, &pr[sym]);
            out[o++] = (u8)sym;
            lz.state = st < 4 ? 0 : st < 10 ? st - 3 : st - 6;
            continue;
        }
        u32 len;
        if (!rc_bit(r, &lz.is_rep[st])) {
            lz.rep[3] = lz.rep[2]; lz.rep[2] = lz.rep[1]; lz.rep[1] = lz.rep[0];
            len = len_decode(r, &lz.len, ps);
            lz.state = st < 7 ? 7 : 10;
            u32 ls = len < 4 ? len : 3;
            u32 slot = tree(r, lz.slot[ls], 6), dist;
            if (slot < 4) dist = slot;
            else {
                int nb = (int)(slot >> 1) - 1;
                dist = (2 | (slot & 1)) << nb;
                if (slot < 14) dist += rtree(r, lz.pos + dist - slot, nb);
                else { dist += rc_direct(r, nb - 4) << 4; dist += rtree(r, lz.align, 4); }
            }
            if (dist == 0xFFFFFFFFu) return -1;
            lz.rep[0] = dist;
        } else {
            if (!rc_bit(r, &lz.g0[st])) {
                if (!rc_bit(r, &lz.rep0long[st][ps])) {
                    if (lz.rep[0] + 1 > p) return -1;
                    lz.state = st < 7 ? 9 : 11;
                    out[o] = out[o - lz.rep[0] - 1]; o++;
                    continue;
                }
            } else {
                u32 dist;
                if (!rc_bit(r, &lz.g1[st])) dist = lz.rep[1];
                else {
                    if (!rc_bit(r, &lz.g2[st])) dist = lz.rep[2];
                    else { dist = lz.rep[3]; lz.rep[3] = lz.rep[2]; }
                    lz.rep[2] = lz.rep[1];
                }
                lz.rep[1] = lz.rep[0]; lz.rep[0] = dist;
            }
            len = len_decode(r, &lz.replen, ps);
            lz.state = st < 7 ? 8 : 11;
        }
        len += 2;
        if (lz.rep[0] + 1 > p || o + len > end) return -1;
        for (u32 i = 0; i < len; i++) { out[o] = out[o - lz.rep[0] - 1]; o++; }
    }
    *pos = o;
    return r->bad ? -1 : 0;
}

static int varint(const u8 *s, u32 n, u32 *i, u32 *v)
{
    u32 r = 0; int sh = 0;
    for (;;) {
        if (*i >= n || sh > 28) return -1;
        u8 b = s[(*i)++];
        r |= (u32)(b & 0x7F) << sh; sh += 7;
        if (!(b & 0x80)) break;
    }
    *v = r;
    return 0;
}

/* Decodes a .xz file into dst. Returns 0, or <0: -1 corrupt, -2 output too small, -3 unsupported. */
int xz_decode(const u8 *src, u32 n, u8 *dst, u32 cap, u32 *outlen)
{
    static const u8 magic[6] = { 0xFD, '7', 'z', 'X', 'Z', 0 };
    if (n < 32 || kmemcmp(src, magic, 6)) return -1;
    u32 i = 12;                                        /* after the stream header */
    u32 hsize = ((u32)src[i] + 1) * 4;
    if (src[i] == 0 || i + hsize > n) return -1;       /* 0x00 = index: no block */
    u32 flags = src[i + 1], j = i + 2, v;
    if ((flags & 3) != 0) return -3;                   /* more than one filter (e.g. BCJ) */
    if (flags & 0x40) if (varint(src, i + hsize, &j, &v) < 0) return -1;
    if (flags & 0x80) if (varint(src, i + hsize, &j, &v) < 0) return -1;
    u32 fid, psz;
    if (varint(src, i + hsize, &j, &fid) < 0 || varint(src, i + hsize, &j, &psz) < 0) return -1;
    if (fid != 0x21) return -3;                        /* LZMA2 only */
    i += hsize;

    u32 pos = 0, dict_base = 0;
    int have_props = 0;
    for (;;) {
        if (i >= n) return -1;
        u8 c = src[i++];
        if (c == 0) break;
        if (c == 1 || c == 2) {
            if (i + 2 > n) return -1;
            u32 sz = (((u32)src[i] << 8) | src[i + 1]) + 1; i += 2;
            if (i + sz > n) return -1;
            if (pos + sz > cap) return -2;
            if (c == 1) dict_base = pos;
            kmemcpy(dst + pos, src + i, sz); pos += sz; i += sz;
            continue;
        }
        if (c < 0x80) return -1;
        if (i + 4 > n) return -1;
        u32 usize = ((((u32)c & 0x1F) << 16) | ((u32)src[i] << 8) | src[i + 1]) + 1;
        u32 csize = (((u32)src[i + 2] << 8) | src[i + 3]) + 1;
        i += 4;
        u32 reset = (c >> 5) & 3;
        if (reset == 3) dict_base = pos;
        if (reset >= 2) {
            if (i >= n) return -1;
            u32 pp = src[i++];
            if (pp >= 9 * 5 * 5) return -1;
            lz.lc = (int)(pp % 9); pp /= 9; lz.lp = (int)(pp % 5); lz.pb = (int)(pp / 5);
            if (lz.lc + lz.lp > 4) return -3;
            have_props = 1;
        }
        if (!have_props) return -1;
        if (reset >= 1) lz_reset_state();
        if (i + csize > n) return -1;
        if (pos + usize > cap) return -2;
        struct rc r;
        if (rc_init(&r, src + i, csize) < 0) return -1;
        if (lz_chunk(&r, dst, &pos, usize, dict_base) < 0) return -1;
        i += csize;
    }
    *outlen = pos;
    return 0;
}
