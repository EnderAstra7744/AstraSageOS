/*
 * Task Manager (APP_TASKS, Ctrl+Alt+K). Two pages:
 *   Applications - every open window with its state and an "End task" button (the window is closed at once, without
 *                  the question the red button may ask);
 *   System       - memory, storage, windows, screen and the time since start, read from the kernel (nothing is made up;
 *                  the kernel has no processor meter, so there is none here).
 * It redraws once a second while it is visible.
 */
#include "gui.h"

#define CW 600
#define CH 420
#define ROW 44

static int page;                                   /* 0 applications, 1 system */
static u32 last_tick;

static const char *T(const char *en, const char *tr) { return config_tr(en, tr); }
static window *me(void) { return wm_find_app(APP_TASKS); }
static gfx_rect cont(void) { window *w = me(); return w ? wm_content_rect(w) : gfx_rect_make(0, 0, 0, 0); }
static void redraw(void) { gfx_rect c = cont(); if (c.w) wm_damage(c); }

static const char *app_title(const window *w)
{
    switch (w->app) {
    case APP_AST: return "AST";
    case APP_FILES: return T("File Explorer", "Dosya Gezgini");
    case APP_WELCOME: return T("Welcome", "Hoşgeldiniz");
    case APP_STORE: return "AstraStore";
    case APP_SETTINGS: return T("Settings", "Ayarlar");
    case APP_TASKS: return T("Task Manager", "Görev Yöneticisi");
    default: return w->title;
    }
}

typedef struct { gfx_rect tab[2]; gfx_rect row[WM_MAX_WINDOWS]; gfx_rect end[WM_MAX_WINDOWS]; window *win[WM_MAX_WINDOWS]; int n; } lay;

static lay layout(gfx_rect cr)
{
    lay l;
    kmemset(&l, 0, sizeof l);
    l.tab[0] = gfx_rect_make(cr.x + 16, cr.y + 14, 150, 32);
    l.tab[1] = gfx_rect_make(cr.x + 174, cr.y + 14, 150, 32);
    for (int z = WM_MAX_WINDOWS - 1; z >= 0; z--) {                  /* front window first */
        int s = wm_order(z);
        window *w = s >= 0 ? wm_slot(s) : 0;
        if (!w || !w->used) continue;
        int i = l.n++;
        l.win[i] = w;
        l.row[i] = gfx_rect_make(cr.x + 16, cr.y + 62 + i * (ROW + 6), cr.w - 32, ROW);
        l.end[i] = gfx_rect_make(l.row[i].x + l.row[i].w - 104, l.row[i].y + 8, 94, ROW - 16);
    }
    return l;
}

static void bar(int x, int y, int w, int pct, u32 col)
{
    const ui_theme *t = ui_th();
    gfx_round_rect(x, y, w, 12, 6, t->scr_track);
    int f = w * pct / 100;
    if (f < 12 && pct > 0) f = 12;
    if (pct > 0) gfx_round_rect(x, y, f, 12, 6, col);
}

static void fmt_bytes(char *out, size_t cap, u32 b)
{
    if (b >= 1048576) ksnprintf(out, cap, "%u.%u MiB", b / 1048576, (b % 1048576) * 10 / 1048576);
    else if (b >= 1024) ksnprintf(out, cap, "%u KiB", b / 1024);
    else ksnprintf(out, cap, "%u B", b);
}

static void tasks_draw(window *w, gfx_rect cr, gfx_rect clip, int focused)
{
    (void)w; (void)focused;
    const ui_theme *t = ui_th();
    gfx_set_clip(&clip);
    gfx_fill_rect(cr.x, cr.y, cr.w, cr.h, t->bg);
    lay l = layout(cr);
    const char *tabs[2] = { T("Applications", "Uygulamalar"), T("System", "Sistem") };
    for (int i = 0; i < 2; i++) {
        gfx_round_rect(l.tab[i].x, l.tab[i].y, l.tab[i].w, l.tab[i].h, 8, i == page ? t->accent : t->btn2);
        gfx_text(l.tab[i].x + (l.tab[i].w - gfx_text_w(tabs[i])) / 2, l.tab[i].y + 8, tabs[i], i == page ? t->btn_main_txt : t->txt);
    }
    if (page == 0) {
        for (int i = 0; i < l.n; i++) {
            window *x = l.win[i];
            gfx_rect r = l.row[i];
            gfx_round_rect(r.x, r.y, r.w, r.h, 8, t->panel);
            draw_app_icon(x->app, r.x + 8, r.y + 6, 32);
            gfx_text(r.x + 52, r.y + 6, app_title(x), t->txt);
            const char *st = x->state == WS_MINIMIZED ? T("Minimised", "Küçültüldü") : x->state == WS_MAXIMIZED ? T("Maximised", "Tam ekran") : T("Running", "Çalışıyor");
            gfx_text(r.x + 52, r.y + 24, st, t->dim);
            int locked = x->app == APP_WELCOME && !config_get()->setup_done;
            gfx_rect e = l.end[i];
            gfx_round_rect(e.x, e.y, e.w, e.h, 7, locked ? t->btn2 : t->danger);
            const char *s = T("End task", "Sonlandır");
            gfx_text(e.x + (e.w - gfx_text_w(s)) / 2, e.y + (e.h - 16) / 2, s, locked ? t->txt_off : 0xFFFFFFFFu);
        }
        if (!l.n) gfx_text(cr.x + 24, cr.y + 70, T("No application is open", "Açık uygulama yok"), t->dim);
        char sm[60];
        ksnprintf(sm, sizeof sm, T("%d of %d windows open", "%d / %d pencere açık"), l.n, WM_MAX_WINDOWS);
        gfx_text(cr.x + 20, cr.y + cr.h - 28, sm, t->dim);
    } else {
        int x0 = cr.x + 28, bw = cr.w - 56, y = cr.y + 70;
        char a[64], b[40], c[40];
        u32 mt = hal_mem_total_mib(), mu = hal_mem_used_mib();
        ksnprintf(a, sizeof a, "%u MiB / %u MiB", mu, mt);
        gfx_text(x0, y, T("Memory (used by the kernel)", "Bellek (çekirdeğin kullandığı)"), t->txt);
        gfx_text(x0 + bw - gfx_text_w(a), y, a, t->dim);
        bar(x0, y + 24, bw, mt ? (int)(mu * 100 / mt) : 0, t->accent);
        u32 nu, bu, bc;
        fs_stats(&nu, &bu, &bc);
        fmt_bytes(b, sizeof b, bu); fmt_bytes(c, sizeof c, bc);
        ksnprintf(a, sizeof a, "%s / %s", b, c);
        y += 64;
        gfx_text(x0, y, T("Storage (files)", "Depolama (dosyalar)"), t->txt);
        gfx_text(x0 + bw - gfx_text_w(a), y, a, t->dim);
        bar(x0, y + 24, bw, bc ? (int)((bu >> 8) * 100 / ((bc >> 8) + 1)) : 0, t->accent);
        y += 64;
        gfx_text(x0, y, T("Files and folders", "Dosya ve klasörler"), t->txt);
        ksnprintf(a, sizeof a, "%u", nu);
        gfx_text(x0 + bw - gfx_text_w(a), y, a, t->dim);
        y += 34;
        u32 up = ui_ms() / 1000;
        ksnprintf(a, sizeof a, "%u:%02u:%02u", up / 3600, up / 60 % 60, up % 60);
        gfx_text(x0, y, T("Time since start", "Açılıştan beri geçen süre"), t->txt);
        gfx_text(x0 + bw - gfx_text_w(a), y, a, t->dim);
        y += 34;
        int sw, sh;
        ui_resolution(&sw, &sh);
        ksnprintf(a, sizeof a, "%d x %d", sw, sh);
        gfx_text(x0, y, T("Screen", "Ekran"), t->txt);
        gfx_text(x0 + bw - gfx_text_w(a), y, a, t->dim);
        y += 34;
        gfx_text(x0, y, T("Signed in", "Oturum açan"), t->txt);
        const char *u = user_name() ? user_name() : "-";
        gfx_text(x0 + bw - gfx_text_w(u), y, u, t->dim);
    }
    gfx_set_clip(0);
}

static void tasks_mouse(window *w, int x, int y, u8 b, u8 prev)
{
    (void)w;
    if (x < 0 || !(b & 1) || (prev & 1)) return;
    lay l = layout(cont());
    for (int i = 0; i < 2; i++) if (gfx_rect_has(&l.tab[i], x, y)) { page = i; redraw(); return; }
    if (page != 0) return;
    for (int i = 0; i < l.n; i++) {
        if (!gfx_rect_has(&l.end[i], x, y)) continue;
        window *t = l.win[i];
        if (t->app == APP_WELCOME && !config_get()->setup_done) return;     /* the setup is not left half-done */
        if (t->app == APP_TASKS) { wm_close(t); return; }
        wm_close(t);
        redraw();
        return;
    }
}

static void tasks_key(window *w, int k)
{
    (void)w;
    if (k == KEY_LEFT || k == KEY_RIGHT || k == '\t') { page = !page; redraw(); }
}

static void tasks_closed(window *w) { (void)w; }

const wm_app app_tasks_ops = { tasks_draw, 0, tasks_closed, 0, tasks_mouse, tasks_key, 0 };

void app_tasks_launch(void)
{
    window *w = me();
    if (w) { if (w->state == WS_MINIMIZED) wm_restore(w); else wm_raise(w); return; }
    int sw, sh;
    ui_resolution(&sw, &sh);
    gfx_rect wk = ui_work_rect();
    int ow = CW + 2 * WM_BORDER, oh = CH + WM_TITLE_H + WM_BORDER;
    w = wm_create(T("Task Manager", "Görev Yöneticisi"), APP_TASKS, (sw - ow) / 2, wk.y + (wk.h - oh) / 2, ow, oh);
    if (!w) return;
    w->minw = 460; w->minh = 320;
    page = 0; last_tick = ui_ms();
    wm_damage(gfx_rect_make(w->r.x - 20, w->r.y - 20, w->r.w + 40, w->r.h + 44));
}

void app_tasks_pump(void)
{
    window *w = me();
    if (!w || w->state == WS_MINIMIZED) return;
    u32 now = ui_ms();
    if (now - last_tick < 1000) return;
    last_tick = now;
    if (wm_busy()) return;
    redraw();
}

/* The icon: a dark tile with a green pulse line and three small bars. */
void tasks_icon_face(u32 *px, int size)
{
    for (int y = 0; y < size; y++) {
        u32 v = (u32)(0x1C + y * 14 / size);
        for (int x = 0; x < size; x++) px[y * size + x] = 0xFF000000u | v << 16 | (v + 4) << 8 | (v + 6);
    }
    gfx_surf t = { size, size, size, px };
    gfx_set_target(&t);
    gfx_set_clip(0);
    int bw = size * 12 / 100, gap = size * 6 / 100, x0 = size * 17 / 100, base = size * 78 / 100;
    static const int hh[4] = { 28, 44, 34, 52 };
    for (int i = 0; i < 4; i++) {
        int h = size * hh[i] / 100;
        gfx_round_rect(x0 + i * (bw + gap), base - h, bw, h, bw / 3 + 1, 0xFF3A4A5A);
    }
    /* pulse line */
    int py[7] = { 52, 52, 40, 64, 26, 52, 52 }, px_[7] = { 10, 28, 36, 46, 58, 68, 90 };
    for (int i = 0; i + 1 < 7; i++) {
        int ax = size * px_[i] / 100, ay = size * py[i] / 100, bx = size * px_[i + 1] / 100, by = size * py[i + 1] / 100;
        int n = (bx - ax) > 0 ? bx - ax : 1;
        for (int k = 0; k <= n; k++) {
            int xx = ax + k, yy = ay + (by - ay) * k / n;
            gfx_circle(xx, yy, size / 40 + 1, 0xFF3CE05A);
        }
    }
    gfx_set_target(0);
}
