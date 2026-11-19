/*
 * zlib-compatible inflate (deflate / zlib / gzip) for native programs. inflate() decodes the whole input on the
 * first call into an internal buffer and hands it out in the chunks the caller asks for. gzopen reads plain or gzip.
 */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <zlib.h>

typedef struct { const unsigned char *in; size_t len, pos; unsigned bitbuf; int bitcnt; unsigned char *out; size_t olen, ocap; int err; } inf;
typedef struct { unsigned short count[16]; unsigned short sym[288]; } huff;

static int getbits(inf *s, int n)
{
    int v = s->bitbuf;
    while (s->bitcnt < n) {
        if (s->pos >= s->len) { s->err = 1; return 0; }
        v |= (int)s->in[s->pos++] << s->bitcnt;
        s->bitcnt += 8;
        s->bitbuf = (unsigned)v;
    }
    s->bitbuf = (unsigned)v >> n;
    s->bitcnt -= n;
    return v & ((1 << n) - 1);
}
static int put1(inf *s, int c)
{
    if (s->olen >= s->ocap) {
        size_t nc = s->ocap ? s->ocap * 2 : 4096;
        unsigned char *p = realloc(s->out, nc);
        if (!p) { s->err = 2; return -1; }
        s->out = p; s->ocap = nc;
    }
    s->out[s->olen++] = (unsigned char)c;
    return 0;
}
static int build(huff *h, const short *len, int n)
{
    int i, left = 1; unsigned short offs[16];
    for (i = 0; i < 16; i++) h->count[i] = 0;
    for (i = 0; i < n; i++) h->count[len[i]]++;
    if (h->count[0] == n) return 0;
    for (i = 1; i < 16; i++) { left <<= 1; left -= h->count[i]; if (left < 0) return left; }
    offs[1] = 0;
    for (i = 1; i < 15; i++) offs[i + 1] = (unsigned short)(offs[i] + h->count[i]);
    for (i = 0; i < n; i++) if (len[i]) h->sym[offs[len[i]]++] = (unsigned short)i;
    return left;
}
static int decode(inf *s, const huff *h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
        code |= getbits(s, 1);
        if (s->err) return -1;
        int count = h->count[len];
        if (code - count < first) return h->sym[index + (code - first)];
        index += count; first += count; first <<= 1; code <<= 1;
    }
    return -1;
}
static const short lbase[] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
static const short lext[] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
static const short dbase[] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
static const short dext[] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };
static int codes(inf *s, const huff *lc, const huff *dc)
{
    for (;;) {
        int sym = decode(s, lc);
        if (sym < 0) return -1;
        if (sym < 256) { if (put1(s, sym)) return -1; }
        else if (sym == 256) return 0;
        else {
            sym -= 257;
            if (sym >= 29) return -1;
            int len = lbase[sym] + getbits(s, lext[sym]);
            int ds = decode(s, dc);
            if (ds < 0 || ds >= 30) return -1;
            size_t dist = (size_t)(dbase[ds] + getbits(s, dext[ds]));
            if (s->err || dist > s->olen) return -1;
            while (len--) if (put1(s, s->out[s->olen - dist])) return -1;
        }
    }
}
static int inflate_raw(inf *s)
{
    int last;
    static huff lc, dc;
    do {
        last = getbits(s, 1);
        int type = getbits(s, 2);
        if (s->err) return -1;
        if (type == 0) {
            s->bitbuf = 0; s->bitcnt = 0;
            if (s->pos + 4 > s->len) return -1;
            unsigned n = s->in[s->pos] | (unsigned)s->in[s->pos + 1] << 8;
            s->pos += 4;
            if (s->pos + n > s->len) return -1;
            while (n--) if (put1(s, s->in[s->pos++])) return -1;
        } else if (type == 1) {
            short l[320]; int i;
            for (i = 0; i < 144; i++) l[i] = 8;
            for (; i < 256; i++) l[i] = 9;
            for (; i < 280; i++) l[i] = 7;
            for (; i < 288; i++) l[i] = 8;
            build(&lc, l, 288);
            for (i = 0; i < 30; i++) l[i] = 5;
            build(&dc, l, 30);
            if (codes(s, &lc, &dc)) return -1;
        } else if (type == 2) {
            static const short order[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
            short l[320]; int i;
            int nlen = getbits(s, 5) + 257, ndist = getbits(s, 5) + 1, ncode = getbits(s, 4) + 4;
            if (s->err || nlen > 286 || ndist > 30) return -1;
            for (i = 0; i < 19; i++) l[i] = 0;
            for (i = 0; i < ncode; i++) l[order[i]] = (short)getbits(s, 3);
            if (build(&lc, l, 19) != 0) return -1;
            int idx = 0;
            while (idx < nlen + ndist) {
                int sym = decode(s, &lc);
                if (sym < 0) return -1;
                if (sym < 16) l[idx++] = (short)sym;
                else {
                    int prev = 0, rep;
                    if (sym == 16) { if (!idx) return -1; prev = l[idx - 1]; rep = 3 + getbits(s, 2); }
                    else if (sym == 17) rep = 3 + getbits(s, 3);
                    else rep = 11 + getbits(s, 7);
                    if (idx + rep > nlen + ndist) return -1;
                    while (rep--) l[idx++] = (short)prev;
                }
            }
            if (l[256] == 0) return -1;
            build(&lc, l, nlen);
            build(&dc, l + nlen, ndist);
            if (codes(s, &lc, &dc)) return -1;
        } else return -1;
    } while (!last);
    return s->err ? -1 : 0;
}

/* decodes gzip, zlib or raw deflate data; returns malloc'ed buffer or 0 */
static unsigned char *unpack(const unsigned char *d, size_t n, size_t *olen)
{
    inf s; memset(&s, 0, sizeof s);
    s.in = d; s.len = n;
    if (n >= 18 && d[0] == 0x1f && d[1] == 0x8b) {
        size_t p = 10; int flg = d[3];
        if (flg & 4) { p += 2 + (d[p] | d[p + 1] << 8); }
        if (flg & 8) { while (p < n && d[p]) p++; p++; }
        if (flg & 16) { while (p < n && d[p]) p++; p++; }
        if (flg & 2) p += 2;
        s.pos = p;
    } else if (n >= 2 && (d[0] & 0x0f) == 8 && ((d[0] << 8) | d[1]) % 31 == 0) s.pos = 2;
    if (inflate_raw(&s) < 0) { free(s.out); return 0; }
    *olen = s.olen;
    if (!s.out) s.out = malloc(1);
    return s.out;
}

typedef struct { unsigned char *buf; size_t len, pos; } zst;
int inflateInit2(z_stream *s, int wb) { (void)wb; s->state = calloc(1, sizeof(zst)); s->total_in = s->total_out = 0; return s->state ? Z_OK : Z_MEM_ERROR; }
int inflateInit(z_stream *s) { return inflateInit2(s, MAX_WBITS); }
int inflate(z_stream *s, int flush)
{
    (void)flush;
    zst *z = s->state;
    if (!z) return Z_STREAM_ERROR;
    if (!z->buf) {
        z->buf = unpack(s->next_in, s->avail_in, &z->len);
        if (!z->buf) return Z_DATA_ERROR;
        s->total_in += s->avail_in; s->avail_in = 0;
    }
    size_t n = z->len - z->pos;
    if (n > s->avail_out) n = s->avail_out;
    memcpy(s->next_out, z->buf + z->pos, n);
    z->pos += n; s->next_out += n; s->avail_out -= (uInt)n; s->total_out += n;
    return z->pos >= z->len ? Z_STREAM_END : Z_OK;
}
int inflateEnd(z_stream *s) { zst *z = s->state; if (z) { free(z->buf); free(z); } s->state = 0; return Z_OK; }
int uncompress(Bytef *dest, uLong *dl, const Bytef *src, uLong sl)
{
    size_t n; unsigned char *o = unpack(src, sl, &n);
    if (!o) return Z_DATA_ERROR;
    if (n > *dl) { free(o); return Z_BUF_ERROR; }
    memcpy(dest, o, n); *dl = n; free(o);
    return Z_OK;
}

struct _AS_GZ { unsigned char *buf; size_t len, pos; };
gzFile gzopen(const char *path, const char *mode)
{
    (void)mode;
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *raw = malloc((size_t)n + 1);
    if (!raw) { fclose(f); return 0; }
    n = (long)fread(raw, 1, (size_t)n, f); fclose(f);
    struct _AS_GZ *g = calloc(1, sizeof *g);
    if (!g) { free(raw); return 0; }
    if (n >= 2 && raw[0] == 0x1f && raw[1] == 0x8b) { g->buf = unpack(raw, (size_t)n, &g->len); free(raw); if (!g->buf) { free(g); return 0; } }
    else { g->buf = raw; g->len = (size_t)n; }
    return g;
}
int gzread(gzFile g, void *b, unsigned n) { size_t r = g->len - g->pos; if (r > n) r = n; memcpy(b, g->buf + g->pos, r); g->pos += r; return (int)r; }
char *gzgets(gzFile g, char *b, int n)
{
    int i = 0;
    while (i < n - 1 && g->pos < g->len) { char c = (char)g->buf[g->pos++]; b[i++] = c; if (c == '\n') break; }
    if (!i) return 0;
    b[i] = 0;
    return b;
}
int gzclose(gzFile g) { if (g) { free(g->buf); free(g); } return 0; }
