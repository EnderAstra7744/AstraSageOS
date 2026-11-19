/*
 * AstraGUI - the window manager. It owns every window's geometry and state (normal / maximised /
 * minimised), the stacking order, the focused window, and the pointer interactions: moving by the
 * title bar, resizing by the edges and corners, the three title-bar buttons and clicks that are
 * passed on to the application. Applications draw only inside their content rectangle; the
 * frame, the order of drawing and the damage tracking are the window manager's job.
 */
#include "gui.h"

#define CLR_CLOSE    0xFFff5f57
#define CLR_MINI     0xFFfebc2e
#define CLR_MAXI     0xFF28c840
#define WIN_RADIUS   10
#define BTN_R        7
#define EDGE         5          /* resize band inside the frame */
#define GRAB         3          /* extra grab margin outside the frame */
#define SHADOW       16
#define TITLE_ICON   22         /* the app icon at the right end of the title bar */

enum { I_NONE, I_DRAG, I_RESIZE, I_APP };
enum { E_L = 1, E_R = 2, E_T = 4, E_B = 8 };
enum { Z_NONE, Z_CLOSE, Z_MIN, Z_MAX, Z_TITLE, Z_CONTENT, Z_EDGE };

static window wins[WM_MAX_WINDOWS];
static int zorder[WM_MAX_WINDOWS], nz = 0;      /* slots, back to front */
static int focus_slot = -1;
static int sw, sh, next_id = 1;
static gfx_rect work;
#define DMG_MAX 6
static gfx_rect dmg[DMG_MAX];                  /* pending damage: a few separate rectangles, not one huge union */
static int ndmg;
static const wm_app *apps[16];

/* drag sprite (see drag_begin) */
#define SPR_W (GFX_MAXW + 2 * SHADOW)
#define SPR_H (GFX_MAXH + 2 * SHADOW + 4)
static u32 spr_px[SPR_W * SPR_H];
static gfx_surf spr;
static int spr_slot = -1;
static int skip_slot = -1;                       /* wm_draw_all leaves this window out (drag background) */
static int drag_own_damage, drag_foreign;       /* damage caused by the drag itself / by anything else while dragging */
static short spr_rx0[SPR_H], spr_rx1[SPR_H];     /* per sprite row: the opaque run that can be copied as it is */

static int mode = I_NONE, mslot, grab_dx, grab_dy, edges;
static gfx_rect start_r;
static int moved;                               /* the pointer has left the press point by more than the click slop */
static int start_px, start_py, prev_buttons, hover_btn, hover_app_slot = -1;
static u32 last_title_click;
static int last_title_slot = -1;

/* ------------------------------------------------------------------ bookkeeping */

void wm_register_app(int app, const wm_app *ops) { if (app >= 0 && app < 16) apps[app] = ops; }

void wm_init(int w, int h, gfx_rect work_area)
{
    kmemset(wins, 0, sizeof wins);
    hover_app_slot = -1; nz = 0; focus_slot = -1; sw = w; sh = h; work = work_area; ndmg = 0;
    mode = I_NONE; prev_buttons = 0; hover_btn = 0; next_id = 1; last_title_slot = -1;
}

void wm_set_work_area(gfx_rect w) { work = w; }

static int rect_area(gfx_rect r) { return r.w * r.h; }

void wm_damage(gfx_rect r)
{
    if (mode == I_DRAG && !drag_own_damage) drag_foreign = 1;
    r = gfx_rect_isect(r, gfx_rect_make(0, 0, sw, sh));
    if (gfx_rect_empty(&r)) return;
    for (int i = 0; i < ndmg;) {                   /* absorb every pending rectangle that overlaps or is close */
        gfx_rect u = gfx_rect_union(dmg[i], r);
        if (rect_area(u) <= (rect_area(dmg[i]) + rect_area(r)) * 5 / 4) {
            r = u;
            dmg[i] = dmg[--ndmg];
            i = 0;
        } else i++;
    }
    if (ndmg == DMG_MAX) {                         /* full: fold into the entry that grows least */
        int best = 0, bg = 0x7FFFFFFF;
        for (int i = 0; i < ndmg; i++) {
            int g = rect_area(gfx_rect_union(dmg[i], r)) - rect_area(dmg[i]);
            if (g < bg) { bg = g; best = i; }
        }
        dmg[best] = gfx_rect_union(dmg[best], r);
        return;
    }
    dmg[ndmg++] = r;
}

int wm_take_damage(gfx_rect *r)
{
    if (!ndmg) return 0;
    *r = dmg[--ndmg];
    return 1;
}

static gfx_rect with_shadow(gfx_rect r)
{
    return gfx_rect_make(r.x - SHADOW, r.y - SHADOW, r.w + 2 * SHADOW, r.h + 2 * SHADOW + 4);
}

static int slot_of(const window *w) { return (int)(w - wins); }

static int zpos(int slot) { for (int i = 0; i < nz; i++) if (zorder[i] == slot) return i; return -1; }

window *wm_slot(int slot) { return slot >= 0 && slot < WM_MAX_WINDOWS && wins[slot].used ? &wins[slot] : 0; }
int wm_order(int idx) { return idx >= 0 && idx < nz ? zorder[idx] : -1; }
window *wm_focused(void) { return wm_slot(focus_slot); }
int wm_busy(void) { return mode != I_NONE; }
int wm_hover_button(void) { return hover_btn; }

int wm_visible_count(void)
{
    int n = 0;
    for (int i = 0; i < nz; i++) if (wins[zorder[i]].state != WS_MINIMIZED) n++;
    return n;
}

window *wm_find_app(int app)
{
    for (int i = 0; i < WM_MAX_WINDOWS; i++) if (wins[i].used && wins[i].app == app) return &wins[i];
    return 0;
}

gfx_rect wm_content_rect(const window *w)
{
    return gfx_rect_make(w->r.x + WM_BORDER, w->r.y + WM_TITLE_H, w->r.w - 2 * WM_BORDER, w->r.h - WM_TITLE_H - WM_BORDER);
}

static void focus_top_visible(void)
{
    focus_slot = -1;
    for (int i = nz - 1; i >= 0; i--)
        if (wins[zorder[i]].state != WS_MINIMIZED) { focus_slot = zorder[i]; break; }
}

/* ------------------------------------------------------------------ lifecycle */

window *wm_create(const char *title, int app, int x, int y, int w, int h)
{
    int slot = -1;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) if (!wins[i].used) { slot = i; break; }
    if (slot < 0) return 0;
    window *win = &wins[slot];
    kmemset(win, 0, sizeof *win);
    win->used = 1; win->id = next_id++; win->app = app;
    kstrncpy(win->title, title, sizeof win->title - 1);
    win->minw = 360; win->minh = 220;
    if (w < win->minw) w = win->minw;
    if (h < win->minh) h = win->minh;
    if (w > sw) w = sw;
    if (h > work.y + work.h) h = work.y + work.h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + w > sw) x = sw - w;
    if (y + h > work.y + work.h) y = work.y + work.h - h;
    win->r = gfx_rect_make(x, y, w, h);
    win->restore = win->r;
    win->state = WS_NORMAL;
    zorder[nz++] = slot;
    focus_slot = slot;
    wm_damage(with_shadow(win->r));
    snd_play(SND_OPEN);
    return win;
}

void wm_destroy(window *w)
{
    if (!w || !w->used) return;
    snd_play(SND_CLOSE);
    int slot = slot_of(w), z = zpos(slot);
    wm_damage(with_shadow(w->r));
    if (z >= 0) { for (int i = z; i < nz - 1; i++) zorder[i] = zorder[i + 1]; nz--; }
    if (mode != I_NONE && mslot == slot) { mode = I_NONE; spr_slot = -1; }
    if (hover_app_slot == slot) hover_app_slot = -1;
    w->used = 0;
    if (focus_slot == slot) focus_top_visible();
    if (focus_slot >= 0) wm_damage(with_shadow(wins[focus_slot].r));
}

void wm_raise(window *w)
{
    if (!w || !w->used) return;
    int slot = slot_of(w), z = zpos(slot);
    if (z >= 0 && z != nz - 1) {
        for (int i = z; i < nz - 1; i++) zorder[i] = zorder[i + 1];
        zorder[nz - 1] = slot;
    }
    if (focus_slot != slot) {
        if (focus_slot >= 0 && wins[focus_slot].used) wm_damage(with_shadow(wins[focus_slot].r));
        focus_slot = slot;
    }
    wm_damage(with_shadow(w->r));
}

static void notify_resized(window *w)
{
    if (apps[w->app] && apps[w->app]->resized) apps[w->app]->resized(w, wm_content_rect(w));
}

void wm_minimize(window *w)
{
    if (!w || !w->used || w->state == WS_MINIMIZED) return;
    w->was_max = w->state == WS_MAXIMIZED;
    w->state = WS_MINIMIZED;                              /* geometry (r, restore) is kept as it was */
    wm_damage(with_shadow(w->r));
    if (focus_slot == slot_of(w)) focus_top_visible();
    if (focus_slot >= 0) wm_damage(with_shadow(wins[focus_slot].r));
}

void wm_restore(window *w)
{
    if (!w || !w->used || w->state != WS_MINIMIZED) return;
    w->state = w->was_max ? WS_MAXIMIZED : WS_NORMAL;
    wm_raise(w);
}

void wm_toggle_maximize(window *w)
{
    if (!w || !w->used || w->state == WS_MINIMIZED) return;
    if (w->snapped) { w->snapped = 0; w->restore = w->r; }       /* maximising a snapped window: the half is what it comes back to */
    wm_damage(with_shadow(w->r));
    if (w->state == WS_MAXIMIZED) {
        w->r = w->restore;
        w->state = WS_NORMAL;
    } else {
        w->restore = w->r;
        w->r = work;
        w->state = WS_MAXIMIZED;
    }
    wm_damage(with_shadow(w->r));
    notify_resized(w);
}

void wm_snap(window *w, int side)
{
    if (!w || !w->used || w->state == WS_MINIMIZED) return;
    int hw = work.w / 2;
    if (hw < w->minw) return;                                   /* the app does not fit in half the screen */
    wm_damage(with_shadow(w->r));
    if (!w->snapped && w->state == WS_NORMAL) w->restore = w->r;
    gfx_rect r = work;
    r.w = hw;
    if (side) r.x = work.x + work.w - hw;
    w->r = r;
    w->state = WS_NORMAL;
    w->snapped = 1;
    wm_damage(with_shadow(w->r));
    notify_resized(w);
}

void wm_unsnap(window *w)
{
    if (!w || !w->used || !w->snapped) return;
    wm_damage(with_shadow(w->r));
    w->r = w->restore;
    w->snapped = 0;
    wm_damage(with_shadow(w->r));
    notify_resized(w);
}

void wm_cycle(void)
{
    if (nz < 2) return;
    window *w = &wins[zorder[0]];                               /* z position 0 = the back */
    if (w->state == WS_MINIMIZED) wm_restore(w); else wm_raise(w);
}

static void close_window(window *w)
{
    const wm_app *a = apps[w->app];
    if (a && a->can_close && !a->can_close(w)) return;        /* the application keeps its window (e.g. asks first) */
    if (a && a->closed) a->closed(w);
    wm_destroy(w);
}

void wm_request_close(window *w)                           /* the close button / Ctrl+Alt+W: the app may ask first */
{
    if (w && w->used) close_window(w);
}

void wm_close(window *w)                                   /* closed by the application itself: no veto */
{
    if (!w || !w->used) return;
    const wm_app *a = apps[w->app];
    if (a && a->closed) a->closed(w);
    wm_destroy(w);
}

/* ------------------------------------------------------------------ hit testing */

static int btn_cx(const window *w, int i) { return w->r.x + 20 + i * 22; }
static int btn_cy(const window *w) { return w->r.y + WM_TITLE_H / 2; }

static int zone_at(const window *w, int x, int y, int *edge_mask)
{
    *edge_mask = 0;
    for (int i = 0; i < 3; i++) {
        int dx = x - btn_cx(w, i), dy = y - btn_cy(w);
        if (dx * dx + dy * dy <= (BTN_R + 2) * (BTN_R + 2)) return Z_CLOSE + i;
    }
    if (w->state == WS_NORMAL) {
        int e = 0;
        int cw = 14;                                      /* corner grab zone */
        int inl = x < w->r.x + EDGE, inr = x >= w->r.x + w->r.w - EDGE;
        int int_ = y < w->r.y + 3, inb = y >= w->r.y + w->r.h - EDGE;
        if (inl) e |= E_L;
        if (inr) e |= E_R;
        if (inb) e |= E_B;
        /* The top edge is a resize edge only at the two corners: along the title bar the top pixels belong to the
         * title (moving the window), otherwise a click that is a few pixels too high starts a resize. */
        if (int_ && (x < w->r.x + cw || x >= w->r.x + w->r.w - cw)) e |= E_T | (x < w->r.x + w->r.w / 2 ? E_L : E_R);
        if (!(e & E_T) && (inl || inr) && y < w->r.y + 8) e |= E_T;          /* the rounded top corner */
        if (inb && !(e & (E_L | E_R))) {                  /* wider grab zone at the bottom corners */
            if (x < w->r.x + cw) e |= E_L; else if (x >= w->r.x + w->r.w - cw) e |= E_R;
        }
        if (e) { *edge_mask = e; return Z_EDGE; }
    }
    if (y < w->r.y + WM_TITLE_H) return Z_TITLE;
    return Z_CONTENT;
}

window *wm_window_at(int x, int y)
{
    for (int i = nz - 1; i >= 0; i--) {
        window *w = &wins[zorder[i]];
        if (w->state == WS_MINIMIZED) continue;
        gfx_rect r = w->r;
        if (w->state == WS_NORMAL) { r.x -= GRAB; r.y -= GRAB; r.w += 2 * GRAB; r.h += 2 * GRAB; }
        if (gfx_rect_has(&r, x, y)) return w;
    }
    return 0;
}

/* ------------------------------------------------------------------ pointer */

/*
 * Moving a window must be cheap: the terminal, frame, title and the soft shadow are rendered ONCE into a
 * transparent sprite when the drag starts; every pointer move then only blits that sprite (plus the wallpaper
 * behind it). Without this each move re-rendered ~1 million pixels, which was far too slow in an emulator.
 */
static void draw_window(window *w, gfx_rect clip, int focused);

/* Window transparency (Settings): frame and inside opacity, 30..100 %. */
static int rendering_sprite;
static u32 snap_px[GFX_MAXW * GFX_MAXH];
static int alpha_frame(void) { return config_get()->win_a_frame * 255 / 100; }
static int alpha_content(void) { return config_get()->win_a_content * 255 / 100; }
static int translucent(void) { return alpha_frame() < 255 || alpha_content() < 255; }

static void drag_begin(window *w)
{
    spr_slot = -1;
    int sw_ = w->r.w + 2 * SHADOW, sh_ = w->r.h + 2 * SHADOW + 4;
    if (sw_ > SPR_W || sh_ > SPR_H) return;                  /* cannot happen on a legal screen: draw normally */
    spr.w = sw_; spr.h = sh_; spr.stride = sw_; spr.px = spr_px;
    kmemset(spr_px, 0, (size_t)sw_ * (size_t)sh_ * 4);
    window tmp = *w;
    tmp.r = gfx_rect_make(SHADOW, SHADOW, w->r.w, w->r.h);
    int saved_hover = hover_btn;
    hover_btn = 0;
    gfx_set_target(&spr);
    gfx_set_target_alpha(1);
    rendering_sprite = 1;
    draw_window(&tmp, gfx_rect_make(0, 0, sw_, sh_), 1);
    rendering_sprite = 0;
    if (translucent()) {                                     /* the sprite carries the transparency itself */
        gfx_rect cr_ = wm_content_rect(&tmp);
        int af = alpha_frame(), ac = alpha_content();
        for (int y = 0; y < tmp.r.h; y++)
            for (int x = 0; x < tmp.r.w; x++) {
                u32 *px = &spr_px[(SHADOW + y) * sw_ + SHADOW + x];
                u32 a = *px >> 24;
                if (!a) continue;
                int inside = SHADOW + x >= cr_.x && SHADOW + x < cr_.x + cr_.w && SHADOW + y >= cr_.y && SHADOW + y < cr_.y + cr_.h;
                a = a * (u32)(inside ? ac : af) / 255;
                *px = a << 24 | (*px & 0xFFFFFF);
            }
    }
    gfx_set_target_alpha(0);
    gfx_set_target(0);
    hover_btn = saved_hover;
    gfx_opaque_runs(&spr, spr_rx0, spr_rx1);
    spr_slot = slot_of(w);
}

static void drag_end(void)
{
    if (spr_slot < 0) return;
    int slot = spr_slot;
    spr_slot = -1;
    if (wins[slot].used) wm_damage(with_shadow(wins[slot].r));
}

static void do_drag(int x, int y)
{
    window *w = &wins[mslot];
    gfx_rect old = w->r;
    int nx = x - grab_dx, ny = y - grab_dy;
    int bottom = sh;                                           /* a window may be dragged over the dock strip: the dock steps down while it touches it */
    int maxx = sw - w->r.w, maxy = bottom - w->r.h;
    if (maxy < 0) maxy = 0;
    if (nx > maxx) nx = maxx;
    if (nx < 0) nx = 0;
    if (ny > maxy) ny = maxy;
    if (ny < 0) ny = 0;
    if (nx == w->r.x && ny == w->r.y) return;
    w->r.x = nx; w->r.y = ny;
    drag_own_damage = 1;
    wm_damage(with_shadow(old));
    wm_damage(with_shadow(w->r));
    drag_own_damage = 0;
}

static void do_resize(int x, int y)
{
    window *w = &wins[mslot];
    gfx_rect old = w->r, s = start_r;
    int dx = x - start_px, dy = y - start_py;
    int x0 = s.x, y0 = s.y, x1 = s.x + s.w, y1 = s.y + s.h;
    if (edges & E_L) x0 += dx;
    if (edges & E_R) x1 += dx;
    if (edges & E_T) y0 += dy;
    if (edges & E_B) y1 += dy;
    if (x1 - x0 < w->minw) { if (edges & E_L) x0 = x1 - w->minw; else x1 = x0 + w->minw; }
    if (y1 - y0 < w->minh) { if (edges & E_T) y0 = y1 - w->minh; else y1 = y0 + w->minh; }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > sw) x1 = sw;
    int bottom = work.y + work.h;
    if (bottom <= 0 || bottom > sh) bottom = sh;
    if (y1 > bottom) y1 = bottom;
    if (x1 - x0 < w->minw || y1 - y0 < w->minh) return;
    gfx_rect nr = gfx_rect_make(x0, y0, x1 - x0, y1 - y0);
    if (nr.x == old.x && nr.y == old.y && nr.w == old.w && nr.h == old.h) return;
    w->r = nr;
    w->restore = nr;
    wm_damage(with_shadow(old));
    wm_damage(with_shadow(nr));
    notify_resized(w);
}

static void set_hover(int b)
{
    if (b == hover_btn) return;
    hover_btn = b;
    window *f = wm_focused();
    if (f) wm_damage(gfx_rect_make(f->r.x, f->r.y, f->r.w, WM_TITLE_H));
}

int wm_key(int key)
{
    window *f = wm_focused();
    if (!f || f->state == WS_MINIMIZED) return 0;
    const wm_app *a = apps[f->app];
    if (!a || !a->key) return 0;
    a->key(f, key);
    return 1;
}

static void app_mouse(window *w, int x, int y, u8 buttons, u8 prev)
{
    const wm_app *a = apps[w->app];
    if (a && a->mouse) a->mouse(w, x, y, buttons, prev);
}

static void hover_leave(void)
{
    if (hover_app_slot >= 0 && wins[hover_app_slot].used) app_mouse(&wins[hover_app_slot], -1, -1, 0, 0);
    hover_app_slot = -1;
}

void wm_pointer(int x, int y, u8 buttons, int over_ui)
{
    u8 prev = (u8)prev_buttons;
    int down = (buttons & 1) && !(prev & 1);
    int rdown = (buttons & 2) && !(prev & 2);
    prev_buttons = buttons;

    if (mode == I_APP) {                                          /* an application holds the pointer */
        window *w = wm_slot(mslot);
        if (!w) { mode = I_NONE; return; }
        app_mouse(w, x, y, buttons, prev);
        if (!(buttons & 3)) mode = I_NONE;
        return;
    }
    if (mode != I_NONE) {
        if (!(buttons & 1) || !wins[mslot].used) { mode = I_NONE; drag_end(); return; }
        if (!moved) {                                              /* a click is not a move: ignore the first few pixels */
            int ddx = x - start_px, ddy = y - start_py;
            if (ddx < 0) ddx = -ddx;
            if (ddy < 0) ddy = -ddy;
            if (ddx < 4 && ddy < 4) return;
            moved = 1;
            last_title_slot = -1;                                  /* a drag never counts as the first click of a double click */
        }
        if (mode == I_DRAG) do_drag(x, y); else do_resize(x, y);
        return;
    }

    window *under = over_ui ? 0 : wm_window_at(x, y);
    int hb = 0, em;
    if (under && under == wm_focused()) {
        int z = zone_at(under, x, y, &em);
        if (z >= Z_CLOSE && z <= Z_MAX) hb = z - Z_CLOSE + 1;
    }
    set_hover(hb);

    /* hover: tell the application under the pointer (only plain moves; a held button is handled below) */
    if (!(down || rdown)) {
        int slot = -1;
        if (under && !buttons) {
            int z = zone_at(under, x, y, &em);
            if (z == Z_CONTENT && apps[under->app] && apps[under->app]->mouse) slot = slot_of(under);
        }
        if (slot != hover_app_slot) hover_leave();
        if (slot >= 0) { hover_app_slot = slot; app_mouse(&wins[slot], x, y, buttons, prev); }
        return;
    }
    hover_leave();
    if (over_ui || !under) return;

    int z = zone_at(under, x, y, &em);
    int slot = slot_of(under);
    if (under != wm_focused()) wm_raise(under);

    if (rdown && !down) {                                          /* right button: only applications use it */
        if (z == Z_CONTENT && apps[under->app] && apps[under->app]->mouse) {
            mode = I_APP; mslot = slot;
            app_mouse(under, x, y, buttons, prev);
        }
        return;
    }

    switch (z) {
    case Z_CLOSE: close_window(under); break;
    case Z_MIN:   wm_minimize(under); break;
    case Z_MAX:   wm_toggle_maximize(under); break;
    case Z_TITLE: {
        u32 now = ui_ms();
        if (last_title_slot == slot && now - last_title_click < 450) {       /* double click */
            wm_toggle_maximize(under);
            last_title_slot = -1;
            break;
        }
        last_title_slot = slot; last_title_click = now;
        if (under->state == WS_NORMAL) {
            under->snapped = 0;
            mode = I_DRAG; mslot = slot; grab_dx = x - under->r.x; grab_dy = y - under->r.y; start_px = x; start_py = y; moved = 0;
            drag_begin(under);
        }
        break;
    }
    case Z_EDGE:
        under->snapped = 0;
        mode = I_RESIZE; mslot = slot; edges = em; start_r = under->r; start_px = x; start_py = y; moved = 0;
        break;
    default: {
        const wm_app *a = apps[under->app];
        if (a && a->mouse) { mode = I_APP; mslot = slot; app_mouse(under, x, y, buttons, prev); }
        else if (a && a->clicked) a->clicked(under, x, y);
        break;
    }
    }
}

/* ------------------------------------------------------------------ drawing */

static void draw_window(window *w, gfx_rect clip, int focused)
{
    gfx_rect full = with_shadow(w->r);
    gfx_rect c = gfx_rect_isect(full, clip);
    if (gfx_rect_empty(&c)) return;
    gfx_set_clip(&c);
    int max = w->state == WS_MAXIMIZED;
    int rad = max ? 0 : WIN_RADIUS;
    gfx_rect r = w->r;

    const ui_theme *th = ui_th();
    if (!max) gfx_shadow(r.x, r.y + 4, r.w, r.h, rad, SHADOW, (th->light ? 80 : 120) * (focused ? 10 : 6) / 10);
    gfx_rect mixr = gfx_rect_isect(r, clip);
    int mix = !rendering_sprite && translucent() && !gfx_rect_empty(&mixr) && mixr.w * mixr.h <= GFX_MAXW * GFX_MAXH;
    if (mix) gfx_snapshot(mixr, snap_px);
    gfx_round_rect(r.x, r.y, r.w, r.h, rad, focused ? th->border_f : th->border_u);
    gfx_round_rect_ex(r.x + 1, r.y + 1, r.w - 2, WM_TITLE_H - 1, rad ? rad - 1 : 0, rad ? rad - 1 : 0, 0, 0,
                      focused ? th->title_f : th->title_u);
    gfx_rect cr = wm_content_rect(w);
    gfx_round_rect_ex(cr.x, cr.y, cr.w, cr.h, 0, 0, rad ? rad - 1 : 0, rad ? rad - 1 : 0, th->content);

    /* title, and the application's icon in the top right corner */
    int tw = gfx_text_w(w->title);
    gfx_text(r.x + (r.w - tw) / 2, r.y + (WM_TITLE_H - 16) / 2, w->title, focused ? th->txt_f : th->txt_u);
    if (r.w > 330) draw_app_icon(w->app, r.x + r.w - 12 - TITLE_ICON, r.y + (WM_TITLE_H - TITLE_ICON) / 2, TITLE_ICON);

    /* buttons: red close, yellow minimise, green maximise / restore */
    static const u32 col[3] = { CLR_CLOSE, CLR_MINI, CLR_MAXI };
    for (int i = 0; i < 3; i++) {
        int bx = btn_cx(w, i), by = btn_cy(w);
        gfx_circle(bx, by, BTN_R, focused ? col[i] : th->btn_off);
        if (focused && hover_btn == i + 1) {
            u32 g = 0xB4000000u;
            if (i == 0) { gfx_line(bx - 2, by - 2, bx + 2, by + 2, g); gfx_line(bx - 2, by + 2, bx + 2, by - 2, g); }
            else if (i == 1) gfx_line(bx - 3, by, bx + 3, by, g);
            else { gfx_line(bx - 3, by, bx + 3, by, g); gfx_line(bx, by - 3, bx, by + 3, g); }
        }
    }

    /* application content */
    gfx_rect cc = gfx_rect_isect(cr, clip);
    if (!gfx_rect_empty(&cc)) {
        gfx_set_clip(&cc);
        const wm_app *a = apps[w->app];
        if (a && a->draw) a->draw(w, cr, cc, focused);
    }
    gfx_set_clip(&clip);
    if (mix) gfx_mix_snapshot(mixr, snap_px, cr, alpha_frame(), alpha_content());
}

int wm_drag_slot(void) { return mode == I_DRAG && spr_slot >= 0 ? spr_slot : -1; }
void wm_set_skip(int slot) { skip_slot = slot; }
int wm_drag_foreign_take(void) { int f = drag_foreign; drag_foreign = 0; return f; }
void wm_draw_dragged(gfx_rect clip)                              /* only the moving window: the cached sprite */
{
    if (wm_drag_slot() < 0) return;
    window *w = &wins[spr_slot];
    gfx_rect c = gfx_rect_isect(with_shadow(w->r), clip);
    if (gfx_rect_empty(&c)) return;
    gfx_rect keep = gfx_get_clip();
    gfx_set_clip(&c);
    gfx_blit_runs(&spr, w->r.x - SHADOW, w->r.y - SHADOW, spr_rx0, spr_rx1);
    gfx_set_clip(&keep);
}

static unsigned culled_total;
unsigned wm_culled_count(void) { return culled_total; }

/* occlusion culling: a window whose damaged part lies completely behind one opaque window above it is not drawn at all */
static int hidden_behind_others(int i, gfx_rect clip)
{
    gfx_rect c = gfx_rect_isect(with_shadow(wins[zorder[i]].r), clip);
    if (gfx_rect_empty(&c)) return 0;
    for (int j = i + 1; j < nz; j++) {
        const window *o = &wins[zorder[j]];
        if (o->state == WS_MINIMIZED || zorder[j] == skip_slot || translucent()) continue;       /* see-through windows hide nothing */
        int in = o->state == WS_MAXIMIZED ? 0 : WIN_RADIUS;                 /* rounded corners show what is below */
        if (c.x >= o->r.x + in && c.y >= o->r.y + in &&
            c.x + c.w <= o->r.x + o->r.w - in && c.y + c.h <= o->r.y + o->r.h - in) return 1;
    }
    return 0;
}

void wm_draw_all(gfx_rect clip)
{
    for (int i = 0; i < nz; i++) {
        window *w = &wins[zorder[i]];
        if (w->state == WS_MINIMIZED || zorder[i] == skip_slot) continue;      /* not drawn at all */
        if (!(spr_slot >= 0 && zorder[i] == spr_slot && mode == I_DRAG) && hidden_behind_others(i, clip)) { culled_total++; continue; }
        if (spr_slot >= 0 && zorder[i] == spr_slot && mode == I_DRAG) {      /* being moved: the cached sprite */
            gfx_rect full = with_shadow(w->r);
            gfx_rect c = gfx_rect_isect(full, clip);
            if (!gfx_rect_empty(&c)) {
                gfx_set_clip(&c);
                gfx_blit_runs(&spr, w->r.x - SHADOW, w->r.y - SHADOW, spr_rx0, spr_rx1);
                gfx_set_clip(&clip);
            }
            continue;
        }
        draw_window(w, clip, zorder[i] == focus_slot);
    }
}

/* Does a visible window touch this rectangle? The notch, the tray and the dock step out of the way while one does. */
int wm_window_hits(gfx_rect r)
{
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (!wins[i].used || wins[i].state == WS_MINIMIZED) continue;
        gfx_rect is = gfx_rect_isect(wins[i].r, r);
        if (!gfx_rect_empty(&is)) return 1;
    }
    return 0;
}
