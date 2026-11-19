/*
 * "AstraSageOS'a hoşgeldiniz." - the welcome and setup application (APP_WELCOME).
 * Opens by itself until the setup was finished. Pages:
 *   0 welcome (particles, As logo)   1 language   2 theme (live preview)   3 login mode (terminal / graphic)
 *   4 drives (detected)   5 package   6 installer (progress + log)
 * The installer really checks the OS root, drive S: and /config and creates what is missing.
 * Closing the window before the setup is done asks for it first (the red button is vetoed once).
 */
#include "gui.h"

enum { P_WELCOME, P_LANG, P_THEME, P_LOGIN, P_DRIVES, P_PKG, P_INSTALL, P_COUNT };
enum { I_IDLE, I_RUN, I_DONE };

#define CW 760
#define CH 500
#define NPART 26
#define LOGN 9

static int  page;
static int  setup_done;                  /* installer finished and settings saved */
static int  warn;                        /* "not finished" question is showing */
static char msg[96];

/* choices */
static int  c_lang, c_theme, c_gui, c_drive;
static int  pkg_yes;
static char pkg_name[32], pkg_file[64];
static int  pfocus;

/* drives */
typedef struct { int present, mb, repo; } drv;
static drv  drives[2];

/* installer */
static int  inst, inst_i, inst_n;
static u32  inst_t;
static char logl[LOGN][80];
static int  logc;
static char phase[64];

/* particles */
typedef struct { int x, y, vx, vy, r; } part;
static part pt[NPART];
static u32  pt_t, rnd = 12345;
static int  pt_ok;

static u32 rnd_next(void) { rnd = rnd * 1664525u + 1013904223u; return rnd >> 8; }

static const char *T(const char *en, const char *tr) { return config_tr(en, tr); }

static window *me(void) { return wm_find_app(APP_WELCOME); }
static gfx_rect cont(void) { window *w = me(); return w ? wm_content_rect(w) : gfx_rect_make(0, 0, 0, 0); }
static void redraw(void) { gfx_rect c = cont(); if (c.w) wm_damage(c); }

/* ---------------------------------------------------------------- installer */

typedef struct { const char *path; int dir; } item;
static const item items[] = {
    { "/bin", 1 }, { "/bin/neofetch", 1 }, { "/bin/neofetch/custom_logo", 1 }, { "/bin/neofetch/custom_logo_txt", 1 },
    { "/.aspi", 1 }, { "/.aspi/app", 1 }, { "/.aspi/lib", 1 }, { "/.term", 1 }, { "/.term/log", 1 },
    { "/user", 1 }, { "/proc", 1 }, { "/.config", 1 }, { "/drives", 1 }, { "/drives/S", 1 }, { "/drives/S/usrs", 1 },
    { "/bin/version", 0 }, { "/.term/term.cfg", 0 }, { "/.term/ast8x16.fnt", 0 }, { "/user/accounts", 0 },
    { "/proc/README", 0 }, { "/proc/kernel_panic", 0 }, { "/proc/heavy_load", 0 },
};
#define NITEMS ((int)(sizeof items / sizeof items[0]))
#define NEXTRA 2                                            /* settings file + saving to disk */


static void logline(const char *s)
{
    if (logc < LOGN) kstrncpy(logl[logc++], s, 79);
    else {
        for (int i = 1; i < LOGN; i++) kmemcpy(logl[i - 1], logl[i], 80);
        kstrncpy(logl[LOGN - 1], s, 79);
    }
    logl[logc - 1][79] = 0;
}

static void inst_reset(void) { inst = I_IDLE; inst_i = 0; logc = 0; phase[0] = 0; }

static void inst_step(void)
{
    char b[96];
    int o = fs_get_jail();
    fs_set_jail(0);
    if (inst_i < NITEMS) {
        const item *it = &items[inst_i];
        ksnprintf(phase, sizeof phase, "%s",
                  inst_i < 21 ? T("Checking the system folders and files (root)", "Sistem klasörleri ve dosyaları denetleniyor (kök)")
                  : inst_i < 36 ? T("Checking drive S:", "S: sürücüsü denetleniyor")
                                : T("Preparing the config folder", "config klasörü hazırlanıyor"));
        int n = fs_resolve(it->path, 0);
        if (n < 0) {
            if (it->dir) fs_create(it->path, 0, 1); else fs_seed_system();
            n = fs_resolve(it->path, 0);
            ksnprintf(b, sizeof b, "%s %s  %s", it->dir ? T("created folder", "klasör oluşturuldu") : T("created file", "dosya oluşturuldu"),
                      it->path, n >= 0 ? "OK" : T("FAILED", "BAŞARISIZ"));
        } else {
            ksnprintf(b, sizeof b, "%s %s  OK", it->dir ? T("folder", "klasör") : T("file", "dosya"), it->path);
        }
        logline(b);
    } else if (inst_i == NITEMS) {
        ksnprintf(phase, sizeof phase, "%s", T("Saving your choices", "Seçimleriniz kaydediliyor"));
        as_settings s = *config_get();
        s.lang = c_lang; s.theme = c_theme; s.login_gui = c_gui; s.repo_drive = c_drive; s.setup_done = 1;
        config_set(&s);
        ksnprintf(b, sizeof b, "/.config/settings  %s", fs_resolve("/.config/settings", 0) > 0 ? "OK" : T("FAILED", "BAŞARISIZ"));
        logline(b);
    } else {
        ksnprintf(phase, sizeof phase, "%s", T("Writing to the disk", "Diske yazılıyor"));
        persist_flush();
        ksnprintf(b, sizeof b, "%s  %s", T("saved to disk", "diske kaydedildi"), persist_active() ? "OK" : T("(no disk: memory only)", "(disk yok: yalnızca bellek)"));
        logline(b);
    }
    fs_set_jail(o);
    inst_i++;
    if (inst_i >= inst_n) {
        inst = I_DONE; setup_done = 1;
        ksnprintf(phase, sizeof phase, "%s", T("Setup finished", "Kurulum tamamlandı"));
        if (pkg_yes && pkg_name[0]) {
            ksnprintf(b, sizeof b, "%s %s", T("package queued:", "paket sırada:"), pkg_name);
            logline(b);
        }
    }
    redraw();
}

/* ---------------------------------------------------------------- drives */

static void detect_drives(void)
{
    u8 sec[512];
    for (int d = 0; d < 2; d++) {
        drives[d].present = d == 0 ? ata_sectors_drive(0) != 0 : ata_init_drive(1);
        drives[d].mb = drives[d].present ? (int)(ata_sectors_drive(d) / 2048) : 0;
        drives[d].repo = drives[d].present && ata_read_drive(d, 0, sec) == 0 && !kmemcmp(sec, "ASPIREPO", 8);
    }
    c_drive = drives[1].repo ? 1 : drives[0].repo ? 0 : 1;
}

/* ---------------------------------------------------------------- particles */

static void pt_seed(int w, int h)
{
    for (int i = 0; i < NPART; i++) {
        pt[i].x = (int)(rnd_next() % (u32)w) << 8; pt[i].y = (int)(rnd_next() % (u32)h) << 8;
        pt[i].vx = (int)(rnd_next() % 161) - 80; pt[i].vy = (int)(rnd_next() % 161) - 80;
        if (!pt[i].vx) pt[i].vx = 20;
        pt[i].r = 1 + (int)(rnd_next() % 3);
    }
    pt_ok = 1;
}

static void pt_step(int w, int h)
{
    for (int i = 0; i < NPART; i++) {
        pt[i].x += pt[i].vx; pt[i].y += pt[i].vy;
        if (pt[i].x < 0) { pt[i].x = 0; pt[i].vx = -pt[i].vx; }
        if (pt[i].y < 0) { pt[i].y = 0; pt[i].vy = -pt[i].vy; }
        if (pt[i].x > w << 8) { pt[i].x = w << 8; pt[i].vx = -pt[i].vx; }
        if (pt[i].y > h << 8) { pt[i].y = h << 8; pt[i].vy = -pt[i].vy; }
    }
}

/* ---------------------------------------------------------------- drawing helpers */

static void button(gfx_rect r, const char *label, int primary, int enabled)
{
    const ui_theme *t = ui_th();
    u32 bg = !enabled ? t->btn2 : primary ? t->btn_main : t->btn2;
    u32 fg = !enabled ? t->txt_off : primary ? t->btn_main_txt : t->txt;
    gfx_round_rect(r.x, r.y, r.w, r.h, 8, bg);
    gfx_text(r.x + (r.w - gfx_text_w(label)) / 2, r.y + (r.h - 16) / 2, label, fg);
}

static void text_c(int cx, int y, const char *s, u32 col) { gfx_text(cx - gfx_text_w(s) / 2, y, s, col); }

static void lines(int x, int y, const char *s, u32 col)             /* '\n' separated */
{
    char t[120];
    while (*s) {
        int n = 0;
        while (s[n] && s[n] != '\n' && n < 118) n++;
        kmemcpy(t, s, (size_t)n); t[n] = 0;
        gfx_text(x, y, t, col);
        y += 22;
        s += n; if (*s == '\n') s++;
    }
}

static void card(gfx_rect r, int sel)
{
    const ui_theme *t = ui_th();
    gfx_shadow(r.x, r.y, r.w, r.h, 12, 8, 40);
    gfx_round_rect(r.x, r.y, r.w, r.h, 12, sel ? t->accent : t->line);
    gfx_round_rect(r.x + 2, r.y + 2, r.w - 4, r.h - 4, 10, sel ? t->cell_sel : t->panel);
}

static void field(gfx_rect r, const char *label, const char *text, int on)
{
    const ui_theme *t = ui_th();
    gfx_text(r.x, r.y - 20, label, t->dim);
    gfx_round_rect(r.x, r.y, r.w, r.h, 7, on ? t->accent : t->line);
    gfx_round_rect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 6, t->field);
    int n = (int)kstrlen(text), max = (r.w - 20) / 8;
    const char *s = text;
    if (n > max) { s += n - max; n = max; }
    gfx_text(r.x + 10, r.y + (r.h - 16) / 2, s, t->txt);
    if (on) gfx_fill_rect(r.x + 10 + n * 8, r.y + (r.h - 16) / 2, 2, 16, t->accent);
}

/* ---------------------------------------------------------------- layout (shared by drawing and clicking) */

typedef struct {
    gfx_rect next, back;
    gfx_rect card[THEME_COUNT];
    gfx_rect yes, no, f1, f2;
    gfx_rect warn_a, warn_b;
} lay;

static lay layout(gfx_rect cr)
{
    lay l;
    int fy = cr.y + cr.h - 60;
    l.next = gfx_rect_make(cr.x + cr.w - 28 - 150, fy, 150, 38);
    l.back = gfx_rect_make(cr.x + 28, fy, 120, 38);
    if (page == P_LANG) {                                   /* five language cards in a row */
        int cw = 128, gap = 14, x0 = cr.x + (cr.w - (5 * cw + 4 * gap)) / 2;
        for (int i = 0; i < 5; i++) l.card[i] = gfx_rect_make(x0 + i * (cw + gap), cr.y + 130, cw, 190);
    } else if (page == P_THEME) {                           /* nine theme cards: 3 x 3 */
        int cw = 224, ch = 92, gx = 14, gy = 8, x0 = cr.x + (cr.w - (3 * cw + 2 * gx)) / 2;
        for (int i = 0; i < THEME_COUNT; i++) l.card[i] = gfx_rect_make(x0 + (i % 3) * (cw + gx), cr.y + 116 + (i / 3) * (ch + gy), cw, ch);
    } else {
        int cw = page == P_LOGIN ? 280 : 240, gap = 24, n = 2;
        int tot = n * cw + (n - 1) * gap, x0 = cr.x + (cr.w - tot) / 2;
        for (int i = 0; i < 3; i++) l.card[i] = gfx_rect_make(x0 + i * (cw + gap), cr.y + 130, cw, 200);
    }
    l.yes = gfx_rect_make(cr.x + cr.w / 2 - 130, cr.y + 150, 120, 40);
    l.no = gfx_rect_make(cr.x + cr.w / 2 + 10, cr.y + 150, 120, 40);
    l.f1 = gfx_rect_make(cr.x + 110, cr.y + 250, cr.w - 220, 34);
    l.f2 = gfx_rect_make(cr.x + 110, cr.y + 322, cr.w - 220, 34);
    l.warn_a = gfx_rect_make(cr.x + cr.w / 2 - 210, cr.y + cr.h / 2 + 30, 200, 38);
    l.warn_b = gfx_rect_make(cr.x + cr.w / 2 + 10, cr.y + cr.h / 2 + 30, 200, 38);
    return l;
}

/* ---------------------------------------------------------------- drawing: pages */

/* The night background (gradient + glow) never changes: it is painted once into a surface and copied row-wise
 * every frame. Painting the two big translucent discs per frame was what made the welcome page stutter. */
static u32 bg_px[CW * CH];
static gfx_surf bg_s = { CW, CH, CW, bg_px };
static int bg_ready;

static void bg_build(void)
{
    gfx_rect keep = gfx_get_clip();
    gfx_set_target(&bg_s);
    for (int i = 0; i < 64; i++) {
        int y0 = CH * i / 64, y1 = CH * (i + 1) / 64;
        int g = 40 - i * 22 / 64, k = 6 - i * 3 / 64;
        gfx_fill_rect(0, y0, CW, y1 - y0 + 1, RGB(k, g, k + 6));
    }
    gfx_circle(CW / 2, 200, 190, ARGB(26, 120, 230, 40));
    gfx_circle(CW / 2, 200, 120, ARGB(26, 140, 240, 60));
    gfx_set_target(0);
    gfx_set_clip(&keep);
    bg_ready = 1;
}

static void draw_particles(gfx_rect cr)
{
    if (cr.w == CW && cr.h == CH) {
        if (!bg_ready) bg_build();
        gfx_blit_opaque(&bg_s, cr.x, cr.y);
    } else {
        gfx_fill_rect(cr.x, cr.y, cr.w, cr.h, RGB(6, 30, 12));         /* resized/maximised: plain night colour */
    }
    for (int i = 0; i < NPART; i++)
        for (int j = i + 1; j < NPART; j++) {
            int dx = (pt[i].x - pt[j].x) >> 8, dy = (pt[i].y - pt[j].y) >> 8;
            int d2 = dx * dx + dy * dy;
            if (d2 < 85 * 85) {
                int a = 70 - d2 * 70 / (85 * 85);
                gfx_line(cr.x + (pt[i].x >> 8), cr.y + (pt[i].y >> 8), cr.x + (pt[j].x >> 8), cr.y + (pt[j].y >> 8), ARGB(a, 155, 224, 28));
            }
        }
    for (int i = 0; i < NPART; i++)
        gfx_circle(cr.x + (pt[i].x >> 8), cr.y + (pt[i].y >> 8), pt[i].r, ARGB(190, 190, 245, 110));
}

static void draw_welcome(gfx_rect cr)
{
    if (!pt_ok) pt_seed(cr.w, cr.h);
    draw_particles(cr);
    int cx = cr.x + cr.w / 2;
    draw_app_icon(APP_WELCOME, cx - 64, cr.y + 60, 128);
    const char *a = "Welcome to AstraSageOS.", *b = "AstraSageOS'a hoşgeldiniz.";
    int s = 2;
    gfx_text_scaled(cx - gfx_text_w_scaled(a, s) / 2 + 1, cr.y + 218, a, 0xFF000000u, s);
    gfx_text_scaled(cx - gfx_text_w_scaled(a, s) / 2, cr.y + 216, a, 0xFFFFFFFFu, s);
    text_c(cx, cr.y + 262, b, 0xFFBFEFA0u);
    text_c(cx, cr.y + 300, "Let's set up your system in a few steps.", 0xFFD8E8D0u);
    text_c(cx, cr.y + 322, "Sistemini birkaç adımda kuralım.", 0xFFD8E8D0u);
}

static void draw_title(gfx_rect cr, const char *t, const char *sub)
{
    const ui_theme *th = ui_th();
    gfx_text_scaled(cr.x + 40, cr.y + 30, t, th->txt, 2);
    if (sub) gfx_text(cr.x + 40, cr.y + 76, sub, th->dim);
    gfx_fill_rect(cr.x + 40, cr.y + 104, cr.w - 80, 1, th->line);
}

static void draw_theme_card(gfx_rect r, int id)
{
    ui_theme_preview(r.x + 8, r.y + 8, 84, r.h - 16, id);
    int cx = r.x + 92 + (r.w - 92) / 2;
    text_c(cx, r.y + r.h / 2 - 20, id == THEME_WHITE ? T("White", "Beyaz") : ui_theme_label(id), ui_th()->txt);
    text_c(cx, r.y + r.h / 2 + 4, id == THEME_WHITE ? T("Light, default", "Açık, varsayılan") : ui_theme_kind(id), ui_th()->dim);
}

static void welcome_draw(window *w, gfx_rect cr, gfx_rect clip, int focused)
{
    (void)w; (void)focused;
    const ui_theme *t = ui_th();
    gfx_set_clip(&clip);
    lay l = layout(cr);
    int cx = cr.x + cr.w / 2;
    if (page == P_WELCOME) {
        draw_welcome(cr);
        button(l.next, T("Continue", "Devam"), 1, 1);
        gfx_set_clip(0);
        if (warn) goto warning;
        return;
    }
    gfx_fill_rect(cr.x, cr.y, cr.w, cr.h, t->bg);
    switch (page) {
    case P_LANG:
        draw_title(cr, "Language / Dil", T("Choose the language of AstraSageOS windows and menus.", "AstraSageOS pencere ve menülerinin dilini seç."));
        {
            static const char *const code[5] = { "EN", "TR", "ES", "RU", "ZH" };
            static const char *const name[5] = { "English", "Türkçe", "Español", "Русский", "简体中文" };
            static const char *const sub[5] = { "Hello", "Merhaba", "Hola", "Привет", "你好" };
            for (int i = 0; i < 5; i++) {
                card(l.card[i], c_lang == i);
                int mx = l.card[i].x + l.card[i].w / 2;
                text_c(mx, l.card[i].y + 40, code[i], t->accent);
                int sc = gfx_text_w(name[i]) * 2 <= l.card[i].w - 12 ? 2 : 1;
                gfx_text_scaled(mx - gfx_text_w_scaled(name[i], sc) / 2, l.card[i].y + 76, name[i], t->txt, sc);
                text_c(mx, l.card[i].y + 140, sub[i], t->dim);
            }
        }
        break;
    case P_THEME:
        draw_title(cr, T("Theme", "Tema"), T("Dock, windows and every place that was black in older versions follow it.", "Dock, pencereler ve eski sürümde siyah olan her yer buna uyar."));
        for (int i = 0; i < THEME_COUNT; i++) { card(l.card[i], c_theme == i); draw_theme_card(l.card[i], i); }
        break;
    case P_LOGIN:
        draw_title(cr, T("Sign-in", "Giriş"), T("How should signing in work?", "Giriş nasıl yapılsın?"));
        for (int i = 0; i < 2; i++) {
            card(l.card[i], c_gui == i);
            int ix = l.card[i].x + l.card[i].w / 2;
            draw_app_icon(i ? APP_LOGIN : APP_AST, ix - 36, l.card[i].y + 22, 72);
            text_c(ix, l.card[i].y + 112, i ? T("Graphic", "Grafik") : T("Terminal", "Terminal"), t->txt);
            const char *d = i ? T("Name and password in a window\nwith the user icon", "Kullanıcı simgeli pencerede\nisim ve şifre")
                              : T("The normal AST terminal asks\nfor your login", "Normal AST terminali\ngirişi senden ister");
            lines(l.card[i].x + 22, l.card[i].y + 140, d, t->dim);
        }
        break;
    case P_DRIVES:
        draw_title(cr, T("Drives", "Sürücüler"), T("Detected automatically. Choose the drive that holds the package repository.", "Otomatik algılandı. Paket deposunun bulunduğu sürücüyü seç."));
        for (int i = 0; i < 2; i++) {
            gfx_rect r = gfx_rect_make(cr.x + 60, cr.y + 130 + i * 74, cr.w - 120, 62);
            card(r, drives[i].present && c_drive == i);
            char b[96];
            ksnprintf(b, sizeof b, i ? T("Drive 1 (secondary disk)", "Sürücü 1 (ikincil disk)") : T("Drive 0 (system disk)", "Sürücü 0 (sistem diski)"));
            gfx_text(r.x + 20, r.y + 12, b, drives[i].present ? t->txt : t->txt_off);
            if (!drives[i].present) ksnprintf(b, sizeof b, "%s", T("not found", "bulunamadı"));
            else ksnprintf(b, sizeof b, "%d MB  -  %s", drives[i].mb, drives[i].repo ? T("package repository found", "paket deposu bulundu") : T("no package repository", "paket deposu yok"));
            gfx_text(r.x + 20, r.y + 34, b, t->dim);
        }
        gfx_text(cr.x + 60, cr.y + 290, T("S:  AstraSage drive - part of the system, always used.", "S:  AstraSage sürücüsü - sistemin parçası, her zaman kullanılır."), t->dim);
        break;
    case P_PKG:
        draw_title(cr, T("Package", "Paket"), T("Do you want to install a package now?", "Şimdi bir paket kurmak ister misin?"));
        button(l.yes, T("Yes", "Evet"), pkg_yes, 1);
        button(l.no, T("No", "Hayır"), !pkg_yes, 1);
        if (pkg_yes) {
            field(l.f1, T("Package name", "Paket ismi"), pkg_name, pfocus == 0);
            field(l.f2, T("Full file name of the package", "Paketin dosyasının tam ismi"), pkg_file, pfocus == 1);
        }
        break;
    case P_INSTALL: {
        draw_title(cr, "Installer", T("Checks the whole system and drive S:, and creates the config folder and what is missing.", "Tüm sistemi ve S: sürücüsünü denetler; config klasörünü ve eksikleri oluşturur."));
        int bx = cr.x + 40, bw = cr.w - 80;
        if (inst == I_IDLE) {
            lines(bx, cr.y + 140, T("Press Install to begin.\nYour language, theme, sign-in mode and drive choices\nare saved when the installer finishes.",
                                    "Başlamak için Kur'a bas.\nDil, tema, giriş şekli ve sürücü seçimlerin\nkurulum bitince kaydedilir."), t->txt);
        } else {
            gfx_text(bx, cr.y + 126, phase, t->txt);
            gfx_round_rect(bx, cr.y + 154, bw, 22, 11, t->line);
            int fill = inst_n ? bw * inst_i / inst_n : 0;
            if (fill > 2) gfx_round_rect(bx, cr.y + 154, fill, 22, 11, t->accent);
            char pc[16];
            ksnprintf(pc, sizeof pc, "%d%%", inst_n ? inst_i * 100 / inst_n : 0);
            text_c(bx + bw / 2, cr.y + 157, pc, inst == I_DONE ? t->btn_main_txt : t->txt);
            gfx_round_rect(bx, cr.y + 190, bw, 20 * LOGN + 14, 8, t->field);
            for (int i = 0; i < logc; i++) gfx_text(bx + 12, cr.y + 197 + i * 20, logl[i], i == logc - 1 ? t->txt : t->dim);
        }
        break;
    }
    }
    /* footer */
    if (page > P_WELCOME && !(page == P_INSTALL && inst == I_RUN)) button(l.back, T("Back", "Geri"), 0, !(page == P_INSTALL && inst == I_DONE));
    {
        const char *nl = page == P_INSTALL ? (inst == I_DONE ? T("OK", "Tamam") : inst == I_RUN ? T("Installing...", "Kuruluyor...") : T("Install", "Kur")) : T("Continue", "Devam");
        button(l.next, nl, 1, !(page == P_INSTALL && inst == I_RUN));
    }
    for (int i = 0; i < P_COUNT; i++) gfx_circle(cx - (P_COUNT - 1) * 9 + i * 18, cr.y + cr.h - 41, i == page ? 5 : 3, i == page ? t->accent : t->line);
    if (msg[0]) text_c(cx, cr.y + cr.h - 96, msg, t->danger);
    gfx_set_clip(0);
    if (!warn) return;
    gfx_set_clip(&clip);
warning:
    {
        const ui_theme *th = ui_th();
        gfx_fill_rect(cr.x, cr.y, cr.w, cr.h, th->dim_modal);
        gfx_rect p = gfx_rect_make(cr.x + cr.w / 2 - 260, cr.y + cr.h / 2 - 110, 520, 230);
        gfx_shadow(p.x, p.y, p.w, p.h, 14, 12, 60);
        gfx_round_rect(p.x, p.y, p.w, p.h, 14, th->panel);
        gfx_text_scaled(p.x + 24, p.y + 20, T("Setup is not finished", "Kurulum bitmedi"), th->danger, 1);
        lines(p.x + 24, p.y + 56, T("Without the AstraSage OS setup and its choices,\nsome things will not work properly or the way\nyou want. Set it up now?",
                                    "AstraSage OS kurulumu ve seçimleri yapılmazsa\nbazı şeyler düzgün ve istediğin gibi çalışmaz.\nŞimdi kurulum yapılsın mı?"), th->txt);
        button(l.warn_a, T("Set up now", "Şimdi kur"), 1, 1);
        button(l.warn_b, T("Close anyway", "Yine de kapat"), 0, 1);
    }
    gfx_set_clip(0);
}

/* ---------------------------------------------------------------- input */

static void enter_page(int p)
{
    page = p; msg[0] = 0;
    if (p == P_DRIVES) detect_drives();
    if (p == P_INSTALL && inst == I_IDLE) { inst_n = NITEMS + NEXTRA; }
    redraw();
}

static void go_next(void)
{
    msg[0] = 0;
    if (page == P_PKG && pkg_yes && (!pkg_name[0] || !pkg_file[0])) {
        kstrncpy(msg, T("Fill in both the package name and the file name.", "Paket ismini ve dosya ismini doldur."), sizeof msg - 1);
        redraw(); return;
    }
    if (page == P_INSTALL) {
        if (inst == I_IDLE) { inst = I_RUN; inst_i = 0; inst_n = NITEMS + NEXTRA; inst_t = ui_ms(); logc = 0; redraw(); }
        else if (inst == I_DONE) {
            window *w = me();
            int run_pkg = pkg_yes && pkg_name[0];
            char cmd[120];
            ksnprintf(cmd, sizeof cmd, "as aspi install %s", pkg_name);
            if (w) wm_close(w);
            if (run_pkg) app_ast_run(cmd);
            else if (c_gui) app_login_launch();
            else ui_launch_app(APP_AST);
        }
        return;
    }
    if (page < P_INSTALL) enter_page(page + 1);
}

static void go_back(void)
{
    if (page > P_WELCOME && !(page == P_INSTALL && inst == I_RUN)) {
        if (page == P_INSTALL) inst_reset();
        enter_page(page - 1);
    }
}

static void set_lang(int i)
{
    c_lang = i;
    as_settings s = *config_get();
    s.lang = i;
    config_set(&s);
    window *w = me();
    if (w) kstrncpy(w->title, "AstraSageOS'a hoşgeldiniz.", sizeof w->title - 1);
    wm_damage(gfx_rect_make(0, 0, gfx_width(), gfx_height()));
}

static void welcome_mouse(window *w, int x, int y, u8 b, u8 prev)
{
    (void)w;
    if (x < 0 || !(b & 1) || (prev & 1)) return;
    gfx_rect cr = cont();
    lay l = layout(cr);
    if (warn) {
        if (gfx_rect_has(&l.warn_a, x, y)) { warn = 0; if (page == P_WELCOME) enter_page(P_LANG); else redraw(); }
        else if (gfx_rect_has(&l.warn_b, x, y)) { warn = 0; window *me_ = me(); if (me_) wm_close(me_); }
        return;
    }
    if (gfx_rect_has(&l.next, x, y)) { go_next(); return; }
    if (gfx_rect_has(&l.back, x, y)) { go_back(); return; }
    switch (page) {
    case P_LANG:
        for (int i = 0; i < 5; i++) if (gfx_rect_has(&l.card[i], x, y)) { set_lang(i); redraw(); }
        break;
    case P_THEME:
        for (int i = 0; i < THEME_COUNT; i++) if (gfx_rect_has(&l.card[i], x, y)) {
            c_theme = i; ui_theme_set(i);
            as_settings s = *config_get(); s.theme = i; config_set(&s);
            redraw();
        }
        break;
    case P_LOGIN:
        for (int i = 0; i < 2; i++) if (gfx_rect_has(&l.card[i], x, y)) { c_gui = i; redraw(); }
        break;
    case P_DRIVES:
        for (int i = 0; i < 2; i++) {
            gfx_rect r = gfx_rect_make(cr.x + 60, cr.y + 130 + i * 74, cr.w - 120, 62);
            if (drives[i].present && gfx_rect_has(&r, x, y)) { c_drive = i; redraw(); }
        }
        break;
    case P_PKG:
        if (gfx_rect_has(&l.yes, x, y)) { pkg_yes = 1; redraw(); }
        else if (gfx_rect_has(&l.no, x, y)) { pkg_yes = 0; redraw(); }
        else if (pkg_yes && gfx_rect_has(&l.f1, x, y)) { pfocus = 0; redraw(); }
        else if (pkg_yes && gfx_rect_has(&l.f2, x, y)) { pfocus = 1; redraw(); }
        break;
    }
}

static void welcome_key(window *w, int k)
{
    (void)w;
    if (warn) { if (k == '\n') { warn = 0; redraw(); } return; }
    if (page == P_PKG && pkg_yes) {
        char *f = pfocus ? pkg_file : pkg_name;
        size_t n = kstrlen(f), cap = pfocus ? 60 : 30;
        if (k == '\t') { pfocus = !pfocus; redraw(); return; }
        if (k == '\b' || k == 127) { if (n) f[n - 1] = 0; redraw(); return; }
        if (k >= 32 && k < 127 && n < cap) { f[n] = (char)k; f[n + 1] = 0; redraw(); return; }
    }
    if (k == '\n' || k == '\r') go_next();
}

static int welcome_can_close(window *w)
{
    (void)w;
    if (setup_done || config_get()->setup_done) return 1;
    warn = 1;
    wm_raise(me());
    redraw();
    return 0;
}

static void welcome_closed(window *w) { (void)w; warn = 0; }

const wm_app app_welcome_ops = { welcome_draw, 0, welcome_closed, 0, welcome_mouse, welcome_key, welcome_can_close };

/* ---------------------------------------------------------------- life cycle */

void app_welcome_init(void)
{
    const as_settings *c = config_get();
    c_lang = c->lang; c_theme = c->theme; c_gui = c->login_gui; c_drive = c->repo_drive > 1 ? 1 : c->repo_drive;
    setup_done = c->setup_done;
    page = P_WELCOME; warn = 0; msg[0] = 0; pkg_yes = 0; pkg_name[0] = pkg_file[0] = 0; pfocus = 0;
    inst_reset(); pt_ok = 0;
}

void app_welcome_launch(void)
{
    window *w = me();
    if (w) { if (w->state == WS_MINIMIZED) wm_restore(w); else wm_raise(w); return; }
    int sw, sh;
    ui_resolution(&sw, &sh);
    gfx_rect wk = ui_work_rect();
    int ow = CW + 2 * WM_BORDER, oh = CH + WM_TITLE_H + WM_BORDER;
    w = wm_create("AstraSageOS'a hoşgeldiniz.", APP_WELCOME, (sw - ow) / 2, wk.y + (wk.h - oh) / 2, ow, oh);
    if (!w) return;
    w->minw = w->r.w; w->minh = w->r.h;
    if (!setup_done) { page = P_WELCOME; inst_reset(); }
    pt_ok = 0; warn = 0;
    wm_damage(gfx_rect_make(w->r.x - 20, w->r.y - 20, w->r.w + 40, w->r.h + 44));
}

void app_welcome_pump(void)
{
    window *w = me();
    if (!w) return;
    u32 now = ui_ms();
    if (inst == I_RUN && now - inst_t >= 45) { inst_t = now; inst_step(); }
    if (page == P_WELCOME && w->state != WS_MINIMIZED && now - pt_t >= 66) {
        pt_t = now;
        if (w != wm_focused() || wm_busy()) return;                       /* nobody looks at it / a window is being moved: no animation load */
        gfx_rect cr = wm_content_rect(w);
        if (!pt_ok) pt_seed(cr.w, cr.h);
        pt_step(cr.w, cr.h);
        wm_damage(cr);
    }
}

int app_welcome_state(void) { return page; }
int app_welcome_done(void) { return setup_done; }
void app_welcome_test_click(int what) { if (what == 0) go_next(); else go_back(); }
