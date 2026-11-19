/*
 * Software 2D graphics for the VBE framebuffer: everything is drawn into a 0xAARRGGBB back buffer
 * in RAM and copied to the screen by gfx_present() - only the damaged rectangle, so there is no
 * flicker. No hardware acceleration is used or claimed.
 */
#include "gui.h"

/* word fill / copy: x86 string instructions where available, plain loops elsewhere */
#if defined(__i386__) || defined(__x86_64__)
#define FILL32(p, n, c) do { size_t n_ = (size_t)(unsigned)(n); __asm__ volatile("rep stosl" : "+D"(p), "+c"(n_) : "a"(c) : "memory"); } while (0)
#define COPY32(d, s, n) do { size_t n_ = (size_t)(unsigned)(n); __asm__ volatile("rep movsl" : "+D"(d), "+S"(s), "+c"(n_) : : "memory"); } while (0)
#else
#define FILL32(p, n, c) do { u32 *q_ = (p); for (int i_ = 0; i_ < (n); i_++) q_[i_] = (c); } while (0)
#define COPY32(d, s, n) do { u32 *q_ = (d); const u32 *r_ = (s); for (int i_ = 0; i_ < (n); i_++) q_[i_] = r_[i_]; } while (0)
#endif

#include "font8x16.h"

static video_info V;
static gfx_surf back_s;
static u32 back_px[GFX_MAXW * GFX_MAXH];
static gfx_surf *tgt = &back_s;
static gfx_rect clip;
static int cur_x, cur_y, cur_vis;

/* ------------------------------------------------------------------ helpers */

gfx_rect gfx_rect_make(int x, int y, int w, int h) { gfx_rect r = { x, y, w, h }; return r; }
int gfx_rect_empty(const gfx_rect *r) { return r->w <= 0 || r->h <= 0; }
gfx_rect gfx_rect_isect(gfx_rect a, gfx_rect b)
{
    int x0 = a.x > b.x ? a.x : b.x, y0 = a.y > b.y ? a.y : b.y;
    int x1 = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w;
    int y1 = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h;
    gfx_rect r = { x0, y0, x1 > x0 ? x1 - x0 : 0, y1 > y0 ? y1 - y0 : 0 };
    return r;
}
gfx_rect gfx_rect_union(gfx_rect a, gfx_rect b)
{
    if (gfx_rect_empty(&a)) return b;
    if (gfx_rect_empty(&b)) return a;
    int x0 = a.x < b.x ? a.x : b.x, y0 = a.y < b.y ? a.y : b.y;
    int x1 = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w;
    int y1 = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
    gfx_rect r = { x0, y0, x1 - x0, y1 - y0 };
    return r;
}
int gfx_rect_has(const gfx_rect *r, int x, int y) { return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h; }

static u32 isqrt32(u32 n)
{
    u32 r = 0, bit = 1u << 30;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= r + bit) { n -= r + bit; r = (r >> 1) + bit; } else r >>= 1;
        bit >>= 2;
    }
    return r;
}

static inline u32 div255(u32 x) { x += 128; return (x + (x >> 8)) >> 8; }

static int tgt_alpha;                               /* 1: the target has its own alpha (sprite), blend "over" it */

void gfx_set_target_alpha(int on) { tgt_alpha = on; }

static u32 blend_over(u32 d, u32 s, u32 a)          /* Porter-Duff "over" onto a pixel that may be transparent */
{
    u32 da = d >> 24;
    if (da == 255) {
        u32 ia = 255 - a;
        return 0xFF000000u | div255((s >> 16 & 255) * a + (d >> 16 & 255) * ia) << 16
             | div255((s >> 8 & 255) * a + (d >> 8 & 255) * ia) << 8 | div255((s & 255) * a + (d & 255) * ia);
    }
    u32 oa = a + div255(da * (255 - a));
    if (!oa) return 0;
    u32 wd = div255(da * (255 - a));                 /* weight of the old colour, 0..255 */
    u32 den = oa;
    u32 r = ((s >> 16 & 255) * a + (d >> 16 & 255) * wd + den / 2) / den;
    u32 g = ((s >> 8 & 255) * a + (d >> 8 & 255) * wd + den / 2) / den;
    u32 b = ((s & 255) * a + (d & 255) * wd + den / 2) / den;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return oa << 24 | r << 16 | g << 8 | b;
}

static inline u32 blend(u32 d, u32 s, u32 a)       /* s: 0x00RRGGBB, a: 0..255 over opaque d */
{
    if (tgt_alpha) return blend_over(d, s, a);
    /* two channels at once (red+blue, then green); alpha 0..255 becomes 0..256 so 0 and 255 are exact */
    u32 a2 = a + (a >> 7), ia = 256 - a2;
    u32 rb = (((d & 0x00FF00FFu) * ia + (s & 0x00FF00FFu) * a2) >> 8) & 0x00FF00FFu;
    u32 g = (((d & 0x0000FF00u) * ia + (s & 0x0000FF00u) * a2) >> 8) & 0x0000FF00u;
    return 0xFF000000u | rb | g;
}

static void clip_to_target(void)
{
    gfx_rect all = { 0, 0, tgt->w, tgt->h };
    clip = gfx_rect_isect(clip, all);
}

int gfx_init(const video_info *vi)
{
    if (!vi || vi->w > GFX_MAXW || vi->h > GFX_MAXH || vi->w < 320 || vi->h < 200) return 0;
    V = *vi;
    back_s.w = (int)vi->w; back_s.h = (int)vi->h; back_s.stride = (int)vi->w; back_s.px = back_px;
    tgt = &back_s;
    clip = gfx_rect_make(0, 0, back_s.w, back_s.h);
    cur_vis = 0;
    return 1;
}
const gfx_surf *gfx_back(void) { return &back_s; }
int gfx_width(void) { return back_s.w; }
int gfx_height(void) { return back_s.h; }

void gfx_set_target(gfx_surf *s)
{
    tgt = s ? s : &back_s;
    clip = gfx_rect_make(0, 0, tgt->w, tgt->h);
}
gfx_rect gfx_get_clip(void) { return clip; }

void gfx_set_clip(const gfx_rect *r)
{
    clip = r ? *r : gfx_rect_make(0, 0, tgt->w, tgt->h);
    clip_to_target();
}

/* ------------------------------------------------------------------ primitives */

void gfx_pixel(int x, int y, u32 color)
{
    if (!gfx_rect_has(&clip, x, y)) return;
    u32 *p = &tgt->px[y * tgt->stride + x];
    u32 a = color >> 24;
    *p = a == 255 ? color : a == 0 ? *p : blend(*p, color, a);
}

void gfx_fill_rect(int x, int y, int w, int h, u32 color)
{
    gfx_rect r = gfx_rect_isect(gfx_rect_make(x, y, w, h), clip);
    u32 a = color >> 24;
    if (gfx_rect_empty(&r) || a == 0) return;
    if (a == 255) {                                                  /* plain fill: one string instruction per row */
        for (int yy = r.y; yy < r.y + r.h; yy++) {
            u32 *p = &tgt->px[yy * tgt->stride + r.x];
            int n = r.w;
            FILL32(p, n, color);
        }
        return;
    }
    if (tgt_alpha) {
        for (int yy = r.y; yy < r.y + r.h; yy++) {
            u32 *p = &tgt->px[yy * tgt->stride + r.x];
            for (int i = 0; i < r.w; i++) p[i] = blend(p[i], color, a);
        }
        return;
    }
    u32 a2 = a + (a >> 7), ia = 256 - a2;                            /* the colour's share is the same for every pixel */
    u32 srb = (color & 0x00FF00FFu) * a2, sg = (color & 0x0000FF00u) * a2;
    for (int yy = r.y; yy < r.y + r.h; yy++) {
        u32 *p = &tgt->px[yy * tgt->stride + r.x];
        for (int i = 0; i < r.w; i++) {
            u32 d = p[i];
            p[i] = 0xFF000000u | (((d & 0x00FF00FFu) * ia + srb) >> 8 & 0x00FF00FFu) | (((d & 0x0000FF00u) * ia + sg) >> 8 & 0x0000FF00u);
        }
    }
}

void gfx_clear(u32 color) { gfx_fill_rect(clip.x, clip.y, clip.w, clip.h, color | 0xFF000000u); }

void gfx_line(int x0, int y0, int x1, int y1, u32 color)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;     /* dy <= 0 */
    int err = dx + dy;
    for (;;) {
        gfx_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void gfx_rect_outline(int x, int y, int w, int h, u32 color)
{
    if (w <= 0 || h <= 0) return;
    gfx_fill_rect(x, y, w, 1, color);
    gfx_fill_rect(x, y + h - 1, w, 1, color);
    gfx_fill_rect(x, y + 1, 1, h - 2, color);
    gfx_fill_rect(x + w - 1, y + 1, 1, h - 2, color);
}

/* Coverage 0..255 of a pixel at distance (dx,dy) 1/16 px units from a corner circle's centre. */
static u32 corner_cov(int dx16, int dy16, int r16)
{
    u32 d = isqrt32((u32)(dx16 * dx16 + dy16 * dy16));          /* 1/16 px */
    int c = r16 - (int)d + 8;                                   /* 0.5 px AA band centred on the edge */
    if (c <= 0) return 0;
    if (c >= 16) return 255;
    return (u32)c * 255 / 16;
}

static u32 with_cov(u32 color, u32 cov)
{
    u32 a = (color >> 24) * cov / 255;
    return (color & 0x00FFFFFFu) | a << 24;
}

void gfx_round_rect_ex(int x, int y, int w, int h, int rtl, int rtr, int rbr, int rbl, u32 color)
{
    if (w <= 0 || h <= 0) return;
    int mr = (w < h ? w : h) / 2;
    int *rad[4] = { &rtl, &rtr, &rbr, &rbl };
    for (int i = 0; i < 4; i++) {
        if (*rad[i] > mr) *rad[i] = mr;
        if (*rad[i] < 0) *rad[i] = 0;
    }
    gfx_rect r = gfx_rect_isect(gfx_rect_make(x, y, w, h), clip);
    if (gfx_rect_empty(&r)) return;
    for (int yy = r.y; yy < r.y + r.h; yy++) {
        int rl = 0, rr = 0, cyl = 0, cyr = 0;
        if (yy - y < rtl) { rl = rtl; cyl = y + rtl; } else if (y + h - 1 - yy < rbl) { rl = rbl; cyl = y + h - rbl; }
        if (yy - y < rtr) { rr = rtr; cyr = y + rtr; } else if (y + h - 1 - yy < rbr) { rr = rbr; cyr = y + h - rbr; }
        int xs = x + rl, xe = x + w - rr;                          /* solid middle [xs, xe) */
        gfx_fill_rect(xs, yy, xe - xs, 1, color);
        for (int xx = x; xx < xs; xx++) {                          /* left corner pixels */
            if (xx < r.x || xx >= r.x + r.w) continue;
            /* pixel centre vs circle centre (x+rl, cyl), 1/16 px units */
            u32 c = corner_cov((xx * 16 + 8) - (x + rl) * 16, (yy * 16 + 8) - cyl * 16, rl * 16);
            if (c) gfx_pixel(xx, yy, with_cov(color, c));
        }
        for (int xx = xe; xx < x + w; xx++) {
            if (xx < r.x || xx >= r.x + r.w) continue;
            u32 c = corner_cov((xx * 16 + 8) - xe * 16, (yy * 16 + 8) - cyr * 16, rr * 16);
            if (c) gfx_pixel(xx, yy, with_cov(color, c));
        }
    }
}

void gfx_round_rect(int x, int y, int w, int h, int r, u32 color) { gfx_round_rect_ex(x, y, w, h, r, r, r, r, color); }

/* Ring of 1 px just inside the rounded rect: outer shape minus the shape shrunk by one. */
void gfx_round_outline(int x, int y, int w, int h, int r, u32 color)
{
    if (w < 3 || h < 3) return;
    int mr = (w < h ? w : h) / 2;
    if (r > mr) r = mr;
    gfx_rect b = gfx_rect_isect(gfx_rect_make(x, y, w, h), clip);
    for (int yy = b.y; yy < b.y + b.h; yy++)
        for (int xx = b.x; xx < b.x + b.w; xx++) {
            int in_edge = (xx == x || yy == y || xx == x + w - 1 || yy == y + h - 1);
            int cxp = xx < x + r ? x + r : xx >= x + w - r ? x + w - r : -1;
            int cyp = yy < y + r ? y + r : yy >= y + h - r ? y + h - r : -1;
            u32 cov;
            if (cxp >= 0 && cyp >= 0) {
                u32 d = isqrt32((u32)((xx * 16 + 8 - cxp * 16) * (xx * 16 + 8 - cxp * 16) + (yy * 16 + 8 - cyp * 16) * (yy * 16 + 8 - cyp * 16)));
                int outer = r * 16 - (int)d + 8, inner = (r - 1) * 16 - (int)d + 8;     /* signed, 16 = 1px */
                int c = (outer > 16 ? 16 : outer) - (inner > 0 ? (inner > 16 ? 16 : inner) : 0);
                if (c <= 0) continue;
                cov = (u32)c * 255 / 16;
            } else if (in_edge) cov = 255; else continue;
            gfx_pixel(xx, yy, with_cov(color, cov));
        }
}

void gfx_circle(int cx, int cy, int r, u32 color)
{
    gfx_round_rect(cx - r, cy - r, 2 * r, 2 * r, r, color);
}

/* Soft shadow: alpha falls off with the distance to the rounded rectangle. */
void gfx_shadow(int x, int y, int w, int h, int r, int spread, int alpha)
{
    gfx_rect band = gfx_rect_isect(gfx_rect_make(x - spread, y - spread, w + 2 * spread, h + 2 * spread), clip);
    if (gfx_rect_empty(&band) || spread <= 0) return;
    int sp16 = spread * 16;
    static u8 lut[16 * 64 + 1];                           /* alpha for a distance outside the box (1/16 px) */
    if (sp16 > 16 * 64) return;
    for (int d = 0; d < sp16; d++) {
        u32 t = (u32)(sp16 - d);
        lut[d] = (u8)((u32)alpha * t * t / ((u32)sp16 * (u32)sp16));
    }
    int hw = w * 8, hh = h * 8;                           /* half sizes in 1/16 px */
    int cxm = x * 16 + hw, cym = y * 16 + hh;
    int kx = hw - r * 16, ky = hh - r * 16, r16 = r * 16;
    /* only the ring around the box is visited (the window covers its inside), and only the four corner arcs need a
       square root: along the straight edges the distance is just the distance to the edge */
    for (int yy = band.y; yy < band.y + band.h; yy++) {
        int dy = yy * 16 + 8 - cym; if (dy < 0) dy = -dy;
        int qy = dy - ky;
        u32 *row = &tgt->px[yy * tgt->stride];
        int inside_row = yy >= y && yy < y + h;
        int xa = band.x, xb = band.x + band.w;
        for (int seg = 0; seg < 2; seg++) {
            int s0 = xa, s1 = xb;
            if (inside_row) { if (seg == 0) s1 = x < xb ? x : xb; else s0 = x + w > xa ? x + w : xa; }
            else if (seg == 1) break;
            for (int xx = s0; xx < s1; xx++) {
                int dx = xx * 16 + 8 - cxm; if (dx < 0) dx = -dx;
                int qx = dx - kx;
                int d;
                if (qx > 0 && qy > 0) d = (int)isqrt32((u32)(qx * qx + qy * qy)) - r16;
                else d = (qx > qy ? qx : qy) - r16;
                if (d <= 0 || d >= sp16) continue;
                u32 a = lut[d];
                if (a) row[xx] = blend(row[xx], 0, a);
            }
        }
    }
}

/* ------------------------------------------------------------------ text */

/* Text is UTF-8: ASCII plus the Turkish letters (and bullet / ellipsis) that font8x16.h carries; anything else
 * is drawn as '?'. */
static u32 utf8_next(const char **ps)
{
    const unsigned char *p = (const unsigned char *)*ps;
    u32 c = *p++;
    if (c >= 0xC2 && c < 0xE0 && (p[0] & 0xC0) == 0x80) { c = (c & 0x1F) << 6 | (p[0] & 0x3F); p++; }
    else if (c >= 0xE0 && c < 0xF0 && (p[0] & 0xC0) == 0x80 && (p[1] & 0xC0) == 0x80) { c = (c & 0x0F) << 12 | (u32)(p[0] & 0x3F) << 6 | (p[1] & 0x3F); p += 2; }
    else if (c >= 0x80) c = '?';
    *ps = (const char *)p;
    return c;
}

static const unsigned char *glyph_cp(u32 cp)
{
    static const unsigned char blank[16];
    if (cp >= AS_FONT_FIRST && cp < AS_FONT_FIRST + AS_FONT_COUNT) return &as_font8x16[(cp - AS_FONT_FIRST) * 16];
    if (cp < 32) return blank;
    for (int i = 0; i < AS_FONT_NEXTRA; i++) if (as_font_extra_cp[i] == cp) return &as_font_extra[i * 16];
    return &as_font8x16[('?' - AS_FONT_FIRST) * 16];
}

/* Chinese characters are two cells wide: the 32-byte glyph is 16 rows for the left cell, then 16 for the right. */
static const unsigned char *wide_glyph(u32 cp)
{
    if (cp < 0x2E80 || !AS_FONT_NWIDE) return 0;
    int lo = 0, hi = AS_FONT_NWIDE - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if (as_font_wide_cp[m] == cp) return &as_font_wide[m * 32];
        if (as_font_wide_cp[m] < cp) lo = m + 1; else hi = m - 1;
    }
    return 0;
}

int gfx_text_w(const char *s)
{
    int w = 0;
    while (*s) { u32 cp = utf8_next(&s); w += wide_glyph(cp) ? 16 : 8; }
    return w;
}

const unsigned char *gfx_font_glyph(unsigned char ch)
{
    static const unsigned char blank[16];
    if (ch >= AS_FONT_FIRST && ch < AS_FONT_FIRST + AS_FONT_COUNT) return &as_font8x16[(ch - AS_FONT_FIRST) * 16];
    if (ch > 126) return &as_font8x16[('?' - AS_FONT_FIRST) * 16];
    return blank;
}

static void glyph_draw(int x, int y, const unsigned char *g, u32 fg, u32 bg)
{
    gfx_rect cell = gfx_rect_isect(gfx_rect_make(x, y, 8, 16), clip);
    if (gfx_rect_empty(&cell)) return;
    if ((bg >> 24) == 0 && (fg >> 24) == 255 && !tgt_alpha) {          /* the usual case: opaque ink on whatever is there */
        if (!g) return;
        for (int row = cell.y; row < cell.y + cell.h; row++) {
            u32 bits = g[row - y];
            if (!bits) continue;
            u32 *p = &tgt->px[row * tgt->stride];
            for (int col = cell.x; col < cell.x + cell.w; col++)
                if ((bits >> (7 - (col - x))) & 1) p[col] = fg;
        }
        return;
    }
    for (int row = cell.y; row < cell.y + cell.h; row++) {
        u32 bits = g ? g[row - y] : 0;
        u32 *p = &tgt->px[row * tgt->stride];
        for (int col = cell.x; col < cell.x + cell.w; col++) {
            int on = (bits >> (7 - (col - x))) & 1;
            u32 c = on ? fg : bg;
            u32 a = c >> 24;
            if (a == 255) p[col] = c;
            else if (a) p[col] = blend(p[col], c, a);
        }
    }
}

int gfx_is_wide(u32 cp) { return wide_glyph(cp) != 0; }

/* one terminal cell by Unicode value; bit 31 = right half of a wide character */
void gfx_glyph_cp(int x, int y, u32 cp, u32 fg, u32 bg)
{
    int right = (int)(cp >> 31); cp &= 0x7FFFFFFFu;
    const unsigned char *w = wide_glyph(cp);
    glyph_draw(x, y, w ? w + (right ? 16 : 0) : glyph_cp(cp), fg, bg);
}

void gfx_glyph(int x, int y, unsigned char ch, u32 fg, u32 bg)
{
    const unsigned char *g = 0;
    if (ch >= AS_FONT_FIRST && ch < AS_FONT_FIRST + AS_FONT_COUNT) g = &as_font8x16[(ch - AS_FONT_FIRST) * 16];
    else if (ch > 126) g = &as_font8x16[('?' - AS_FONT_FIRST) * 16];
    glyph_draw(x, y, g, fg, bg);
}

void gfx_text(int x, int y, const char *s, u32 color)
{
    while (*s) {
        u32 cp = utf8_next(&s);
        const unsigned char *w = wide_glyph(cp);
        if (w) { glyph_draw(x, y, w, color, 0); glyph_draw(x + 8, y, w + 16, color, 0); x += 16; }
        else { glyph_draw(x, y, glyph_cp(cp), color, 0); x += 8; }
    }
}

int gfx_text_w_scaled(const char *s, int scale) { return gfx_text_w(s) * scale; }

void gfx_text_scaled(int x, int y, const char *s, u32 color, int scale)
{
    if (scale <= 1) { gfx_text(x, y, s, color); return; }
    while (*s) {
        u32 cp = utf8_next(&s);
        const unsigned char *w = wide_glyph(cp);
        for (int half = 0; half < (w ? 2 : 1); half++) {
            const unsigned char *g = w ? w + half * 16 : glyph_cp(cp);
            for (int gy = 0; gy < 16; gy++)
                for (int gx = 0; gx < 8; gx++)
                    if (g[gy] >> (7 - gx) & 1) gfx_fill_rect(x + gx * scale, y + gy * scale, scale, scale, color);
            x += 8 * scale;
        }
    }
}

/* ------------------------------------------------------------------ images */

void gfx_blit_alpha(const gfx_surf *src, int x, int y, int alpha)
{
    gfx_rect r = gfx_rect_isect(gfx_rect_make(x, y, src->w, src->h), clip);
    if (gfx_rect_empty(&r) || alpha <= 0) return;
    for (int yy = r.y; yy < r.y + r.h; yy++) {
        const u32 *s = &src->px[(yy - y) * src->stride + (r.x - x)];
        u32 *d = &tgt->px[yy * tgt->stride + r.x];
        for (int i = 0; i < r.w; i++) {
            u32 a = s[i] >> 24;
            if (alpha < 255) a = div255(a * (u32)alpha);
            if (a == 255) d[i] = s[i] | 0xFF000000u;
            else if (a) d[i] = blend(d[i], s[i], a);
        }
    }
}
void gfx_blit(const gfx_surf *src, int x, int y) { gfx_blit_alpha(src, x, y, 255); }

/*
 * Blit of a sprite whose rows are mostly opaque: rx0/rx1 give, per row, the longest run of fully opaque pixels
 * (see gfx_opaque_runs). That run is a plain row copy, only the soft edges (shadow, round corners) are blended.
 */
void gfx_opaque_runs(const gfx_surf *src, short *rx0, short *rx1)
{
    for (int y = 0; y < src->h; y++) {
        const u32 *s = &src->px[y * src->stride];
        int best0 = 0, best1 = 0, run0 = -1;
        for (int x = 0; x <= src->w; x++) {
            int op = x < src->w && (s[x] >> 24) == 255;
            if (op && run0 < 0) run0 = x;
            if (!op && run0 >= 0) { if (x - run0 > best1 - best0) { best0 = run0; best1 = x; } run0 = -1; }
        }
        rx0[y] = (short)best0; rx1[y] = (short)best1;
    }
}

void gfx_blit_runs(const gfx_surf *src, int x, int y, const short *rx0, const short *rx1)
{
    gfx_rect r = gfx_rect_isect(gfx_rect_make(x, y, src->w, src->h), clip);
    if (gfx_rect_empty(&r)) return;
    for (int yy = r.y; yy < r.y + r.h; yy++) {
        int sy = yy - y;
        const u32 *s = &src->px[sy * src->stride + (r.x - x)];
        u32 *d = &tgt->px[yy * tgt->stride + r.x];
        int c0 = rx0[sy] - (r.x - x), c1 = rx1[sy] - (r.x - x);          /* the opaque run in destination-row coordinates */
        if (c0 < 0) c0 = 0;
        if (c1 > r.w) c1 = r.w;
        if (c1 < c0) c1 = c0 = 0;
        for (int i = 0; i < c0; i++) { u32 a = s[i] >> 24; if (a == 255) d[i] = s[i]; else if (a) d[i] = blend(d[i], s[i], a); }
        if (c1 > c0) { const u32 *sp = s + c0; u32 *dp = d + c0; int n = c1 - c0; COPY32(dp, sp, n); }
        for (int i = c1 > c0 ? c1 : c0; i < r.w; i++) { u32 a = s[i] >> 24; if (a == 255) d[i] = s[i]; else if (a) d[i] = blend(d[i], s[i], a); }
    }
}

/* Source scaled (nearest) into w x h at x,y with an extra 0..255 opacity: used by the dock animation. */
void gfx_blit_scaled(const gfx_surf *src, int x, int y, int w, int h, int alpha)
{
    if (w <= 0 || h <= 0 || alpha <= 0) return;
    gfx_rect r = gfx_rect_isect(gfx_rect_make(x, y, w, h), clip);
    if (gfx_rect_empty(&r)) return;
    for (int yy = r.y; yy < r.y + r.h; yy++) {
        int sy = (yy - y) * src->h / h;
        const u32 *srow = &src->px[sy * src->stride];
        u32 *d = &tgt->px[yy * tgt->stride];
        for (int xx = r.x; xx < r.x + r.w; xx++) {
            u32 c = srow[(xx - x) * src->w / w];
            u32 a = c >> 24;
            if (alpha < 255) a = div255(a * (u32)alpha);
            if (a == 255) d[xx] = c | 0xFF000000u;
            else if (a) d[xx] = blend(d[xx], c, a);
        }
    }
}

/* Copy of a surface whose pixels are all opaque (the wallpaper): plain row copies, no per-pixel work. */
void gfx_blit_opaque(const gfx_surf *src, int x, int y)
{
    gfx_rect r = gfx_rect_isect(gfx_rect_make(x, y, src->w, src->h), clip);
    if (gfx_rect_empty(&r)) return;
    for (int yy = r.y; yy < r.y + r.h; yy++) {
        const u32 *sp = &src->px[(yy - y) * src->stride + (r.x - x)];
        u32 *dp = &tgt->px[yy * tgt->stride + r.x];
        int n = r.w;
        COPY32(dp, sp, n);
    }
}

/*
 * Scaled drawing of a BMP. Down-scaling averages every source pixel under the destination pixel
 * (box filter), up-scaling interpolates bilinearly; images with alpha are filtered with
 * alpha-weighted colours. Writes straight ARGB into dst, no blending. Aspect ratio is kept:
 * cover fills the whole destination and crops the overflow, otherwise the image is fitted inside
 * and centred (the rest of the destination is left as it was).
 */
static u32 px_bilinear(const bmp_view *v, u32 fx, u32 fy)
{
    int ix = (int)(fx >> 16), iy = (int)(fy >> 16);
    u32 wx = (fx >> 8) & 255, wy = (fy >> 8) & 255;
    u32 c[4] = { bmp_get(v, ix, iy), bmp_get(v, ix + 1, iy), bmp_get(v, ix, iy + 1), bmp_get(v, ix + 1, iy + 1) };
    u32 w[4] = { (256 - wx) * (256 - wy), wx * (256 - wy), (256 - wx) * wy, wx * wy };
    if (!v->has_alpha) {
        u32 r = 0, g = 0, b = 0;
        for (int i = 0; i < 4; i++) { r += (c[i] >> 16 & 255) * (w[i] >> 8); g += (c[i] >> 8 & 255) * (w[i] >> 8); b += (c[i] & 255) * (w[i] >> 8); }
        return 0xFF000000u | (r >> 8 > 255 ? 255 : r >> 8) << 16 | (g >> 8 > 255 ? 255 : g >> 8) << 8 | (b >> 8 > 255 ? 255 : b >> 8);
    }
    double A = 0, R = 0, G = 0, B = 0;
    for (int i = 0; i < 4; i++) {
        double a = (double)(c[i] >> 24) * w[i];
        A += a; R += a * (c[i] >> 16 & 255); G += a * (c[i] >> 8 & 255); B += a * (c[i] & 255);
    }
    if (A <= 0) return 0;
    return (u32)(A / 65536.0 + 0.5) << 24 | (u32)(R / A + 0.5) << 16 | (u32)(G / A + 0.5) << 8 | (u32)(B / A + 0.5);
}

static u32 px_box(const bmp_view *v, u32 x0, u32 x1, u32 y0, u32 y1)
{
    u32 ax0 = x0 >> 16, ax1 = (x1 - 1) >> 16, ay0 = y0 >> 16, ay1 = (y1 - 1) >> 16;
    if (ax1 >= (u32)v->w) ax1 = (u32)v->w - 1;
    if (ay1 >= (u32)v->h) ay1 = (u32)v->h - 1;
    if (ax0 > ax1) ax0 = ax1;
    if (ay0 > ay1) ay0 = ay1;
    u32 tw = 0, a = 0, r = 0, g = 0, b = 0;
    double A = 0, R = 0, G = 0, B = 0;
    for (u32 yy = ay0; yy <= ay1; yy++) {
        u32 lo = y0 > yy << 16 ? y0 : yy << 16, hi = y1 < (yy + 1) << 16 ? y1 : (yy + 1) << 16;
        u32 wy = hi > lo ? (hi - lo) >> 11 : 0;                     /* 0..32 */
        for (u32 xx = ax0; xx <= ax1; xx++) {
            u32 l2 = x0 > xx << 16 ? x0 : xx << 16, h2 = x1 < (xx + 1) << 16 ? x1 : (xx + 1) << 16;
            u32 w = wy * (h2 > l2 ? (h2 - l2) >> 11 : 0);
            u32 c = bmp_get(v, (int)xx, (int)yy);
            if (v->has_alpha) {
                double wa = (double)w * (c >> 24);
                A += wa; R += wa * (c >> 16 & 255); G += wa * (c >> 8 & 255); B += wa * (c & 255); tw += w;
            } else {
                r += (c >> 16 & 255) * w; g += (c >> 8 & 255) * w; b += (c & 255) * w; tw += w;
            }
        }
    }
    (void)a;
    if (!tw) return bmp_get(v, (int)ax0, (int)ay0);
    if (v->has_alpha) {
        if (A <= 0) return 0;
        return (u32)(A / tw + 0.5) << 24 | (u32)(R / A + 0.5) << 16 | (u32)(G / A + 0.5) << 8 | (u32)(B / A + 0.5);
    }
    return 0xFF000000u | (r / tw) << 16 | (g / tw) << 8 | (b / tw);
}

void gfx_draw_bmp_scaled(const bmp_view *v, gfx_surf *dst, int dx, int dy, int dw, int dh, int cover)
{
    if (!v || !v->w || !v->h || !dst || dw <= 0 || dh <= 0) return;
    double scx = (double)dw / v->w, scy = (double)dh / v->h;
    double sc = cover ? (scx > scy ? scx : scy) : (scx < scy ? scx : scy);
    int iw = (int)(v->w * sc + 0.5), ih = (int)(v->h * sc + 0.5);
    if (iw < 1) iw = 1;
    if (ih < 1) ih = 1;
    int ox = dx + (dw - iw) / 2, oy = dy + (dh - ih) / 2;
    gfx_rect area = gfx_rect_isect(gfx_rect_isect(gfx_rect_make(ox, oy, iw, ih), gfx_rect_make(dx, dy, dw, dh)),
                                   gfx_rect_make(0, 0, dst->w, dst->h));
    if (gfx_rect_empty(&area)) return;
    double inv_d = 65536.0 / sc;
    if (inv_d > 8192.0 * 65536.0 / 64.0) inv_d = 8192.0 * 65536.0 / 64.0;     /* ratio capped at 64:1 */
    u32 inv = (u32)inv_d;
    if (!inv) inv = 1;
    for (int yy = area.y; yy < area.y + area.h; yy++) {
        u32 y0 = (u32)((double)(yy - oy) * inv_d);
        u32 *row = &dst->px[yy * dst->stride];
        u32 x0 = (u32)((double)(area.x - ox) * inv_d);
        for (int xx = area.x; xx < area.x + area.w; xx++, x0 += inv) {
            u32 c;
            if (inv <= 65536) {
                u32 fx = x0 + inv / 2, fy = y0 + inv / 2;
                fx = fx > 32768 ? fx - 32768 : 0;
                fy = fy > 32768 ? fy - 32768 : 0;
                c = px_bilinear(v, fx, fy);
            } else c = px_box(v, x0, x0 + inv, y0, y0 + inv);
            row[xx] = c;
        }
    }
}

/* ------------------------------------------------------------------ screen output */

static const char *const cursor_art[] = {
    "X           ", "XX          ", "XoX         ", "XooX        ", "XoooX       ", "XooooX      ",
    "XoooooX     ", "XooooooX    ", "XoooooooX   ", "XooooooooX  ", "XoooooXXXXX ", "XooXooX     ",
    "XoX XooX    ", "XX  XooX    ", "X    XooX   ", "     XooX   ", "      XX    "
};
#define CUR_W 12
#define CUR_H 17

void gfx_cursor_set(int x, int y, int visible) { cur_x = x; cur_y = y; cur_vis = visible; }

static inline u32 pack(u32 argb)
{
    return (argb >> 16 & 255) << V.rpos | (argb >> 8 & 255) << V.gpos | (argb & 255) << V.bpos;
}

static void put_screen(u8 *line, int x, u32 argb)
{
    u32 p = pack(argb);
    if (V.bpp == 32) { ((u32 *)line)[x] = p; }
    else { line[x * 3] = (u8)p; line[x * 3 + 1] = (u8)(p >> 8); line[x * 3 + 2] = (u8)(p >> 16); }
}

/* Copies the rectangle of the back buffer to the framebuffer; the pointer is drawn on top. */
void gfx_present(gfx_rect r, int cx, int cy)
{
    (void)cx; (void)cy;
    r = gfx_rect_isect(r, gfx_rect_make(0, 0, back_s.w, back_s.h));
    if (gfx_rect_empty(&r) || !V.addr) return;
    int fast = V.bpp == 32 && V.rpos == 16 && V.gpos == 8 && V.bpos == 0;
    for (int y = r.y; y < r.y + r.h; y++) {
        u8 *line = (u8 *)(size_t)V.addr + (size_t)y * V.pitch;
        const u32 *src = &back_px[y * back_s.stride];
        if (fast) { u32 *d = (u32 *)line + r.x; const u32 *sp = src + r.x; int n = r.w; COPY32(d, sp, n); }
        else for (int x = r.x; x < r.x + r.w; x++) put_screen(line, x, src[x]);
        if (cur_vis && y >= cur_y && y < cur_y + CUR_H) {
            const char *art = cursor_art[y - cur_y];
            for (int x = r.x; x < r.x + r.w; x++) {
                int ax = x - cur_x;
                if (ax < 0 || ax >= CUR_W) continue;
                char ch = art[ax];
                if (ch == 'X') put_screen(line, x, 0xFF000000u);
                else if (ch == 'o') put_screen(line, x, 0xFFFFFFFFu);
            }
        }
    }
}

/* ---- window transparency: copy a region of the target before a window is painted, then mix it back ---- */
void gfx_snapshot(gfx_rect r, u32 *out)
{
    for (int y = 0; y < r.h; y++) kmemcpy(out + y * r.w, &tgt->px[(r.y + y) * tgt->stride + r.x], (size_t)r.w * 4);
}

/* every pixel of r becomes snapshot + (painted - snapshot) * a/255, a = a_inner inside `inner`, else a_outer */
void gfx_mix_snapshot(gfx_rect r, const u32 *snap, gfx_rect inner, int a_outer, int a_inner)
{
    for (int y = 0; y < r.h; y++) {
        u32 *p = &tgt->px[(r.y + y) * tgt->stride + r.x];
        const u32 *q = snap + y * r.w;
        int in_y = r.y + y >= inner.y && r.y + y < inner.y + inner.h;
        for (int x = 0; x < r.w; x++) {
            int a = in_y && r.x + x >= inner.x && r.x + x < inner.x + inner.w ? a_inner : a_outer;
            if (a < 255 && p[x] != q[x]) p[x] = blend(q[x], p[x], (u32)a);
        }
    }
}
