/*
 * Settings (APP_SETTINGS) - the system app for everything the welcome app asked once, plus a few more.
 * It is part of the system: it cannot be deleted from the desktop or the 9-dot menu. Sections:
 *   Appearance (the nine themes)   Windows (frame / inside transparency)   Language (EN TR ES RU ZH)
 *   Sound (volume + a test chime)  Sign-in (terminal / graphic)            Account (name, password)   About
 */
#include "gui.h"

enum { S_APPEAR, S_WALL, S_WINDOWS, S_LANG, S_SOUND, S_LOGIN, S_ACCOUNT, S_KEYS, S_ABOUT, S_COUNT };
#define CW 720
#define CH 480
#define SIDE 176

static int  sec;                                   /* current section */
static int  drag;                                  /* slider being dragged: 0 none, 1 frame, 2 inside, 3 volume */
static char fld[4][40];                            /* account fields: new name, password, password, new password */
static int  focus;
static char msg[100];
static int  msg_ok;

static const char *T(const char *en, const char *tr) { return config_tr(en, tr); }
static window *me(void) { return wm_find_app(APP_SETTINGS); }
static gfx_rect cont(void) { window *w = me(); return w ? wm_content_rect(w) : gfx_rect_make(0, 0, 0, 0); }
static void redraw(void) { gfx_rect c = cont(); if (c.w) wm_damage(c); }
static void redraw_all(void) { wm_damage(gfx_rect_make(0, 0, gfx_width(), gfx_height())); }

static const char *sec_name(int s)
{
    switch (s) {
    case S_APPEAR:  return T("Appearance", "Görünüm");
    case S_WALL:    return T("Wallpaper", "Arka plan");
    case S_WINDOWS: return T("Windows", "Pencereler");
    case S_LANG:    return T("Language", "Dil");
    case S_SOUND:   return T("Sound", "Ses");
    case S_LOGIN:   return T("Sign-in", "Giriş");
    case S_ACCOUNT: return T("Account", "Hesap");
    case S_KEYS:    return T("Shortcuts", "Kısayollar");
    default:        return T("About", "Hakkında");
    }
}

/* ---------------------------------------------------------------- small widgets */

static void text_c(int cx, int y, const char *s, u32 col) { gfx_text(cx - gfx_text_w(s) / 2, y, s, col); }

static void button(gfx_rect r, const char *label, int primary, int enabled)
{
    const ui_theme *t = ui_th();
    u32 bg = !enabled ? t->btn2 : primary ? t->btn_main : t->btn2;
    u32 fg = !enabled ? t->txt_off : primary ? t->btn_main_txt : t->txt;
    gfx_round_rect(r.x, r.y, r.w, r.h, 8, bg);
    text_c(r.x + r.w / 2, r.y + (r.h - 16) / 2, label, fg);
}

static void card(gfx_rect r, int sel)
{
    const ui_theme *t = ui_th();
    gfx_round_rect(r.x, r.y, r.w, r.h, 10, sel ? t->accent : t->line);
    gfx_round_rect(r.x + 2, r.y + 2, r.w - 4, r.h - 4, 8, sel ? t->cell_sel : t->panel);
}

static void title(gfx_rect pg, const char *name, const char *sub)
{
    const ui_theme *t = ui_th();
    gfx_text_scaled(pg.x + 24, pg.y + 18, name, t->txt, 2);
    if (sub) gfx_text(pg.x + 24, pg.y + 56, sub, t->dim);
    gfx_fill_rect(pg.x + 24, pg.y + 82, pg.w - 48, 1, t->line);
}

static void slider(gfx_rect r, int pct, int lo)                  /* r: the track area */
{
    const ui_theme *t = ui_th();
    int ty = r.y + r.h / 2 - 3;
    gfx_round_rect(r.x, ty, r.w, 6, 3, t->scr_track);
    int x = r.x + (pct - lo) * r.w / (100 - lo);
    gfx_round_rect(r.x, ty, x - r.x < 6 ? 6 : x - r.x, 6, 3, t->accent);
    gfx_circle(x, r.y + r.h / 2, 9, t->accent_hi);
    gfx_circle(x, r.y + r.h / 2, 5, t->bg);
}

static int slider_val(gfx_rect r, int x, int lo)
{
    int v = lo + (x - r.x) * (100 - lo) / r.w;
    return v < lo ? lo : v > 100 ? 100 : v;
}

static void field(gfx_rect r, const char *label, const char *text, int on, int secret)
{
    const ui_theme *t = ui_th();
    gfx_text(r.x, r.y - 20, label, t->dim);
    gfx_round_rect(r.x, r.y, r.w, r.h, 7, on ? t->accent : t->line);
    gfx_round_rect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 6, t->field);
    char shown[40];
    int n = (int)kstrlen(text);
    for (int i = 0; i < n; i++) shown[i] = secret ? '*' : text[i];
    shown[n] = 0;
    int max = (r.w - 20) / 8;
    const char *s = shown;
    if (n > max) { s += n - max; n = max; }
    gfx_text(r.x + 10, r.y + (r.h - 16) / 2, s, t->txt);
    if (on) gfx_fill_rect(r.x + 10 + n * 8, r.y + (r.h - 16) / 2, 2, 16, t->accent);
}

/* ---------------------------------------------------------------- layout (drawing and clicking share it) */

typedef struct {
    gfx_rect side[S_COUNT], page;
    gfx_rect theme[THEME_COUNT];
    gfx_rect s_frame, s_inside, s_vol, play, lang[LANG_COUNT], mode[2];
    gfx_rect f[4], b_name, b_pass;
    gfx_rect wall_prev, wall_open, wall_auto;
} lay;

static lay layout(gfx_rect cr)
{
    lay l;
    kmemset(&l, 0, sizeof l);
    for (int i = 0; i < S_COUNT; i++) l.side[i] = gfx_rect_make(cr.x + 10, cr.y + 14 + i * 40, SIDE - 20, 36);
    l.page = gfx_rect_make(cr.x + SIDE, cr.y, cr.w - SIDE, cr.h);
    gfx_rect pg = l.page;
    int cw = (pg.w - 48 - 2 * 12) / 3, ch = 100;
    for (int i = 0; i < THEME_COUNT; i++) l.theme[i] = gfx_rect_make(pg.x + 24 + (i % 3) * (cw + 12), pg.y + 98 + (i / 3) * (ch + 10), cw, ch);
    l.s_frame = gfx_rect_make(pg.x + 24, pg.y + 120, pg.w - 48 - 56, 28);
    l.s_inside = gfx_rect_make(pg.x + 24, pg.y + 190, pg.w - 48 - 56, 28);
    l.s_vol = gfx_rect_make(pg.x + 24, pg.y + 130, pg.w - 48 - 56, 28);
    l.play = gfx_rect_make(pg.x + 24, pg.y + 200, 170, 38);
    for (int i = 0; i < LANG_COUNT; i++) l.lang[i] = gfx_rect_make(pg.x + 24, pg.y + 98 + i * 58, pg.w - 48, 50);
    for (int i = 0; i < 2; i++) l.mode[i] = gfx_rect_make(pg.x + 24 + i * 250, pg.y + 108, 230, 190);
    int fw = (pg.w - 48 - 20) / 2;
    l.f[0] = gfx_rect_make(pg.x + 24, pg.y + 126, fw, 32);
    l.f[1] = gfx_rect_make(pg.x + 24 + fw + 20, pg.y + 126, fw, 32);
    l.b_name = gfx_rect_make(pg.x + 24, pg.y + 170, 190, 36);
    l.f[2] = gfx_rect_make(pg.x + 24, pg.y + 266, fw, 32);
    l.f[3] = gfx_rect_make(pg.x + 24 + fw + 20, pg.y + 266, fw, 32);
    l.b_pass = gfx_rect_make(pg.x + 24, pg.y + 310, 190, 36);
    l.wall_prev = gfx_rect_make(pg.x + 24, pg.y + 100, 320, 180);
    l.wall_open = gfx_rect_make(pg.x + 24, pg.y + 300, 250, 38);
    l.wall_auto = gfx_rect_make(pg.x + 286, pg.y + 300, 230, 38);
    return l;
}

/* ---------------------------------------------------------------- drawing */

static void draw_pages(gfx_rect cr, lay *l)
{
    const ui_theme *t = ui_th();
    const as_settings *c = config_get();
    gfx_rect pg = l->page;
    switch (sec) {
    case S_APPEAR:
        title(pg, T("Appearance", "Görünüm"), T("Dock, windows and every place that was black in older versions follow the theme.", "Dock, pencereler ve eski sürümde siyah olan her yer temayı izler."));
        for (int i = 0; i < THEME_COUNT; i++) {
            gfx_rect r = l->theme[i];
            card(r, c->theme == i);
            ui_theme_preview(r.x + 8, r.y + 8, r.w - 16, 64, i);
            text_c(r.x + r.w / 2, r.y + 76, ui_theme_label(i), t->txt);
        }
        break;
    case S_WALL: {
        title(pg, T("Wallpaper", "Arka plan"), T("Every theme has its own picture. Pick another one with the button or with Ctrl+Alt+B.", "Her temanın kendi resmi var. Düğmeyle ya da CTRL+ALT+B ile başkasını seç."));
        int cur = wp_effective();
        gfx_round_rect(l->wall_prev.x - 3, l->wall_prev.y - 3, l->wall_prev.w + 6, l->wall_prev.h + 6, 8, t->line);
        gfx_blit_opaque(wp_preview(cur), l->wall_prev.x, l->wall_prev.y);
        gfx_text(l->wall_prev.x + l->wall_prev.w + 20, l->wall_prev.y + 6, wp_name(cur), t->txt);
        gfx_text(l->wall_prev.x + l->wall_prev.w + 20, l->wall_prev.y + 30, c->wallpaper < 0 ? T("The theme's own picture", "Temanın kendi resmi") : T("Chosen by you", "Senin seçtiğin"), t->dim);
        button(l->wall_open, T("Change wallpaper...", "Arka planı değiştir..."), 1, 1);
        button(l->wall_auto, T("Use the theme's picture", "Temanın resmini kullan"), 0, c->wallpaper >= 0);
        break;
    }
    case S_KEYS: {
        title(pg, T("Shortcuts", "Kısayollar"), T("Work from the keyboard.", "Klavyeyle çalış."));
        for (int i = 0; i < ui_shortcut_count(); i++) {
            int y = pg.y + 96 + i * 28;
            gfx_text(pg.x + 24, y, ui_shortcut_keys(i), t->accent);
            gfx_text(pg.x + 24 + 200, y, ui_shortcut_text(i), t->txt);
        }
        break;
    }
    case S_WINDOWS:
        title(pg, T("Windows", "Pencereler"), T("How see-through window frames and window insides are.", "Pencere çerçevesi ve iç kısmı ne kadar saydam olsun."));
        gfx_text(l->s_frame.x, l->s_frame.y - 22, T("Window frame", "Pencere çerçevesi"), t->txt);
        slider(l->s_frame, c->win_a_frame, 30);
        { char b[8]; ksnprintf(b, sizeof b, "%d%%", c->win_a_frame); gfx_text(l->s_frame.x + l->s_frame.w + 14, l->s_frame.y + 6, b, t->txt); }
        gfx_text(l->s_inside.x, l->s_inside.y - 22, T("Window inside", "Pencere içi"), t->txt);
        slider(l->s_inside, c->win_a_content, 30);
        { char b[8]; ksnprintf(b, sizeof b, "%d%%", c->win_a_content); gfx_text(l->s_inside.x + l->s_inside.w + 14, l->s_inside.y + 6, b, t->txt); }
        gfx_text(pg.x + 24, pg.y + 250, T("Preview: the stripes are behind the window", "Önizleme: çizgiler pencerenin arkasında"), t->dim);
        {
            gfx_rect pv = gfx_rect_make(pg.x + 24, pg.y + 278, pg.w - 48, 150);
            gfx_fill_rect(pv.x, pv.y, pv.w, pv.h, t->bar);
            for (int i = 0; i < pv.w; i += 24) gfx_fill_rect(pv.x + i, pv.y, 10, pv.h, t->accent);
            u32 af = (u32)(c->win_a_frame * 255 / 100), ac = (u32)(c->win_a_content * 255 / 100);
            gfx_rect wr = gfx_rect_make(pv.x + 60, pv.y + 14, pv.w - 120, 122);
            u32 fr = af << 24 | (t->title_f & 0xFFFFFF), ct = ac << 24 | (t->content & 0xFFFFFF);
            gfx_fill_rect(wr.x, wr.y, wr.w, 26, fr);                       /* title bar */
            gfx_fill_rect(wr.x, wr.y + 26, 5, wr.h - 26, fr);              /* left, right and bottom frame */
            gfx_fill_rect(wr.x + wr.w - 5, wr.y + 26, 5, wr.h - 26, fr);
            gfx_fill_rect(wr.x + 5, wr.y + wr.h - 5, wr.w - 10, 5, fr);
            gfx_fill_rect(wr.x + 5, wr.y + 26, wr.w - 10, wr.h - 31, ct);  /* the inside */
            gfx_text(wr.x + 14, wr.y + 40, T("Window inside", "Pencere içi"), t->txt);
            gfx_circle(wr.x + 14, wr.y + 13, 5, 0xFFFF5F57);
        }
        break;
    case S_LANG:
        title(pg, T("Language", "Dil"), T("Choose the language of AstraSageOS windows and menus.", "AstraSageOS pencere ve menülerinin dilini seç."));
        for (int i = 0; i < LANG_COUNT; i++) {
            static const char *const name[LANG_COUNT] = { "English", "Türkçe", "Español", "Русский", "简体中文" };
            static const char *const code[LANG_COUNT] = { "EN", "TR", "ES", "RU", "ZH" };
            card(l->lang[i], c->lang == i);
            gfx_text(l->lang[i].x + 18, l->lang[i].y + 17, code[i], t->accent);
            gfx_text_scaled(l->lang[i].x + 60, l->lang[i].y + 9, name[i], t->txt, 2);
        }
        break;
    case S_SOUND:
        title(pg, T("Sound", "Ses"), T("The volume of the window sounds and of the tray slider.", "Pencere seslerinin ve tepsideki çubuğun ses düzeyi."));
        gfx_text(l->s_vol.x, l->s_vol.y - 22, T("Volume", "Ses düzeyi"), t->txt);
        slider(l->s_vol, c->volume, 0);
        { char b[8]; ksnprintf(b, sizeof b, "%d%%", c->volume); gfx_text(l->s_vol.x + l->s_vol.w + 14, l->s_vol.y + 6, b, t->txt); }
        button(l->play, T("Play sound", "Sesi çal"), 1, c->volume > 0);
        gfx_text(pg.x + 24, pg.y + 262, T("Window sounds need a Sound Blaster 16 or Intel AC97 sound card.", "Pencere sesleri için Sound Blaster 16 veya Intel AC97 ses kartı gerekir."), t->dim);
        break;
    case S_LOGIN:
        title(pg, T("Sign-in", "Giriş"), T("How should signing in work?", "Giriş nasıl yapılsın?"));
        for (int i = 0; i < 2; i++) {
            card(l->mode[i], c->login_gui == i);
            int ix = l->mode[i].x + l->mode[i].w / 2;
            draw_app_icon(i ? APP_LOGIN : APP_AST, ix - 36, l->mode[i].y + 22, 72);
            text_c(ix, l->mode[i].y + 108, i ? T("Graphic", "Grafik") : T("Terminal", "Terminal"), t->txt);
        }
        break;
    case S_ACCOUNT: {
        const char *u = user_name();
        title(pg, T("Account", "Hesap"), 0);
        char b[80];
        if (u) ksnprintf(b, sizeof b, "%s %s", T("Signed in as", "Giriş yapan:"), u);
        else kstrncpy(b, T("Sign in first", "Önce giriş yap"), sizeof b - 1);
        gfx_text(pg.x + 24, pg.y + 56, b, t->dim);
        field(l->f[0], T("New name", "Yeni ad"), fld[0], focus == 0, 0);
        field(l->f[1], T("Current password", "Şimdiki şifre"), fld[1], focus == 1, 1);
        button(l->b_name, T("Change name", "Adı değiştir"), 1, u != 0);
        field(l->f[2], T("Current password", "Şimdiki şifre"), fld[2], focus == 2, 1);
        field(l->f[3], T("New password", "Yeni şifre"), fld[3], focus == 3, 1);
        button(l->b_pass, T("Change password", "Şifreyi değiştir"), 1, u != 0);
        if (msg[0]) gfx_text(pg.x + 24, pg.y + 372, msg, msg_ok ? t->accent : t->danger);
        break;
    }
    default: {
        title(pg, T("About", "Hakkında"), 0);
        int sw, sh;
        ui_resolution(&sw, &sh);
        draw_app_icon(APP_WELCOME, pg.x + 24, pg.y + 100, 96);
        char b[100];
        gfx_text_scaled(pg.x + 140, pg.y + 104, AS_OS_NAME, t->txt, 2);
        ksnprintf(b, sizeof b, "%s %s", T("Version", "Sürüm"), AS_VERSION);
        gfx_text(pg.x + 140, pg.y + 146, b, t->dim);
        ksnprintf(b, sizeof b, "%s: %s %s", T("Kernel", "Çekirdek"), AS_KERNEL_NAME, AS_VERSION);
        gfx_text(pg.x + 140, pg.y + 172, b, t->dim);
        ksnprintf(b, sizeof b, "%s: %d x %d", T("Screen", "Ekran"), sw, sh);
        gfx_text(pg.x + 140, pg.y + 198, b, t->dim);
        ksnprintf(b, sizeof b, "%s: %u / %u MiB", T("Memory (RAM)", "Bellek (RAM)"), (unsigned)hal_mem_used_mib(), (unsigned)hal_mem_total_mib());
        gfx_text(pg.x + 140, pg.y + 224, b, t->dim);
        break;
    }
    }
    (void)cr;
}

static void settings_draw(window *w, gfx_rect cr, gfx_rect clip, int focused)
{
    (void)focused;
    kstrncpy(w->title, T("Settings", "Ayarlar"), sizeof w->title - 1);          /* follows the language */
    const ui_theme *t = ui_th();
    gfx_set_clip(&clip);
    lay l = layout(cr);
    gfx_fill_rect(cr.x, cr.y, cr.w, cr.h, t->bg);
    gfx_fill_rect(cr.x, cr.y, SIDE, cr.h, t->side);
    gfx_fill_rect(cr.x + SIDE - 1, cr.y, 1, cr.h, t->line);
    for (int i = 0; i < S_COUNT; i++) {
        if (i == sec) gfx_round_rect(l.side[i].x, l.side[i].y, l.side[i].w, l.side[i].h, 8, t->sel);
        gfx_text(l.side[i].x + 14, l.side[i].y + 10, sec_name(i), i == sec ? t->txt : t->dim);
    }
    draw_pages(cr, &l);
    gfx_set_clip(0);
}

/* ---------------------------------------------------------------- input */

static void apply_alpha(int which, int v)
{
    as_settings s = *config_get();
    if (which == 1) s.win_a_frame = v; else s.win_a_content = v;
    config_set(&s);
    redraw_all();
}

static void set_volume(int v)
{
    as_settings s = *config_get();
    s.volume = v;
    config_set(&s);
    redraw();
}

static void do_change(int pass)
{
    int rc;
    msg[0] = 0;
    if (pass) rc = user_change_password(fld[2], fld[3]); else rc = user_change_name(fld[1], fld[0]);
    if (rc < 0) { kstrncpy(msg, user_strerror(rc), sizeof msg - 1); msg_ok = 0; }
    else {
        kstrncpy(msg, pass ? T("Password changed.", "Şifre değiştirildi.") : T("Name changed.", "Ad değiştirildi."), sizeof msg - 1);
        msg_ok = 1;
        for (int i = 0; i < 4; i++) fld[i][0] = 0;
    }
    redraw();
}

static void settings_mouse(window *w, int x, int y, u8 b, u8 prev)
{
    (void)w;
    if (x < 0) { drag = 0; return; }
    gfx_rect cr = cont();
    lay l = layout(cr);
    int down = (b & 1), fresh = (b & 1) && !(prev & 1);
    if (!down) { drag = 0; return; }
    if (drag) {                                              /* a slider follows the pointer even outside its track */
        if (drag == 1) apply_alpha(1, slider_val(l.s_frame, x, 30));
        else if (drag == 2) apply_alpha(2, slider_val(l.s_inside, x, 30));
        else set_volume(slider_val(l.s_vol, x, 0));
        return;
    }
    if (!fresh) return;
    for (int i = 0; i < S_COUNT; i++) if (gfx_rect_has(&l.side[i], x, y)) { sec = i; msg[0] = 0; redraw(); return; }
    switch (sec) {
    case S_APPEAR:
        for (int i = 0; i < THEME_COUNT; i++) if (gfx_rect_has(&l.theme[i], x, y)) {
            as_settings s = *config_get(); s.theme = i; config_set(&s);
            ui_theme_set(i);
            redraw_all();
        }
        break;
    case S_WALL:
        if (gfx_rect_has(&l.wall_open, x, y)) wp_picker_toggle();
        else if (gfx_rect_has(&l.wall_auto, x, y)) { wp_apply(-1); redraw_all(); }
        break;
    case S_WINDOWS: {
        gfx_rect a = l.s_frame, c2 = l.s_inside;
        a.x -= 12; a.w += 24; c2.x -= 12; c2.w += 24;
        if (gfx_rect_has(&a, x, y)) { drag = 1; apply_alpha(1, slider_val(l.s_frame, x, 30)); }
        else if (gfx_rect_has(&c2, x, y)) { drag = 2; apply_alpha(2, slider_val(l.s_inside, x, 30)); }
        break;
    }
    case S_LANG:
        for (int i = 0; i < LANG_COUNT; i++) if (gfx_rect_has(&l.lang[i], x, y)) {
            as_settings s = *config_get(); s.lang = i; config_set(&s);
            redraw_all();
        }
        break;
    case S_SOUND: {
        gfx_rect a = l.s_vol; a.x -= 12; a.w += 24;
        if (gfx_rect_has(&a, x, y)) { drag = 3; set_volume(slider_val(l.s_vol, x, 0)); }
        else if (gfx_rect_has(&l.play, x, y)) snd_play(SND_OPEN);
        break;
    }
    case S_LOGIN:
        for (int i = 0; i < 2; i++) if (gfx_rect_has(&l.mode[i], x, y)) {
            as_settings s = *config_get(); s.login_gui = i; config_set(&s);
            redraw();
        }
        break;
    case S_ACCOUNT:
        for (int i = 0; i < 4; i++) if (gfx_rect_has(&l.f[i], x, y)) { focus = i; redraw(); return; }
        if (user_name() && gfx_rect_has(&l.b_name, x, y)) do_change(0);
        else if (user_name() && gfx_rect_has(&l.b_pass, x, y)) do_change(1);
        break;
    }
}

static void settings_key(window *w, int k)
{
    (void)w;
    if (sec != S_ACCOUNT) return;
    char *f = fld[focus];
    size_t n = kstrlen(f), cap = focus == 0 ? 16 : 32;
    if (k == '\t') { focus = (focus + 1) & 3; redraw(); return; }
    if (k == '\n' || k == '\r') { if (user_name()) do_change(focus >= 2); return; }
    if (k == '\b' || k == 127) { if (n) f[n - 1] = 0; redraw(); return; }
    if (k >= 32 && k < 127 && n < cap) { f[n] = (char)k; f[n + 1] = 0; redraw(); }
}

static void settings_closed(window *w)
{
    (void)w;
    for (int i = 0; i < 4; i++) fld[i][0] = 0;              /* passwords do not stay in memory after the window closes */
    msg[0] = 0; drag = 0;
}

const wm_app app_settings_ops = { settings_draw, 0, settings_closed, 0, settings_mouse, settings_key, 0 };

void app_settings_launch(void)
{
    window *w = me();
    if (w) { if (w->state == WS_MINIMIZED) wm_restore(w); else wm_raise(w); return; }
    int sw, sh;
    ui_resolution(&sw, &sh);
    gfx_rect wk = ui_work_rect();
    int ow = CW + 2 * WM_BORDER, oh = CH + WM_TITLE_H + WM_BORDER;
    w = wm_create(T("Settings", "Ayarlar"), APP_SETTINGS, (sw - ow) / 2, wk.y + (wk.h - oh) / 2, ow, oh);
    if (!w) return;
    w->minw = w->r.w; w->minh = w->r.h;
    focus = 0; msg[0] = 0; drag = 0;
    wm_damage(gfx_rect_make(w->r.x - 20, w->r.y - 20, w->r.w + 40, w->r.h + 44));
}

/* The Settings icon: a white-to-light-grey tile with a grey gear. */
void settings_icon_face(u32 *px, int size)
{
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            u32 d = (u32)((x + y) * 100 / (2 * size));
            u32 v = 0xFC - d * 0x28 / 100;
            px[y * size + x] = 0xFF000000u | v << 16 | v << 8 | (v + 2 > 255 ? 255 : v + 2);
        }
    gfx_surf t = { size, size, size, px };
    gfx_set_target(&t);
    gfx_set_clip(0);
    static const short cs[8][2] = { { 1000, 0 }, { 707, 707 }, { 0, 1000 }, { -707, 707 }, { -1000, 0 }, { -707, -707 }, { 0, -1000 }, { 707, -707 } };
    int c = size / 2, rt = size * 33 / 100, rd = size * 26 / 100, rh = size * 11 / 100, rk = size * 8 / 100 + 1;
    const u32 grey = 0xFF6B6F76, dark = 0xFF565A61;
    for (int i = 0; i < 8; i++) gfx_circle(c + cs[i][0] * rt / 1000, c + cs[i][1] * rt / 1000, rk, grey);
    gfx_circle(c, c, rd + 1, grey);
    gfx_circle(c, c, rd - size / 24 - 1, dark);
    gfx_circle(c, c, rh + 1, 0xFFF1F2F4);
    gfx_set_target(0);
}
