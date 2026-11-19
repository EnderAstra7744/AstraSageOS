/*
 * Graphical login. Chosen in the welcome app ("login with: graphic"). It is a full-screen layer drawn above
 * everything (no window): the wallpaper, a blurred column with the big clock and the date, a name field, a
 * password field, the sign-in button and the power buttons. While it is up it owns the pointer and the keyboard,
 * so nothing behind it can be reached. Enter or the button signs in with the same user_login() the terminal
 * uses; "Create account" makes a new account.
 */
#include "gui.h"


static char name[40], pass[40];
static int  focus;                      /* 0 name, 1 password */
static int  locked;                     /* 1: "welcome back": the saved user only types the password */
static char msg[96];
static int  msg_err;

/* the user silhouette: head + shoulders, in two greys */
void ui_user_glyph(int cx, int cy, int size, u32 body, u32 rim)
{
    int hr = size * 22 / 100;                                   /* head radius */
    int hy = cy - size * 14 / 100;
    int sw = size * 70 / 100, sh = size * 36 / 100;
    int sy = cy + size * 12 / 100;
    gfx_round_rect_ex(cx - sw / 2 - 2, sy - 2, sw + 4, sh + 4, sw / 2 + 2, sw / 2 + 2, 2, 2, rim);
    gfx_circle(cx, hy, hr + 2, rim);
    gfx_round_rect_ex(cx - sw / 2, sy, sw, sh, sw / 2, sw / 2, 1, 1, body);
    gfx_circle(cx, hy, hr, body);
}

void login_icon_face(u32 *px, int size)
{
    gfx_surf t = { size, size, size, px };
    for (int y = 0; y < size; y++) {
        u32 sh = (u32)(y * 20 / size);
        for (int x = 0; x < size; x++) px[y * size + x] = 0xFF000000u | (0x4A - sh) << 16 | (0x4C - sh) << 8 | (0x52 - sh);
    }
    gfx_set_target(&t);
    gfx_set_clip(0);
    ui_user_glyph(size / 2, size * 52 / 100, size * 90 / 100, 0xFFF1F2F4u, 0xFF8E9299u);
    gfx_set_target(0);
}


/* ------------------------------------------------------------------ the full-screen layer */

static int on;                                    /* the layer is up */
static int hot_pw;                                /* the pointer is over a power button (0 none, 1 shutdown, 2 reboot) */
static int clock_min = -1;
static u32 strip_px[800 * GFX_MAXH];
static gfx_surf strip;
static int strip_w, strip_for;

int ui_login_active(void) { return on; }

typedef struct { gfx_rect nf, pf, ok, mk; gfx_rect pw[2]; int clk_y, clk_scale, date_y, col_x, col_w; } lay;
static lay layout(void)
{
    lay l;
    int sw, sh;
    ui_resolution(&sw, &sh);
    l.clk_scale = sh >= 900 ? 8 : (sh >= 600 ? 6 : 4);
    l.col_w = sw * 40 / 100 > 768 ? 768 : (sw * 40 / 100 < 380 ? (sw < 380 ? sw : 380) : sw * 40 / 100);
    l.col_x = (sw - l.col_w) / 2;
    l.clk_y = sh * 10 / 100;
    l.date_y = l.clk_y + 16 * l.clk_scale + 20;
    int fw = 384 < l.col_w - 40 ? 384 : l.col_w - 40;
    int fx = (sw - fw) / 2;
    int y0 = sh * 53 / 100;
    l.nf = gfx_rect_make(fx, y0, fw, 38);
    l.pf = gfx_rect_make(fx, y0 + 58, fw, 38);
    l.ok = gfx_rect_make(fx, y0 + 116, fw, 40);
    l.mk = gfx_rect_make(fx, y0 + 164, fw, 24);
    int py = y0 + 204;
    if (py < sh * 80 / 100) py = sh * 80 / 100;
    for (int i = 0; i < 2; i++) l.pw[i] = gfx_rect_make(sw / 2 - 90 + i * 100, py, 80, 70);
    return l;
}

/* The blurred column: the wallpaper averaged over 8x8 blocks, softened, darkened, faded out at both edges. */
static void build_strip(const lay *l)
{
    const gfx_surf *wl = ui_wallpaper_surf();
    int sw, sh;
    ui_resolution(&sw, &sh);
    int bw = (sw + 7) / 8, bh = (sh + 7) / 8;
    static u32 sm[(GFX_MAXW / 8 + 1) * (GFX_MAXH / 8 + 1)], sm2[(GFX_MAXW / 8 + 1) * (GFX_MAXH / 8 + 1)];
    for (int by = 0; by < bh; by++)
        for (int bx = 0; bx < bw; bx++) {
            u32 r = 0, g = 0, b = 0, n = 0;
            for (int y = by * 8; y < by * 8 + 8 && y < sh; y++)
                for (int x = bx * 8; x < bx * 8 + 8 && x < sw; x++) {
                    u32 c = wl->px[y * wl->stride + x];
                    r += (c >> 16) & 255; g += (c >> 8) & 255; b += c & 255; n++;
                }
            sm[by * bw + bx] = n ? (r / n) << 16 | (g / n) << 8 | (b / n) : 0;
        }
    for (int pass = 0; pass < 2; pass++) {                       /* 3x3 box, twice */
        for (int by = 0; by < bh; by++)
            for (int bx = 0; bx < bw; bx++) {
                u32 r = 0, g = 0, b = 0, n = 0;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        int y = by + dy, x = bx + dx;
                        if (y < 0 || y >= bh || x < 0 || x >= bw) continue;
                        u32 c = sm[y * bw + x];
                        r += (c >> 16) & 255; g += (c >> 8) & 255; b += c & 255; n++;
                    }
                sm2[by * bw + bx] = (r / n) << 16 | (g / n) << 8 | (b / n);
            }
        for (int i = 0; i < bw * bh; i++) sm[i] = sm2[i];
    }
    strip_w = l->col_w;
    strip.w = strip_w; strip.h = sh; strip.stride = strip_w; strip.px = strip_px;
    int fade = strip_w / 6;
    for (int y = 0; y < sh; y++) {
        int gy = y * 32 - 128; if (gy < 0) gy = 0;
        int iy = gy >> 8, fy = gy & 255, iy1 = iy + 1 < bh ? iy + 1 : bh - 1;
        if (iy >= bh) iy = bh - 1;
        for (int x = 0; x < strip_w; x++) {
            int gx = (l->col_x + x) * 32 - 128; if (gx < 0) gx = 0;
            int ix = gx >> 8, fx = gx & 255, ix1 = ix + 1 < bw ? ix + 1 : bw - 1;
            if (ix >= bw) ix = bw - 1;
            u32 c00 = sm[iy * bw + ix], c10 = sm[iy * bw + ix1], c01 = sm[iy1 * bw + ix], c11 = sm[iy1 * bw + ix1];
            u32 out = 0;
            for (int sft = 16; sft >= 0; sft -= 8) {
                u32 a = (c00 >> sft) & 255, b = (c10 >> sft) & 255, c = (c01 >> sft) & 255, d = (c11 >> sft) & 255;
                u32 top = a * (256 - fx) + b * fx, bot = c * (256 - fx) + d * fx;
                u32 v = (top * (256 - fy) + bot * fy) >> 16;
                v = v * 62 / 100;                                  /* darker than the wallpaper beside it */
                out |= v << sft;
            }
            int e = x < fade ? x : (strip_w - 1 - x < fade ? strip_w - 1 - x : fade);
            u32 al = (u32)(e * 255 / (fade ? fade : 1));
            strip_px[y * strip_w + x] = al << 24 | out;
        }
    }
    strip_for = (int)ui_wallpaper_serial();
}

static void pill(gfx_rect r, int on_, const char *text, int stars, int icon)
{
    const ui_theme *t = ui_th();
    (void)t;
    gfx_round_rect(r.x, r.y, r.w, r.h, r.h / 2, on_ ? 0xB0FFFFFFu : 0x55FFFFFFu);
    gfx_round_rect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, r.h / 2 - 1, on_ ? 0xC02A3045u : 0xA0202636u);
    if (icon == 0) ui_user_glyph(r.x + 22, r.y + r.h / 2, 22, 0xFFEEF1F6u, 0xFF9AA3B2u);
    else {                                                        /* a small padlock */
        gfx_round_rect(r.x + 14, r.y + r.h / 2 - 2, 16, 12, 3, 0xFFEEF1F6u);
        gfx_round_rect(r.x + 17, r.y + r.h / 2 - 9, 10, 10, 5, 0xFFEEF1F6u);
        gfx_round_rect(r.x + 20, r.y + r.h / 2 - 6, 4, 8, 2, 0xFF202636u);
    }
    char tmp[44];
    int n = (int)kstrlen(text);
    if (n > 38) n = 38;
    for (int i = 0; i < n; i++) tmp[i] = stars ? '*' : text[i];
    tmp[n] = 0;
    int max = (r.w - 70) / 8;
    const char *s = tmp;
    if (n > max) { s += n - max; n = max; }
    int tw = n * 8;
    int tx = r.x + (r.w - tw) / 2;
    gfx_text(tx, r.y + (r.h - 16) / 2, s, 0xFFFFFFFFu);
    if (on_) gfx_fill_rect(tx + tw + 1, r.y + (r.h - 16) / 2, 2, 16, 0xFFFFFFFFu);
}

static void power_icon(gfx_rect r, int which, int hot)
{
    int cx = r.x + r.w / 2, cy = r.y + 24;
    gfx_circle(cx, cy, 23, hot ? 0xFFFFFFFFu : 0xFFD7DCE6u);
    gfx_circle(cx, cy, 21, hot ? 0xFF3A4260u : 0xFF232A3Cu);
    u32 w = 0xFFFFFFFFu;
    if (which == 0) {                                             /* power symbol: ring with a gap and a bar */
        gfx_circle(cx, cy + 1, 9, w); gfx_circle(cx, cy + 1, 7, hot ? 0xFF3A4260u : 0xFF232A3Cu);
        gfx_fill_rect(cx - 5, cy - 11, 10, 9, hot ? 0xFF3A4260u : 0xFF232A3Cu);
        gfx_fill_rect(cx - 1, cy - 12, 3, 12, w);
    } else {                                                      /* reboot: ring with an arrow head */
        gfx_circle(cx, cy, 9, w); gfx_circle(cx, cy, 7, hot ? 0xFF3A4260u : 0xFF232A3Cu);
        gfx_fill_rect(cx, cy - 11, 11, 7, hot ? 0xFF3A4260u : 0xFF232A3Cu);
        gfx_fill_rect(cx + 2, cy - 12, 8, 3, w);
        gfx_fill_rect(cx + 8, cy - 12, 3, 8, w);
    }
    const char *s = which == 0 ? config_tr("Shutdown", "Kapat") : config_tr("Reboot", "Yeniden başlat");
    gfx_text(cx - gfx_text_w(s) / 2, r.y + 52, s, 0xFFE4E8F0u);
}

void login_screen_draw(gfx_rect clip)
{
    if (!on) return;
    int sw, sh;
    ui_resolution(&sw, &sh);
    lay l = layout();
    if (strip_for != (int)ui_wallpaper_serial() || strip_w != l.col_w) build_strip(&l);
    gfx_set_clip(&clip);
    gfx_blit_opaque(ui_wallpaper_surf(), 0, 0);
    gfx_fill_rect(0, 0, sw, sh, 0x66000000u);
    gfx_blit(&strip, l.col_x, 0);
    char tm[16], dt[48];
    shell_clock_strings(tm, sizeof tm, dt, sizeof dt);
    gfx_text_scaled((sw - gfx_text_w_scaled(tm, l.clk_scale)) / 2, l.clk_y, tm, 0xFFFFFFFFu, l.clk_scale);
    int ds = sh >= 700 ? 2 : 1;
    gfx_text_scaled((sw - gfx_text_w_scaled(dt, ds)) / 2, l.date_y, dt, 0xFFFFFFFFu, ds);
    const char *ttl = locked ? config_tr("Welcome back", "Tekrar hoş geldin") : config_tr("Sign in to AstraSageOS", "AstraSageOS'a giriş yap");
    gfx_text((sw - gfx_text_w(ttl)) / 2, l.nf.y - 34, ttl, 0xFFDCE2EEu);
    pill(l.nf, focus == 0 && !locked, name, 0, 0);
    pill(l.pf, focus == 1, pass, 1, 1);
    gfx_round_rect(l.ok.x, l.ok.y, l.ok.w, l.ok.h, l.ok.h / 2, 0xF0262B3Au);
    gfx_round_rect(l.ok.x + 1, l.ok.y + 1, l.ok.w - 2, l.ok.h - 2, l.ok.h / 2 - 1, 0xFF313750u);
    const char *s1 = config_tr("Sign in", "Giriş yap");
    gfx_text(l.ok.x + (l.ok.w - gfx_text_w(s1)) / 2, l.ok.y + (l.ok.h - 16) / 2, s1, 0xFFFFFFFFu);
    const char *s2 = locked ? config_tr("Other account", "Başka hesap") : config_tr("Create account", "Hesap oluştur");
    gfx_text(l.mk.x + (l.mk.w - gfx_text_w(s2)) / 2, l.mk.y + 4, s2, 0xFFC4CCDCu);
    if (msg[0]) gfx_text((sw - gfx_text_w(msg)) / 2, l.ok.y + l.ok.h + 2 + 0, msg, msg_err ? 0xFFFF8A8Au : 0xFF9BE8C0u);
    for (int i = 0; i < 2; i++) power_icon(l.pw[i], i, hot_pw == i + 1);
    const char *os = "AstraSageOS";
    gfx_text((sw - gfx_text_w(os)) / 2, sh - 36, os, 0xFFB8C0D0u);
    gfx_set_clip(0);
}

static void redraw(void)
{
    int sw, sh;
    ui_resolution(&sw, &sh);
    if (on) wm_damage(gfx_rect_make(0, 0, sw, sh));
}

static void redraw_form(void)
{
    if (!on) return;
    lay l = layout();
    wm_damage(gfx_rect_make(l.nf.x - 4, l.nf.y - 44, l.nf.w + 8, l.mk.y + l.mk.h - l.nf.y + 70));
}

static void say(int err, const char *m) { kstrncpy(msg, m, sizeof msg - 1); msg[sizeof msg - 1] = 0; msg_err = err; redraw_form(); }

static void close_screen(void)
{
    on = 0;
    name[0] = pass[0] = msg[0] = 0; locked = 0;
    redraw();
}

static void do_login(int create)
{
    if (!name[0]) { say(1, config_tr("Type a user name", "Kullanıcı adı yaz")); focus = 0; return; }
    if (!pass[0]) { say(1, config_tr("Type a password", "Şifre yaz")); focus = 1; return; }
    int r = create ? user_create(name, pass) : user_login(name, pass);
    if (r >= 0 && !create) { user_unlock_clear(); locked = 0; }
    if (r < 0) { say(1, user_strerror(r)); pass[0] = 0; focus = 1; return; }
    if (create) {
        r = user_login(name, pass);
        if (r < 0) { say(1, user_strerror(r)); return; }
    }
    close_screen();
    ui_launch_app(APP_AST);
}

/* pointer: while the layer is up everything lands here and nothing reaches the desktop */
int login_screen_pointer(const mouse_ev *e, int down)
{
    if (!on) return 0;
    lay l = layout();
    int h = 0;
    for (int i = 0; i < 2; i++) if (gfx_rect_has(&l.pw[i], e->x, e->y)) h = i + 1;
    if (h != hot_pw) { hot_pw = h; wm_damage(gfx_rect_make(l.pw[0].x - 10, l.pw[0].y - 6, 220, l.pw[0].h + 12)); }
    if (!down) return 1;
    if (gfx_rect_has(&l.nf, e->x, e->y)) { if (!locked) { focus = 0; redraw_form(); } }
    else if (gfx_rect_has(&l.pf, e->x, e->y)) { focus = 1; redraw_form(); }
    else if (gfx_rect_has(&l.ok, e->x, e->y)) do_login(0);
    else if (gfx_rect_has(&l.mk, e->x, e->y)) {
        if (locked) { locked = 0; name[0] = pass[0] = msg[0] = 0; focus = 0; redraw_form(); }
        else do_login(1);
    }
    else if (h == 1) { persist_flush(); hal_shutdown(); }
    else if (h == 2) { persist_flush(); hal_reboot(); }
    return 1;
}

int login_screen_key(int k)
{
    if (!on) return 0;
    if (locked) focus = 1;
    char *f = focus ? pass : name;
    size_t n = kstrlen(f);
    if (k == '\t' || k == KEY_DOWN || k == KEY_UP) { if (!locked) focus = !focus; }
    else if (k == '\n' || k == '\r') { if (focus == 0 && !pass[0]) focus = 1; else { do_login(0); return 1; } }
    else if (k == '\b' || k == 127) { if (n) f[n - 1] = 0; }
    else if (k >= 32 && k < 127 && n < 30) { f[n] = (char)k; f[n + 1] = 0; }
    else return 1;
    msg[0] = 0;
    redraw_form();
    return 1;
}

void login_screen_tick(void)                                     /* the clock on the lock screen follows the minute */
{
    if (!on) return;
    char tm[16], dt[48];
    shell_clock_strings(tm, sizeof tm, dt, sizeof dt);
    int m = (tm[3] - '0') * 10 + (tm[4] - '0') + 100 * ((tm[0] - '0') * 10 + (tm[1] - '0'));
    if (m == clock_min) return;
    clock_min = m;
    lay l = layout();
    int sw, sh;
    ui_resolution(&sw, &sh);
    (void)sh;
    wm_damage(gfx_rect_make(l.col_x, l.clk_y - 4, l.col_w, l.date_y + 40 - l.clk_y));
    (void)sw;
}

void app_login_test_close(void) { close_screen(); }

void app_login_init(void) { name[0] = pass[0] = msg[0] = 0; focus = 0; locked = 0; on = 0; strip_for = -1; strip_w = 0; }

void app_login_launch(void)
{
    if (on) return;
    on = 1; focus = 0; name[0] = pass[0] = msg[0] = 0; clock_min = -1; hot_pw = 0;
    redraw();
}

/* After the first login the password is asked at every start: the saved account is put aside and the login screen
 * opens with the name filled in. Returns 1 when the lock screen was shown. */
int ui_lock_now(void)
{
    if (!user_lock_session()) return 0;
    const char *who = user_locked_name();
    app_login_launch();
    kstrncpy(name, who, sizeof name - 1); name[sizeof name - 1] = 0;
    pass[0] = msg[0] = 0; focus = 1; locked = 1;
    redraw();
    return 1;
}

int ui_boot_lock(void)
{
    ui_setup_reconcile();
    if (!config_get()->setup_done || !user_name()) return 0;
    return ui_lock_now();
}
