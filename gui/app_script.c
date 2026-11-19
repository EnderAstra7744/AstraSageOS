/*
 * The window of an AstraScript program (APP_SCRIPT): gui_open("Title", w, h) creates it, gui_rect / gui_text /
 * gui_sprite draw into its own pixel surface, gui_wait reads keys and clicks. The script runs on the AST
 * coroutine; while it waits (gui_wait / gui_show) it yields, so the desktop keeps running. One window at a time.
 */
#include "gui.h"

#define SG_MAXW 1000
#define SG_MAXH 640
#define KQ 64

static u32  spx[SG_MAXW * SG_MAXH];
static int  sw_, sh_, is_open, dirty;
static int  keyq[KQ], kh, kt;
static int  click_on, click_x, click_y;
static int  waiting;
static u32  deadline;
static u32  pal[16];
#define EQ 128
static struct { int type, x, y, code; } evq[EQ];
static int  eh, et;
static int  mdx = -1, mdy = -1;
static void ev_push(int type, int x, int y, int code)
{
    int n = (et + 1) % EQ;
    if (n == eh) return;
    evq[et].type = type; evq[et].x = x; evq[et].y = y; evq[et].code = code;
    et = n;
}

static void damage_content(void)
{
    window *w = wm_find_app(APP_SCRIPT);
    if (w) wm_damage(wm_content_rect(w));
}

static u8 icon_bmp[4096];
static bmp_view icon_view;
static int has_icon;
const bmp_view *sgui_icon(void) { return has_icon ? &icon_view : 0; }
/* Init(Icon="file.bmp"): the picture of the window in the dock and on the desktop (a BMP, at most 4096 bytes like every file); 0 = refused */
int sgui_set_icon(const u8 *data, u32 len)
{
    if (len == 0 || len > sizeof icon_bmp) return 0;
    for (u32 i = 0; i < len; i++) icon_bmp[i] = data[i];
    if (bmp_open(icon_bmp, len, &icon_view) != BMP_OK) { has_icon = 0; return 0; }
    has_icon = 1;
    ui_icon_forget(APP_SCRIPT);
    return 1;
}

int sgui_is_open(void) { return is_open; }
const char *sgui_title(void) { window *w = wm_find_app(APP_SCRIPT); return w ? w->title : "App"; }

int sgui_open(const char *title, int w, int h)
{
    if (!ui_active()) return 0;
    if (is_open && wm_find_app(APP_SCRIPT)) return 1;
    if (w < 64) w = 64;
    if (h < 48) h = 48;
    int sw, sh;
    ui_resolution(&sw, &sh);
    gfx_rect wk = ui_work_rect();
    if (w > SG_MAXW) w = SG_MAXW;
    if (h > SG_MAXH) h = SG_MAXH;
    if (w + 2 * WM_BORDER > sw - 8) w = sw - 8 - 2 * WM_BORDER;
    if (h + WM_TITLE_H + WM_BORDER > wk.y + wk.h - 8) h = wk.y + wk.h - 8 - WM_TITLE_H - WM_BORDER;
    int ow = w + 2 * WM_BORDER, oh = h + WM_TITLE_H + WM_BORDER;
    has_icon = 0;
    ui_icon_forget(APP_SCRIPT);
    window *win = wm_create(title && *title ? title : "App", APP_SCRIPT, (sw - ow) / 2, (wk.y + wk.h - oh) / 2, ow, oh);
    if (!win) return 0;
    win->r.x = (sw - ow) / 2; win->r.y = (wk.y + wk.h - oh) / 2; win->r.w = ow; win->r.h = oh;      /* exactly as asked, below the usual minimum */
    win->restore = win->r; win->minw = ow; win->minh = oh;
    sw_ = w; sh_ = h;
    for (int i = 0; i < sw_ * sh_; i++) spx[i] = 0xFF000000u;
    for (int i = 0; i < 16; i++) pal[i] = 0xFFFFFFFFu;
    kh = kt = 0; eh = et = 0; mdx = mdy = -1; click_on = 0; waiting = 0; dirty = 1; is_open = 1;
    wm_damage(gfx_rect_make(win->r.x - 20, win->r.y - 20, win->r.w + 40, win->r.h + 44));
    return 1;
}

void sgui_close(void)
{
    window *w = wm_find_app(APP_SCRIPT);
    is_open = 0; has_icon = 0;
    if (w) wm_close(w);
}

void sgui_abort(void)                       /* the shell the script ran in is gone */
{
    if (is_open) sgui_close();
    is_open = 0; waiting = 0;
}

static void px_set(int x, int y, u32 rgb)
{
    if (x < 0 || y < 0 || x >= sw_ || y >= sh_) return;
    spx[y * sw_ + x] = 0xFF000000u | (rgb & 0xFFFFFF);
}

void sgui_clear(u32 rgb)
{
    if (!is_open) return;
    for (int i = 0; i < sw_ * sh_; i++) spx[i] = 0xFF000000u | (rgb & 0xFFFFFF);
    dirty = 1;
}

void sgui_rect(int x, int y, int w, int h, u32 rgb)
{
    if (!is_open) return;
    for (int j = y < 0 ? 0 : y; j < y + h && j < sh_; j++)
        for (int i = x < 0 ? 0 : x; i < x + w && i < sw_; i++) spx[j * sw_ + i] = 0xFF000000u | (rgb & 0xFFFFFF);
    dirty = 1;
}

void sgui_pixel(int x, int y, u32 rgb) { if (is_open) { px_set(x, y, rgb); dirty = 1; } }

void sgui_text(int x, int y, const char *s, u32 rgb, int scale)
{
    if (!is_open) return;
    if (scale < 1) scale = 1;
    if (scale > 8) scale = 8;
    gfx_surf surf = { sw_, sh_, sw_, spx };
    gfx_rect keep = gfx_get_clip();
    gfx_set_target(&surf);
    gfx_set_clip(0);
    gfx_text_scaled(x, y, s, 0xFF000000u | (rgb & 0xFFFFFF), scale);      /* UTF-8: ASCII + Turkish letters */
    gfx_set_target(0);
    gfx_set_clip(&keep);
    dirty = 1;
}

void sgui_pal(int i, u32 rgb) { if (i >= 0 && i < 16) pal[i] = 0xFF000000u | (rgb & 0xFFFFFF); }

/* one band of a pixel map: each row is a string; '.' (or ' ') is transparent, 0-9 a-f pick the palette */
void sgui_sprite_row(int x, int y, int scale, const char *row)
{
    if (!is_open) return;
    int mirror = scale < 0;                       /* negative scale: mirrored left-right */
    if (mirror) scale = -scale;
    if (scale < 1) scale = 1;
    if (scale > 16) scale = 16;
    int len = 0;
    while (row[len]) len++;
    for (int i = 0; i < len; i++) {
        char c = row[mirror ? len - 1 - i : i];
        int p = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (p < 0) continue;
        for (int a = 0; a < scale; a++)
            for (int b = 0; b < scale; b++) px_set(x + i * scale + b, y + a, pal[p]);
    }
    dirty = 1;
}

/* ---- events and waiting ---- */

int app_script_wake(void)                 /* AST pump: should the shell coroutine run now? */
{
    if (!waiting) return 0;
    return !is_open || kh != kt || eh != et || click_on || (int)(ui_ms() - deadline) >= 0;
}

static void frame_yield(void)
{
    coro *c = coro_current();
    if (c) coro_yield();
}

void sgui_show(void)
{
    if (!is_open) return;
    if (dirty) { damage_content(); dirty = 0; }
    waiting = 1; deadline = ui_ms();      /* give the desktop one frame */
    frame_yield();
    waiting = 0;
}

/* a key code, 1000 for a click (see sgui_mouse), 0 on timeout, -1 when the window was closed. ms < 0: wait for ever */
int sgui_wait(int ms)
{
    if (!is_open) return -1;
    if (dirty) { damage_content(); dirty = 0; }
    u32 end = ui_ms() + (u32)(ms < 0 ? 0x7FFFFFFF : ms);
    for (;;) {
        if (!is_open) { waiting = 0; return -1; }
        if (kh != kt) { int k = keyq[kh]; kh = (kh + 1) % KQ; waiting = 0; return k; }
        if (click_on) { click_on = 0; waiting = 0; return 1000; }
        if ((int)(ui_ms() - end) >= 0) { waiting = 0; return 0; }
        if (!coro_current()) { waiting = 0; return 0; }
        waiting = 1; deadline = end;
        coro_yield();
    }
}

/* native programs: next input event. type 1 key (code), 2 move, 3 button down, 4 button up (code = bit), 5 closed.
 * Returns 1 with *ev filled, 0 on timeout, -1 when the window is gone. */
int sgui_event(int *ev, int ms)
{
    u32 end = ui_ms() + (u32)(ms < 0 ? 0x7FFFFFFF : ms);
    if (dirty && is_open) { damage_content(); dirty = 0; }
    for (;;) {
        if (eh != et) { ev[0] = evq[eh].type; ev[1] = evq[eh].x; ev[2] = evq[eh].y; ev[3] = evq[eh].code; eh = (eh + 1) % EQ; waiting = 0; return 1; }
        if (!is_open) { waiting = 0; return -1; }
        if ((int)(ui_ms() - end) >= 0 || !coro_current()) { waiting = 0; return 0; }
        waiting = 1; deadline = end;
        coro_yield();
    }
}

/* copies a w*h block of 0xAARRGGBB pixels (row stride in pixels) into the window picture */
void sgui_blit(const u32 *src, int x, int y, int w, int h, int stride)
{
    if (!is_open) return;
    for (int r = 0; r < h; r++) {
        int dy = y + r;
        if (dy < 0 || dy >= sh_) continue;
        for (int c = 0; c < w; c++) {
            int dx = x + c;
            if (dx < 0 || dx >= sw_) continue;
            spx[dy * sw_ + dx] = 0xFF000000u | src[r * stride + c];
        }
    }
    dirty = 1;
}

int sgui_size(int *w, int *h) { *w = sw_; *h = sh_; return is_open; }

void sgui_mouse(int *x, int *y) { *x = click_x; *y = click_y; }

/* ---- window manager callbacks ---- */

static void sc_draw(window *w, gfx_rect cr, gfx_rect clip, int focused)
{
    (void)w; (void)focused;
    gfx_rect full = gfx_rect_make(cr.x, cr.y, cr.w, cr.h);
    gfx_rect c = gfx_rect_isect(full, clip);
    if (gfx_rect_empty(&c)) return;
    gfx_fill_rect(c.x, c.y, c.w, c.h, 0xFF000000u);
    if (!is_open) return;
    gfx_surf s = { sw_, sh_, sw_, spx };
    gfx_blit_opaque(&s, cr.x, cr.y);
}

static void sc_closed(window *w) { (void)w; is_open = 0; ev_push(5, 0, 0, 0); }

static void sc_mouse(window *w, int x, int y, u8 b, u8 prev)
{
    if (x < 0) return;
    gfx_rect cr = wm_content_rect(w);
    int rx = x - cr.x, ry = y - cr.y;
    if (rx < 0 || ry < 0 || rx >= sw_ || ry >= sh_) { if (!(b | prev)) return; if (rx < 0) rx = 0; if (ry < 0) ry = 0; if (rx >= sw_) rx = sw_ - 1; if (ry >= sh_) ry = sh_ - 1; }
    if (rx != mdx || ry != mdy) { mdx = rx; mdy = ry; ev_push(2, rx, ry, 0); }
    for (int bit = 1; bit <= 4; bit <<= 1) {
        if ((b & bit) && !(prev & bit)) ev_push(3, rx, ry, bit);
        if (!(b & bit) && (prev & bit)) ev_push(4, rx, ry, bit);
    }
    if ((b & 1) && !(prev & 1)) { click_x = rx; click_y = ry; click_on = 1; }
}

static void sc_key(window *w, int key)
{
    (void)w;
    int n = (kt + 1) % KQ;
    if (n == kh) return;
    keyq[kt] = key; kt = n;
    ev_push(1, 0, 0, key);
}

const wm_app app_script_ops = { sc_draw, 0, sc_closed, 0, sc_mouse, sc_key, 0 };
