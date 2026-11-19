/*
 * Wallpapers and the wallpaper picker (Ctrl+Alt+B).
 *
 * 11 pictures are built into the kernel: one for each of the 9 themes (picture number = theme number) and two more.
 * The setting "wallpaper" is -1 (the theme's own picture) or the number of a picture. The picker is a full-screen
 * layer: the desktop is dimmed, a search field slides in at the top and the pictures stand in a row of slanted cards
 * that glide sideways (the card in the middle is the chosen one, bigger and with an accent edge).
 * Left / Right (or the wheel) move, Enter applies, Esc closes, Del goes back to the theme's own picture, typing
 * searches by name.
 */
#include "gui.h"

extern int inflate_raw(const u8 *src, u32 srclen, u8 *dst, u32 dstcap, u32 *outlen, u32 *used);

#define WPD(n) extern const u8 asset_wp##n##_start[], asset_wp##n##_end[], asset_wpt##n##_start[], asset_wpt##n##_end[];
WPD(0) WPD(1) WPD(2) WPD(3) WPD(4) WPD(5) WPD(6) WPD(7) WPD(8) WPD(9) WPD(10)
#define WPE(n, en, tr) { en, tr, asset_wp##n##_start, asset_wp##n##_end, asset_wpt##n##_start, asset_wpt##n##_end }

typedef struct { const char *en, *tr; const u8 *s, *e, *ts, *te; } wpic;
static const wpic pics[WP_COUNT] = {
    WPE(0, "Cherry Lake",    "Kiraz Gölü"),
    WPE(1, "Event Horizon",  "Olay Ufku"),
    WPE(2, "Green Waves",    "Yeşil Dalgalar"),
    WPE(3, "Aurora",         "Kutup Işığı"),
    WPE(4, "Golden Valley",  "Altın Vadi"),
    WPE(5, "Crimson Forest", "Kızıl Orman"),
    WPE(6, "Blue Ridge",     "Mavi Sırtlar"),
    WPE(7, "Samurai Dusk",   "Samuray Alacası"),
    WPE(8, "Auro Galaxy",    "Auro Galaksisi"),
    WPE(9, "Crimson Waves",  "Kızıl Dalgalar"),
    WPE(10, "Emerald Layers", "Zümrüt Katmanlar"),
};

/* ---- the pictures: "WPZ1", u16 width, u16 height, raw DEFLATE of PNG-filtered RGB rows (made by sys/tools/make_wallpapers.py) ---- */

#define SRC_MAXW 1280
#define SRC_MAXH 720
static u8 unz[SRC_MAXH * (SRC_MAXW * 3 + 1)];                    /* inflated rows (scratch) */
static u32 srcpx[SRC_MAXW * SRC_MAXH];                           /* the decoded picture */

static int paeth(int a, int b, int c)
{
    int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

/* Decodes into srcpx (ARGB, opaque). Returns 0 and the size, or a negative number. */
static int wpz_decode(const u8 *d, const u8 *e, int *ow, int *oh)
{
    if (e - d < 9 || d[0] != 'W' || d[1] != 'P' || d[2] != 'Z' || d[3] != '1') return -1;
    int w = d[4] | d[5] << 8, h = d[6] | d[7] << 8;
    if (w < 1 || h < 1 || w > SRC_MAXW || h > SRC_MAXH) return -2;
    u32 need = (u32)h * ((u32)w * 3 + 1), got = 0, used = 0;
    if (inflate_raw(d + 8, (u32)(e - d - 8), unz, need, &got, &used) != 0 || got != need) return -3;
    int stride = w * 3;
    for (int y = 0; y < h; y++) {
        u8 *row = unz + (u32)y * (stride + 1);
        int f = row[0];
        u8 *cur = row + 1;
        u8 *prev = y ? row - stride : 0;                              /* the previous row, already unfiltered (its filter byte is just before cur) */
        if (f > 4) return -4;
        for (int i = 0; i < stride; i++) {
            int a = i >= 3 ? cur[i - 3] : 0, b = prev ? prev[i] : 0, c = (i >= 3 && prev) ? prev[i - 3] : 0;
            int pr = f == 0 ? 0 : f == 1 ? a : f == 2 ? b : f == 3 ? (a + b) >> 1 : paeth(a, b, c);
            cur[i] = (u8)(cur[i] + pr);
        }
        u32 *o = srcpx + (u32)y * w;
        for (int x = 0; x < w; x++) o[x] = 0xFF000000u | (u32)cur[x * 3] << 16 | (u32)cur[x * 3 + 1] << 8 | cur[x * 3 + 2];
    }
    *ow = w; *oh = h;
    return 0;
}

/* "Cover" scaling with fixed point bilinear sampling: the picture fills dw x dh, the overflow is cropped evenly. */
static u16 xi_[GFX_MAXW + 1]; static u8 xf_[GFX_MAXW + 1];
static void cover_scale(const u32 *src, int w, int h, u32 *dst, int dw, int dh)
{
    u32 step, ox, oy;
    if ((u32)dw * h >= (u32)dh * w) {                                /* screen is wider: the picture's full width is used */
        step = ((u32)w << 16) / (u32)dw;
        ox = 0; oy = (((u32)h << 16) - (u32)dh * step) / 2;
    } else {
        step = ((u32)h << 16) / (u32)dh;
        oy = 0; ox = (((u32)w << 16) - (u32)dw * step) / 2;
    }
    for (int x = 0; x < dw; x++) {
        u32 fx = ox + (u32)x * step;
        u32 ix = fx >> 16;
        if (ix >= (u32)w - 1) { ix = (u32)w - 1; xf_[x] = 0; } else xf_[x] = (u8)((fx >> 8) & 255);
        xi_[x] = (u16)ix;
    }
    for (int y = 0; y < dh; y++) {
        u32 fy = oy + (u32)y * step;
        u32 iy = fy >> 16, wy = (fy >> 8) & 255;
        if (iy >= (u32)h - 1) { iy = (u32)h - 1; wy = 0; }
        const u32 *r0 = src + iy * w, *r1 = iy + 1 < (u32)h ? r0 + w : r0;
        u32 *o = dst + (u32)y * dw;
        for (int x = 0; x < dw; x++) {
            u32 ix = xi_[x], wx = xf_[x], jx = ix + 1 < (u32)w ? ix + 1 : ix;
            u32 a = r0[ix], b = r0[jx], c = r1[ix], d = r1[jx];
            u32 t_rb = ((a & 0xFF00FFu) * (256 - wx) + (b & 0xFF00FFu) * wx) >> 8 & 0xFF00FFu;
            u32 t_g = (((a >> 8) & 255) * (256 - wx) + ((b >> 8) & 255) * wx) >> 8;
            u32 u_rb = ((c & 0xFF00FFu) * (256 - wx) + (d & 0xFF00FFu) * wx) >> 8 & 0xFF00FFu;
            u32 u_g = (((c >> 8) & 255) * (256 - wx) + ((d >> 8) & 255) * wx) >> 8;
            u32 rb = (t_rb * (256 - wy) + u_rb * wy) >> 8 & 0xFF00FFu;
            u32 g = (t_g * (256 - wy) + u_g * wy) >> 8;
            o[x] = 0xFF000000u | rb | g << 8;
        }
    }
}

/* Renders picture idx (or its thumbnail) filling dw x dh pixels of dst. 0 = ok. */
int wp_render(int idx, int thumb_, u32 *dst, int dw, int dh)
{
    if (idx < 0 || idx >= WP_COUNT) return -9;
    int w, h;
    int rc = thumb_ ? wpz_decode(pics[idx].ts, pics[idx].te, &w, &h) : wpz_decode(pics[idx].s, pics[idx].e, &w, &h);
    if (rc) return rc;
    cover_scale(srcpx, w, h, dst, dw, dh);
    return 0;
}

const char *wp_name(int i) { return i < 0 || i >= WP_COUNT ? "" : config_tr(pics[i].en, pics[i].tr); }

int wp_effective(void)
{
    int w = config_get()->wallpaper;
    if (w >= 0 && w < WP_COUNT) return w;
    int t = ui_theme_id();
    return t >= 0 && t < 9 ? t : 2;
}

void wp_apply(int idx)                                           /* -1 = the theme's own picture */
{
    as_settings s = *config_get();
    s.wallpaper = idx < 0 || idx >= WP_COUNT ? -1 : idx;
    config_set(&s);
}

/* ------------------------------------------------------------------ the picker */

#define TW 320
#define TH 180
#define PK_MS 260

static int open_, want_open;
static int anim;                                  /* 0..256 */
static u32 last_ms;
static int sel;                                   /* index into flt[] */
static int pos;                                   /* scroll position, 1/256 card, eases to sel * 256 */
static char query[24];
static int flt[WP_COUNT], nflt;
static gfx_surf thumb[WP_COUNT];
static u32 thumb_px[WP_COUNT][TW * TH];
static int thumbs_ready;
static int hover = -1;
static u32 rowbuf[1024 + 64];

int wp_picker_active(void) { return open_; }

static void build_thumbs(void);
const gfx_surf *wp_preview(int idx)
{
    build_thumbs();
    return &thumb[idx < 0 || idx >= WP_COUNT ? 0 : idx];
}

static int ease(int t) { if (t <= 0) return 0; if (t >= 256) return 256; return t * t * (3 * 256 - 2 * t) / (256 * 256); }
static int iabs(int a) { return a < 0 ? -a : a; }

static void build_thumbs(void)
{
    if (thumbs_ready) return;
    for (int i = 0; i < WP_COUNT; i++) {
        thumb[i].w = TW; thumb[i].h = TH; thumb[i].stride = TW; thumb[i].px = thumb_px[i];
        if (wp_render(i, 1, thumb_px[i], TW, TH) != 0)
            for (int k = 0; k < TW * TH; k++) thumb_px[i][k] = 0xFF303030u;
    }
    thumbs_ready = 1;
}

static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }
static int has_sub(const char *hay, const char *needle)
{
    size_t n = kstrlen(needle), h = kstrlen(hay);
    if (!n) return 1;
    for (size_t i = 0; i + n <= h; i++) {
        size_t j = 0;
        while (j < n && lower(hay[i + j]) == lower(needle[j])) j++;
        if (j == n) return 1;
    }
    return 0;
}

static void refilter(int keep_pic)
{
    nflt = 0;
    for (int i = 0; i < WP_COUNT; i++)
        if (has_sub(pics[i].en, query) || has_sub(pics[i].tr, query)) flt[nflt++] = i;
    sel = 0;
    for (int k = 0; k < nflt; k++) if (flt[k] == keep_pic) sel = k;
}

static void damage_all(void) { int sw, sh; ui_resolution(&sw, &sh); wm_damage(gfx_rect_make(0, 0, sw, sh)); }

void wp_picker_toggle(void)
{
    if (ui_login_active()) return;
    if (want_open) { want_open = 0; last_ms = ui_ms(); damage_all(); return; }
    build_thumbs();
    query[0] = 0;
    refilter(wp_effective());
    pos = sel * 256;
    want_open = 1; open_ = 1; hover = -1;
    last_ms = ui_ms();
    damage_all();
}

static void close_picker(void) { want_open = 0; last_ms = ui_ms(); damage_all(); }

void wp_picker_tick(void)
{
    if (!open_) return;
    u32 now = ui_ms();
    int dt = (int)(now - last_ms);
    if (dt < 0 || dt > 200) dt = 16;
    last_ms = now;
    int moved = 0;
    int step = dt * 256 / PK_MS;
    if (want_open && anim < 256) { anim += step; if (anim > 256) anim = 256; moved = 1; }
    if (!want_open && anim > 0) { anim -= step; if (anim < 0) anim = 0; moved = 1; if (!anim) open_ = 0; }
    int goal = sel * 256;
    if (pos != goal) {
        int d = goal - pos;
        int k = dt * 256 / 110;                                  /* ~110 ms time constant */
        if (k > 256) k = 256;
        int mv = d * k / 256;
        if (mv == 0) mv = d > 0 ? 1 : -1;
        if (iabs(mv) > iabs(d)) mv = d;
        pos += mv;
        moved = 1;
    }
    if (!open_ || (moved && (want_open ? anim < 256 : 1))) damage_all();
    else if (moved) {                                            /* fade finished: only the band with the cards and the name moves */
        int sw, sh; ui_resolution(&sw, &sh);
        int y0 = sh / 2 - 220 < 0 ? 0 : sh / 2 - 220, y1 = sh / 2 + 280 > sh ? sh : sh / 2 + 280;
        wm_damage(gfx_rect_make(0, y0, sw, y1 - y0));
    }
}

typedef struct { int x, y, w, h, slant; } card_geo;

static void card_geometry(int i, int sw, int sh, card_geo *g, int *dim)
{
    int cw = sw * 34 / 100; if (cw > 640) cw = 640; if (cw < 200) cw = 200;
    int d = i * 256 - pos;
    int ad = iabs(d);
    int sc = 256 - (ad < 512 ? ad : 512) * 56 / 256;
    int w = cw * sc / 256, h = w * 9 / 16;
    int near = ad < 256 ? ad : 256, far = ad > 256 ? ad - 256 : 0;
    if (far > 512) far = 512;
    int off = cw * 68 / 100 * near / 256 + cw * 40 / 100 * far / 256;
    int cx = sw / 2 + (d < 0 ? -off : off);
    int rise = (256 - ease(anim)) * (sh / 4) / 256;
    g->w = w; g->h = h; g->x = cx - w / 2; g->y = sh * 50 / 100 - h / 2 + rise;
    g->slant = h * 14 / 100;
    *dim = ad >= 256 ? 70 + (ad > 512 ? 40 : (ad - 256) * 40 / 256) : ad * 70 / 256;
}

static u16 cardx[1024 + 64];
static void draw_card(const card_geo *g, int pic, int dim, int selected, int alpha)
{
    const ui_theme *t = ui_th();
    int b = selected ? 3 : 0;
    const gfx_surf *src = &thumb[pic];
    if (g->w <= 0 || g->w > 1024) return;
    for (int k = 0; k < g->w; k++) cardx[k] = (u16)((u32)k * TW / (u32)g->w);   /* once per card, not once per pixel */
    u32 keep = (u32)(256 - dim);
    for (int ry = -b; ry < g->h + b; ry++) {
        int shift = (g->h / 2 - ry) * g->slant / g->h;            /* slanted: the top leans right */
        int x0 = g->x + shift - b, w = g->w + 2 * b;
        if (x0 + w <= 0 || x0 >= GFX_MAXW || w <= 0 || w > 1024 + 64) continue;
        int inside = ry >= 0 && ry < g->h;
        if (!inside) { for (int k = 0; k < w; k++) rowbuf[k] = t->accent | 0xFF000000u; }
        else {
            const u32 *srow = src->px + (u32)(ry * TH / g->h) * TW;
            for (int k = 0; k < b; k++) rowbuf[k] = rowbuf[w - 1 - k] = t->accent | 0xFF000000u;
            u32 *o = rowbuf + b;
            for (int k = 0; k < g->w; k++) {
                u32 c = srow[cardx[k]];
                u32 rb = (((c & 0xFF00FFu) * keep) >> 8) & 0xFF00FFu, gg = (((c >> 8) & 255) * keep) >> 8;
                o[k] = 0xFF000000u | rb | gg << 8;
            }
        }
        gfx_surf row = { w, 1, w, rowbuf };
        if (alpha >= 255) gfx_blit_opaque(&row, x0, g->y + ry);
        else gfx_blit_alpha(&row, x0, g->y + ry, alpha);
    }
}

void wp_picker_draw(gfx_rect clip)
{
    if (!open_) return;
    int sw, sh;
    ui_resolution(&sw, &sh);
    const ui_theme *t = ui_th();
    int e = ease(anim);
    gfx_set_clip(&clip);
    gfx_fill_rect(0, 0, sw, sh, (u32)(0xC0 * e / 256) << 24 | 0x0A0E18u);
    /* search field */
    int fw = 340, fh = 34, fx = (sw - fw) / 2, fy = 36 + (256 - e) * -40 / 256;
    gfx_round_rect(fx, fy, fw, fh, 10, 0x80FFFFFFu);
    gfx_round_rect(fx + 1, fy + 1, fw - 2, fh - 2, 9, 0xE0222633u);
    gfx_circle(fx + 20, fy + 16, 6, 0xFFB8C0D0u); gfx_circle(fx + 20, fy + 16, 4, 0xFF222633u);
    gfx_fill_rect(fx + 24, fy + 20, 7, 3, 0xFFB8C0D0u);
    if (query[0]) {
        gfx_text(fx + 42, fy + 9, query, 0xFFFFFFFFu);
        gfx_fill_rect(fx + 42 + (int)kstrlen(query) * 8, fy + 9, 2, 16, t->accent);
    } else gfx_text(fx + 42, fy + 9, config_tr("Search wallpapers", "Arka plan ara"), 0xFF8D96A8u);
    /* the cards, far ones first */
    int order[WP_COUNT];
    for (int k = 0; k < nflt; k++) order[k] = k;
    for (int a = 0; a < nflt; a++)
        for (int b2 = a + 1; b2 < nflt; b2++)
            if (iabs(order[b2] * 256 - pos) > iabs(order[a] * 256 - pos)) { int tmp = order[a]; order[a] = order[b2]; order[b2] = tmp; }
    for (int n = 0; n < nflt; n++) {
        int k = order[n];
        card_geo g; int dim;
        card_geometry(k, sw, sh, &g, &dim);
        if (g.x > sw + 60 || g.x + g.w < -60) continue;
        int center = k == sel && iabs(k * 256 - pos) < 96;
        draw_card(&g, flt[k], dim, center, e);
    }
    if (nflt) {
        card_geo g; int dim;
        card_geometry(sel, sw, sh, &g, &dim);
        const char *nm = wp_name(flt[sel]);
        int ty = g.y + g.h + 26;
        gfx_text_scaled((sw - gfx_text_w_scaled(nm, 2)) / 2, ty, nm, (u32)(255 * e / 256) << 24 | 0xFFFFFFu, 2);
        char info[48];
        const char *tag = config_get()->wallpaper == flt[sel] ? config_tr("current", "kullanımda")
                          : (config_get()->wallpaper < 0 && wp_effective() == flt[sel]) ? config_tr("theme default", "tema varsayılanı") : "";
        ksnprintf(info, sizeof info, "%d / %d   %s", sel + 1, nflt, tag);
        gfx_text((sw - gfx_text_w(info)) / 2, ty + 36, info, 0xFFB8C0D0u);
    } else {
        const char *m = config_tr("No wallpaper found", "Arka plan bulunamadı");
        gfx_text((sw - gfx_text_w(m)) / 2, sh / 2, m, 0xFFB8C0D0u);
    }
    const char *hint = config_tr("< >  move    Enter  apply    Del  theme default    Esc  close",
                                 "< >  gez    Enter  uygula    Del  tema varsayılanı    Esc  kapat");
    gfx_text((sw - gfx_text_w(hint)) / 2, sh - 118 + (256 - e) * 30 / 256, hint, 0xFFB8C0D0u);
    gfx_set_clip(0);
}

static void choose(int k)
{
    if (k < 0 || k >= nflt) return;
    wp_apply(flt[k]);
    close_picker();
}

int wp_picker_key(int k)
{
    if (!open_) return 0;
    if (k == 27) close_picker();
    else if (k == KEY_LEFT) { if (sel > 0) sel--; }
    else if (k == KEY_RIGHT) { if (sel + 1 < nflt) sel++; }
    else if (k == KEY_HOME) sel = 0;
    else if (k == KEY_END) { if (nflt) sel = nflt - 1; }
    else if (k == '\n' || k == '\r') choose(sel);
    else if (k == KEY_DEL) { wp_apply(-1); close_picker(); }
    else if (k == '\b') { size_t n = kstrlen(query); if (n) { int keep = nflt ? flt[sel] : -1; query[n - 1] = 0; refilter(keep); } }
    else if (k >= 32 && k < 127 && kstrlen(query) < 20) {
        size_t n = kstrlen(query);
        int keep = nflt ? flt[sel] : -1;
        query[n] = (char)k; query[n + 1] = 0;
        refilter(keep);
    }
    damage_all();
    return 1;                                                    /* the picker takes every key while it is open */
}

int wp_picker_pointer(const mouse_ev *e, int down)
{
    if (!open_) return 0;
    int sw, sh;
    ui_resolution(&sw, &sh);
    int hit = -1;
    for (int k = nflt - 1; k >= 0; k--) {
        card_geo g; int dim;
        card_geometry(k, sw, sh, &g, &dim);
        int shift = (g.h / 2 - (e->y - g.y)) * g.slant / g.h;
        if (e->y >= g.y && e->y < g.y + g.h && e->x >= g.x + shift && e->x < g.x + shift + g.w) {
            if (hit < 0 || iabs(k * 256 - pos) < iabs(hit * 256 - pos)) hit = k;
        }
    }
    if (down) {
        if (hit == sel) choose(hit);
        else if (hit >= 0) { sel = hit; damage_all(); }
        else if (e->y > 80) close_picker();
    }
    return 1;
}

void wp_picker_wheel(int notches)
{
    if (!open_) return;
    while (notches > 0 && sel + 1 < nflt) { sel++; notches--; }
    while (notches < 0 && sel > 0) { sel--; notches++; }
    damage_all();
}
