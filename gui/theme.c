/*
 * AstraSageOS themes. One palette is active at a time; the window manager (frames, title bars), the dock,
 * the File Explorer, the other apps and the terminal colours all read it. The choice is made in the
 * welcome app and is saved in /config/settings.
 *   white (default) - light grey/white windows, dark text
 *   black           - the look of the older versions
 *   green           - dark green
 */
#include "gui.h"

static int cur_theme = THEME_WHITE;
static u32 serial;

static ui_theme themes[THEME_COUNT] = {
    [THEME_WHITE] = {
        .name = "white", .light = 1,
        .border_f = 0xFFA9ADB4, .border_u = 0xFFCDD0D5, .title_f = 0xFFE8EAED, .title_u = 0xFFF1F2F4,
        .txt_f = 0xFF1E2024, .txt_u = 0xFF8A8E95, .btn_off = 0xFFC3C6CC, .content = 0xFFFAFAFB,
        .dock_bg = ARGB(214, 247, 248, 250), .dock_line = ARGB(70, 0, 0, 0), .label = 0xFFF4F4F4,
        .bg = 0xFFFFFFFF, .side = 0xFFF1F2F4, .bar = 0xFFE6E8EB, .line = 0xFFCFD2D8,
        .txt = 0xFF1D1F23, .dim = 0xFF6B6F76, .accent = 0xFF4E9A06, .accent_hi = 0xFF8AD01A,
        .sel = ARGB(90, 106, 196, 30), .hov = ARGB(40, 0, 0, 0),
        .field = 0xFFFFFFFF, .btn2 = 0xFFD9DCE1, .btn_main = 0xFF6DAE14, .btn_main_txt = 0xFF0D1A00,
        .panel = 0xFFFFFFFF, .menu = 0xFFF6F7F8, .menu_hot = 0xFFCFEBA8, .danger = 0xFFC62828,
        .txt_off = 0xFFA5A9B0, .cell_sel = 0xFFE3F2CC, .cell_hov = 0xFFEDEFF2,
        .scr_track = 0xFFE6E8EB, .scr_thumb = 0xFFB4B8BF, .dim_modal = ARGB(80, 0, 0, 0),
        .term_bg = 0xFFFAFAFB, .term_cursor = 0xFF22262B, .term_cursor_txt = 0xFFF5F5F5,
    },
    [THEME_BLACK] = {
        .name = "black", .light = 0,
        .border_f = 0xFF4A4A4F, .border_u = 0xFF303033, .title_f = 0xFF2D2D30, .title_u = 0xFF252527,
        .txt_f = 0xFFE8E8E8, .txt_u = 0xFF8D8D92, .btn_off = 0xFF55555A, .content = 0xFF000000,
        .dock_bg = ARGB(200, 30, 30, 34), .dock_line = ARGB(46, 255, 255, 255), .label = 0xFFF2F2F2,
        .bg = 0xFF1C1C1F, .side = 0xFF242428, .bar = 0xFF2B2B30, .line = 0xFF3A3A40,
        .txt = 0xFFE6E6E6, .dim = 0xFF8D8D94, .accent = 0xFF9BE01C, .accent_hi = 0xFF9BE01C,
        .sel = ARGB(86, 155, 224, 28), .hov = ARGB(40, 255, 255, 255),
        .field = 0xFF17171A, .btn2 = 0xFF3A3A40, .btn_main = 0xFF6DAE14, .btn_main_txt = 0xFF0D1A00,
        .panel = 0xFF26262B, .menu = 0xFF2A2A30, .menu_hot = 0xFF45672A, .danger = 0xFFFF6B5E,
        .txt_off = 0xFF5A5A60, .cell_sel = 0xFF2B3A17, .cell_hov = 0xFF28282B,
        .scr_track = 0xFF2A2A2E, .scr_thumb = 0xFF6B6B72, .dim_modal = ARGB(120, 0, 0, 0),
        .term_bg = 0xFF000000, .term_cursor = 0xFFD8D8D8, .term_cursor_txt = 0xFF000000,
    },
    [THEME_GREEN] = {
        .name = "green", .light = 0,
        .border_f = 0xFF2F6B3A, .border_u = 0xFF1F4A28, .title_f = 0xFF16391E, .title_u = 0xFF11291A,
        .txt_f = 0xFFDFF5E0, .txt_u = 0xFF7FA787, .btn_off = 0xFF2F5A38, .content = 0xFF0A180D,
        .dock_bg = ARGB(205, 14, 38, 22), .dock_line = ARGB(70, 155, 224, 28), .label = 0xFFEAF8EB,
        .bg = 0xFF0D1F12, .side = 0xFF11291A, .bar = 0xFF173523, .line = 0xFF2A5A37,
        .txt = 0xFFE3F6E5, .dim = 0xFF86AC8D, .accent = 0xFF9BE01C, .accent_hi = 0xFF9BE01C,
        .sel = ARGB(96, 155, 224, 28), .hov = ARGB(36, 200, 255, 200),
        .field = 0xFF0A180D, .btn2 = 0xFF22472D, .btn_main = 0xFF6DAE14, .btn_main_txt = 0xFF0D1A00,
        .panel = 0xFF12301C, .menu = 0xFF14331F, .menu_hot = 0xFF2F6B2A, .danger = 0xFFFF8A7E,
        .txt_off = 0xFF4B7355, .cell_sel = 0xFF1F4A22, .cell_hov = 0xFF153221,
        .scr_track = 0xFF173523, .scr_thumb = 0xFF3F7A4B, .dim_modal = ARGB(130, 0, 10, 0),
        .term_bg = 0xFF0A180D, .term_cursor = 0xFFC8F0C8, .term_cursor_txt = 0xFF0A180D,
    },
};

/* The six colour themes are made from a base colour and two accents (see derive()). */
static int derived;
static u32 mixc(u32 a, u32 b, int t)               /* a -> b, t in 0..256 */
{
    u32 r = 0;
    for (int sh = 0; sh <= 16; sh += 8) {
        int x = (int)(a >> sh & 255), y = (int)(b >> sh & 255);
        r |= (u32)(x + (y - x) * t / 256) << sh;
    }
    return 0xFF000000u | r;
}
static u32 with_alpha(u32 c, u32 a) { return a << 24 | (c & 0xFFFFFF); }
static int lum(u32 c) { return (int)(((c >> 16 & 255) * 30 + (c >> 8 & 255) * 59 + (c & 255) * 11) / 100); }

static void derive(ui_theme *t, const char *name, int light, u32 base, u32 acc, u32 acc2)
{
    const u32 W = 0xFFFFFFFF, K = 0xFF000000;
    kmemset(t, 0, sizeof *t);
    t->name = name; t->light = light;
    t->accent = acc; t->accent_hi = acc2; t->btn_main = acc;
    t->btn_main_txt = lum(acc) > 140 ? 0xFF0D1220 : 0xFFFFFFFF;
    t->label = 0xFFF4F4F4;
    if (light) {
        t->border_f = mixc(base, K, 90); t->border_u = mixc(base, K, 46); t->title_f = mixc(base, K, 20); t->title_u = mixc(base, W, 100);
        t->txt_f = 0xFF22201C; t->txt_u = mixc(K, base, 140); t->btn_off = mixc(base, K, 60); t->content = mixc(base, W, 150);
        t->dock_bg = with_alpha(mixc(base, W, 140), 214); t->dock_line = ARGB(70, 0, 0, 0);
        t->bg = mixc(base, W, 175); t->side = mixc(base, K, 10); t->bar = mixc(base, K, 20); t->line = mixc(base, K, 42);
        t->txt = 0xFF1D1F23; t->dim = 0xFF666A72; t->sel = with_alpha(acc, 90); t->hov = ARGB(40, 0, 0, 0);
        t->field = W; t->btn2 = mixc(base, K, 30); t->panel = mixc(base, W, 175); t->menu = mixc(base, W, 130);
        t->menu_hot = mixc(base, acc, 90); t->danger = 0xFFC62828; t->txt_off = mixc(base, K, 90);
        t->cell_sel = mixc(base, acc, 70); t->cell_hov = mixc(base, K, 16); t->scr_track = t->bar; t->scr_thumb = mixc(base, K, 72);
        t->dim_modal = ARGB(80, 0, 0, 0);
        t->term_bg = t->content; t->term_cursor = 0xFF22262B; t->term_cursor_txt = 0xFFF5F5F5;
    } else {
        t->border_f = mixc(acc, base, 150); t->border_u = mixc(base, W, 36); t->title_f = mixc(base, W, 22); t->title_u = mixc(base, K, 26);
        t->txt_f = 0xFFE9EEF8; t->txt_u = mixc(0xFFE9EEF8, base, 130); t->btn_off = mixc(base, W, 78); t->content = mixc(base, K, 90);
        t->dock_bg = with_alpha(mixc(base, K, 70), 205); t->dock_line = with_alpha(acc, 70);
        t->bg = base; t->side = mixc(base, W, 16); t->bar = mixc(base, W, 26); t->line = mixc(base, W, 52);
        t->txt = 0xFFE9EEF8; t->dim = mixc(0xFFE9EEF8, base, 120); t->sel = with_alpha(acc, 96); t->hov = ARGB(36, 255, 255, 255);
        t->field = mixc(base, K, 90); t->btn2 = mixc(base, W, 46); t->panel = mixc(base, W, 18); t->menu = mixc(base, W, 24);
        t->menu_hot = mixc(base, acc, 90); t->danger = 0xFFFF7B6E; t->txt_off = mixc(base, W, 90);
        t->cell_sel = mixc(base, acc, 64); t->cell_hov = mixc(base, W, 20); t->scr_track = t->bar; t->scr_thumb = mixc(base, W, 90);
        t->dim_modal = with_alpha(mixc(base, K, 200), 130);
        t->term_bg = t->content; t->term_cursor = mixc(W, acc, 50); t->term_cursor_txt = t->content;
    }
}

static void ensure(void)
{
    if (derived) return;
    derived = 1;
    derive(&themes[THEME_NAVY],   "navy",   0, 0xFF0F1B3D, 0xFF4F8DFF, 0xFF8FB8FF);
    derive(&themes[THEME_YELLOW], "yellow", 1, 0xFFF6E7A8, 0xFFE0A100, 0xFFFFD43B);
    derive(&themes[THEME_RED],    "red",    0, 0xFF2D1013, 0xFFFF4D4D, 0xFFFF9A8A);
    derive(&themes[THEME_BLUE],   "blue",   1, 0xFFC9E1FA, 0xFF1E7FE0, 0xFF5FB0FF);
    derive(&themes[THEME_PURPLE], "purple", 0, 0xFF22123B, 0xFFB06BFF, 0xFFD9B0FF);
    derive(&themes[THEME_AURO],   "auro",   0, 0xFF0B1B24, 0xFF35E0B0, 0xFF9B7BFF);
}

const ui_theme *ui_th(void) { ensure(); return &themes[cur_theme]; }
const ui_theme *ui_theme_by_id(int id) { ensure(); return &themes[id < 0 || id >= THEME_COUNT ? 0 : id]; }

const char *ui_theme_label(int id)
{
    switch (id) {
    case THEME_WHITE:  return config_tr("White (default)", "Beyaz (varsayılan)");
    case THEME_BLACK:  return config_tr("Black", "Siyah");
    case THEME_GREEN:  return config_tr("Green", "Yeşil");
    case THEME_NAVY:   return config_tr("Dark blue", "Koyu mavi");
    case THEME_YELLOW: return config_tr("Yellow", "Sarı");
    case THEME_RED:    return config_tr("Red", "Kırmızı");
    case THEME_BLUE:   return config_tr("Blue", "Mavi");
    case THEME_PURPLE: return config_tr("Purple", "Mor");
    default:           return "Auro";
    }
}
const char *ui_theme_kind(int id) { return ui_theme_by_id(id)->light ? config_tr("Light", "Açık") : config_tr("Dark", "Koyu"); }
int ui_theme_id(void) { return cur_theme; }
u32 ui_theme_serial(void) { return serial; }

void ui_theme_set(int id)
{
    if (id < 0 || id >= THEME_COUNT || id == cur_theme) return;
    cur_theme = id;
    serial++;
}

/* Terminal cell colours (RGB, no alpha) -> what is drawn in the active theme. On the light theme text on the
 * default black background becomes dark text on the light page; cells with their own background (editor bars,
 * error lines, the cursor) keep their colours. */
u32 ui_term_bg(u32 bg)
{
    ensure();
    if ((bg & 0xFFFFFF) == 0) return themes[cur_theme].term_bg;
    return 0xFF000000u | bg;
}

u32 ui_term_fg(u32 fg, u32 bg)
{
    ensure();
    if (!themes[cur_theme].light || (bg & 0xFFFFFF) != 0) return 0xFF000000u | fg;
    u32 r = fg >> 16 & 255, g = fg >> 8 & 255, b = fg & 255;
    u32 l = (r * 30 + g * 59 + b * 11) / 100;                 /* 0..255 */
    u32 f = 77 + (255 - l) * 115 / 255;                       /* 0.30 .. 0.75 of the colour, 0..255 scale */
    return 0xFF000000u | (r * f / 255) << 16 | (g * f / 255) << 8 | (b * f / 255);
}

/* A small picture of a theme (wallpaper, one window, the dock) for the welcome app and the Settings app. */
void ui_theme_preview(int x, int y, int w, int h, int id)
{
    const ui_theme *t = ui_theme_by_id(id);
    ensure();
    u32 c0 = mixc(t->bg, 0xFF000000, 110), c1 = mixc(t->accent, t->bg, 150);
    for (int i = 0; i < h; i++) gfx_fill_rect(x, y + i, w, 1, mixc(c0, c1, i * 256 / h));
    if (id == THEME_AURO) {                                            /* aurora: slanted green and violet bands */
        for (int i = 0; i < h; i++) {
            int o = (i * 3) % 29 - 14;
            gfx_fill_rect(x + w / 6 + o, y + i, w / 5, 1, mixc(c0, t->accent, 120 - i * 80 / h));
            gfx_fill_rect(x + w / 2 + o + 8, y + i, w / 4, 1, mixc(c0, t->accent_hi, 110 - i * 70 / h));
        }
    }
    gfx_round_outline(x, y, w, h, 6, t->line);
    int ww = w * 70 / 100, wx = x + (w - ww) / 2, wy = y + h / 9, wh = h * 52 / 100;
    gfx_round_rect_ex(wx, wy, ww, 10, 4, 4, 0, 0, t->title_f);
    gfx_fill_rect(wx, wy + 10, ww, wh - 10, t->content);
    gfx_rect_outline(wx, wy, ww, wh, t->border_f);
    gfx_fill_rect(wx + ww - 9, wy + 3, 5, 5, t->btn_off);
    gfx_fill_rect(wx + 6, wy + 17, ww * 55 / 100, 3, t->accent);
    gfx_fill_rect(wx + 6, wy + 24, ww * 35 / 100, 3, t->dim);
    int dw = w * 46 / 100, dh = 18;
    gfx_round_rect(x + (w - dw) / 2, y + h - dh - 4, dw, dh, 7, t->dock_bg);
    for (int i = 0; i < 3; i++) draw_app_icon(i == 0 ? APP_AST : i == 1 ? APP_FILES : APP_STORE, x + (w - dw) / 2 + 6 + i * ((dw - 12) / 3), y + h - dh - 2, 14);
}
