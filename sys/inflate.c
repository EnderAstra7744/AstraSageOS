/*
 * DEFLATE (RFC 1951) decoder for .aspi / .jar (zip) and .tar.gz packages.
 * Based on the structure of zlib's "puff": canonical Huffman decoding, one bit at a
 * time. Small and slow, but only a few hundred KiB are ever decoded.
 */
#include "astra.h"

#define MAXBITS 15
#define MAXLCODES 286
#define MAXDCODES 30
#define FIXLCODES 288

struct huff { u16 count[MAXBITS + 1]; u16 symbol[FIXLCODES]; };

struct st {
    const u8 *in; u32 inlen, incnt;
    u32 bitbuf; int bitcnt;
    u8 *out; u32 outcap, outcnt;
};

static int bits(struct st *s, int need)
{
    u32 val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->incnt >= s->inlen) return -1;
        val |= (u32)s->in[s->incnt++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return (int)(val & ((1u << need) - 1));
}

static int decode(struct st *s, const struct huff *h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= MAXBITS; len++) {
        int b = bits(s, 1);
        if (b < 0) return -1;
        code |= b;
        int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count; first += count; first <<= 1; code <<= 1;
    }
    return -2;
}

static int construct(struct huff *h, const short *length, int n)
{
    short offs[MAXBITS + 1];
    for (int len = 0; len <= MAXBITS; len++) h->count[len] = 0;
    for (int sym = 0; sym < n; sym++) h->count[length[sym]]++;
    if (h->count[0] == n) return 0;
    int left = 1;
    for (int len = 1; len <= MAXBITS; len++) {
        left <<= 1; left -= h->count[len];
        if (left < 0) return left;
    }
    offs[1] = 0;
    for (int len = 1; len < MAXBITS; len++) offs[len + 1] = (short)(offs[len] + h->count[len]);
    for (int sym = 0; sym < n; sym++) if (length[sym]) h->symbol[offs[length[sym]]++] = (u16)sym;
    return left;
}

static int codes(struct st *s, const struct huff *lencode, const struct huff *distcode)
{
    static const short lens[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
    static const short lext[29] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
    static const short dists[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
    static const short dext[30] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };
    for (;;) {
        int sym = decode(s, lencode);
        if (sym < 0) return -1;
        if (sym < 256) {
            if (s->outcnt >= s->outcap) return -2;
            s->out[s->outcnt++] = (u8)sym;
        } else if (sym == 256) {
            return 0;
        } else {
            sym -= 257;
            if (sym >= 29) return -1;
            int e = bits(s, lext[sym]); if (e < 0) return -1;
            u32 len = (u32)(lens[sym] + e);
            sym = decode(s, distcode);
            if (sym < 0 || sym >= 30) return -1;
            e = bits(s, dext[sym]); if (e < 0) return -1;
            u32 dist = (u32)(dists[sym] + e);
            if (dist > s->outcnt) return -1;
            if (s->outcnt + len > s->outcap) return -2;
            for (u32 i = 0; i < len; i++) { s->out[s->outcnt] = s->out[s->outcnt - dist]; s->outcnt++; }
        }
    }
}

static int fixed(struct st *s)
{
    static struct huff lencode, distcode;
    static int built;
    if (!built) {
        short lengths[FIXLCODES];
        int sym;
        for (sym = 0; sym < 144; sym++) lengths[sym] = 8;
        for (; sym < 256; sym++) lengths[sym] = 9;
        for (; sym < 280; sym++) lengths[sym] = 7;
        for (; sym < FIXLCODES; sym++) lengths[sym] = 8;
        construct(&lencode, lengths, FIXLCODES);
        for (sym = 0; sym < MAXDCODES; sym++) lengths[sym] = 5;
        construct(&distcode, lengths, MAXDCODES);
        built = 1;
    }
    return codes(s, &lencode, &distcode);
}

static int dynamic(struct st *s)
{
    static const short order[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
    short lengths[MAXLCODES + MAXDCODES];
    struct huff lencode, distcode;
    int nlen = bits(s, 5), ndist = bits(s, 5), ncode = bits(s, 4);
    if (nlen < 0 || ndist < 0 || ncode < 0) return -1;
    nlen += 257; ndist += 1; ncode += 4;
    if (nlen > MAXLCODES || ndist > MAXDCODES) return -1;
    int index;
    for (index = 0; index < ncode; index++) { int b = bits(s, 3); if (b < 0) return -1; lengths[order[index]] = (short)b; }
    for (; index < 19; index++) lengths[order[index]] = 0;
    if (construct(&lencode, lengths, 19) != 0) return -1;
    index = 0;
    while (index < nlen + ndist) {
        int sym = decode(s, &lencode);
        if (sym < 0) return -1;
        if (sym < 16) { lengths[index++] = (short)sym; continue; }
        int len = 0, rep;
        if (sym == 16) {
            if (index == 0) return -1;
            len = lengths[index - 1];
            rep = bits(s, 2); if (rep < 0) return -1; rep += 3;
        } else if (sym == 17) { rep = bits(s, 3); if (rep < 0) return -1; rep += 3; }
        else { rep = bits(s, 7); if (rep < 0) return -1; rep += 11; }
        if (index + rep > nlen + ndist) return -1;
        while (rep--) lengths[index++] = (short)len;
    }
    if (lengths[256] == 0) return -1;
    int err = construct(&lencode, lengths, nlen);
    if (err && (err < 0 || nlen != lencode.count[0] + lencode.count[1])) return -1;
    err = construct(&distcode, lengths + nlen, ndist);
    if (err && (err < 0 || ndist != distcode.count[0] + distcode.count[1])) return -1;
    return codes(s, &lencode, &distcode);
}

/* Decodes a raw DEFLATE stream. Returns 0 and sets *used (input bytes consumed) / *outlen. */
int inflate_raw(const u8 *src, u32 srclen, u8 *dst, u32 dstcap, u32 *outlen, u32 *used)
{
    struct st s;
    s.in = src; s.inlen = srclen; s.incnt = 0; s.bitbuf = 0; s.bitcnt = 0;
    s.out = dst; s.outcap = dstcap; s.outcnt = 0;
    int last, err;
    do {
        last = bits(&s, 1);
        int type = bits(&s, 2);
        if (last < 0 || type < 0) return -1;
        if (type == 0) {
            s.bitbuf = 0; s.bitcnt = 0;
            if (s.incnt + 4 > s.inlen) return -1;
            u32 len = s.in[s.incnt] | (u32)s.in[s.incnt + 1] << 8;
            u32 nlen = s.in[s.incnt + 2] | (u32)s.in[s.incnt + 3] << 8;
            if (len != (~nlen & 0xFFFF)) return -1;
            s.incnt += 4;
            if (s.incnt + len > s.inlen) return -1;
            if (s.outcnt + len > s.outcap) return -2;
            kmemcpy(s.out + s.outcnt, s.in + s.incnt, len);
            s.incnt += len; s.outcnt += len;
            err = 0;
        } else if (type == 1) err = fixed(&s);
        else if (type == 2) err = dynamic(&s);
        else return -1;
        if (err) return err;
    } while (!last);
    *outlen = s.outcnt;
    if (used) *used = s.incnt;
    return 0;
}
