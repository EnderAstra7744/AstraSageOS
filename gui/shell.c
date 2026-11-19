/*
 * The desktop shell: the notch at the top centre (clock; click it for calendar + big clock + weather), the three tray icons
 * at the top right (power, network, volume) with their animated menus, and the app grid (the 9-dot icon in the dock).
 * Everything opens and closes with an animation (ease on a 0..256 value per panel). Nothing here is made up: the time is
 * the RTC, the weather comes from the network (`as weather`) or says why it could not, the network panel shows the real card
 * and says plainly that there is no Wi-Fi/Bluetooth driver.
 */
#include "gui.h"

#define TOP SHELL_TOP
enum { P_NONE = 0, P_CLOCK, P_POWER, P_NET, P_VOL, P_GRID, P_NOTIF, P_COUNT };

/* notification centre: the last notices (newest first), a red dot on the bell until the panel is opened */
#define NOTIF_MAX 8
static struct { char msg[100]; u8 h, m; } notif[NOTIF_MAX];
static int notif_n, notif_unread;
#define ANIM_MS 220

static int sw, sh;
static int target;                         /* the panel that is open (or opening) */
static int anim[P_COUNT];                  /* 0..256 */
static u32 last_tick;
static int sleeping;

/* clock */
static hal_time now_t;
static u32 now_read;
static int view_y, view_m;                 /* month shown in the calendar */
static int minute_seen = -1;

/* weather */
static char wx_temp[24], wx_cond[40], wx_place[40], wx_err[72];
static int wx_state;                       /* 0 none yet, 1 loading, 2 ok, 3 failed */
static u32 wx_ser, wx_when;
static int wx_waiting;

/* network panel */
static net_info ni;
static int wifi_n, bt_n;
static char wifi_who[48], net_msg[100];
static int net_busy;
static u32 net_ser;

/* grid */
typedef struct { int app; char pkg[32]; } gapp;
static gapp gapps[40];
static int ngapps, ghot = -1, gmenu = -1, gmx, gmy;

static int ease(int t) { if (t <= 0) return 0; if (t >= 256) return 256; return t * t * (3 * 256 - 2 * t) / (256 * 256); }
static int lerp(int a, int b, int e) { return a + (b - a) * e / 256; }
static int imin(int a, int b) { return a < b ? a : b; }

/* ---------------------------------------------------------------- time */

static void read_time(void)
{
    u32 t = ui_ms();
    if (now_read && t - now_read < 400) return;
    now_read = t ? t : 1;
    hal_datetime(&now_t);
}

static int days_in(int y, int m)
{
    static const int d[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return d[(m - 1) % 12];
}

static int weekday(int y, int m, int d)                  /* 0 = Monday */
{
    static const int t[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3) y--;
    int w = (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;     /* 0 = Sunday */
    return (w + 6) % 7;
}

static const char *month_name(int m)
{
    static const char *const en[12] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
    static const char *const tr[12] = { "Ocak", "Şubat", "Mart", "Nisan", "Mayıs", "Haziran", "Temmuz", "Ağustos", "Eylül", "Ekim", "Kasım", "Aralık" };
    m = ((m - 1) % 12 + 12) % 12;
    return config_tr(en[m], tr[m]);
}

static const char *day_name(int wd)
{
    static const char *const en[7] = { "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday" };
    static const char *const tr[7] = { "Pazartesi", "Salı", "Çarşamba", "Perşembe", "Cuma", "Cumartesi", "Pazar" };
    return config_tr(en[wd % 7], tr[wd % 7]);
}

void shell_clock_strings(char *tm, size_t tc, char *dt, size_t dc)
{
    read_time();
    ksnprintf(tm, tc, "%02u:%02u", now_t.hour, now_t.min);
    ksnprintf(dt, dc, "%s %u %s", day_name(weekday((int)now_t.year, (int)now_t.month, (int)now_t.day)), now_t.day, month_name((int)now_t.month));
}


/* ---------------------------------------------------------------- weather (as weather, in the background) */

static void wx_parse(const char *o)
{
    const char *ln = 0;
    for (const char *p = o; *p; p++) if ((p == o || p[-1] == '\n') && p[0] == 'W' && p[1] == 'X' && p[2] == '|') ln = p;
    wx_temp[0] = wx_cond[0] = wx_place[0] = wx_err[0] = 0;
    if (!ln) { wx_state = 3; kstrncpy(wx_err, config_tr("No answer", "Cevap yok"), sizeof wx_err - 1); return; }
    ln += 3;
    if (!kstrncmp(ln, "ERR|", 4)) {
        wx_state = 3;
        int n = 0; for (const char *p = ln + 4; *p && *p != '\n' && *p != '\r' && n < (int)sizeof wx_err - 1; p++) wx_err[n++] = *p;
        wx_err[n] = 0;
        return;
    }
    char *dst[3] = { wx_temp, wx_cond, wx_place };
    int cap[3] = { 23, 39, 39 }, i = 0, n = 0;
    for (const char *p = ln; *p && *p != '\n' && *p != '\r' && i < 3; p++) {
        if (*p == '|') { dst[i][n] = 0; i++; n = 0; continue; }
        if (n < cap[i]) dst[i][n++] = *p;
    }
    if (i < 3) dst[i][n] = 0;
    wx_state = wx_temp[0] ? 2 : 3;
    if (wx_state == 3) kstrncpy(wx_err, config_tr("Unreadable answer", "Anlaşılmayan cevap"), sizeof wx_err - 1);
}

static void wx_poll(int want)
{
    if (wx_waiting && app_ast_bg_serial() != wx_ser) {
        wx_waiting = 0;
        wx_parse(app_ast_bg_output());
        wx_when = ui_ms();
    }
    if (!want || wx_waiting) return;
    if (!user_name()) { wx_state = 3; kstrncpy(wx_err, config_tr("Sign in to load the weather", "Hava durumu için giriş yap"), sizeof wx_err - 1); return; }
    if (wx_state == 2 && ui_ms() - wx_when < 600000u) return;               /* fresh for ten minutes */
    if (wx_state == 3 && wx_when && ui_ms() - wx_when < 30000u) return;     /* do not hammer a failing network */
    if (app_ast_bg_busy()) return;
    if (app_ast_bg("weather")) { wx_waiting = 1; wx_ser = app_ast_bg_serial(); wx_state = 1; if (!wx_when) wx_when = ui_ms(); }
}

/* ---------------------------------------------------------------- geometry */

/* The notch and the tray icons step up out of the screen while a window touches them, and come back when it leaves (0..256) */
static int dodge_notch, dodge_tray;
#define DODGE_MARGIN 6
static gfx_rect notch_home(void) { return gfx_rect_make((sw - 124) / 2 - DODGE_MARGIN, 0, 124 + 2 * DODGE_MARGIN, 26); }
static gfx_rect tray_home(void) { return gfx_rect_make(sw - 12 - 4 * 34 - 8 - DODGE_MARGIN, 0, 4 * 34 + 8 + 2 * DODGE_MARGIN + 12, 26); }

static gfx_rect notch_rect(void)
{
    int e = ease(anim[P_CLOCK]);
    int ow = imin(580, sw - 24), oh = 250;
    int w = lerp(124, ow, e), h = lerp(26, oh, e);
    return gfx_rect_make((sw - w) / 2, -(h + 4) * ease(dodge_notch) / 256, w, h);
}

#define TRAY_N 4
static const int tray_panel[TRAY_N] = { P_NOTIF, P_POWER, P_NET, P_VOL };
static int tray_off(void) { return -32 * ease(dodge_tray) / 256; }
static gfx_rect tray_rect(void) { return gfx_rect_make(sw - 12 - TRAY_N * 34 - 8, 2 + tray_off(), TRAY_N * 34 + 8, 24); }
static gfx_rect tray_icon(int i) { return gfx_rect_make(sw - 12 - (TRAY_N - i) * 34 - 2, 3 + tray_off(), 30, 22); }   /* i: 0 bell, 1 power, 2 net, 3 volume */

void shell_notify(const char *msg)
{
    hal_time t; hal_datetime(&t);
    for (int i = NOTIF_MAX - 1; i > 0; i--) notif[i] = notif[i - 1];
    kstrncpy(notif[0].msg, msg, sizeof notif[0].msg - 1); notif[0].msg[sizeof notif[0].msg - 1] = 0;
    notif[0].h = (u8)t.hour; notif[0].m = (u8)t.min;
    if (notif_n < NOTIF_MAX) notif_n++;
    notif_unread++;
    wm_damage(gfx_rect_make(sw - 12 - TRAY_N * 34 - 8, 0, TRAY_N * 34 + 20, 340));
}
int shell_notif_count(void) { return notif_n; }
int shell_notif_unread(void) { return notif_unread; }
void shell_notif_clear(void) { notif_n = 0; notif_unread = 0; wm_damage(gfx_rect_make(0, 0, sw, 340)); }

static int panel_w(int p) { return p == P_POWER ? 270 : p == P_NET ? 330 : p == P_NOTIF ? 350 : 290; }
static int panel_h(int p) { return p == P_POWER ? 118 : p == P_NET ? 278 : p == P_NOTIF ? 56 + NOTIF_MAX * 40 : 76; }
static gfx_rect panel_rect(int p, int full)
{
    int e = full ? 256 : ease(anim[p]);
    int w = panel_w(p), h = panel_h(p);
    return gfx_rect_make(sw - 10 - w, TOP + 2, w, lerp(8, h, e));
}

/* ---------------------------------------------------------------- icons */

static void glyph_power(int cx, int cy, u32 col)
{
    for (int y = -9; y <= 9; y++)
        for (int x = -9; x <= 9; x++) {
            int d2 = x * x + y * y;
            if (d2 >= 36 && d2 <= 56 && !(y < -1 && x > -4 && x < 4)) gfx_pixel(cx + x, cy + y + 1, col);
        }
    gfx_fill_rect(cx - 1, cy - 8, 2, 8, col);
}

static void glyph_wifi(int cx, int cy, u32 col)
{
    for (int y = -12; y <= 0; y++)
        for (int x = -12; x <= 12; x++) {
            int d2 = x * x + y * y, ax = x < 0 ? -x : x;
            if (-y < ax) continue;
            if ((d2 >= 25 && d2 <= 40) || (d2 >= 64 && d2 <= 84) || (d2 >= 121 && d2 <= 150)) gfx_pixel(cx + x, cy + y + 5, col);
        }
    gfx_circle(cx, cy + 5, 2, col);
}

static void glyph_speaker(int cx, int cy, int level, u32 col)
{
    gfx_fill_rect(cx - 8, cy - 3, 5, 6, col);
    for (int i = 0; i < 7; i++) gfx_fill_rect(cx - 3 + i, cy - 3 - i, 1, 6 + 2 * i, col);
    if (level <= 0) { gfx_line(cx + 6, cy - 4, cx + 12, cy + 4, col); gfx_line(cx + 12, cy - 4, cx + 6, cy + 4, col); return; }
    int waves = level > 66 ? 2 : 1;
    for (int k = 1; k <= waves; k++)
        for (int y = -9; y <= 9; y++)
            for (int x = 1; x <= 12; x++) {
                int d2 = x * x + y * y, r = 3 + k * 3;
                if (x >= (y < 0 ? -y : y) && d2 >= r * r - r && d2 <= r * r + r) gfx_pixel(cx + 3 + x, cy + y, col);
            }
}

static void glyph_moon(int cx, int cy, u32 col, u32 bg)
{
    gfx_circle(cx, cy, 11, col);
    gfx_circle(cx + 6, cy - 4, 9, bg);
}

static void glyph_restart(int cx, int cy, u32 col)
{
    for (int y = -10; y <= 10; y++)
        for (int x = -10; x <= 10; x++) {
            int d2 = x * x + y * y;
            if (d2 >= 56 && d2 <= 90 && !(x > 0 && y < 0 && y > -5)) gfx_pixel(cx + x, cy + y, col);
        }
    gfx_fill_rect(cx + 2, cy - 12, 8, 2, col);                           /* the arrow head */
    gfx_fill_rect(cx + 8, cy - 12, 2, 8, col);
}

void grid_icon_face(u32 *px, int size)                                   /* the 9-dot dock icon */
{
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            u32 d = (u32)((x + y) * 100 / (2 * size));
            u32 v = 0x3C - d * 0x1C / 100;
            px[y * size + x] = 0xFF000000u | v << 16 | (v + 4) << 8 | (v + 12);
        }
    gfx_surf t = { size, size, size, px };
    gfx_set_target(&t);
    gfx_set_clip(0);
    int step = size * 24 / 100, r = size >= 48 ? size / 14 : size / 10;
    if (r < 1) r = 1;
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 3; i++) gfx_circle(size / 2 + (i - 1) * step, size / 2 + (j - 1) * step, r + 1, 0xFFFFFFFFu);
    gfx_set_target(0);
}

/* ---------------------------------------------------------------- grid data */

static void grid_collect(void)
{
    ngapps = 0;
    static const int builtin[] = { APP_AST, APP_FILES, APP_STORE, APP_SETTINGS, APP_TASKS, APP_WELCOME };
    for (int i = 0; i < (int)(sizeof builtin / sizeof builtin[0]); i++) {
        if (builtin[i] == APP_WELCOME && config_get()->setup_done) continue;          /* the welcome app is gone after the setup */
        gapps[ngapps].app = builtin[i]; gapps[ngapps].pkg[0] = 0; ngapps++;
    }
    static char names[32][32];
    int n = aspi_app_packages(names, 32);
    for (int i = 0; i < n && ngapps < 40; i++) { gapps[ngapps].app = -1; kstrncpy(gapps[ngapps].pkg, names[i], 31); gapps[ngapps].pkg[31] = 0; ngapps++; }
}

static int grid_cols_n(void) { int c = (sw - 100) / 140; return c < 2 ? 2 : c > 7 ? 7 : c; }
static gfx_rect grid_cell(int i)
{
    int C = grid_cols_n(), rows = (ngapps + C - 1) / C;
    int cw = 140, ch = 128;
    int x0 = (sw - C * cw) / 2, y0 = TOP + (sh - TOP - 120 - rows * ch) / 2;
    if (y0 < TOP + 24) y0 = TOP + 24;
    return gfx_rect_make(x0 + (i % C) * cw, y0 + (i / C) * ch, cw, ch);
}
static int grid_at(int x, int y) { for (int i = 0; i < ngapps; i++) { gfx_rect r = grid_cell(i); if (gfx_rect_has(&r, x, y)) return i; } return -1; }

/* ---------------------------------------------------------------- control */

static void damage_top(void) { wm_damage(gfx_rect_make(0, 0, sw, TOP + 300)); }

static void rescan_net(void)
{
    net_status(&ni);
    wifi_n = pci_wireless_scan(0, wifi_who, sizeof wifi_who);
    bt_n = pci_wireless_scan(1, (char[48]){ 0 }, 48);
    net_msg[0] = 0;
}

void shell_open(int p)
{
    if (p == target) return;
    if (target == P_GRID || p == P_GRID) wm_damage(gfx_rect_make(0, 0, sw, sh));
    target = p;
    gmenu = -1;
    if (p == P_NET) rescan_net();
    if (p == P_GRID) { grid_collect(); ghot = -1; }
    if (p == P_NOTIF) notif_unread = 0;
    if (p == P_CLOCK) { read_time(); view_y = (int)now_t.year; view_m = (int)now_t.month; }
    damage_top();
}
void shell_toggle_grid(void) { shell_open(target == P_GRID ? P_NONE : P_GRID); }
int  shell_panel(void) { return target; }
int  shell_sleeping(void) { return sleeping; }
int  shell_anim(int p) { return p > 0 && p < P_COUNT ? anim[p] : 0; }

void shell_init(int w, int h)
{
    sw = w; sh = h; target = P_NONE;
    for (int i = 0; i < P_COUNT; i++) anim[i] = 0;
    sleeping = 0; last_tick = ui_ms(); now_read = 0; minute_seen = -1;
    wx_state = 0; wx_waiting = 0; wx_when = 0; net_busy = 0;
}

/* advances the animations; returns 1 while something moves */
int shell_tick(void)
{
    u32 t = ui_ms();
    int dt = (int)(t - last_tick);
    last_tick = t;
    if (dt < 0 || dt > 200) dt = 200;
    int step = dt * 256 / ANIM_MS;
    if (step < 1) step = 1;
    int moving = 0;
    for (int p = 1; p < P_COUNT; p++) {
        int tg = p == target ? 256 : 0;
        if (anim[p] == tg) continue;
        anim[p] += anim[p] < tg ? step : -step;
        if ((anim[p] > tg && step > 0 && tg == 256) || anim[p] > 256) anim[p] = 256;
        if (anim[p] < 0) anim[p] = 0;
        moving = 1;
        if (p == P_GRID) wm_damage(gfx_rect_make(0, 0, sw, sh)); else damage_top();
    }
    {   /* notch / tray: out of the way of windows, back when they have gone (never while their own panel is open) */
        int tn = (anim[P_CLOCK] == 0 && target != P_CLOCK && wm_window_hits(notch_home())) ? 256 : 0;
        int tt = (target == P_NONE && wm_window_hits(tray_home())) ? 256 : 0;
        if (dodge_notch != tn) { dodge_notch += dodge_notch < tn ? step : -step; if (dodge_notch > 256) dodge_notch = 256; if (dodge_notch < 0) dodge_notch = 0; moving = 1; wm_damage(gfx_rect_make((sw - 140) / 2, 0, 140, TOP + 4)); }
        if (dodge_tray != tt) { dodge_tray += dodge_tray < tt ? step : -step; if (dodge_tray > 256) dodge_tray = 256; if (dodge_tray < 0) dodge_tray = 0; moving = 1; wm_damage(gfx_rect_make(sw - 12 - TRAY_N * 34 - 20, 0, TRAY_N * 34 + 32, TOP + 4)); }
    }
    read_time();
    if ((int)now_t.min != minute_seen) { minute_seen = (int)now_t.min; wm_damage(gfx_rect_make((sw - 140) / 2, 0, 140, TOP)); }
    if (target == P_CLOCK && anim[P_CLOCK] >= 256) {
        static u32 sec_last;
        if (now_t.sec != sec_last) { sec_last = now_t.sec; damage_top(); }
    }
    wx_poll(target == P_CLOCK);
    {
        static int wx_seen = -1;
        if (wx_state != wx_seen) { wx_seen = wx_state; damage_top(); }
    }
    if (net_busy && app_ast_bg_serial() != net_ser) {
        net_busy = 0;
        const char *o = app_ast_bg_output(), *ln = o;
        for (const char *q = o; *q; q++) if (*q == '\n' && q[1]) ln = q + 1;
        kstrncpy(net_msg, ln, sizeof net_msg - 1);
        for (char *e = net_msg; *e; e++) if (*e == '\n' || *e == 0x1b) *e = 0;
        rescan_net();
        damage_top();
    }
    return moving;
}

/* ---------------------------------------------------------------- drawing */

#define PANEL_BG   0xF2191B20u
#define PANEL_LINE 0x40FFFFFFu
#define WHITE      0xFFFFFFFFu
#define MUTED      0xFFA0A6B2u

static int hit_row(gfx_rect r, int x, int y) { return gfx_rect_has(&r, x, y); }

static void draw_notch(gfx_rect clip)
{
    gfx_rect n = notch_rect();
    gfx_rect c = gfx_rect_isect(gfx_rect_make(n.x - 20, 0, n.w + 40, n.h + 30), clip);
    if (gfx_rect_empty(&c)) return;
    int e = ease(anim[P_CLOCK]);
    gfx_set_clip(&c);
    if (e > 0) gfx_shadow(n.x, n.y + 2, n.w, n.h, 20, 14, 90 * e / 256);
    gfx_round_rect_ex(n.x, n.y, n.w, n.h, 0, 0, lerp(13, 26, e), lerp(13, 26, e), 0xFF000000u);
    char t[24];
    if (e < 200) {
        ksnprintf(t, sizeof t, "%02u:%02u", now_t.hour, now_t.min);
        gfx_text(n.x + (n.w - gfx_text_w(t)) / 2, 5, t, WHITE);
        return;
    }
    int x0 = n.x + 22, y0 = 18;
    /* calendar */
    ksnprintf(t, sizeof t, "%s %d", month_name(view_m), view_y);
    gfx_text(x0, y0, t, WHITE);
    gfx_text(x0 + 170, y0, "<", MUTED); gfx_text(x0 + 190, y0, ">", MUTED);
    static const char *const wd[LANG_COUNT][7] = {
        { "M", "T", "W", "T", "F", "S", "S" }, { "P", "S", "Ç", "P", "C", "C", "P" }, { "L", "M", "X", "J", "V", "S", "D" },
        { "П", "В", "С", "Ч", "П", "С", "В" }, { "一", "二", "三", "四", "五", "六", "日" } };
    for (int i = 0; i < 7; i++) {
        const char *h = wd[config_get()->lang][i];
        gfx_text(x0 + i * 27 + 12 - gfx_text_w(h) / 2, y0 + 26, h, MUTED);
    }
    int first = weekday(view_y, view_m, 1), dn = days_in(view_y, view_m);
    for (int d = 1; d <= dn; d++) {
        int idx = first + d - 1, cx = x0 + (idx % 7) * 27 + 12, cy = y0 + 54 + (idx / 7) * 24;
        int today = view_y == (int)now_t.year && view_m == (int)now_t.month && d == (int)now_t.day;
        if (today) gfx_circle(cx, cy + 8, 11, ui_th()->accent);
        ksnprintf(t, sizeof t, "%d", d);
        gfx_text(cx - gfx_text_w(t) / 2, cy, t, today ? 0xFF101010u : WHITE);
    }
    /* big clock, date */
    int mx = n.x + n.w / 2 - 24 + (n.w > 520 ? 20 : 0);
    ksnprintf(t, sizeof t, "%02u:%02u", now_t.hour, now_t.min);
    gfx_text_scaled(mx - gfx_text_w_scaled(t, 3) / 2 + 24, y0 + 34, t, WHITE, 3);
    ksnprintf(t, sizeof t, ":%02u", now_t.sec);
    gfx_text(mx + 24 + gfx_text_w_scaled("00:00", 3) / 2 + 4, y0 + 56, t, MUTED);
    ksnprintf(t, sizeof t, "%s", day_name(weekday((int)now_t.year, (int)now_t.month, (int)now_t.day)));
    gfx_text(mx + 24 - gfx_text_w(t) / 2, y0 + 96, t, WHITE);
    ksnprintf(t, sizeof t, "%u %s %u", now_t.day, month_name((int)now_t.month), now_t.year);
    gfx_text(mx + 24 - gfx_text_w(t) / 2, y0 + 118, t, MUTED);
    /* weather */
    int wx = n.x + n.w - 150;
    gfx_round_rect(wx - 10, y0 - 4, 138, 150, 14, 0xFF14161BU);
    gfx_text(wx, y0 + 2, config_tr("Weather", "Hava durumu"), MUTED);
    if (wx_state == 2) {
        gfx_text_scaled(wx, y0 + 28, wx_temp, WHITE, 2);
        gfx_text(wx, y0 + 70, wx_cond, WHITE);
        gfx_text(wx, y0 + 92, wx_place, MUTED);
    } else if (wx_state == 1) {
        gfx_text(wx, y0 + 40, config_tr("Loading...", "Yükleniyor..."), WHITE);
    } else {
        gfx_text(wx, y0 + 40, config_tr("No weather data", "Hava durumu yok"), WHITE);
        int ln = 0; char part[20]; int k = 0;
        for (const char *p = wx_err; ; p++) {
            if (*p == ' ' || !*p) {
                part[k] = 0;
                if (k) gfx_text(wx, y0 + 64 + ln * 18, part, MUTED);
                (void)0;
                k = 0; if (*p) { if (++ln > 3) break; } else break;
            } else if (k < 15) part[k++] = *p;
        }
    }
}

static void draw_tray(gfx_rect clip)
{
    gfx_rect tr = tray_rect();
    gfx_rect c = gfx_rect_isect(gfx_rect_make(tr.x - 4, 0, tr.w + 8, TOP), clip);
    if (gfx_rect_empty(&c)) return;
    gfx_set_clip(&c);
    gfx_round_rect(tr.x, tr.y, tr.w, tr.h, 12, 0xB0000000u);
    int vol = config_get()->volume;
    for (int i = 0; i < TRAY_N; i++) {
        gfx_rect r = tray_icon(i);
        int on = target == tray_panel[i];
        if (on) gfx_round_rect(r.x, r.y, r.w, r.h, 9, 0x40FFFFFFu);
        int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
        if (i == 0) {                                                       /* bell */
            gfx_round_rect(cx - 6, cy - 7, 12, 11, 5, WHITE);
            gfx_fill_rect(cx - 8, cy + 3, 16, 2, WHITE);
            gfx_circle(cx, cy + 8, 2, WHITE);
            if (notif_unread) gfx_circle(cx + 7, cy - 6, 4, 0xFFFF4D4Du);
        }
        else if (i == 1) glyph_power(cx, cy, WHITE);
        else if (i == 2) glyph_wifi(cx, cy - 1, WHITE);
        else glyph_speaker(cx - 2, cy, vol, WHITE);
    }
}

static void row_button(gfx_rect r, const char *label, int enabled)
{
    gfx_round_rect(r.x, r.y, r.w, r.h, r.h / 2, enabled ? ui_th()->accent : 0xFF3A3D45u);
    gfx_text(r.x + (r.w - gfx_text_w(label)) / 2, r.y + (r.h - 16) / 2, label, enabled ? 0xFF101010u : MUTED);
}

static gfx_rect net_btn(void) { gfx_rect p = panel_rect(P_NET, 1); return gfx_rect_make(p.x + 16, p.y + 118, 150, 28); }

static void draw_popup(int p, gfx_rect clip)
{
    int e = ease(anim[p]);
    if (e <= 0) return;
    gfx_rect r = panel_rect(p, 0), full = panel_rect(p, 1);
    gfx_rect c = gfx_rect_isect(gfx_rect_make(r.x - 20, r.y - 4, r.w + 40, r.h + 40), clip);
    if (gfx_rect_empty(&c)) return;
    gfx_set_clip(&c);
    gfx_shadow(r.x, r.y + 3, r.w, r.h, 16, 14, 110 * e / 256);
    gfx_round_rect(r.x, r.y, r.w, r.h, 16, PANEL_BG);
    gfx_round_outline(r.x, r.y, r.w, r.h, 16, PANEL_LINE);
    gfx_rect cc = gfx_rect_isect(c, r);
    gfx_set_clip(&cc);
    int x = full.x + 16, y = full.y + 14;
    char t[96];
    if (p == P_NOTIF) {
        gfx_text(x, y, config_tr("Notifications", "Bildirimler"), WHITE);
        gfx_rect cl = gfx_rect_make(full.x + full.w - 100, full.y + 10, 86, 26);
        if (notif_n) row_button(cl, config_tr("Clear all", "Temizle"), 1);
        if (!notif_n) gfx_text(x, y + 40, config_tr("No new notifications", "Yeni bildirim yok"), MUTED);
        for (int i = 0; i < notif_n; i++) {
            int ry = full.y + 48 + i * 40;
            gfx_round_rect(full.x + 10, ry, full.w - 20, 36, 8, 0xFF262932u);
            ksnprintf(t, sizeof t, "%02u:%02u", notif[i].h, notif[i].m);
            gfx_text(full.x + 18, ry + 10, t, MUTED);
            char line[44]; kstrncpy(line, notif[i].msg, 43); line[43] = 0;
            if (kstrlen(notif[i].msg) > 43) { line[40] = '.'; line[41] = '.'; line[42] = '.'; line[43] = 0; }
            gfx_text(full.x + 68, ry + 10, line, WHITE);
        }
    } else if (p == P_POWER) {
        static const char *const lab_en[3] = { "Sleep", "Restart", "Shut down" }, *const lab_tr[3] = { "Uyku", "Yeniden başlat", "Kapat" };
        for (int i = 0; i < 3; i++) {
            gfx_rect b = gfx_rect_make(full.x + 12 + i * 82, full.y + 12, 76, 94);
            gfx_round_rect(b.x, b.y, b.w, b.h, 12, 0xFF262932u);
            int cx = b.x + b.w / 2, cy = b.y + 34;
            if (i == 0) glyph_moon(cx, cy, 0xFFE8C860u, 0xFF262932u);
            else if (i == 1) glyph_restart(cx, cy, 0xFF6CC3FAu);
            else glyph_power(cx, cy, 0xFFFF6B6Bu);
            const char *l = config_tr(lab_en[i], lab_tr[i]);
            int lw = gfx_text_w(l);
            if (lw > b.w - 4) { gfx_text(b.x + 2, b.y + 66, l, WHITE); } else gfx_text(b.x + (b.w - lw) / 2, b.y + 66, l, WHITE);
        }
    } else if (p == P_VOL) {
        int vol = config_get()->volume;
        glyph_speaker(x + 8, full.y + 38, vol, WHITE);
        gfx_round_rect(x + 36, full.y + 34, 170, 8, 4, 0xFF3A3D45u);
        if (vol > 0) gfx_round_rect(x + 36, full.y + 34, 170 * vol / 100 < 8 ? 8 : 170 * vol / 100, 8, 4, ui_th()->accent);
        gfx_circle(x + 36 + 170 * vol / 100, full.y + 38, 9, WHITE);
        ksnprintf(t, sizeof t, "%d%%", vol);
        gfx_text(x + 222, full.y + 30, t, WHITE);
    } else if (p == P_NET) {
        gfx_text(x, y, config_tr("Network", "Ağ"), WHITE);
        gfx_round_rect(x - 4, y + 24, panel_w(P_NET) - 24, 78, 10, 0xFF262932u);
        if (!ni.card) { gfx_text(x + 8, y + 36, config_tr("No network card", "Ağ kartı yok"), 0xFFFF8A80u); gfx_text(x + 8, y + 58, ni.name, MUTED); }
        else {
            gfx_circle(x + 14, y + 42, 5, ni.link && ni.ip ? 0xFF3CC05A : 0xFFD68910);
            gfx_text(x + 28, y + 34, "Ethernet", WHITE);
            gfx_text(x + 106, y + 34, ni.link ? config_tr("connected", "bağlı") : config_tr("no cable", "kablo yok"), MUTED);
            { char a[24]; net_fmt_ip(a, sizeof a, ni.ip); ksnprintf(t, sizeof t, "%s  %s", a, ni.name); }
            gfx_text(x + 8, y + 58, t, MUTED);
            gfx_text(x + 8, y + 78, config_tr("This is also the phone's / PC's Wi-Fi in an emulator", "Emülatörde telefonun / PC'nin Wi-Fi'si de budur"), 0xFF7A808C);
        }
        row_button(net_btn(), net_busy ? config_tr("Working...", "Çalışıyor...") : config_tr("Get address (DHCP)", "Adres al (DHCP)"), ni.card && !net_busy);
        if (net_msg[0]) gfx_text(x + 160, y + 112, net_msg, ui_th()->accent);
        gfx_text(x, y + 156, "Wi-Fi", WHITE);
        gfx_text(x + 70, y + 156, wifi_n ? config_tr("adapter found, no driver", "adaptör var, sürücü yok") : config_tr("no adapter", "adaptör yok"), MUTED);
        gfx_text(x, y + 178, config_tr("Nearby networks: none can be listed", "Yakındaki ağlar: listelenemiyor"), MUTED);
        gfx_text(x, y + 206, "Bluetooth", WHITE);
        gfx_text(x + 90, y + 206, bt_n ? config_tr("controller found, no driver", "denetleyici var, sürücü yok") : config_tr("no controller (needs USB)", "denetleyici yok (USB gerekir)"), MUTED);
    }
}

static void draw_grid(gfx_rect clip)
{
    int e = ease(anim[P_GRID]);
    if (e <= 0) return;
    gfx_rect all = gfx_rect_isect(clip, gfx_rect_make(0, TOP, sw, sh - TOP));
    if (gfx_rect_empty(&all)) return;
    gfx_set_clip(&all);
    gfx_fill_rect(0, TOP, sw, sh - TOP, (u32)(185 * e / 256) << 24 | 0x0A0B10u);
    for (int i = 0; i < ngapps; i++) {
        gfx_rect r = grid_cell(i);
        gfx_rect ic = gfx_rect_make(r.x + (r.w - 72) / 2, r.y + 12, 72, 72);
        if (i == ghot) gfx_round_rect(r.x + 6, r.y + 4, r.w - 12, r.h - 8, 14, 0x30FFFFFFu);
        int s = 72 * (160 + 96 * e / 256) / 256;
        int a = e < 255 ? e : 255;
        const char *label;
        if (gapps[i].app >= 0) {
            const gfx_surf *surf = render_app_icon(gapps[i].app, 72);
            gfx_set_clip(&all);
            if (surf) gfx_blit_scaled(surf, ic.x + (72 - s) / 2, ic.y + (72 - s) / 2, s, s, a);
            label = ui_app_label(gapps[i].app);
        } else if (ui_named_icon_id(gapps[i].pkg) >= 0) {                  /* the same picture the desktop shows (NetSurf, FroggyCode ...) */
            const gfx_surf *surf = ui_named_icon(ui_named_icon_id(gapps[i].pkg), 72);
            gfx_set_clip(&all);
            if (surf) gfx_blit_scaled(surf, ic.x + (72 - s) / 2, ic.y + (72 - s) / 2, s, s, a);
            label = gapps[i].pkg;
        } else {
            gfx_round_rect(ic.x + (72 - s) / 2, ic.y + (72 - s) / 2, s, s, 16, ui_th()->accent);
            char l1[2] = { gapps[i].pkg[0], 0 };
            gfx_text_scaled(ic.x + 24, ic.y + 20, l1, 0xFF101010u, 2);
            label = gapps[i].pkg;
        }
        char t[20];
        kstrncpy(t, label, 17); t[17] = 0;
        if (e >= 200) gfx_text(r.x + (r.w - gfx_text_w(t)) / 2, r.y + 94, t, WHITE);
    }
    if (gmenu >= 0 && e >= 200) {
        gfx_rect m = gfx_rect_make(gmx, gmy, 190, 34);
        gfx_shadow(m.x, m.y + 2, m.w, m.h, 10, 10, 100);
        gfx_round_rect(m.x, m.y, m.w, m.h, 10, PANEL_BG);
        int on = gapps[gmenu].app >= 0 ? ui_desk_has_app(gapps[gmenu].app) : ui_desk_has_pkg(gapps[gmenu].pkg);
        gfx_text(m.x + 14, m.y + 9, on ? config_tr("Already on the desktop", "Masaüstünde zaten var") : config_tr("Add to desktop", "Masaüstüne ekle"), on ? MUTED : WHITE);
    }
}

void shell_draw(gfx_rect clip)
{
    read_time();
    if (target != P_GRID || anim[P_GRID] < 256) { /* the grid dims the desktop below the strip only */ }
    draw_grid(clip);
    draw_notch(clip);
    draw_tray(clip);
    for (int p = P_POWER; p <= P_VOL; p++) draw_popup(p, clip);
    draw_popup(P_NOTIF, clip);
    if (sleeping) {
        gfx_set_clip(&clip);
        gfx_fill_rect(0, 0, sw, sh, 0xFF000000u);
        const char *s = config_tr("Sleeping - press a key or click to wake up", "Uykuda - uyandırmak için bir tuşa bas ya da tıkla");
        gfx_text((sw - gfx_text_w(s)) / 2, sh / 2 - 8, s, 0xFF55595F);
    }
    gfx_set_clip(&clip);
}

/* ---------------------------------------------------------------- input */

static void set_volume(int v)
{
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    as_settings s = *config_get();
    if (s.volume == v) return;
    s.volume = v;
    config_set(&s);
    damage_top();
}

/* 1 = the shell used the event */
int shell_pointer(const mouse_ev *e, int ldown, int rdown, int lup)
{
    (void)lup;
    static int drag_vol;
    if (sleeping) { if (ldown || rdown || (e->buttons & 7)) { sleeping = 0; wm_damage(gfx_rect_make(0, 0, sw, sh)); } return 1; }
    int x = e->x, y = e->y;
    if (drag_vol) {
        if (!(e->buttons & 1)) drag_vol = 0;
        else { gfx_rect f = panel_rect(P_VOL, 1); set_volume((x - (f.x + 16 + 36)) * 100 / 170); return 1; }
    }
    if (target == P_GRID && anim[P_GRID] > 0) {
        int h = grid_at(x, y);
        if (h != ghot) { ghot = h; wm_damage(gfx_rect_make(0, TOP, sw, sh - TOP)); }
        if (ui_dock_hit(x, y) == APP_GRID || (y >= sh - 110 && ui_dock_hit(x, y) >= 0)) return 0;
        if (gmenu >= 0 && (ldown || rdown)) {
            gfx_rect m = gfx_rect_make(gmx, gmy, 190, 34);
            if (ldown && gfx_rect_has(&m, x, y)) {
                if (gapps[gmenu].app >= 0) ui_desk_add_app(gapps[gmenu].app); else ui_desk_add_pkg(gapps[gmenu].pkg);
            }
            gmenu = -1; wm_damage(gfx_rect_make(0, TOP, sw, sh - TOP));
            return 1;
        }
        if (rdown && h >= 0) { gmenu = h; gmx = x; gmy = y; wm_damage(gfx_rect_make(0, TOP, sw, sh - TOP)); return 1; }
        if (ldown) {
            if (h >= 0) {
                gapp g = gapps[h];
                shell_open(P_NONE);
                if (g.app >= 0) ui_launch_app(g.app); else ui_launch_package(g.pkg);
            } else if (y >= TOP) shell_open(P_NONE);
            if (y >= TOP) return 1;
        } else if (y >= TOP) return 1;
    }
    if (ldown) {
        gfx_rect n = notch_rect();
        if (gfx_rect_has(&n, x, y)) {
            if (target != P_CLOCK) { shell_open(P_CLOCK); return 1; }
            if (anim[P_CLOCK] >= 256) {                                        /* calendar arrows */
                int x0 = n.x + 22, y0 = 18;
                gfx_rect l = gfx_rect_make(x0 + 164, y0 - 4, 20, 24), r = gfx_rect_make(x0 + 184, y0 - 4, 24, 24);
                if (gfx_rect_has(&l, x, y)) { if (--view_m < 1) { view_m = 12; view_y--; } damage_top(); return 1; }
                if (gfx_rect_has(&r, x, y)) { if (++view_m > 12) { view_m = 1; view_y++; } damage_top(); return 1; }
            }
            if (y < TOP) { shell_open(P_NONE); }
            return 1;
        }
        for (int i = 0; i < TRAY_N; i++) {
            gfx_rect r = tray_icon(i);
            if (gfx_rect_has(&r, x, y)) { shell_open(target == tray_panel[i] ? P_NONE : tray_panel[i]); return 1; }
        }
        if (target == P_NOTIF) {
            gfx_rect pr = panel_rect(P_NOTIF, 1);
            if (!gfx_rect_has(&pr, x, y)) { shell_open(P_NONE); return 1; }
            gfx_rect cl = gfx_rect_make(pr.x + pr.w - 100, pr.y + 10, 86, 26);
            if (notif_n && gfx_rect_has(&cl, x, y)) shell_notif_clear();
            return 1;
        }
        if (target >= P_POWER && target <= P_VOL) {
            gfx_rect pr = panel_rect(target, 1);
            if (!gfx_rect_has(&pr, x, y)) { shell_open(P_NONE); return 1; }
            if (target == P_POWER) {
                for (int i = 0; i < 3; i++) {
                    gfx_rect b = gfx_rect_make(pr.x + 12 + i * 82, pr.y + 12, 76, 94);
                    if (!hit_row(b, x, y)) continue;
                    if (i == 0) { shell_open(P_NONE); sleeping = 1; wm_damage(gfx_rect_make(0, 0, sw, sh)); }
                    else if (i == 1) { persist_flush(); hal_reboot(); }
                    else { persist_flush(); hal_shutdown(); }
                    return 1;
                }
            } else if (target == P_VOL) {
                gfx_rect sl = gfx_rect_make(pr.x + 16 + 20, pr.y + 22, 190, 34);
                gfx_rect sp = gfx_rect_make(pr.x + 10, pr.y + 22, 30, 34);
                if (gfx_rect_has(&sp, x, y)) { static int last = 70; int v = config_get()->volume; if (v) { last = v; set_volume(0); } else set_volume(last); }
                else if (gfx_rect_has(&sl, x, y)) { drag_vol = 1; set_volume((x - (pr.x + 16 + 36)) * 100 / 170); }
            } else if (target == P_NET) {
                if (ni.card && !net_busy && gfx_rect_has(&(gfx_rect){ net_btn().x, net_btn().y, net_btn().w, net_btn().h }, x, y)) {
                    if (!user_name()) kstrncpy(net_msg, config_tr("Sign in first", "Önce giriş yap"), sizeof net_msg - 1);
                    else if (!app_ast_bg("net dhcp")) kstrncpy(net_msg, config_tr("Busy, try again", "Meşgul, tekrar dene"), sizeof net_msg - 1);
                    else { net_busy = 1; net_ser = app_ast_bg_serial(); net_msg[0] = 0; }
                    damage_top();
                }
            }
            return 1;
        }
        if (target == P_CLOCK) {
            gfx_rect n2 = notch_rect();
            if (!gfx_rect_has(&n2, x, y)) { shell_open(P_NONE); return 1; }
        }
    }
    {   /* a click on the strip itself belongs to the shell (nothing else lives there) */
        gfx_rect tr = tray_rect();
        if (y < TOP && (ldown || rdown) && gfx_rect_has(&tr, x, y)) return 1;
    }
    return 0;
}

int shell_key(int k)
{
    if (sleeping) { sleeping = 0; wm_damage(gfx_rect_make(0, 0, sw, sh)); return 1; }
    if (target != P_NONE && k == 27) { shell_open(P_NONE); return 1; }
    if (target == P_VOL) {
        int v = config_get()->volume;
        if (k == KEY_LEFT || k == KEY_DOWN) { set_volume(v - 5); return 1; }
        if (k == KEY_RIGHT || k == KEY_UP) { set_volume(v + 5); return 1; }
    }
    if (target == P_GRID) return 1;                                            /* the grid swallows typing */
    return 0;
}
