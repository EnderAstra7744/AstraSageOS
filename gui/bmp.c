/*
 * BMP decoder. Supported: uncompressed (BI_RGB) 24-bit, 32-bit and 8-bit palette images,
 * bottom-up or top-down, any row padding, and 32-bit BI_BITFIELDS in plain BGRA order.
 * Alpha is used only when the file really declares an alpha mask (V4/V5 header or BITFIELDS).
 * Everything else (RLE, 1/4/16-bit, OS/2 headers) is rejected with BMP_EFORMAT - never guessed.
 * All offsets and sizes from the file are validated before any pixel is read.
 */
#include "gui.h"

static u32 rd16(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8; }
static u32 rd32(const u8 *p) { return rd16(p) | rd16(p + 2) << 16; }

const char *bmp_strerror(int e)
{
    switch (e) {
    case BMP_ETRUNC:  return "file is truncated";
    case BMP_EMAGIC:  return "not a BMP file";
    case BMP_EFORMAT: return "unsupported BMP format (need uncompressed 8/24/32-bit)";
    case BMP_ESIZE:   return "image size out of range";
    default:          return "ok";
    }
}

int bmp_open(const u8 *d, u32 len, bmp_view *v)
{
    kmemset(v, 0, sizeof *v);
    if (!d || len < 26) return BMP_ETRUNC;
    if (d[0] != 'B' || d[1] != 'M') return BMP_EMAGIC;
    u32 off = rd32(d + 10), hdr = rd32(d + 14);
    if (hdr < 40 || hdr > 124) return BMP_EFORMAT;                 /* OS/2 core header and unknown */
    if (len < 14 + hdr) return BMP_ETRUNC;
    int w = (int)rd32(d + 18), h = (int)rd32(d + 22);
    u32 planes = rd16(d + 26), bpp = rd16(d + 28), comp = rd32(d + 30), clrused = rd32(d + 46);
    if (planes != 1) return BMP_EFORMAT;
    int topdown = h < 0;
    if (topdown) h = -h;
    if (w < 1 || h < 1 || w > 8192 || h > 8192) return BMP_ESIZE;
    int alpha = 0;
    if (bpp == 32) {
        if (comp == 3) {                                           /* BITFIELDS: only plain BGR(A) */
            if (hdr < 52 && len < 14 + 40 + 12) return BMP_ETRUNC;
            u32 mr = rd32(d + 54), mg = rd32(d + 58), mb = rd32(d + 62), ma = hdr >= 56 ? rd32(d + 66) : 0;
            if (len < 70) return BMP_ETRUNC;
            if (mr != 0x00FF0000u || mg != 0x0000FF00u || mb != 0x000000FFu) return BMP_EFORMAT;
            if (ma == 0xFF000000u) alpha = 1; else if (ma) return BMP_EFORMAT;
        } else if (comp == 0) {
            if (hdr >= 108 && rd32(d + 66) == 0xFF000000u) alpha = 1;
        } else return BMP_EFORMAT;
    } else if (bpp == 24 || bpp == 8) {
        if (comp != 0) return BMP_EFORMAT;
    } else return BMP_EFORMAT;

    u32 rowbytes = (((u32)w * bpp + 31) / 32) * 4;
    u32 need = rowbytes * (u32)h;
    if (off < 14 + hdr || off > len || need > len - off) return BMP_ETRUNC;
    if (bpp == 8) {
        u32 npal = clrused ? clrused : 256;
        if (npal > 256) return BMP_EFORMAT;
        u32 pal_off = 14 + hdr;
        if (comp == 3) pal_off += 0;
        if (pal_off + npal * 4 > off) return BMP_ETRUNC;
        v->pal = d + pal_off; v->npal = npal;
    }
    v->w = w; v->h = h; v->bpp = (int)bpp; v->topdown = topdown; v->has_alpha = alpha;
    v->rowbytes = rowbytes; v->pix = d + off;
    return BMP_OK;
}

u32 bmp_get(const bmp_view *v, int x, int y)
{
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= v->w) x = v->w - 1;
    if (y >= v->h) y = v->h - 1;
    int row = v->topdown ? y : v->h - 1 - y;
    const u8 *p = v->pix + (u32)row * v->rowbytes;
    if (v->bpp == 24) { p += x * 3; return 0xFF000000u | (u32)p[2] << 16 | (u32)p[1] << 8 | p[0]; }
    if (v->bpp == 32) { p += x * 4; return (v->has_alpha ? (u32)p[3] << 24 : 0xFF000000u) | (u32)p[2] << 16 | (u32)p[1] << 8 | p[0]; }
    u32 i = p[x];
    if (i >= v->npal) i = 0;
    const u8 *c = v->pal + i * 4;
    return 0xFF000000u | (u32)c[2] << 16 | (u32)c[1] << 8 | c[0];
}
