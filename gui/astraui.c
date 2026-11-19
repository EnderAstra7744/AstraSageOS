/*
 * AstraUI - the desktop environment: wallpaper, desktop icon, the bottom dock, the unified app
 * icon pipeline, the boot splash and the event loop that ties pointer, keyboard, window manager
 * and applications together.
 *
 * Drawing order (back to front): wallpaper, desktop icons, windows (back to front, the focused
 * one last), dock, pointer. The dock is above every window; a click on the dock is never passed
 * to a window behind it.
 */
#include "gui.h"
#include "io.h"

extern const u8 asset_boot_logo_start[], asset_boot_logo_end[];
extern const u8 asset_ast_icon_start[], asset_ast_icon_end[];
extern const u8 asset_as_logo_start[], asset_as_logo_end[];
extern const u8 asset_ic_froggy_start[], asset_ic_froggy_end[], asset_ic_netsurf_start[], asset_ic_netsurf_end[];
extern const u8 asset_ic_script_start[], asset_ic_script_end[];

static int active, sw, sh;
static gfx_rect dock, work_area;
static int dock_icon, dock_pad, dock_gap, dock_h, hover_item = -1;
static int dock_apps[APP_COUNT], dock_n;       /* apps shown in the dock: those with a window, plus those still animating out */
static int dscale[APP_COUNT];                  /* 0..256 per app: how far its icon has grown into the dock */
static int ddodge;                             /* 0..256: how far the dock has stepped down because a window touches it */
static gfx_rect dock_home;                      /* where the dock rests (rise complete, not dodging) */
static int dvis;                               /* 0..256: how far the dock itself has risen from the bottom edge */
static u32 danim_last;

/* ------------------------------------------------------------------ clock (PIT channel 0) */

u32 (*ui_ms_hook)(void);

u32 ui_ms(void)
{
    if (ui_ms_hook) return ui_ms_hook();
    static int inited;
    static u32 last, ms, rem;
    if (!inited) {                         /* mode 2, full 16-bit count: counts down 65535..0 every 54.9 ms */
        outb(0x43, 0x34); outb(0x40, 0); outb(0x40, 0);
        outb(0x43, 0x00);
        u32 lo = inb(0x40); last = lo | (u32)inb(0x40) << 8;
        inited = 1;
    }
    outb(0x43, 0x00);
    u32 lo = inb(0x40), now = lo | (u32)inb(0x40) << 8;
    rem += (last - now) & 0xFFFF;
    last = now;
    ms += rem / 1193;
    rem %= 1193;
    return ms;
}

/* ------------------------------------------------------------------ notices for the AST banner */

static char notes[4][100];
static int nnotes;
void ui_note(const char *m) { shell_notify(m); if (nnotes < 4) { kstrncpy(notes[nnotes], m, 99); notes[nnotes][99] = 0; nnotes++; } }
int ui_take_note(char *out, size_t cap)
{
    if (!nnotes) return 0;
    kstrncpy(out, notes[0], cap - 1); out[cap - 1] = 0;
    for (int i = 1; i < nnotes; i++) kmemcpy(notes[i - 1], notes[i], sizeof notes[i]);
    nnotes--;
    return 1;
}

int ui_active(void) { return active; }
void ui_resolution(int *w, int *h) { *w = sw; *h = sh; }

/* ------------------------------------------------------------------ unified app icons */

static void script_icon_face(u32 *px, int size);
typedef struct { const char *file; const char *en, *tr; const u8 *start, *end; void (*face)(u32 *px, int size); int ok; bmp_view v; } icon_src;
static icon_src isrc[APP_COUNT] = {
    [APP_AST]     = { "assets/ast_icon.bmp", "AST", "AST", asset_ast_icon_start, asset_ast_icon_end, 0, 0, { 0 } },
    [APP_FILES]   = { 0, "File Explorer", "Dosya Gezgini", 0, 0, files_icon_face, 1, { 0 } },
    [APP_WELCOME] = { "assets/as_logo.bmp", "Welcome", "Hoşgeldiniz", asset_as_logo_start, asset_as_logo_end, 0, 0, { 0 } },
    [APP_LOGIN]   = { 0, "Login", "Giriş", 0, 0, login_icon_face, 1, { 0 } },
    [APP_SCRIPT]  = { 0, "App", "Uygulama", 0, 0, script_icon_face, 1, { 0 } },
    [APP_STORE]   = { 0, "AstraStore", "AstraStore", 0, 0, store_icon_face, 1, { 0 } },
    [APP_GRID]    = { 0, "Applications", "Uygulamalar", 0, 0, grid_icon_face, 1, { 0 } },
    [APP_SETTINGS] = { 0, "Settings", "Ayarlar", 0, 0, settings_icon_face, 1, { 0 } },
    [APP_TASKS]   = { 0, "Task Manager", "Görev Yöneticisi", 0, 0, tasks_icon_face, 1, { 0 } },
};

const char *ui_app_label(int app);
static const char *app_label(int app) { return app == APP_SCRIPT ? sgui_title() : config_tr(isrc[app].en, isrc[app].tr); }

#define ICON_MAX 128
#define ICON_CACHE 24
static struct { int app, size, used; gfx_surf s; u32 px[ICON_MAX * ICON_MAX]; } icache[ICON_CACHE];
static u32 icon_tmp[ICON_MAX * ICON_MAX];
static int icons_loaded;

static void load_icons(void)
{
    if (icons_loaded) return;
    icons_loaded = 1;
    for (int i = 0; i < APP_COUNT; i++) {
        if (!isrc[i].start) continue;
        int rc = bmp_open(isrc[i].start, (u32)(isrc[i].end - isrc[i].start), &isrc[i].v);
        isrc[i].ok = rc == BMP_OK;
        if (rc != BMP_OK) {
            char m[100];
            ksnprintf(m, sizeof m, "icon %s: %s - using a plain icon", isrc[i].file, bmp_strerror(rc));
            ui_note(m);
        }
    }
}

static double dabs(double x) { return x < 0 ? -x : x; }
#if defined(__i386__) || defined(__x86_64__)
static double dsqrt(double x) { double r; __asm__("fsqrt" : "=t"(r) : "0"(x)); return r; }
#else
static double dsqrt(double x) { return __builtin_sqrt(x); }
#endif

/* Squircle (superellipse |x|^5 + |y|^5 = 1) coverage of pixel (x, y) in a size x size icon, 0..255. */
static u32 squircle_cov(int x, int y, int size)
{
    double half = size / 2.0;
    double u = dabs((x + 0.5 - half) / half), v = dabs((y + 0.5 - half) / half);
    double u2 = u * u, v2 = v * v;
    double f = u2 * u2 * u + v2 * v2 * v;
    double u4 = u2 * u2, v4 = v2 * v2;
    double g = 5.0 / half * dsqrt(u4 * u4 + v4 * v4);          /* |grad f| per pixel */
    if (g < 1e-6) return 255;
    double d = (f - 1.0) / g;                                    /* signed distance in pixels, + = outside */
    double c = 0.5 - d;
    if (c <= 0) return 0;
    if (c >= 1) return 255;
    return (u32)(c * 255.0 + 0.5);
}

static u32 mix(u32 a, u32 b, u32 t)       /* a over... plain lerp of two opaque colours */
{
    u32 r = ((a >> 16 & 255) * (255 - t) + (b >> 16 & 255) * t) / 255;
    u32 g = ((a >> 8 & 255) * (255 - t) + (b >> 8 & 255) * t) / 255;
    u32 bl = ((a & 255) * (255 - t) + (b & 255) * t) / 255;
    return r << 16 | g << 8 | bl;
}

/* The plain icon used when a bitmap is missing: green squircle with the app's initial. */
static void fallback_face(int app, u32 *px, int size)
{
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) px[y * size + x] = 0xFF000000u | mix(0x9be01c, 0x4c8a00, (u32)(y * 255 / size));
    const char *lab = isrc[app].en;
    int n = (int)kstrlen(lab);
    int sc = size / 20 > 1 ? size / 20 : 1;
    int tw = n * 8 * sc, th = 16 * sc, x0 = (size - tw) / 2, y0 = (size - th) / 2;
    for (int i = 0; i < n; i++) {
        const unsigned char *g = gfx_font_glyph((unsigned char)lab[i]);
        for (int gy = 0; gy < 16; gy++)
            for (int gx = 0; gx < 8; gx++)
                if (g[gy] >> (7 - gx) & 1)
                    for (int yy = 0; yy < sc; yy++)
                        for (int xx = 0; xx < sc; xx++) {
                            int px_x = x0 + (i * 8 + gx) * sc + xx, px_y = y0 + gy * sc + yy;
                            if (px_x >= 0 && px_x < size && px_y >= 0 && px_y < size) px[px_y * size + px_x] = 0xFF101010u;
                        }
    }
}

/* Icons supplied as pictures: FroggyCode, NetSurf and the AstraScript logo (the default of every script window). */
typedef struct { const u8 *a, *b; bmp_view v; int ok, loaded; } named_ic;
static named_ic nic[NI_COUNT] = {
    [NI_FROGGY]  = { asset_ic_froggy_start,  asset_ic_froggy_end,  { 0 }, 0, 0 },
    [NI_NETSURF] = { asset_ic_netsurf_start, asset_ic_netsurf_end, { 0 }, 0, 0 },
    [NI_SCRIPT]  = { asset_ic_script_start,  asset_ic_script_end,  { 0 }, 0, 0 },
};

int ui_named_icon_id(const char *name)
{
    if (!name) return -1;
    if (!kstrncmp(name, "Froggy", 6)) return NI_FROGGY;
    if (!kstrncmp(name, "NetSurf", 7)) return NI_NETSURF;
    return -1;
}

static void named_face(int id, u32 *px, int size)           /* opaque square picture, scaled to size */
{
    named_ic *n = &nic[id];
    if (!n->loaded) { n->loaded = 1; n->ok = bmp_open(n->a, (u32)(n->b - n->a), &n->v) == BMP_OK; }
    for (int i = 0; i < size * size; i++) px[i] = 0xFF000000u;
    if (!n->ok) return;
    gfx_surf t = { size, size, size, px };
    gfx_draw_bmp_scaled(&n->v, &t, 0, 0, size, size, 0);
    for (int i = 0; i < size * size; i++) px[i] |= 0xFF000000u;
}

#define NIC_CACHE 12
static struct { int id, size, used; gfx_surf s; u32 px[ICON_MAX * ICON_MAX]; } nicache[NIC_CACHE];
static int nic_next;

const gfx_surf *ui_named_icon(int id, int size)
{
    if (id < 0 || id >= NI_COUNT) return 0;
    if (size > ICON_MAX) size = ICON_MAX;
    if (size < 8) size = 8;
    for (int i = 0; i < NIC_CACHE; i++)
        if (nicache[i].used && nicache[i].id == id && nicache[i].size == size) return &nicache[i].s;
    int slot = nic_next++ % NIC_CACHE;
    named_face(id, icon_tmp, size);
    gfx_surf *s = &nicache[slot].s;
    s->w = s->h = s->stride = size; s->px = nicache[slot].px;
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) s->px[y * size + x] = squircle_cov(x, y, size) << 24 | (icon_tmp[y * size + x] & 0xFFFFFF);
    nicache[slot].id = id; nicache[slot].size = size; nicache[slot].used = 1;
    return s;
}

/* The icon of a running AstraScript app: FroggyCode / NetSurf have their own picture, every other app the AstraScript logo. */
static void script_icon_face(u32 *px, int size)
{
    const bmp_view *own = sgui_icon();
    if (own) {                                                      /* Init(Icon="...") */
        for (int i = 0; i < size * size; i++) px[i] = 0xFF000000u;
        gfx_surf t = { size, size, size, px };
        gfx_draw_bmp_scaled(own, &t, 0, 0, size, size, 0);
        for (int i = 0; i < size * size; i++) px[i] |= 0xFF000000u;
        return;
    }
    int id = ui_named_icon_id(sgui_title());
    named_face(id >= 0 ? id : NI_SCRIPT, px, size);
}

void ui_icon_forget(int app)
{
    for (int i = 0; i < ICON_CACHE; i++) if (icache[i].used && icache[i].app == app) icache[i].used = 0;
}

/*
 * One icon at one size: the source bitmap is scaled (aspect ratio kept, centred, never stretched)
 * into the common squircle icon area and the squircle mask makes everything outside transparent.
 * The result is cached, so neither the dock nor the desktop re-masks per frame.
 */
const gfx_surf *render_app_icon(int app, int size)
{
    if (app < 0 || app >= APP_COUNT) return 0;
    if (size > ICON_MAX) size = ICON_MAX;
    if (size < 8) size = 8;
    load_icons();
    for (int i = 0; i < ICON_CACHE; i++)
        if (icache[i].used && icache[i].app == app && icache[i].size == size) return &icache[i].s;
    int slot = 0;
    for (int i = 0; i < ICON_CACHE; i++) if (!icache[i].used) { slot = i; break; }
    gfx_surf *s = &icache[slot].s;
    s->w = s->h = size; s->stride = size; s->px = icache[slot].px;
    icache[slot].app = app; icache[slot].size = size; icache[slot].used = 1;

    const u32 base = 0xFF111113u;
    if (isrc[app].face) {
        gfx_rect keep = gfx_get_clip();
        isrc[app].face(icon_tmp, size);
        gfx_set_clip(&keep);
        for (int y = 0; y < size; y++)
            for (int x = 0; x < size; x++)
                s->px[y * size + x] = squircle_cov(x, y, size) << 24 | (icon_tmp[y * size + x] & 0xFFFFFF);
    } else if (isrc[app].ok) {
        gfx_surf t = { size, size, size, icon_tmp };
        for (int i = 0; i < size * size; i++) icon_tmp[i] = 0;
        gfx_draw_bmp_scaled(&isrc[app].v, &t, 0, 0, size, size, 0);
        for (int y = 0; y < size; y++)
            for (int x = 0; x < size; x++) {
                u32 c = icon_tmp[y * size + x], a = c >> 24;
                u32 col = a == 255 ? c & 0xFFFFFF : a ? mix(base & 0xFFFFFF, c & 0xFFFFFF, a) : base & 0xFFFFFF;
                s->px[y * size + x] = squircle_cov(x, y, size) << 24 | col;
            }
    } else {
        fallback_face(app, icon_tmp, size);
        for (int y = 0; y < size; y++)
            for (int x = 0; x < size; x++)
                s->px[y * size + x] = squircle_cov(x, y, size) << 24 | (icon_tmp[y * size + x] & 0xFFFFFF);
    }
    return s;
}

void draw_app_icon(int app, int x, int y, int size)
{
    const gfx_surf *s = render_app_icon(app, size);
    if (s) gfx_blit(s, x, y);
}

/* ------------------------------------------------------------------ wallpaper */

static gfx_surf wall;
static u32 wall_px[GFX_MAXW * GFX_MAXH];
static u32 wall_serial = 1;
const gfx_surf *ui_wallpaper_surf(void) { return &wall; }
u32 ui_wallpaper_serial(void) { return wall_serial; }

static int wall_idx = -1;

static void prepare_wallpaper(void)
{
    wall_serial++;
    wall_idx = wp_effective();
    wall.w = sw; wall.h = sh; wall.stride = sw; wall.px = wall_px;
    int rc = wp_render(wall_idx, 0, wall_px, sw, sh);
    if (rc == 0) return;
    char m[100];
    ksnprintf(m, sizeof m, "wallpaper %d: error %d - plain background", wall_idx, rc);
    ui_note(m);
    for (int y = 0; y < sh; y++)
        for (int x = 0; x < sw; x++) wall_px[y * sw + x] = 0xFF000000u | mix(0x2a2a2d, 0x101012, (u32)(y * 255 / sh));
}

/* ------------------------------------------------------------------ dock (animated) */

#define DOCK_MS 240

static int ease(int t)                                        /* smoothstep on 0..256 */
{
    if (t <= 0) return 0;
    if (t >= 256) return 256;
    return t * t * (3 * 256 - 2 * t) / (256 * 256);
}

/* The work area never changes (a strip at the bottom is always kept free). The dock exists only while at least
 * one app has a window: icons grow into it when an app opens and shrink out of it when the app closes, and the
 * dock itself slides up from / down to the bottom edge. */
static void dock_geometry(void)
{
    dock_icon = sh >= 700 ? 56 : 44;
    dock_pad = 10; dock_gap = 14;
    dock_h = dock_icon + 2 * dock_pad + 6;
    work_area = gfx_rect_make(0, SHELL_TOP, sw, sh - dock_h - 18 - SHELL_TOP);
}

static void dock_layout(void)
{
    dock_n = 0;
    int content = 0, last_e = 0;
    for (int k = 0; k < APP_COUNT; k++) {
        int i = k == 0 ? APP_GRID : k - 1;                   /* the 9-dot icon is always first */
        if (k > 0 && i == APP_GRID) continue;
        if (dscale[i] <= 0) continue;
        dock_apps[dock_n++] = i;
        int e = ease(dscale[i]);
        content += (dock_icon + dock_gap) * e / 256;
        last_e = e;
    }
    content -= dock_gap * last_e / 256;
    if (!dock_n || content <= 0) { dock = gfx_rect_make(0, 0, 0, 0); return; }
    int w = content + 2 * dock_pad;
    int rise = ease(dvis) * (256 - ease(ddodge)) / 256;
    dock = gfx_rect_make((sw - w) / 2, sh - dock_h - 10 + (dock_h + 16) * (256 - rise) / 256, w, dock_h);
    dock_home = gfx_rect_make((sw - w) / 2, sh - dock_h - 10, w, dock_h + 10);   /* touching it, no margin: a maximised window stops 8 px above it */
}

gfx_rect ui_dock_rect(void) { return dock; }
gfx_rect ui_work_rect(void) { return work_area; }
int ui_dock_count(void)                                       /* apps that really have a window */
{
    int n = 0;
    for (int i = 0; i < APP_COUNT; i++) if (wm_find_app(i)) n++;
    return n;
}

static gfx_rect dock_item_rect(int i)
{
    int x = dock.x + dock_pad;
    for (int j = 0; j < i; j++) x += (dock_icon + dock_gap) * ease(dscale[dock_apps[j]]) / 256;
    int e = ease(dscale[dock_apps[i]]);
    int s = dock_icon * e / 256;
    if (s < 4) s = 4;
    return gfx_rect_make(x, dock.y + dock_pad + (dock_icon - s) / 2, s, s);
}

int ui_dock_hit(int x, int y)
{
    for (int i = 0; i < dock_n; i++) {
        if (ease(dscale[dock_apps[i]]) < 128 || (dock_apps[i] != APP_GRID && !wm_find_app(dock_apps[i]))) continue;
        gfx_rect r = dock_item_rect(i);
        r.x -= dock_gap / 2; r.w += dock_gap; r.y = dock.y; r.h = dock.h;
        if (gfx_rect_has(&r, x, y)) return dock_apps[i];
    }
    return -1;
}

static gfx_rect dock_span(gfx_rect d) { return gfx_rect_make(d.x - 24, d.y - 24, d.w + 48, d.h + 48); }

/* Moves the animation on by the time passed. 1 = something changed (the caller redraws). */
static int dock_update(int settle)
{
    u32 now = ui_ms();
    int dt = (int)(now - danim_last);
    danim_last = now;
    if (dt < 0 || dt > 200) dt = 200;
    int step = dt * 256 / DOCK_MS;
    if (step < 1) step = 1;
    if (settle) step = 256;
    int any_target = 0, moved = 0;
    for (int i = 0; i < APP_COUNT; i++) {
        int t = (i == APP_GRID || wm_find_app(i)) ? 256 : 0;
        if (t) any_target = 1;
        if (dscale[i] == t) continue;
        if (dscale[i] < t) { dscale[i] += step; if (dscale[i] > t) dscale[i] = t; }
        else               { dscale[i] -= step; if (dscale[i] < t) dscale[i] = t; }
        moved = 1;
    }
    int dt_ = (dock_home.w && wm_window_hits(dock_home)) ? 256 : 0;     /* a window touches the dock: it steps down, and returns when the window leaves */
    if (ddodge != dt_) { ddodge += ddodge < dt_ ? step : -step; if (ddodge > 256) ddodge = 256; if (ddodge < 0) ddodge = 0; moved = 1; }
    int vt = any_target ? 256 : 0;
    if (dvis != vt) {
        dvis += dvis < vt ? step : -step;
        if (dvis > 256) dvis = 256;
        if (dvis < 0) dvis = 0;
        moved = 1;
    }
    return moved;
}

static int dock_busy(void)
{
    for (int i = 0; i < APP_COUNT; i++) if (dscale[i] != ((i == APP_GRID || wm_find_app(i)) ? 256 : 0)) return 1;
    if (dvis != 256) return 1;
    return ddodge != ((dock_home.w && wm_window_hits(dock_home)) ? 256 : 0);
}

static void draw_dock(gfx_rect clip)
{
    if (!dock_n || !dock.w) return;
    gfx_rect c = gfx_rect_isect(dock_span(dock), clip);
    if (gfx_rect_empty(&c)) return;
    const ui_theme *th = ui_th();
    gfx_set_clip(&c);
    int rise = ease(dvis), fade = rise < 255 ? rise : 255;
    gfx_shadow(dock.x, dock.y + 3, dock.w, dock.h, 18, 16, (th->light ? 70 : 110) * fade / 256);
    u32 bgc = th->dock_bg, lnc = th->dock_line;
    bgc = (bgc & 0xFFFFFFu) | ((bgc >> 24) * (u32)fade / 256) << 24;
    lnc = (lnc & 0xFFFFFFu) | ((lnc >> 24) * (u32)fade / 256) << 24;
    gfx_round_rect(dock.x, dock.y, dock.w, dock.h, 18, bgc);               /* translucent: the wallpaper shows through */
    gfx_round_outline(dock.x, dock.y, dock.w, dock.h, 18, lnc);
    for (int i = 0; i < dock_n; i++) {
        int app = dock_apps[i];
        int e = ease(dscale[app]);
        gfx_rect r = dock_item_rect(i);
        const gfx_surf *ic = render_app_icon(app, dock_icon);
        gfx_set_clip(&c);                                                  /* render_app_icon may have touched the clip */
        if (hover_item == app && e == 256) gfx_round_rect(r.x - 5, r.y - 5, r.w + 10, r.h + 10, 14, th->hov);
        if (ic) {
            if (e >= 256) gfx_blit(ic, r.x, r.y);
            else gfx_blit_scaled(ic, r.x, r.y, r.w, r.h, e < 255 ? e : 255);
        }
        window *w = wm_find_app(app);
        if (w && e >= 256) {                                    /* a dot under the icon: green = open, grey = minimised */
            u32 col = w->state == WS_MINIMIZED ? ARGB(170, 140, 140, 140) : th->accent;
            gfx_circle(r.x + r.w / 2, r.y + r.h + 7, 2, col);
        }
    }
    gfx_set_clip(&clip);
}

/* ------------------------------------------------------------------ desktop: icons, shortcuts, panels */

#define DI_SIZE  64
#define CELL_W   100
#define CELL_H   112
#define DI_MAX   32
typedef struct { int used, kind, app, cx, cy; char path[96]; } ditem;     /* kind 0 = application, 1 = file/folder shortcut, 2 = ASPI package of type app (path = package name) */
static ditem ditems[DI_MAX];
static int sel_icon = -1, press_item = -1, press_x, press_y, drag_item = -1, drag_x, drag_y;
static u32 last_icon_click;
static u32 seen_gen = 0xFFFFFFFFu;
static int seen_user = -1;

enum { OV_NONE, OV_MENU, OV_INFO, OV_ERROR };
static int ov_mode, ov_item = -1, ov_x, ov_y, ov_hot = -1, ov_n;
static char ov_lines[8][100], ov_title[40];

static gfx_rect desk_cell_rect(int cx, int cy) { return gfx_rect_make(24 + cx * CELL_W, work_area.y + 20 + cy * CELL_H, 96, 104); }
static int grid_cols(void) { int c = (sw - 24) / CELL_W; return c < 1 ? 1 : c; }
static int grid_rows(void) { int r = (work_area.h - 20) / CELL_H; return r < 1 ? 1 : r; }

static int node_abs(int node, char *out, size_t cap)
{
    int chain[24], n = 0;
    for (int x = node; x > 0 && n < 24; x = fs_parent(x)) chain[n++] = x;
    size_t o = 0;
    out[0] = 0;
    for (int i = n - 1; i >= 0 && o + 2 < cap; i--) o += (size_t)ksnprintf(out + o, cap - o, "/%s", fs_name(chain[i]));
    if (!n) ksnprintf(out, cap, "/");
    return n ? 1 : 0;
}

static int shortcut_node(const ditem *d)
{
    int o = fs_get_jail();
    fs_set_jail(0);
    int n = fs_resolve(d->path, 0);
    fs_set_jail(o);
    return n > 0 ? n : -1;
}

static int shortcut_visible(const ditem *d)             /* only the signed-in user's own shortcuts, and only while the file exists */
{
    const char *u = user_name();
    if (!u) return 0;
    char pre[48];
    ksnprintf(pre, sizeof pre, "/drives/S/usrs/%s/", u);
    if (kstrncmp(d->path, pre, kstrlen(pre))) return 0;
    return shortcut_node(d) > 0;
}

static int item_visible(const ditem *d)
{
    if (d->used && d->kind == 0 && d->app == APP_WELCOME && config_get()->setup_done) return 0;        /* the welcome app leaves after the setup */
    return d->used && (d->kind == 0 || d->kind == 2 || shortcut_visible(d));
}

static int cell_taken(int cx, int cy, int except)
{
    for (int i = 0; i < DI_MAX; i++)
        if (i != except && item_visible(&ditems[i]) && ditems[i].cx == cx && ditems[i].cy == cy) return 1;
    return 0;
}

static void nearest_free_cell(int cx, int cy, int except, int *ox, int *oy)
{
    int C = grid_cols(), R = grid_rows();
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;
    if (cx >= C) cx = C - 1;
    if (cy >= R) cy = R - 1;
    int best = -1, bx = cx, by = cy;
    for (int y = 0; y < R; y++)
        for (int x = 0; x < C; x++) {
            if (cell_taken(x, y, except)) continue;
            int d = (x - cx) * (x - cx) + (y - cy) * (y - cy);
            if (best < 0 || d < best) { best = d; bx = x; by = y; }
        }
    *ox = bx; *oy = by;
}

/* The icon of an ASPI "app" package: green squircle with the package's first letter(s); cached per name. */
#define PKG_ICONS 8
static struct { char name[32]; int used; gfx_surf s; u32 px[DI_SIZE * DI_SIZE]; } pkgic[PKG_ICONS];
static int pkgic_next;

static void draw_pkg_icon(const char *name, int x, int y, int size)
{
    if (size != DI_SIZE) return;
    int slot = -1;
    for (int i = 0; i < PKG_ICONS; i++) if (pkgic[i].used && !kstrcmp(pkgic[i].name, name)) slot = i;
    if (slot < 0) {
        slot = pkgic_next++ % PKG_ICONS;
        kstrncpy(pkgic[slot].name, name, 31); pkgic[slot].used = 1;
        u32 *px = pkgic[slot].px;
        int nid = ui_named_icon_id(name);
        if (nid >= 0) {
            const gfx_surf *ns = ui_named_icon(nid, size);
            for (int i = 0; i < size * size; i++) px[i] = ns->px[i];
            pkgic[slot].s.w = pkgic[slot].s.h = pkgic[slot].s.stride = size; pkgic[slot].s.px = px;
            gfx_blit(&pkgic[slot].s, x, y);
            return;
        }
        for (int yy = 0; yy < size; yy++)
            for (int xx = 0; xx < size; xx++)
                px[yy * size + xx] = squircle_cov(xx, yy, size) << 24 | mix(0x9be01c, 0x4c8a00, (u32)(yy * 255 / size));
        char lab[3] = { name[0], name[1] && name[1] >= 'A' && name[1] <= 'Z' ? name[1] : 0, 0 };
        int n = lab[1] ? 2 : 1, sc = 3, tw = n * 8 * sc, x0 = (size - tw) / 2, y0 = (size - 16 * sc) / 2;
        for (int i = 0; i < n; i++) {
            const unsigned char *g = gfx_font_glyph((unsigned char)lab[i]);
            for (int gy = 0; gy < 16; gy++)
                for (int gx = 0; gx < 8; gx++)
                    if (g[gy] >> (7 - gx) & 1)
                        for (int a = 0; a < sc; a++)
                            for (int b = 0; b < sc; b++) {
                                int qx = x0 + (i * 8 + gx) * sc + b, qy = y0 + gy * sc + a;
                                if (qx >= 0 && qx < size && qy >= 0 && qy < size) px[qy * size + qx] = 0xFF101010u;
                            }
        }
        pkgic[slot].s.w = pkgic[slot].s.h = pkgic[slot].s.stride = size; pkgic[slot].s.px = px;
    }
    gfx_blit(&pkgic[slot].s, x, y);
}

void ui_launch_package(const char *name);
static void launch_package(const char *name) { ui_launch_package(name); }
void ui_launch_package(const char *name)
{
    char cmd[32], line[80];
    aspi_app_command(name, cmd, sizeof cmd);
    if (cmd[0]) ksnprintf(line, sizeof line, "as %s", cmd);
    else ksnprintf(line, sizeof line, "aspi run %s", name);
    if (user_name()) { if (!app_ast_bg(line)) ui_note(config_tr("Another background task is still running - try again in a moment", "Başka bir arka plan görevi sürüyor - biraz sonra tekrar dene")); }
    else app_ast_run(line);                              /* not signed in: the terminal asks for the login */
}

static void desk_save(void)
{
    char b[1800];
    int o = 0;
    for (int i = 0; i < DI_MAX && o < 1600; i++) {
        const ditem *d = &ditems[i];
        if (!d->used) continue;
        if (d->kind == 0) o += ksnprintf(b + o, sizeof b - (size_t)o, "A %d %d %d\n", d->app, d->cx, d->cy);
        else if (d->kind == 2) o += ksnprintf(b + o, sizeof b - (size_t)o, "P %d %d %s\n", d->cx, d->cy, d->path);
        else o += ksnprintf(b + o, sizeof b - (size_t)o, "F %d %d %s\n", d->cx, d->cy, d->path);
    }
    int j = fs_get_jail();
    fs_set_jail(0);
    int f = fs_resolve("/.config/desktop", 0);
    if (f == FS_ENOENT) f = fs_create("/.config/desktop", 0, 0);
    if (f > 0) fs_write(f, b, (u32)o, 0);
    fs_set_jail(j);
    persist_flush();
}

static void desk_user_renamed(const char *oldn, const char *newn)       /* shortcuts point into the account's folder */
{
    char pre[48];
    ksnprintf(pre, sizeof pre, "/drives/S/usrs/%s/", oldn);
    size_t pl = kstrlen(pre);
    for (int i = 0; i < DI_MAX; i++) {
        if (!ditems[i].used || ditems[i].kind != 1 || kstrncmp(ditems[i].path, pre, pl)) continue;
        char np[96];
        ksnprintf(np, sizeof np, "/drives/S/usrs/%s/%s", newn, ditems[i].path + pl);
        kstrncpy(ditems[i].path, np, sizeof ditems[i].path - 1);
    }
    desk_save();
    wm_damage(gfx_rect_make(0, 0, sw, sh));
}

static void desk_defaults(void)
{
    kmemset(ditems, 0, sizeof ditems);
    static const int apps[] = { APP_AST, APP_FILES, APP_STORE, APP_WELCOME };
    for (int i = 0; i < 4; i++) { ditems[i].used = 1; ditems[i].kind = 0; ditems[i].app = apps[i]; ditems[i].cx = 0; ditems[i].cy = i; }
}

static int numtok(const char **p)
{
    while (**p == ' ') (*p)++;
    int n = 0, any = 0;
    while (**p >= '0' && **p <= '9') { n = n * 10 + (**p - '0'); (*p)++; any = 1; }
    return any ? n : -1;
}

static void desk_load(void)
{
    desk_defaults();
    int j = fs_get_jail();
    fs_set_jail(0);
    int f = fs_resolve("/.config/desktop", 0);
    if (f > 0) {
        const char *d = fs_data(f);
        u32 len = fs_size(f), i = 0;
        while (i < len) {
            u32 e = i;
            while (e < len && d[e] != '\n') e++;
            char line[130];
            u32 n = e - i < sizeof line - 1 ? e - i : (u32)sizeof line - 1;
            kmemcpy(line, d + i, n); line[n] = 0;
            const char *p = line + 1;
            if (line[0] == 'A') {
                int app = numtok(&p), cx = numtok(&p), cy = numtok(&p);
                for (int k = 0; k < DI_MAX; k++)
                    if (ditems[k].used && ditems[k].kind == 0 && ditems[k].app == app && cx >= 0 && cy >= 0) { ditems[k].cx = cx; ditems[k].cy = cy; }
            } else if (line[0] == 'P') {
                int cx = numtok(&p), cy = numtok(&p);
                while (*p == ' ') p++;
                for (int k = 0; k < DI_MAX && cx >= 0 && cy >= 0 && *p; k++)
                    if (!ditems[k].used) {
                        ditems[k].used = 1; ditems[k].kind = 2; ditems[k].cx = cx; ditems[k].cy = cy;
                        kstrncpy(ditems[k].path, p, 31);
                        break;
                    }
            } else if (line[0] == 'F') {
                int cx = numtok(&p), cy = numtok(&p);
                while (*p == ' ') p++;
                for (int k = 0; k < DI_MAX && cx >= 0 && cy >= 0 && *p == '/'; k++)
                    if (!ditems[k].used) {
                        ditems[k].used = 1; ditems[k].kind = 1; ditems[k].cx = cx; ditems[k].cy = cy;
                        kstrncpy(ditems[k].path, p, sizeof ditems[k].path - 1);
                        break;
                    }
            }
            i = e + 1;
        }
    }
    fs_set_jail(j);
}

static void damage_desktop(void) { wm_damage(gfx_rect_make(0, 0, sw, work_area.y + work_area.h + 20)); }

/* what the explorer calls when something is dropped on the desktop */
static void ov_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    kvsnprintf(ov_lines[0], sizeof ov_lines[0], fmt, ap);
    va_end(ap);
    ksnprintf(ov_title, sizeof ov_title, "%s", config_tr("Error", "Hata"));
    ov_n = 1; ov_mode = OV_ERROR; ov_item = -1;
    wm_damage(gfx_rect_make(0, 0, sw, sh));
}

int ui_desktop_drop(int node, int x, int y)
{
    if (!user_name()) { ov_error("%s", config_tr("Error: sign in first.", "Hata: önce giriş yapın.")); return -1; }
    int g = fs_guard_modify(node);                              /* only the user's own files; never system / root files */
    if (g < 0 || node <= 0) {
        ov_error(config_tr("Error: '%s' is a system file and cannot be placed on the desktop.",
                           "Hata: '%s' bir sistem dosyasıdır, masaüstüne bırakılamaz."), fs_name(node));
        return -2;
    }
    char path[96];
    node_abs(node, path, sizeof path);
    for (int i = 0; i < DI_MAX; i++)
        if (ditems[i].used && ditems[i].kind == 1 && !kstrcmp(ditems[i].path, path)) {
            ov_error(config_tr("'%s' is already on the desktop.", "'%s' zaten masaüstünde."), fs_name(node));
            return -3;
        }
    int slot = -1;
    for (int i = 0; i < DI_MAX; i++) if (!ditems[i].used) { slot = i; break; }
    if (slot < 0) { ov_error("%s", config_tr("Error: the desktop is full.", "Hata: masaüstü dolu.")); return -4; }
    int cx, cy;
    nearest_free_cell((x - 24) / CELL_W, (y - 20 - work_area.y) / CELL_H, -1, &cx, &cy);
    ditems[slot].used = 1; ditems[slot].kind = 1; ditems[slot].cx = cx; ditems[slot].cy = cy;
    kstrncpy(ditems[slot].path, path, sizeof ditems[slot].path - 1);
    desk_save();
    damage_desktop();
    return 0;
}

int ui_desktop_count(void) { int n = 0; for (int i = 0; i < DI_MAX; i++) if (item_visible(&ditems[i])) n++; return n; }
int ui_overlay_mode(void) { return ov_mode; }   /* 0 none, 1 menu, 2 info, 3 error */

static void desk_label(const ditem *d, char *out, size_t cap)
{
    if (d->kind == 0) { kstrncpy(out, app_label(d->app), cap - 1); out[cap - 1] = 0; return; }
    if (d->kind == 2) { kstrncpy(out, d->path, cap - 1); out[cap - 1] = 0; return; }
    int n = shortcut_node(d);
    kstrncpy(out, n > 0 ? fs_name(n) : "?", cap - 1);
    out[cap - 1] = 0;
}

static void draw_item_icon(const ditem *d, int x, int y)           /* x,y: top-left of the DI_SIZE box */
{
    if (d->kind == 0) { draw_app_icon(d->app, x, y, DI_SIZE); return; }
    if (d->kind == 2) { draw_pkg_icon(d->path, x, y, DI_SIZE); return; }
    int n = shortcut_node(d);
    if (n > 0) app_files_draw_node_icon(n, x + (DI_SIZE - 40) / 2, y + (DI_SIZE - 40) / 2);
}

static void draw_desktop_icons(gfx_rect clip)
{
    const ui_theme *th = ui_th();
    for (int i = 0; i < DI_MAX; i++) {
        const ditem *d = &ditems[i];
        if (!item_visible(d)) continue;
        gfx_rect r = desk_cell_rect(d->cx, d->cy);
        gfx_rect c = gfx_rect_isect(r, clip);
        if (gfx_rect_empty(&c)) continue;
        gfx_set_clip(&c);
        if (sel_icon == i) gfx_round_rect(r.x, r.y, r.w, r.h, 12, ARGB(80, 255, 255, 255));
        if (drag_item == i) { gfx_set_clip(&clip); continue; }     /* being carried: only its ghost is drawn */
        draw_item_icon(d, r.x + (r.w - DI_SIZE) / 2, r.y + 6);
        gfx_set_clip(&c);
        char lab[24];
        desk_label(d, lab, sizeof lab);
        if (gfx_text_w(lab) > r.w - 2) { int k = (r.w - 2) / 8 - 2; if (k < 1) k = 1; int m = 0; const char *q = lab; while (*q && m < k) { if ((*q & 0xC0) != 0x80) m++; q++; } while ((*q & 0xC0) == 0x80) q++; kstrncpy(lab + (q - lab), "..", 3); }
        int tx = r.x + (r.w - gfx_text_w(lab)) / 2, ty = r.y + 6 + DI_SIZE + 4;
        gfx_text(tx + 1, ty + 1, lab, ARGB(220, 0, 0, 0));
        gfx_text(tx, ty, lab, th->label);
    }
    gfx_set_clip(&clip);
}

static void draw_drag_ghost(gfx_rect clip)
{
    if (drag_item < 0) return;
    gfx_rect r = gfx_rect_make(drag_x - DI_SIZE / 2 - 4, drag_y - DI_SIZE / 2 - 4, DI_SIZE + 8, DI_SIZE + 8);
    gfx_rect c = gfx_rect_isect(r, clip);
    if (gfx_rect_empty(&c)) return;
    gfx_set_clip(&c);
    int cx, cy;
    nearest_free_cell((drag_x - 24) / CELL_W, (drag_y - 20 - work_area.y) / CELL_H, drag_item, &cx, &cy);
    gfx_set_clip(&clip);
    gfx_rect cell = desk_cell_rect(cx, cy);
    gfx_rect cc = gfx_rect_isect(cell, clip);
    if (!gfx_rect_empty(&cc)) { gfx_set_clip(&cc); gfx_round_rect(cell.x, cell.y, cell.w, cell.h, 12, ARGB(60, 255, 255, 255)); }
    gfx_set_clip(&c);
    draw_item_icon(&ditems[drag_item], drag_x - DI_SIZE / 2, drag_y - DI_SIZE / 2);
    gfx_set_clip(&clip);
}

/* --- overlay panels (context menu, info, error) --- */

static const char *ov_menu_text(int i) { return i == 0 ? config_tr("Info", "Bilgi") : config_tr("Delete", "Sil"); }
static gfx_rect ov_menu_rect(void)
{
    int w = 160, h = 2 * 28 + 10, x = ov_x, y = ov_y;
    if (x + w > sw) x = sw - w;
    if (y + h > sh) y = sh - h;
    return gfx_rect_make(x, y, w, h);
}
static gfx_rect ov_panel_rect(void)
{
    int w = 440, h = 60 + ov_n * 20 + 56;
    if (ov_mode == OV_ERROR) h = 60 + 2 * 20 + 56;
    return gfx_rect_make((sw - w) / 2, (sh - h) / 3, w, h);
}
static gfx_rect ov_ok_rect(void) { gfx_rect p = ov_panel_rect(); return gfx_rect_make(p.x + p.w - 16 - 96, p.y + p.h - 44, 96, 30); }

static void draw_overlay(gfx_rect clip)
{
    if (ov_mode == OV_NONE) return;
    const ui_theme *th = ui_th();
    if (ov_mode == OV_MENU) {
        gfx_rect m = ov_menu_rect();
        gfx_rect c = gfx_rect_isect(gfx_rect_make(m.x - 16, m.y - 16, m.w + 32, m.h + 32), clip);
        if (gfx_rect_empty(&c)) return;
        gfx_set_clip(&c);
        gfx_shadow(m.x, m.y + 3, m.w, m.h, 8, 12, 120);
        gfx_round_rect(m.x, m.y, m.w, m.h, 8, th->line);
        gfx_round_rect(m.x + 1, m.y + 1, m.w - 2, m.h - 2, 7, th->menu);
        for (int i = 0; i < 2; i++) {
            gfx_rect r = gfx_rect_make(m.x + 5, m.y + 5 + i * 28, m.w - 10, 26);
            if (ov_hot == i) gfx_round_rect(r.x, r.y, r.w, r.h, 5, th->menu_hot);
            gfx_text(r.x + 10, r.y + 5, ov_menu_text(i), i == 1 ? th->danger : th->txt);
        }
        gfx_set_clip(&clip);
        return;
    }
    gfx_rect p = ov_panel_rect();
    gfx_rect c = gfx_rect_isect(gfx_rect_make(p.x - 20, p.y - 20, p.w + 40, p.h + 44), clip);
    if (gfx_rect_empty(&c)) return;
    gfx_set_clip(&c);
    gfx_shadow(p.x, p.y + 4, p.w, p.h, 10, 16, 130);
    gfx_round_rect(p.x, p.y, p.w, p.h, 10, th->line);
    gfx_round_rect(p.x + 1, p.y + 1, p.w - 2, p.h - 2, 9, th->panel);
    gfx_text(p.x + 18, p.y + 16, ov_title, ov_mode == OV_ERROR ? th->danger : th->accent);
    for (int i = 0; i < ov_n; i++) {
        int per = (p.w - 36) / 8;
        const char *t = ov_lines[i];
        if (ov_mode == OV_ERROR) {                                  /* wrap an error over two lines */
            int n = (int)kstrlen(t);
            char a[100], b[100];
            int cut = n > per ? per : n;
            if (cut < n) while (cut > 0 && t[cut] != ' ') cut--;
            if (cut <= 0) cut = n > per ? per : n;
            kmemcpy(a, t, (size_t)cut); a[cut] = 0;
            while (t[cut] == ' ') cut++;
            kstrncpy(b, t + cut, 99); b[99] = 0;
            gfx_text(p.x + 18, p.y + 48, a, th->txt);
            gfx_text(p.x + 18, p.y + 68, b, th->txt);
            break;
        }
        gfx_text(p.x + 18, p.y + 48 + i * 20, t, th->txt);
    }
    gfx_rect ok = ov_ok_rect();
    gfx_round_rect(ok.x, ok.y, ok.w, ok.h, 6, th->btn_main);
    gfx_text(ok.x + (ok.w - 16) / 2, ok.y + 7, "OK", th->btn_main_txt);
    gfx_set_clip(&clip);
}

static void ov_close(void) { ov_mode = OV_NONE; ov_item = -1; ov_hot = -1; wm_damage(gfx_rect_make(0, 0, sw, sh)); }

const char *ui_app_label(int app) { return app_label(app); }
static void ov_info(int idx)
{
    const ditem *d = &ditems[idx];
    ov_n = 0;
    char lab[24];
    desk_label(d, lab, sizeof lab);
    ksnprintf(ov_title, sizeof ov_title, "%s", config_tr("Info", "Bilgi"));
    ksnprintf(ov_lines[ov_n++], 100, "%s %s", config_tr("Name:    ", "Ad:      "), lab);
    if (d->kind == 0) {
        ksnprintf(ov_lines[ov_n++], 100, "%s %s", config_tr("Type:    ", "Tür:     "), config_tr("Application", "Uygulama"));
        ksnprintf(ov_lines[ov_n++], 100, "%s %s", config_tr("Source:  ", "Kaynak:  "), config_tr("built in (installed with AstraSageOS)", "yerleşik (AstraSageOS ile gelir)"));
        ksnprintf(ov_lines[ov_n++], 100, "%s %s", config_tr("Delete:  ", "Silme:   "), config_tr("not possible", "mümkün değil"));
        ksnprintf(ov_lines[ov_n++], 100, "%s %s", config_tr("Open:    ", "Açılış:  "), config_tr("double click", "çift tık"));
    } else if (d->kind == 2) {
        ksnprintf(ov_lines[ov_n++], 100, "%s %s", config_tr("Type:    ", "Tür:     "), config_tr("Application (ASPI package)", "Uygulama (ASPI paketi)"));
        ksnprintf(ov_lines[ov_n++], 100, "%s %s", config_tr("Source:  ", "Kaynak:  "), "AstraStore / aspi");
        ksnprintf(ov_lines[ov_n++], 100, "%s", config_tr("Delete uninstalls the package (everywhere).", "Sil paketi kaldırır (her yerden)."));
        ksnprintf(ov_lines[ov_n++], 100, "%s %s", config_tr("Open:    ", "Açılış:  "), config_tr("double click", "çift tık"));
    } else {
        int n = shortcut_node(d);
        char a[40];
        ksnprintf(ov_lines[ov_n++], 100, "%s %s", config_tr("Type:    ", "Tür:     "), n > 0 && fs_is_dir(n) ? config_tr("Folder (desktop shortcut)", "Klasör (masaüstü kısayolu)") : config_tr("File (desktop shortcut)", "Dosya (masaüstü kısayolu)"));
        ksnprintf(ov_lines[ov_n++], 100, "%s %.40s", config_tr("Path:    ", "Yol:     "), d->path + 8);
        if (n > 0 && !fs_is_dir(n)) {
            ksnprintf(a, sizeof a, "%u B", fs_size_real(n));
            ksnprintf(ov_lines[ov_n++], 100, "%s %s", config_tr("Size:    ", "Boyut:   "), a);
        }
        ksnprintf(ov_lines[ov_n++], 100, "%s", config_tr("Delete removes the shortcut only.", "Sil yalnızca kısayolu kaldırır."));
    }
    ov_mode = OV_INFO; ov_item = idx;
    wm_damage(gfx_rect_make(0, 0, sw, sh));
}

static void ov_delete(int idx)
{
    ditem *d = &ditems[idx];
    char lab[24];
    desk_label(d, lab, sizeof lab);
    if (d->kind == 0) {                                             /* the applications that come with the system stay */
        ov_error(config_tr("Error: '%s' is a built-in application and cannot be deleted.",
                           "Hata: '%s' yerleşik bir uygulamadır, silinemez."), lab);
        return;
    }
    if (d->kind == 2) {                                             /* an ASPI app: delete = uninstall, so the desktop and the app menu agree */
        char c[64];
        ksnprintf(c, sizeof c, "aspi remove %s", d->path);
        if (!user_name() || !app_ast_bg(c)) { ov_error(config_tr("Error: cannot remove '%s' right now (sign in / another task is running).", "Hata: '%s' şu an kaldırılamıyor (giriş yap / başka görev sürüyor)."), lab); return; }
        ov_mode = OV_NONE;
        wm_damage(gfx_rect_make(0, 0, sw, sh));
        return;                                                     /* the icon goes when the package is gone (desk_follow_files) */
    }
    d->used = 0;
    if (sel_icon == idx) sel_icon = -1;
    ov_mode = OV_NONE;
    desk_save();
    wm_damage(gfx_rect_make(0, 0, sw, sh));
}

/* returns 1 when the overlay consumed the event */
static int overlay_pointer(const mouse_ev *e, int ldown, int rdown)
{
    if (ov_mode == OV_NONE) return 0;
    if (ov_mode == OV_MENU) {
        gfx_rect m = ov_menu_rect();
        int hot = -1;
        if (gfx_rect_has(&m, e->x, e->y)) { hot = (e->y - m.y - 5) / 28; if (hot < 0 || hot > 1) hot = -1; }
        if (hot != ov_hot) { ov_hot = hot; wm_damage(m); }
        if (ldown || rdown) {
            int it = ov_item;
            if (ldown && hot == 0) ov_info(it);
            else if (ldown && hot == 1) ov_delete(it);
            else ov_close();
        }
        return 1;
    }
    if (ldown) {
        gfx_rect ok = ov_ok_rect(), p = ov_panel_rect();
        if (gfx_rect_has(&ok, e->x, e->y) || !gfx_rect_has(&p, e->x, e->y)) ov_close();
    }
    return 1;
}

static int overlay_key(int k)
{
    if (ov_mode == OV_NONE) return 0;
    if (k == 27 || k == '\n') ov_close();
    return 1;
}

/* ------------------------------------------------------------------ compose / present */

static void compose_scene(gfx_rect clip);

/*
 * While a window is carried, everything else on the screen is composed ONCE into drag_bg; every frame then only copies
 * the damaged part of it and puts the cached window sprite on top. Without this, each step re-rendered whatever the window
 * uncovered (terminal text, shadows, icons) - the main reason moving windows felt slow in the emulator.
 */
static u32 drag_bg_px[GFX_MAXW * GFX_MAXH];
static gfx_surf drag_bg;
static int drag_bg_ok, drag_bg_slot = -1;

static void drag_bg_build(int slot)
{
    drag_bg.w = sw; drag_bg.h = sh; drag_bg.stride = sw; drag_bg.px = drag_bg_px;
    gfx_set_target(&drag_bg);
    wm_set_skip(slot);
    compose_scene(gfx_rect_make(0, 0, sw, sh));
    wm_set_skip(-1);
    gfx_set_target(0);
    drag_bg_ok = 1; drag_bg_slot = slot;
}

static void compose(gfx_rect clip)
{
    int slot = wm_drag_slot();
    if (slot < 0) { drag_bg_ok = 0; compose_scene(clip); return; }
    if (wm_drag_foreign_take()) drag_bg_ok = 0;                 /* something else changed (e.g. terminal output): rebuild */
    if (!drag_bg_ok || drag_bg_slot != slot) drag_bg_build(slot);
    gfx_set_clip(&clip);
    gfx_blit_opaque(&drag_bg, 0, 0);
    gfx_set_clip(0);
    wm_draw_dragged(clip);
}

static void compose_scene(gfx_rect clip)
{
    gfx_set_clip(&clip);
    gfx_blit_opaque(&wall, 0, 0);
    draw_desktop_icons(clip);
    wm_draw_all(clip);
    draw_dock(clip);
    shell_draw(clip);
    draw_drag_ghost(clip);
    app_files_draw_ghost();
    draw_overlay(clip);
    wp_picker_draw(clip);
    login_screen_draw(clip);                                     /* the full-screen login covers everything */
    gfx_set_clip(0);
}

static int mx_, my_, cur_dirty;
static gfx_rect cur_old;
static gfx_rect cursor_box(void) { return gfx_rect_make(mx_ - 1, my_ - 1, 16, 21); }

/* The dock follows the windows: when an app opens, is minimised or closes, its icon grows or shrinks and the dock
 * rises or sinks; both the old and the new area are redrawn. */
static void dock_state_damage(int settle)
{
    gfx_rect old = dock;
    static int last_n = -1;
    static u32 last_sig = 0xFFFFFFFFu;
    u32 sig = 0;
    for (int i = 0; i < APP_COUNT; i++) {
        window *w = wm_find_app(i);
        sig = sig * 3 + (w ? (w->state == WS_MINIMIZED ? 2u : 1u) : 0u);
    }
    int busy = dock_busy() || settle;
    if (!busy && sig == last_sig && last_n == dock_n) { danim_last = ui_ms(); return; }
    if (busy) dock_update(settle);
    else danim_last = ui_ms();
    last_sig = sig;
    dock_layout();
    last_n = dock_n;
    if (hover_item >= 0 && hover_item != APP_GRID && !wm_find_app(hover_item)) hover_item = -1;
    if (old.w) wm_damage(dock_span(old));
    if (dock.w) wm_damage(dock_span(dock));
}

void ui_anim_settle(void)                                       /* jump to the end of every animation (tests) */
{
    dock_state_damage(1);
    dock_state_damage(1);
}

static void present_cursor(void)
{
    if (!cur_dirty) return;
    cur_dirty = 0;
    gfx_rect n = cursor_box();
    gfx_rect is = gfx_rect_isect(cur_old, n);
    if (!gfx_rect_empty(&is)) { gfx_present(gfx_rect_union(cur_old, n), mx_, my_); return; }   /* one copy: no frame without the pointer in between */
    gfx_present(cur_old, mx_, my_);
    gfx_present(n, mx_, my_);
}

static void desk_follow_files(void);
static void desk_save(void);

int ui_desk_has_app(int app) { for (int i = 0; i < DI_MAX; i++) if (ditems[i].used && ditems[i].kind == 0 && ditems[i].app == app) return 1; return 0; }
int ui_desk_has_pkg(const char *name) { for (int i = 0; i < DI_MAX; i++) if (ditems[i].used && ditems[i].kind == 2 && !kstrcmp(ditems[i].path, name)) return 1; return 0; }
static void desk_add(int kind, int app, const char *pkg)
{
    int slot = -1;
    for (int i = 0; i < DI_MAX; i++) if (!ditems[i].used) { slot = i; break; }
    if (slot < 0) return;
    int cx, cy;
    nearest_free_cell(0, 0, -1, &cx, &cy);
    ditems[slot].used = 1; ditems[slot].kind = kind; ditems[slot].app = app; ditems[slot].cx = cx; ditems[slot].cy = cy;
    ditems[slot].path[0] = 0;
    if (pkg) { kstrncpy(ditems[slot].path, pkg, 95); ditems[slot].path[95] = 0; }
    desk_save();
    damage_desktop();
}
void ui_desk_add_app(int app) { if (!ui_desk_has_app(app)) desk_add(0, app, 0); }
void ui_desk_add_pkg(const char *name) { if (!ui_desk_has_pkg(name)) desk_add(2, 0, name); }

static void frame(void)
{
    gfx_rect d;
    static u32 seen_theme = 0xFFFFFFFFu;
    if (ui_theme_serial() != seen_theme) { seen_theme = ui_theme_serial(); wm_damage(gfx_rect_make(0, 0, sw, sh)); }
    if (wall_idx != wp_effective()) { prepare_wallpaper(); wm_damage(gfx_rect_make(0, 0, sw, sh)); }   /* theme or picker changed the picture */
    wp_picker_tick();
    desk_follow_files();
    app_ast_pending_pump();
    app_files_pump();
    app_welcome_pump();
    app_store_pump();
    app_tasks_pump();
    shell_tick();
    dock_state_damage(0);
    app_ast_pump_damage();
    /* tearing: the emulator scans the framebuffer on its own clock, so every compose that happens between two copies is visible as a
     * half-updated frame. Everything is composed into the back buffer first, then all rectangles are copied in one quick burst. */
    gfx_rect burst[24]; int nb = 0;
    while (wm_take_damage(&d)) {
        compose(d);
        if (nb == 24) { for (int i = 0; i < nb; i++) gfx_present(burst[i], mx_, my_); nb = 0; }
        burst[nb++] = d;
    }
    for (int i = 0; i < nb; i++) gfx_present(burst[i], mx_, my_);
    present_cursor();                                           /* last: the pointer moves together with what it carries */
}

/* ------------------------------------------------------------------ boot splash */

/* the boot log in the top right corner: green = done, blue = information, yellow = warning, red = error */
static void splash_log(int shown)
{
    static const u32 colr[4] = { 0x5AA9FF, 0x3CE05A, 0xF2C94C, 0xFF4D4D };
    static const char *const tag[4] = { "[INFO] ", "[ OK ] ", "[WARN] ", "[FAIL] " };
    int colw = sw / 2 - 16;
    int chars = colw / 8;
    int rows = (sh - 24) / 18;
    int n = shown < bl_count() ? shown : bl_count();
    int first = n > rows ? n - rows : 0;
    int x = sw - colw - 12;
    gfx_set_clip(0);
    gfx_fill_rect(x - 4, 0, colw + 16, sh, 0xFF000000);
    for (int i = first; i < n; i++) {
        int lv = bl_level(i);
        if (lv < 0 || lv > 3) lv = 0;
        char t[120];
        ksnprintf(t, sizeof t, "%s%s", tag[lv], bl_text(i));
        if ((int)kstrlen(t) > chars) { t[chars - 2] = '.'; t[chars - 1] = '.'; t[chars] = 0; }
        gfx_text(x, 12 + (i - first) * 18, t, COL(colr[lv]));
    }
}

static void splash_paint(int shown, int fill_pct)
{
    gfx_set_target(0);
    gfx_set_clip(0);
    gfx_clear(0x000000);
    bmp_view v;
    int rc = bmp_open(asset_boot_logo_start, (u32)(asset_boot_logo_end - asset_boot_logo_start), &v);
    int lw = sh / 3 < 256 ? sh / 3 : 256;
    int lx = (sw / 2 - lw) / 2, ly = sh / 2 - lw / 2 - 20;       /* the logo sits in the left half, the log in the right */
    if (rc == BMP_OK) gfx_draw_bmp_scaled(&v, (gfx_surf *)gfx_back(), lx, ly, lw, lw, 0);
    else gfx_text(lx + lw / 2 - 12, ly + lw / 2, "As", COL(0x9be01c));
    const char *t = AS_OS_NAME;
    gfx_text(lx + (lw - gfx_text_w(t)) / 2, ly + lw + 22, t, COL(0xBBBBBB));
    int bw = 200, bx = lx + (lw - bw) / 2, by = ly + lw + 56;
    gfx_round_rect(bx, by, bw, 4, 2, COL(0x2a2a2a));
    int fill = bw * fill_pct / 100;
    if (fill > 3) gfx_round_rect(bx, by, fill, 4, 2, COL(0x9be01c));
    splash_log(shown);
    gfx_present(gfx_rect_make(0, 0, sw, sh), 0, 0);
}

/* Shows the lines recorded so far one after the other (they are real: each was written by the step it describes). */
static void splash_reveal(int from, int to, u32 ms, int pct0, int pct1)
{
    u32 t0 = ui_ms(), spins = 0;
    for (;;) {
        u32 el = ui_ms() - t0;
        int k = ms ? (int)((u32)(to - from) * (el > ms ? ms : el) / ms) : to - from;
        int pct = pct0 + (pct1 - pct0) * (int)(el > ms ? ms : el) / (int)(ms ? ms : 1);
        splash_paint(from + k, pct);
        if (el >= ms || ++spins > 400000) break;
        for (volatile int i = 0; i < 2000; i++) { }
    }
    splash_paint(to, pct1);
}

static int splash_shown;

void ui_boot_splash(void)
{
    const video_info *vi = video_get();
    if (!vi || !gfx_init(vi)) return;
    sw = (int)vi->w; sh = (int)vi->h;
    int rc = bmp_open(asset_boot_logo_start, (u32)(asset_boot_logo_end - asset_boot_logo_start), &(bmp_view){0});
    if (rc == BMP_OK) bl_add(BL_OK, "Boot logo loaded: assets/boot_logo.bmp");
    else bl_add(BL_ERR, "Boot logo assets/boot_logo.bmp: %s", bmp_strerror(rc));
    splash_reveal(0, bl_count(), 700, 0, 60);
    splash_shown = bl_count();
}

/* After ui_start: the steps taken meanwhile are added, the bar completes. */
void ui_boot_splash_finish(void)
{
    if (!video_get() || !splash_shown) return;
    bl_add(BL_OK, "Desktop ready: %dx%d, theme %d", sw, sh, config_get()->theme);
    splash_reveal(splash_shown, bl_count(), 500, 60, 100);
    splash_shown = bl_count();
}

/* ------------------------------------------------------------------ desktop start, events */

/* Shortcuts whose file was deleted (in AST or the explorer) disappear; a different user shows different shortcuts. */
static void desk_follow_files(void)
{
    int u = user_name() ? (int)kstrlen(user_name()) * 31 + user_name()[0] : 0;
    if (fs_generation() == seen_gen && u == seen_user) return;
    seen_gen = fs_generation(); seen_user = u;
    int changed = 0;
    {   /* ASPI packages of type app: an icon each (new ones appear, removed ones go) */
        static char names[DI_MAX][32];
        int n = aspi_app_packages(names, DI_MAX);
        for (int i = 0; i < DI_MAX; i++) {
            if (!ditems[i].used || ditems[i].kind != 2) continue;
            int have = 0;
            for (int k = 0; k < n; k++) if (!kstrcmp(names[k], ditems[i].path)) have = 1;
            if (!have) { ditems[i].used = 0; changed = 1; }
        }
        for (int k = 0; k < n; k++) {
            int have = 0, slot = -1;
            for (int i = 0; i < DI_MAX; i++) {
                if (ditems[i].used && ditems[i].kind == 2 && !kstrcmp(ditems[i].path, names[k])) have = 1;
                if (!ditems[i].used && slot < 0) slot = i;
            }
            if (have || slot < 0) continue;
            int cx, cy;
            nearest_free_cell(1, 0, -1, &cx, &cy);
            ditems[slot].used = 1; ditems[slot].kind = 2; ditems[slot].cx = cx; ditems[slot].cy = cy;
            kstrncpy(ditems[slot].path, names[k], 31); ditems[slot].path[31] = 0;
            changed = 1;
        }
    }
    for (int i = 0; i < DI_MAX; i++)
        if (ditems[i].used && ditems[i].kind == 1 && user_name() && shortcut_node(&ditems[i]) < 0) {
            char pre[48];
            ksnprintf(pre, sizeof pre, "/drives/S/usrs/%s/", user_name());
            if (!kstrncmp(ditems[i].path, pre, kstrlen(pre))) { ditems[i].used = 0; changed = 1; }   /* gone for good */
        }
    if (changed) desk_save();
    damage_desktop();
}

int ui_start(void)
{
    const video_info *vi = video_get();
    if (!vi) return 0;
    if (!gfx_init(vi)) return 0;
    sw = (int)vi->w; sh = (int)vi->h;
    dock_geometry();
    shell_init(sw, sh);
    user_rename_hook = desk_user_renamed;
    ui_theme_set(config_get()->theme);
    prepare_wallpaper();
    bl_add(BL_OK, "Wallpaper prepared (%dx%d)", sw, sh);
    load_icons();
    {
        int okn = 0, tot = 0;
        for (int i = 0; i < APP_COUNT; i++) if (isrc[i].start) { tot++; if (isrc[i].ok) okn++; }
        bl_add(okn == tot ? BL_OK : BL_WARN, "App icons loaded: %d of %d assets/*.bmp", okn, tot);
    }
    ui_theme_set(config_get()->theme);
    bl_add(BL_OK, "Theme applied: %d", config_get()->theme);
    wm_init(sw, sh, work_area);
    wm_register_app(APP_AST, &app_ast_ops);
    wm_register_app(APP_FILES, &app_files_ops);
    wm_register_app(APP_WELCOME, &app_welcome_ops);
    wm_register_app(APP_SETTINGS, &app_settings_ops);
    wm_register_app(APP_TASKS, &app_tasks_ops);
    wm_register_app(APP_SCRIPT, &app_script_ops);
    wm_register_app(APP_STORE, &app_store_ops);
    app_ast_init();
    app_files_init();
    app_welcome_init();
    app_login_init();
    app_store_init();
    kmemset(dscale, 0, sizeof dscale);
    dvis = 0; dock_n = 0; dock = gfx_rect_make(0, 0, 0, 0);
    desk_load();
    bl_add(fs_resolve("/.config/desktop", 0) > 0 ? BL_OK : BL_INFO, fs_resolve("/.config/desktop", 0) > 0 ? "Desktop layout read: /.config/desktop" : "Desktop layout: /.config/desktop not found, defaults used");
    ov_mode = OV_NONE; sel_icon = -1; drag_item = -1; press_item = -1;
    mouse_set_bounds(sw, sh);
    mouse_position(&mx_, &my_);
    gfx_cursor_set(mx_, my_, 1);
    danim_last = ui_ms();
    active = 1;
    wm_damage(gfx_rect_make(0, 0, sw, sh));
    return 1;
}

/* What opens by itself at start: the welcome app until the setup was done; afterwards the terminal or the
 * graphical login, depending on what was chosen there. */
/* An account that exists means the setup was done, even when the "setup_done" flag never reached the disk: the flag is
 * written now and the welcome app (window, desktop icon, grid entry) goes away. */
void ui_setup_reconcile(void)
{
    const as_settings *c = config_get();
    if (c->setup_done) return;
    if (!user_name() && !user_account_count()) return;
    as_settings s = *c;
    s.setup_done = 1;
    config_set(&s);
    window *w = wm_find_app(APP_WELCOME);
    if (w) wm_close(w);
    wm_damage(gfx_rect_make(0, 0, sw, sh));
}

void ui_first_apps(void)
{
    ui_setup_reconcile();
    const as_settings *c = config_get();
    if (!c->setup_done) { app_welcome_launch(); return; }
    if (c->login_gui) { if (!user_name()) app_login_launch(); return; }
    app_ast_launch();
}

void ui_launch_app(int app)
{
    if (app == APP_AST) app_ast_launch();
    else if (app == APP_FILES) app_files_launch();
    else if (app == APP_WELCOME) { ui_setup_reconcile(); if (!config_get()->setup_done) app_welcome_launch(); }     /* gone once the setup is finished */
    else if (app == APP_SETTINGS) app_settings_launch();
    else if (app == APP_TASKS) app_tasks_launch();
    else if (app == APP_LOGIN) app_login_launch();
    else if (app == APP_STORE) app_store_launch();
    else if (app == APP_GRID) shell_toggle_grid();
}

void ui_launch_net(void) { shell_open(3); }

/* ------------------------------------------------------------------ keyboard shortcuts */

static const struct { const char *keys, *en, *tr; } shortcuts[] = {
    { "Ctrl+Alt+T", "Open the terminal",              "Terminali aç" },
    { "Ctrl+Alt+F", "Open the file explorer",         "Dosya gezginini aç" },
    { "Ctrl+Alt+S", "Open Settings",                  "Ayarları aç" },
    { "Ctrl+Alt+B", "Change the wallpaper",           "Arka planı değiştir" },
    { "Ctrl+Alt+G", "Application grid",               "Uygulama menüsü" },
    { "Ctrl+Alt+P", "Open AstraStore",                "AstraStore'u aç" },
    { "Ctrl+Alt+K", "Task manager",                   "Görev yöneticisi" },
    { "Ctrl+Alt+W", "Close the window in front",      "Öndeki pencereyi kapat" },
    { "Ctrl+Alt+D", "Show the desktop / bring back",  "Masaüstünü göster / geri getir" },
    { "Ctrl+Alt+L", "Lock the screen",                "Ekranı kilitle" },
    { "Alt+Tab",    "Switch to the next window",      "Sonraki pencereye geç" },
    { "Alt+Left / Alt+Right", "Snap the window to the left / right half", "Pencereyi sol / sağ yarıya yasla" },
    { "Alt+Up / Alt+Down",    "Maximise / restore or minimise",           "Büyüt / eski boyut veya küçült" },
};
int ui_shortcut_count(void) { return (int)(sizeof shortcuts / sizeof shortcuts[0]); }
const char *ui_shortcut_keys(int i) { return shortcuts[i].keys; }
const char *ui_shortcut_text(int i) { return config_tr(shortcuts[i].en, shortcuts[i].tr); }

static void show_desktop(void)
{
    int any = 0;
    for (int z = 0; z < WM_MAX_WINDOWS; z++) {
        int s = wm_order(z);
        window *w = s >= 0 ? wm_slot(s) : 0;
        if (w && w->state != WS_MINIMIZED) { any = 1; break; }
    }
    for (int z = 0; z < WM_MAX_WINDOWS; z++) {
        int s = wm_order(z);
        window *w = s >= 0 ? wm_slot(s) : 0;
        if (!w) continue;
        if (any && w->state != WS_MINIMIZED) wm_minimize(w);
        else if (!any && w->state == WS_MINIMIZED) wm_restore(w);
    }
}

int ui_hotkey(int k)
{
    if (!active || ui_login_active() || wp_picker_active()) return 0;
    if (k >= KEY_HOT && k < KEY_HOT + 26) {
        switch ('a' + (k - KEY_HOT)) {
        case 't': ui_launch_app(APP_AST); break;
        case 'f': ui_launch_app(APP_FILES); break;
        case 's': ui_launch_app(APP_SETTINGS); break;
        case 'b': wp_picker_toggle(); break;
        case 'g': ui_launch_app(APP_GRID); break;
        case 'p': ui_launch_app(APP_STORE); break;
        case 'k': ui_launch_app(APP_TASKS); break;
        case 'w': { window *w = wm_focused(); if (w && w->used && w->state != WS_MINIMIZED) wm_request_close(w); break; }
        case 'd': show_desktop(); break;
        case 'l': ui_lock_now(); break;
        default: return 0;
        }
        return 1;
    }
    if (k == KEY_ALT_TAB) { wm_cycle(); return 1; }
    if (k >= KEY_ALT_LEFT && k <= KEY_ALT_DOWN) {
        window *w = wm_focused();
        if (!w || !w->used || w->state == WS_MINIMIZED) return 1;
        if (k == KEY_ALT_LEFT) wm_snap(w, 0);
        else if (k == KEY_ALT_RIGHT) wm_snap(w, 1);
        else if (k == KEY_ALT_UP) wm_toggle_maximize(w);
        else if (w->state == WS_MAXIMIZED) wm_toggle_maximize(w);
        else if (w->snapped) wm_unsnap(w);
        else wm_minimize(w);
        return 1;
    }
    return 0;
}

static void move_pointer(int x, int y)
{
    if (x == mx_ && y == my_) return;
    if (!cur_dirty) { cur_old = cursor_box(); cur_dirty = 1; }
    else cur_old = gfx_rect_union(cur_old, cursor_box());
    mx_ = x; my_ = y;
    gfx_cursor_set(mx_, my_, 1);
}

static int item_at(int x, int y)
{
    for (int i = 0; i < DI_MAX; i++) {
        if (!item_visible(&ditems[i])) continue;
        gfx_rect r = desk_cell_rect(ditems[i].cx, ditems[i].cy);
        if (gfx_rect_has(&r, x, y)) return i;
    }
    return -1;
}

static void pointer_event(const mouse_ev *e)
{
    static int prev;
    int down = (e->buttons & 1) && !(prev & 1);
    int rdown = (e->buttons & 2) && !(prev & 2);
    int up = !(e->buttons & 1) && (prev & 1);
    prev = e->buttons;

    if (login_screen_pointer(e, down)) { move_pointer(e->x, e->y); return; }
    if (wp_picker_pointer(e, down)) { move_pointer(e->x, e->y); return; }
    if (overlay_pointer(e, down, rdown)) { move_pointer(e->x, e->y); return; }
    if (shell_pointer(e, down, rdown, up)) { move_pointer(e->x, e->y); return; }

    int over_dock = dock_n && dock.w && gfx_rect_has(&dock, e->x, e->y);
    int h = over_dock ? ui_dock_hit(e->x, e->y) : -1;
    if (h != hover_item) { hover_item = h; if (dock.w) wm_damage(gfx_rect_make(dock.x - 8, dock.y - 8, dock.w + 16, dock.h + 16)); }

    /* carrying a desktop icon */
    if (press_item >= 0 && (e->buttons & 1)) {
        int dx = e->x - press_x, dy = e->y - press_y;
        if (drag_item < 0 && dx * dx + dy * dy > 36) { drag_item = press_item; sel_icon = press_item; damage_desktop(); }
        if (drag_item >= 0) {
            wm_damage(gfx_rect_make(drag_x - DI_SIZE / 2 - 8, drag_y - DI_SIZE / 2 - 8, DI_SIZE + 16, DI_SIZE + 16));
            drag_x = e->x; drag_y = e->y;
            wm_damage(gfx_rect_make(drag_x - DI_SIZE / 2 - 8, drag_y - DI_SIZE / 2 - 8, DI_SIZE + 16, DI_SIZE + 16));
            wm_damage(gfx_rect_make(0, 0, 0, 0));
            int cx, cy;
            nearest_free_cell((drag_x - 24) / CELL_W, (drag_y - 20 - work_area.y) / CELL_H, drag_item, &cx, &cy);
            static int pcx = -1, pcy = -1;                              /* the highlighted cell moved: repaint the OLD one too, */
            if (pcx >= 0 && (pcx != cx || pcy != cy)) wm_damage(desk_cell_rect(pcx, pcy));   /* or its translucent squares pile up */
            pcx = cx; pcy = cy;
            wm_damage(desk_cell_rect(cx, cy));
        }
        move_pointer(e->x, e->y);
        return;
    }
    if (up && press_item >= 0) {
        if (drag_item >= 0) {
            int cx, cy;
            nearest_free_cell((e->x - 24) / CELL_W, (e->y - 20 - work_area.y) / CELL_H, drag_item, &cx, &cy);
            ditems[drag_item].cx = cx; ditems[drag_item].cy = cy;
            drag_item = -1;
            desk_save();
            damage_desktop();
        }
        press_item = -1;
        move_pointer(e->x, e->y);
        return;
    }

    if ((down || rdown) && !wm_busy()) {
        if (over_dock) {
            if (down && h >= 0) ui_launch_app(h);
        } else if (!wm_window_at(e->x, e->y)) {
            int hit = item_at(e->x, e->y);
            if (rdown && hit >= 0) {                                   /* right click on an icon: Info / Delete */
                sel_icon = hit; ov_mode = OV_MENU; ov_item = hit; ov_x = e->x; ov_y = e->y; ov_hot = -1;
                damage_desktop();
                wm_damage(gfx_rect_make(0, 0, sw, sh));
                move_pointer(e->x, e->y);
                return;
            }
            if (down) {
                if (hit >= 0) {
                    u32 now = ui_ms();
                    if (sel_icon == hit && now - last_icon_click < 500) {
                        press_item = -1;
                        sel_icon = -1;
                        if (ditems[hit].kind == 0) ui_launch_app(ditems[hit].app);
                        else if (ditems[hit].kind == 2) launch_package(ditems[hit].path);
                        else { int n = shortcut_node(&ditems[hit]); if (n > 0) app_files_open_node(n); }
                        last_icon_click = 0;
                    } else { sel_icon = hit; press_item = hit; press_x = e->x; press_y = e->y; last_icon_click = now; }
                } else sel_icon = -1;
                damage_desktop();
            }
        }
    }
    wm_pointer(e->x, e->y, e->buttons, over_dock);
    move_pointer(e->x, e->y);
}

/* One round of the desktop loop; the test harness calls this directly. */
void ui_step(void)
{
    int k, guard = 0;
    while (guard++ < 256 && kbd_hw_pending()) {        /* drain everything waiting: a mouse packet is three bytes */
        k = kbd_hw_poll();
        if (k >= 0 && !login_screen_key(k) && !ui_hotkey(k) && !wp_picker_key(k) && !overlay_key(k) && !shell_key(k)) wm_key(k);
    }
    mouse_ev e, n;
    if (mouse_pop(&e)) {                                        /* a backlog of plain moves is folded into the newest one: */
        while (mouse_pop(&n)) {                                 /* after a slow frame the pointer then goes straight to where it is */
            if (n.buttons == e.buttons) e = n; else { pointer_event(&e); e = n; }
        }
        pointer_event(&e);
    }
    {   int wz = mouse_wheel_take();
        if (ui_login_active()) wz = 0;
        if (wp_picker_active()) { wp_picker_wheel(wz); wz = 0; }                          /* the wheel moves up / down like the arrow keys in the focused window */
        while (wz > 0) { wm_key(KEY_DOWN); wz--; }
        while (wz < 0) { wm_key(KEY_UP); wz++; }
    }
    login_screen_tick();
    app_ast_pump();
    frame();
}

void ui_run(void)
{
    frame();
    for (;;) {
        ui_step();
        cpu_relax();
    }
}
