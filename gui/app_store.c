/*
 * AstraStore (APP_STORE): a shop window for ASPI applications, laid out like a real app store.
 *   left   : the store logo, Discover / Downloaded, the signed-in user
 *   top    : a search box (just type; Esc clears it)
 *   Discover: a featured banner, category chips (All / Developer / Internet) and a grid of app cards
 *   Downloaded: what is installed (Open / Update / Remove)
 *   page   : icon, name, publisher, install / open / remove, facts, description and sample pictures
 * Installing and removing run the real `aspi install` / `aspi remove` commands in the AST window (the store types
 * them for you), so the log, the progress bar and the package logic are exactly the ones the terminal uses.
 */
#include "gui.h"

extern const u8 asset_sf1_start[], asset_sf1_end[], asset_sf2_start[], asset_sf2_end[], asset_sf3_start[], asset_sf3_end[];
extern const u8 asset_sn1_start[], asset_sn1_end[], asset_sn2_start[], asset_sn2_end[], asset_sn3_start[], asset_sn3_end[];

typedef struct {
    const char *name, *publisher, *version;
    const char *en_short, *tr_short;
    const char *en_long, *tr_long;
    int ready;                                   /* 0 = listed, but not installable yet */
    const char *not_ready_en, *not_ready_tr;
    const u8 *shot[3], *shot_end[3];
    int kind;                                    /* icon style */
} entry;

static const entry cat[] = {
    { "FroggyCode", "EnderAstra / AstraSage", "2.0.0",
      "Coding buddy with a pixel frog", "Piksel kurbağalı kodlama arkadaşı",
      "Froggy is a coding assistant for AstraSageOS (a native C program) in the style of an AI chat app: chat, projects and plugins "
      "on the left, a Terminal and Console at the bottom, your history on the right. It lists and reads your files, writes example "
      "programs, runs AST commands, looks things up on Wikipedia when you allow it and remembers what you teach it. It asks first: "
      "\"Her zaman\" or \"Sadece Bu Sefer\".",
      "Froggy, AstraSageOS için yapay zeka sohbet uygulamaları tarzında bir kodlama yardımcısı (yerel C programı): solda sohbet, "
      "projeler ve eklentiler, altta Terminal ve Konsol, sağda geçmişin. Dosyalarını listeler ve okur, örnek program yazar, AST "
      "komutları çalıştırır, izin verirsen Vikipedi'den bakar ve ona öğrettiklerini hatırlar. Önce sorar: "
      "\"Her zaman\" ya da \"Sadece Bu Sefer\".",
      1, 0, 0, { asset_sf1_start, asset_sf2_start, asset_sf3_start }, { asset_sf1_end, asset_sf2_end, asset_sf3_end }, 0 },
    { "NetSurf", "NetSurf Project", "3.11.0",
      "Small, fast web browser", "Küçük ve hızlı web tarayıcısı",
      "NetSurf is a lightweight, free web browser with its own HTML and CSS engine, written in C. This is the AstraSageOS port: it shows web pages over HTTP "
      "and HTTPS through the built-in TLS 1.2 client. Early version: no JavaScript, no downloads, no forms that need POST.",
      "NetSurf, kendi HTML ve CSS motoruna sahip, C ile yazılmış hafif ve özgür bir web tarayıcısıdır. Bu, AstraSageOS portu: web sayfalarını HTTP üzerinden "
      "gösterir ve yerleşik TLS 1.2 istemcisiyle HTTPS'i gösterir. Erken sürüm: JavaScript yok, indirme yok, POST isteyen formlar yok.",
      1, 0, 0,
      { asset_sn1_start, asset_sn2_start, asset_sn3_start }, { asset_sn1_end, asset_sn2_end, asset_sn3_end }, 1 },
};
#define NCAT ((int)(sizeof cat / sizeof cat[0]))

#define SW_ 760
#define SH_ 500
#define SIDE 176

static int tab;                 /* 0 discover, 1 downloaded */
static int page = -1;           /* catalogue index shown on the detail page, -1 = list */
static int hot = -1;            /* hovered button id */
static int catf;                /* category chip: 0 all, 1 developer, 2 internet */
static char query[28];
static int busy_i = -1, busy_was;
static u32 busy_t, busy_ser;
static char msg[100];
static bmp_view shot_v[NCAT][3];
static int shot_ok[NCAT][3];
static u32 logo_px[44 * 44];
static gfx_surf logo_s = { 44, 44, 44, logo_px };

static gfx_rect cont(void) { window *w = wm_find_app(APP_STORE); return w ? wm_content_rect(w) : gfx_rect_make(0, 0, 0, 0); }
static void redraw(void) { gfx_rect c = cont(); if (c.w) wm_damage(c); }
static int installed(int i) { return aspi_installed(cat[i].name); }
static int catid(int i) { return cat[i].kind == 0 ? 1 : 2; }
static const char *catname(int id) { return id == 0 ? config_tr("All", "Hepsi") : id == 1 ? config_tr("Developer", "Geliştirme") : config_tr("Internet", "İnternet"); }
static const char *langname(int i) { (void)i; return config_tr("C (native)", "C (yerel)"); }

/* ---- icons: drawn, never copied ---- */
static void store_icon(int i, int x, int y, int s)
{
    if (s <= 128) {
        const gfx_surf *ic = ui_named_icon(cat[i].kind == 0 ? NI_FROGGY : NI_NETSURF, s);
        if (ic) { gfx_blit(ic, x, y); return; }
    }
    if (cat[i].kind == 0) {                       /* FroggyCode: green tile with an F */
        gfx_round_rect(x, y, s, s, s / 4, 0xFF3F7D0F);
        gfx_round_rect(x + 1, y + 1, s - 2, s - 2, s / 4, 0xFF7CC21A);
        gfx_round_rect(x + 2, y + 2, s - 4, (s - 4) / 2, s / 5, 0x30FFFFFF);
        int sc = s >= 80 ? 4 : s >= 48 ? 3 : 2;
        gfx_text_scaled(x + (s - 8 * sc) / 2, y + (s - 16 * sc) / 2, "F", 0xFF0B2A00, sc);
    } else {                                      /* browser: blue globe */
        gfx_round_rect(x, y, s, s, s / 4, 0xFF1D4E8F);
        gfx_round_rect(x + 1, y + 1, s - 2, s - 2, s / 4, 0xFF2F7DD1);
        int cx = x + s / 2, cy = y + s / 2, r = s * 34 / 100;
        gfx_circle(cx, cy, r + 1, 0xFFE6F2FF);
        gfx_circle(cx, cy, r - 1, 0xFF3C95E8);
        gfx_line(cx - r + 2, cy, cx + r - 2, cy, 0xFFE6F2FF);
        gfx_line(cx, cy - r + 2, cx, cy + r - 2, 0xFFE6F2FF);
        gfx_line(cx - r * 6 / 10, cy - r * 6 / 10, cx + r * 6 / 10, cy - r * 6 / 10, 0xFFE6F2FF);
        gfx_line(cx - r * 6 / 10, cy + r * 6 / 10, cx + r * 6 / 10, cy + r * 6 / 10, 0xFFE6F2FF);
    }
}

/* the AstraStore icon (dock, desktop, window title, sidebar): indigo-to-teal tile, a white shopping bag with a cut-out A, a gold sparkle */
void store_icon_face(u32 *px, int size)
{
    gfx_surf t = { size, size, size, px };
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            u32 d = (u32)((x + y) * 100 / (2 * size));                      /* 0 top-left .. 100 bottom-right */
            u32 r = 0x3A - d * 0x30 / 100, g = 0x3C + d * 0x6C / 100, b = 0xC8 - d * 0x18 / 100;
            px[y * size + x] = 0xFF000000u | r << 16 | g << 8 | b;
        }
    gfx_set_target(&t);
    gfx_set_clip(0);
    int bw = size * 56 / 100, bh = size * 42 / 100, bx = (size - bw) / 2, by = size * 40 / 100;
    int hx = size * 33 / 100, hw = size * 34 / 100, hy = size * 17 / 100, hh = size * 38 / 100;
    gfx_round_rect(bx + size / 40, by + size / 32, bw, bh, size / 8, 0x40000000);          /* soft shadow */
    gfx_round_outline(hx, hy, hw, hh, size * 17 / 100, 0xFFFFFFFFu);                       /* the strap */
    gfx_round_outline(hx + 1, hy + 1, hw - 2, hh - 2, size * 17 / 100, 0xFFFFFFFFu);
    if (size >= 48) gfx_round_outline(hx + 2, hy + 2, hw - 4, hh - 4, size * 17 / 100, 0xFFFFFFFFu);
    gfx_round_rect(bx, by, bw, bh, size / 8, 0xFFFFFFFFu);
    int sc = size >= 96 ? 5 : size >= 64 ? 3 : size >= 36 ? 2 : 1;
    gfx_text_scaled(size / 2 - 4 * sc, by + (bh - 16 * sc) / 2, "A", 0xFF2B46B8u, sc);
    int sx = size * 78 / 100, sy = size * 22 / 100, sr = size >= 48 ? size / 10 : size / 8;   /* four-point sparkle */
    gfx_line(sx - sr, sy, sx + sr, sy, 0xFFFFD84A);
    gfx_line(sx, sy - sr, sx, sy + sr, 0xFFFFD84A);
    gfx_line(sx - sr / 2, sy - sr / 2, sx + sr / 2, sy + sr / 2, 0xFFFFE98A);
    gfx_line(sx - sr / 2, sy + sr / 2, sx + sr / 2, sy - sr / 2, 0xFFFFE98A);
    gfx_circle(sx, sy, size >= 48 ? 2 : 1, 0xFFFFFFFFu);
    gfx_set_target(0);
}

/* ---- text helpers ---- */
static int wrap(int x, int y, int w, const char *s, u32 col, int maxlines)
{
    int per = w / 8, line = 0;
    while (*s && line < maxlines) {
        int n = 0, last = -1;
        while (s[n] && n < per) { if (s[n] == ' ') last = n; n++; }
        if (s[n] && last > 0) n = last;
        while (s[n] && (s[n] & 0xC0) == 0x80) n++;                  /* never cut a UTF-8 letter in half */
        char buf[128];
        int c = n < 127 ? n : 127;
        kmemcpy(buf, s, (size_t)c); buf[c] = 0;
        gfx_text(x, y + line * 20, buf, col);
        s += n; while (*s == ' ') s++;
        line++;
    }
    return line;
}

static void clip_text(int x, int y, int w, const char *s, u32 col)
{
    char b[96];
    int per = w / 8; if (per > 90) per = 90;
    int n = (int)kstrlen(s);
    if (n > per) { kmemcpy(b, s, (size_t)(per > 3 ? per - 3 : 0)); b[per > 3 ? per - 3 : 0] = 0; kstrncpy(b + kstrlen(b), "...", 4); }
    else kstrncpy(b, s, sizeof b - 1), b[sizeof b - 1] = 0;
    gfx_text(x, y, b, col);
}

static char lc(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }
static int contains_i(const char *hay, const char *needle)
{
    if (!needle[0]) return 1;
    for (; *hay; hay++) {
        int k = 0;
        while (needle[k] && hay[k] && lc(hay[k]) == lc(needle[k])) k++;
        if (!needle[k]) return 1;
    }
    return 0;
}
static int matches(int i)
{
    if (catf && catid(i) != catf) return 0;
    return contains_i(cat[i].name, query) || contains_i(cat[i].publisher, query) || contains_i(cat[i].en_short, query) || contains_i(cat[i].tr_short, query);
}

/* ---- layout (shared by drawing and clicking) ---- */
enum { B_TAB0 = 1, B_TAB1, B_BACK, B_MAIN, B_SECOND, B_SEARCH, B_HERO, B_HEROBTN, B_CHIP = 10, B_ROW = 20, B_ROWBTN = 40, B_ROWBTN2 = 60, B_ROWBTN3 = 80 };
typedef struct { int id; gfx_rect r; } hit;
static hit hits[64]; static int nhits;
static void hit_add(int id, gfx_rect r) { if (nhits < 64) { hits[nhits].id = id; hits[nhits].r = r; nhits++; } }
static int hit_at(int x, int y) { for (int i = nhits - 1; i >= 0; i--) if (gfx_rect_has(&hits[i].r, x, y)) return hits[i].id; return -1; }

static void button(gfx_rect r, const char *label, int id, int main, int enabled)
{
    const ui_theme *t = ui_th();
    u32 bg = !enabled ? t->btn2 : main ? (hot == id ? t->accent_hi : t->btn_main) : (hot == id ? t->hov : t->btn2);
    u32 fg = !enabled ? t->txt_off : main ? t->btn_main_txt : t->txt;
    gfx_round_rect(r.x, r.y, r.w, r.h, r.h / 2 > 16 ? 16 : r.h / 2, bg);
    gfx_text(r.x + (r.w - gfx_text_w(label)) / 2, r.y + (r.h - 16) / 2, label, fg);
    if (enabled) hit_add(id, r);
}

/* the label of the main button of an app: Install / Working... / Open / Coming soon */
static const char *main_label(int i)
{
    if (busy_i == i) return config_tr("Working...", "Çalışıyor...");
    if (installed(i)) return config_tr("Open", "Aç");
    return cat[i].ready ? config_tr("Install", "Yükle") : config_tr("Coming soon", "Yakında");
}
static int main_enabled(int i) { return busy_i != i && (installed(i) || cat[i].ready); }

static void nav_glyph(int which, int x, int y, u32 col)
{
    if (which == 0) {                               /* compass */
        gfx_circle(x + 9, y + 9, 9, col);
        gfx_circle(x + 9, y + 9, 7, ui_th()->side);
        gfx_line(x + 12, y + 6, x + 6, y + 12, col); gfx_line(x + 11, y + 6, x + 5, y + 12, col);
        gfx_circle(x + 9, y + 9, 1, col);
    } else {                                        /* arrow into a tray */
        gfx_fill_rect(x + 8, y + 1, 3, 10, col);
        gfx_line(x + 3, y + 8, x + 9, y + 14, col); gfx_line(x + 15, y + 8, x + 9, y + 14, col);
        gfx_line(x + 4, y + 8, x + 9, y + 13, col); gfx_line(x + 14, y + 8, x + 9, y + 13, col);
        gfx_fill_rect(x + 1, y + 16, 17, 2, col);
    }
}

static void draw_sidebar(gfx_rect c)
{
    const ui_theme *t = ui_th();
    gfx_fill_rect(c.x, c.y, SIDE, c.h, t->side);
    gfx_blit(&logo_s, c.x + 14, c.y + 14);
    gfx_text(c.x + 66, c.y + 14, "AstraStore", t->txt);
    gfx_text(c.x + 66, c.y + 34, config_tr("App store", "Mağaza"), t->dim);
    gfx_rect b0 = gfx_rect_make(c.x + 10, c.y + 72, SIDE - 20, 38), b1 = gfx_rect_make(c.x + 10, c.y + 116, SIDE - 20, 38);
    int on0 = tab == 0 && page < 0, on1 = tab == 1 && page < 0;
    gfx_round_rect(b0.x, b0.y, b0.w, b0.h, 10, on0 ? t->sel : hot == B_TAB0 ? t->hov : t->side);
    gfx_round_rect(b1.x, b1.y, b1.w, b1.h, 10, on1 ? t->sel : hot == B_TAB1 ? t->hov : t->side);
    if (on0) gfx_fill_rect(c.x, b0.y + 8, 3, 22, t->accent);
    if (on1) gfx_fill_rect(c.x, b1.y + 8, 3, 22, t->accent);
    nav_glyph(0, b0.x + 12, b0.y + 10, on0 ? t->accent : t->dim);
    nav_glyph(1, b1.x + 12, b1.y + 10, on1 ? t->accent : t->dim);
    gfx_text(b0.x + 40, b0.y + 11, config_tr("Discover", "Keşfet"), t->txt);
    gfx_text(b1.x + 40, b1.y + 11, config_tr("Downloaded", "İndirilenler"), t->txt);
    hit_add(B_TAB0, b0); hit_add(B_TAB1, b1);
    int n = 0;
    for (int i = 0; i < NCAT; i++) if (installed(i)) n++;
    char s[40];
    ksnprintf(s, sizeof s, "%d %s", n, config_tr("installed", "yüklü"));
    gfx_text(c.x + 16, c.y + c.h - 52, s, t->dim);
    const char *u = user_name();
    clip_text(c.x + 16, c.y + c.h - 30, SIDE - 28, u ? u : config_tr("Not signed in", "Giriş yapılmadı"), u ? t->txt : t->danger);
}

static void draw_search(gfx_rect c)
{
    const ui_theme *t = ui_th();
    int x0 = c.x + SIDE + 24, w = c.w - SIDE - 48;
    int sw = w > 380 ? 380 : w;
    gfx_rect r = gfx_rect_make(x0, c.y + 14, sw, 34);
    gfx_round_rect(r.x, r.y, r.w, r.h, 17, t->line);
    gfx_round_rect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 16, t->panel);
    gfx_circle(r.x + 20, r.y + 15, 6, t->dim); gfx_circle(r.x + 20, r.y + 15, 4, t->panel);        /* magnifier */
    gfx_line(r.x + 25, r.y + 20, r.x + 30, r.y + 25, t->dim); gfx_line(r.x + 24, r.y + 20, r.x + 30, r.y + 26, t->dim);
    if (query[0]) {
        gfx_text(r.x + 42, r.y + 9, query, t->txt);
        gfx_text(r.x + 42 + gfx_text_w(query), r.y + 9, "_", t->accent);
        gfx_rect x = gfx_rect_make(r.x + r.w - 30, r.y + 4, 26, 26);
        gfx_text(x.x + 9, x.y + 5, "x", t->dim);
        hit_add(B_SEARCH, x);
    } else gfx_text(r.x + 42, r.y + 9, config_tr("Search apps", "Uygulama ara"), t->dim);
}

static void draw_card(int i, gfx_rect row, int compact)
{
    const ui_theme *t = ui_th();
    gfx_round_rect(row.x, row.y, row.w, row.h, 12, hot == B_ROW + i ? t->hov : t->panel);
    gfx_round_outline(row.x, row.y, row.w, row.h, 12, ui_th()->line);
    hit_add(B_ROW + i, row);
    store_icon(i, row.x + 12, row.y + 12, 56);
    clip_text(row.x + 80, row.y + 12, row.w - 92, cat[i].name, t->txt);
    clip_text(row.x + 80, row.y + 32, row.w - 92, cat[i].publisher, t->dim);
    if (!compact) wrap(row.x + 12, row.y + 72, row.w - 24, config_tr(cat[i].en_short, cat[i].tr_short), t->dim, 1);
    gfx_rect b = gfx_rect_make(row.x + 12, row.y + row.h - 38, 96, 28);
    button(b, main_label(i), B_ROWBTN + i, !installed(i), main_enabled(i));
}

static void draw_hero(gfx_rect r, int i)
{
    gfx_rect clip_save = gfx_rect_make(0, 0, 0, 0); (void)clip_save;
    u32 base = cat[i].kind == 0 ? 0xFF1E5130 : 0xFF1D3F73, mid = cat[i].kind == 0 ? 0xFF2E7A47 : 0xFF2C62A8;
    gfx_round_rect(r.x, r.y, r.w, r.h, 16, base);
    gfx_round_rect(r.x + r.w * 38 / 100, r.y, r.w * 62 / 100, r.h, 16, mid);
    gfx_fill_rect(r.x + r.w * 38 / 100, r.y, 24, r.h, mid);                                      /* square off the seam between the two parts */
    gfx_round_rect(r.x + r.w - 150, r.y + 14, 130, r.h - 28, 18, 0x14FFFFFF);
    gfx_round_rect(r.x + r.w - 110, r.y + 30, 90, r.h - 60, 14, 0x12FFFFFF);
    hit_add(B_HERO, r);
    gfx_round_rect(r.x + 18, r.y + 14, 86, 20, 10, 0x40FFFFFF);
    gfx_text(r.x + 18 + (86 - gfx_text_w(config_tr("FEATURED", "ÖNE ÇIKAN"))) / 2, r.y + 16, config_tr("FEATURED", "ÖNE ÇIKAN"), 0xFFFFFFFF);
    store_icon(i, r.x + r.w - 118, r.y + (r.h - 88) / 2, 88);
    gfx_text_scaled(r.x + 18, r.y + 42, cat[i].name, 0xFFFFFFFF, 2);
    wrap(r.x + 18, r.y + 80, r.w - 160, config_tr(cat[i].en_short, cat[i].tr_short), 0xFFDCEBE0, 1);
    gfx_rect b = gfx_rect_make(r.x + 18, r.y + r.h - 42, 110, 30);
    button(b, main_label(i), B_HEROBTN, !installed(i), main_enabled(i));
}

static int grid(gfx_rect area, int y, int onlyinst)
{
    const ui_theme *t = ui_th();
    int w = area.w;
    int cols = w >= 480 ? 2 : 1;
    int cw = (w - (cols - 1) * 12) / cols, ch = 134, shown = 0;
    for (int i = 0; i < NCAT; i++) {
        if (!matches(i) || (onlyinst && !installed(i))) continue;
        gfx_rect row = gfx_rect_make(area.x + (shown % cols) * (cw + 12), y + (shown / cols) * (ch + 12), cw, ch);
        if (row.y + row.h > area.y + area.h) break;
        draw_card(i, row, 0);
        shown++;
    }
    (void)t;
    return shown;
}

static void draw_discover(gfx_rect c)
{
    const ui_theme *t = ui_th();
    int x0 = c.x + SIDE + 24, w = c.w - SIDE - 48;
    int y = c.y + 64;
    gfx_rect area = gfx_rect_make(x0, y, w, c.y + c.h - y - 30);
    int searching = query[0] != 0;
    if (!searching && c.h >= 430 && matches(0) && catf != 2) {
        draw_hero(gfx_rect_make(x0, y, w, 148), 0);
        y += 164;
    }
    gfx_text(x0, y + 6, searching ? config_tr("Results", "Sonuçlar") : config_tr("All apps", "Tüm uygulamalar"), t->txt);
    int cx = x0 + (searching ? 90 : 150);
    for (int k = 0; k < 3; k++) {                                   /* category chips */
        int cwid = gfx_text_w(catname(k)) + 24;
        if (cx + cwid > x0 + w) break;
        gfx_rect r = gfx_rect_make(cx, y, cwid, 28);
        gfx_round_rect(r.x, r.y, r.w, r.h, 14, catf == k ? t->btn_main : hot == B_CHIP + k ? t->hov : t->panel);
        gfx_text(r.x + 12, r.y + 6, catname(k), catf == k ? t->btn_main_txt : t->txt);
        hit_add(B_CHIP + k, r);
        cx += cwid + 8;
    }
    y += 42;
    area.y = y; area.h = c.y + c.h - y - 30;
    int n = grid(area, y, 0);
    if (!n) gfx_text(x0, y + 10, config_tr("No app matches your search.", "Aramana uyan uygulama yok."), t->dim);
}

static void draw_downloaded(gfx_rect c)
{
    const ui_theme *t = ui_th();
    int x0 = c.x + SIDE + 24, w = c.w - SIDE - 48;
    int y = c.y + 64;
    gfx_text_scaled(x0, y, config_tr("Downloaded", "İndirilenler"), t->txt, 2);
    y += 44;
    int shown = 0;
    for (int i = 0; i < NCAT; i++) {
        if (!installed(i) || !matches(i)) continue;
        gfx_rect row = gfx_rect_make(x0, y, w, 84);
        gfx_round_rect(row.x, row.y, row.w, row.h, 12, hot == B_ROW + i ? t->hov : t->panel);
        hit_add(B_ROW + i, row);
        store_icon(i, row.x + 14, row.y + 14, 56);
        gfx_text(row.x + 84, row.y + 16, cat[i].name, t->txt);
        char sub[80];
        ksnprintf(sub, sizeof sub, "%s  -  %s %s", cat[i].publisher, config_tr("Version", "Sürüm"), cat[i].version);
        clip_text(row.x + 84, row.y + 40, w - 84 - 280, sub, t->dim);
        int bx = row.x + row.w - 100;
        button(gfx_rect_make(bx, row.y + 28, 88, 30), config_tr("Remove", "Sil"), B_ROWBTN2 + i, 0, busy_i != i);
        button(gfx_rect_make(bx - 96, row.y + 28, 88, 30), config_tr("Update", "Güncelle"), B_ROWBTN3 + i, 0, busy_i != i);
        button(gfx_rect_make(bx - 192, row.y + 28, 88, 30), config_tr("Open", "Aç"), B_ROWBTN + i, 1, busy_i != i);
        y += 96; shown++;
    }
    if (!shown) {
        gfx_text(x0, y + 10, config_tr("Nothing installed yet.", "Henüz yüklü uygulama yok."), t->txt);
        gfx_text(x0, y + 34, config_tr("Open Discover to find apps.", "Uygulama bulmak için Keşfet'i aç."), t->dim);
    }
}

static void fact(int x, int y, int w, const char *k, const char *v)
{
    const ui_theme *t = ui_th();
    gfx_text(x, y, k, t->dim);
    clip_text(x, y + 20, w, v, t->txt);
}

static void draw_page(gfx_rect c)
{
    const ui_theme *t = ui_th();
    const entry *e = &cat[page];
    int x0 = c.x + SIDE + 24, w = c.w - SIDE - 48;
    button(gfx_rect_make(x0, c.y + 14, 90, 30), config_tr("< Back", "< Geri"), B_BACK, 0, 1);
    int y = c.y + 58;
    store_icon(page, x0, y, 96);
    gfx_text_scaled(x0 + 116, y + 4, e->name, t->txt, 2);
    gfx_text(x0 + 116, y + 40, e->publisher, t->accent);
    char v[40]; ksnprintf(v, sizeof v, "%s %s", config_tr("Version", "Sürüm"), e->version);
    gfx_text(x0 + 116, y + 62, v, t->dim);
    int inst = installed(page);
    gfx_rect b1 = gfx_rect_make(x0 + w - 150, y + 6, 140, 36);
    gfx_rect b0 = gfx_rect_make(x0 + w - 150, y + 50, 140, 32);
    button(b1, main_label(page), B_MAIN, !inst, main_enabled(page));
    if (inst) button(b0, config_tr("Remove", "Sil"), B_SECOND, 0, busy_i != page);
    y += 112;
    gfx_round_rect(x0, y, w, 56, 12, t->panel);                      /* facts strip */
    int fw = (w - 24) / 4;
    fact(x0 + 12, y + 8, fw - 8, config_tr("Category", "Kategori"), catname(catid(page)));
    fact(x0 + 12 + fw, y + 8, fw - 8, config_tr("Language", "Dil"), langname(page));
    fact(x0 + 12 + 2 * fw, y + 8, fw - 8, config_tr("Price", "Fiyat"), config_tr("Free", "Ücretsiz"));
    fact(x0 + 12 + 3 * fw, y + 8, fw - 8, config_tr("Status", "Durum"), inst ? config_tr("Installed", "Yüklü") : e->ready ? config_tr("Not installed", "Yüklü değil") : config_tr("Coming soon", "Yakında"));
    y += 70;
    if (!e->ready) { clip_text(x0, y, w, config_tr(e->not_ready_en, e->not_ready_tr), t->danger); y += 24; }
    int lines = wrap(x0, y, w, config_tr(e->en_long, e->tr_long), t->txt, 7);
    y += lines * 20 + 14;
    if (y + 130 < c.y + c.h) {
        gfx_text(x0, y, config_tr("Screenshots", "Örnek görseller"), t->dim);
        y += 22;
        int any = 0;
        for (int k = 0; k < 3; k++) {
            if (!shot_ok[page][k]) continue;
            int sx = x0 + k * 176, sw = 168, sh = 112;
            if (sx + sw > x0 + w) break;
            gfx_round_rect(sx - 1, y - 1, sw + 2, sh + 2, 6, t->line);
            gfx_draw_bmp_scaled(&shot_v[page][k], (gfx_surf *)gfx_back(), sx, y, sw, sh, 1);
            any = 1;
        }
        if (!any) gfx_text(x0, y, config_tr("No screenshots yet.", "Henüz görsel yok."), t->dim);
    }
}

void app_store_pump(void) { if (busy_i >= 0 && (app_ast_bg_serial() != busy_ser)) redraw(); }

static void store_draw(window *w, gfx_rect c, gfx_rect clip, int focused)
{
    (void)w; (void)focused;
    const ui_theme *t = ui_th();
    gfx_set_clip(&clip);
    nhits = 0;
    if (busy_i >= 0 && (app_ast_bg_serial() != busy_ser || ui_ms() - busy_t > 900000u)) {
        busy_i = -1;
        const char *o = app_ast_bg_output(), *ln = o, *q;
        for (q = o; *q; q++) if (*q == '\n' && q[1]) ln = q + 1;
        if (kstrstr(o, "FAILED") || kstrstr(o, "rror") || kstrstr(o, "not ")) { kstrncpy(msg, ln, sizeof msg - 1); for (char *e = msg; *e; e++) if (*e == '\n') *e = 0; }
    }
    gfx_fill_rect(c.x, c.y, c.w, c.h, t->bg);
    draw_sidebar(c);
    if (page < 0) draw_search(c);
    if (page >= 0) draw_page(c);
    else if (tab == 0) draw_discover(c);
    else draw_downloaded(c);
    if (msg[0]) gfx_text(c.x + SIDE + 24, c.y + c.h - 26, msg, t->accent);
    gfx_set_clip(0);
}

static void run_cmd(const char *cmd, int i)
{
    if (!user_name()) { kstrncpy(msg, config_tr("Sign in first (open AST to log in).", "Önce giriş yap (AST'yi açıp giriş yap)."), sizeof msg - 1); redraw(); return; }
    if (!app_ast_bg(cmd)) { kstrncpy(msg, config_tr("Another download is still running - try again in a moment.", "Başka bir indirme sürüyor - biraz sonra tekrar dene."), sizeof msg - 1); redraw(); return; }
    { busy_i = i; busy_was = installed(i); busy_t = ui_ms(); busy_ser = app_ast_bg_serial(); msg[0] = 0; }
    redraw();
}

static void do_install(int i) { char c[64]; ksnprintf(c, sizeof c, "aspi install %s", cat[i].name); run_cmd(c, i); }
static void do_remove(int i)  { char c[64]; ksnprintf(c, sizeof c, "aspi remove %s", cat[i].name); run_cmd(c, i); }
static void do_open(int i)
{
    char cmd[40];
    if (aspi_app_command(cat[i].name, cmd, sizeof cmd) > 0 && cmd[0]) { char c[64]; ksnprintf(c, sizeof c, "as %s", cmd); if (user_name()) app_ast_bg(c); else app_ast_run(c); }
}
static void do_main(int i) { if (installed(i)) do_open(i); else do_install(i); }

static void store_mouse(window *w, int x, int y, u8 b, u8 prev)
{
    (void)w;
    int h = x < 0 ? -1 : hit_at(x, y);
    if (h != hot) { hot = h; redraw(); }
    if (x < 0 || !(b & 1) || (prev & 1) || h < 0) return;
    msg[0] = 0;
    if (h == B_TAB0) { tab = 0; page = -1; }
    else if (h == B_TAB1) { tab = 1; page = -1; }
    else if (h == B_BACK) page = -1;
    else if (h == B_MAIN) do_main(page);
    else if (h == B_SECOND) do_remove(page);
    else if (h == B_SEARCH) query[0] = 0;
    else if (h == B_HERO) page = 0;
    else if (h == B_HEROBTN) do_main(0);
    else if (h >= B_CHIP && h < B_CHIP + 3) catf = h - B_CHIP;
    else if (h >= B_ROWBTN3 && h < B_ROWBTN3 + NCAT) do_install(h - B_ROWBTN3);          /* Update = install again over the old one */
    else if (h >= B_ROWBTN2 && h < B_ROWBTN2 + NCAT) do_remove(h - B_ROWBTN2);
    else if (h >= B_ROWBTN && h < B_ROWBTN + NCAT) do_main(h - B_ROWBTN);
    else if (h >= B_ROW && h < B_ROW + NCAT) page = h - B_ROW;
    redraw();
}

static void store_key(window *w, int k)
{
    (void)w;
    int n = (int)kstrlen(query);
    if (k == 27) {
        if (page >= 0) page = -1;
        else if (query[0]) query[0] = 0;
    } else if (k == '\t') { tab = !tab; page = -1; }
    else if ((k == 8 || k == 127) && n) { query[n - 1] = 0; }
    else if (k >= 32 && k < 127 && n < (int)sizeof query - 1) { query[n] = (char)k; query[n + 1] = 0; page = -1; tab = 0; }
    else return;
    redraw();
}

static void store_closed(window *w) { (void)w; page = -1; hot = -1; query[0] = 0; catf = 0; }

const wm_app app_store_ops = { store_draw, 0, store_closed, 0, store_mouse, store_key, 0 };

void app_store_init(void)
{
    for (int i = 0; i < NCAT; i++)
        for (int k = 0; k < 3; k++)
            shot_ok[i][k] = cat[i].shot[k] && bmp_open(cat[i].shot[k], (u32)(cat[i].shot_end[k] - cat[i].shot[k]), &shot_v[i][k]) == BMP_OK;
    store_icon_face(logo_px, 44);
}

void app_store_launch(void)
{
    window *w = wm_find_app(APP_STORE);
    if (w) { if (w->state == WS_MINIMIZED) wm_restore(w); else wm_raise(w); return; }
    int sw, sh;
    ui_resolution(&sw, &sh);
    gfx_rect wk = ui_work_rect();
    int ow = SW_ + 2 * WM_BORDER, oh = SH_ + WM_TITLE_H + WM_BORDER;
    if (ow > sw - 8) ow = sw - 8;
    if (oh > wk.y + wk.h - 8) oh = wk.y + wk.h - 8;
    w = wm_create("AstraStore", APP_STORE, (sw - ow) / 2, wk.y + (wk.h - oh) / 2, ow, oh);
    if (!w) return;
    w->minw = 600; w->minh = 420;
    tab = 0; page = -1; msg[0] = 0; busy_i = -1; query[0] = 0; catf = 0;
    wm_damage(gfx_rect_make(w->r.x - 20, w->r.y - 20, w->r.w + 40, w->r.h + 44));
}
