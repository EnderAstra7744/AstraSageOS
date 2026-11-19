/*
 * Small PNG decoder (8-bit grey / RGB / palette / grey+alpha / RGBA, not interlaced) for the neofetch logo.
 * Files in the in-RAM file system are at most 4096 bytes, so the pictures are tiny; the output is capped at 128x128.
 */
#include "astra.h"

int inflate_raw(const u8 *src, u32 srclen, u8 *dst, u32 dstcap, u32 *outlen, u32 *used);

#define PNG_MAXDIM 128
static u8 idat[4096 + 64];
static u8 raw[PNG_MAXDIM * (1 + PNG_MAXDIM * 4) + 64];

static u32 be32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }

static int paeth(int a, int b, int c)
{
    int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
    return (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
}

/* Decodes into px (0xAARRGGBB, w*h entries, at most cap). Returns 0 or a negative error. */
int png_decode_small(const u8 *d, u32 n, int *wo, int *ho, u32 *px, u32 cap)
{
    static const u8 sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    u32 w = 0, h = 0, il = 0, pos = 8, nid = 0;
    int depth = 0, ctype = -1;
    u8 pal[256 * 3], palA[256];
    int npal = 0;
    if (n < 40 || kmemcmp(d, sig, 8)) return -1;
    for (int i = 0; i < 256; i++) palA[i] = 255;
    while (pos + 12 <= n) {
        u32 len = be32(d + pos);
        const u8 *t = d + pos + 4, *body = d + pos + 8;
        if (len > n || pos + 12 + len > n) return -2;
        if (!kmemcmp(t, "IHDR", 4) && len >= 13) {
            w = be32(body); h = be32(body + 4); depth = body[8]; ctype = body[9]; il = body[12];
        } else if (!kmemcmp(t, "PLTE", 4)) {
            npal = (int)(len / 3) > 256 ? 256 : (int)(len / 3);
            for (int i = 0; i < npal * 3; i++) pal[i] = body[i];
        } else if (!kmemcmp(t, "tRNS", 4) && ctype == 3) {
            for (u32 i = 0; i < len && i < 256; i++) palA[i] = body[i];
        } else if (!kmemcmp(t, "IDAT", 4)) {
            if (nid + len > sizeof idat) return -3;
            kmemcpy(idat + nid, body, len); nid += len;
        } else if (!kmemcmp(t, "IEND", 4)) break;
        pos += 12 + len;
    }
    if (!w || !h || w > PNG_MAXDIM || h > PNG_MAXDIM || w * h > cap) return -4;
    if (il || (depth != 8 && !(ctype == 3 && depth <= 8)) ) return -5;
    int ch = ctype == 0 ? 1 : ctype == 2 ? 3 : ctype == 3 ? 1 : ctype == 4 ? 2 : ctype == 6 ? 4 : 0;
    if (!ch || nid < 7) return -6;
    u32 bpp_bits = (u32)ch * (u32)depth, stride = (w * bpp_bits + 7) / 8, got = 0;
    if (h * (stride + 1) > sizeof raw) return -7;
    if (inflate_raw(idat + 2, nid - 2, raw, sizeof raw, &got, 0) < 0 || got < h * (stride + 1)) return -8;
    u32 bpp = (bpp_bits + 7) / 8;
    for (u32 y = 0; y < h; y++) {                         /* undo the scanline filters in place */
        u8 *row = raw + y * (stride + 1) + 1, *up = y ? row - (stride + 1) : 0;
        int f = row[-1];
        for (u32 x = 0; x < stride; x++) {
            int a = x >= bpp ? row[x - bpp] : 0, b = up ? up[x] : 0, c = (up && x >= bpp) ? up[x - bpp] : 0;
            int v = row[x];
            switch (f) { case 1: v += a; break; case 2: v += b; break; case 3: v += (a + b) / 2; break; case 4: v += paeth(a, b, c); break; default: break; }
            row[x] = (u8)v;
        }
    }
    for (u32 y = 0; y < h; y++) {
        const u8 *row = raw + y * (stride + 1) + 1;
        for (u32 x = 0; x < w; x++) {
            u32 r, g, b, a = 255;
            if (ctype == 3) {
                u32 idx;
                if (depth == 8) idx = row[x];
                else { u32 bit = x * (u32)depth; idx = (row[bit >> 3] >> (8 - depth - (bit & 7))) & ((1u << depth) - 1); }
                if ((int)idx >= npal) idx = 0;
                r = pal[idx * 3]; g = pal[idx * 3 + 1]; b = pal[idx * 3 + 2]; a = palA[idx];
            } else if (ctype == 0) { r = g = b = row[x]; }
            else if (ctype == 4) { r = g = b = row[x * 2]; a = row[x * 2 + 1]; }
            else { r = row[x * ch]; g = row[x * ch + 1]; b = row[x * ch + 2]; if (ctype == 6) a = row[x * 4 + 3]; }
            px[y * w + x] = a << 24 | r << 16 | g << 8 | b;
        }
    }
    *wo = (int)w; *ho = (int)h;
    return 0;
}
