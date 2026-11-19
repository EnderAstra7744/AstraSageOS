/*
 * OS configuration kept in the OS root: /config/settings (language, theme, login mode, setup state, the drive
 * of the package repository). /config/<user>.config (saved logins) is handled by users.c.
 * Plain text, "key=value" per line; unknown lines are ignored.
 */
#include "astra.h"

static as_settings cur = { LANG_EN, 0, 0, 0, 1, 70, -1, -1, -1, { -1, -1, -1 }, -1, "", "", 100, 100, -1 };

static const struct { const char *n; int rgb; } cnames[] = {
    { "red", 0xE53935 }, { "green", 0x43A047 }, { "blue", 0x1E88E5 }, { "yellow", 0xFDD835 }, { "orange", 0xFB8C00 },
    { "purple", 0x8E24AA }, { "pink", 0xEC407A }, { "cyan", 0x00ACC1 }, { "white", 0xFFFFFF }, { "black", 0x000000 },
    { "gray", 0x757575 }, { "grey", 0x757575 }, { "lime", 0xAFFF00 }, { "teal", 0x00897B }, { "brown", 0x6D4C41 },
    { "gold", 0xFFC107 }, { "navy", 0x1A237E }, { "magenta", 0xD500F9 }, { 0, 0 }
};

static int hexv(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

/* "red", "#ff8800", "ff8800" or "default" (-1). Returns 0 when the text is a colour. */
int color_parse(const char *s, int *rgb)
{
    if (!kstrcmp(s, "default") || !kstrcmp(s, "-")) { *rgb = -1; return 0; }
    for (int i = 0; cnames[i].n; i++) {
        const char *a = s, *b = cnames[i].n;
        while (*a && (*a | 32) == *b) { a++; b++; }
        if (!*a && !*b) { *rgb = cnames[i].rgb; return 0; }
    }
    const char *h = s + (s[0] == '#');
    if (kstrlen(h) != 6) return -1;
    int v = 0;
    for (int i = 0; i < 6; i++) { int d = hexv(h[i]); if (d < 0) return -1; v = v << 4 | d; }
    *rgb = v;
    return 0;
}

static int colnum(const char *v) { int c; return color_parse(v, &c) == 0 ? c : -1; }

static int ensure_dir(void)
{
    int o = fs_get_jail();
    fs_set_jail(0);
    int d = fs_resolve("/.config", 0);
    if (d == FS_ENOENT) d = fs_create("/.config", 0, 1);
    fs_set_jail(o);
    return d;
}

static int num(const char *v, int lo, int hi, int dflt)
{
    if (*v < '0' || *v > '9') return dflt;
    int n = 0;
    while (*v >= '0' && *v <= '9') n = n * 10 + (*v++ - '0');
    return n < lo || n > hi ? dflt : n;
}

void config_init(void)
{
    cur.lang = LANG_EN; cur.theme = 0; cur.login_gui = 0; cur.setup_done = 0; cur.repo_drive = 1;
    cur.tab_color = cur.line_color = cur.input_color = cur.nf_info = -1;
    cur.volume = 70; cur.win_a_frame = cur.win_a_content = 100; cur.wallpaper = -1;
    cur.nf_logo[0] = cur.nf_logo[1] = cur.nf_logo[2] = -1; cur.nf_png[0] = cur.nf_txt[0] = 0;
    int o = fs_get_jail();
    fs_set_jail(0);
    ensure_dir();
    int f = fs_resolve("/.config/settings", 0);
    if (f > 0) {
        const char *d = fs_data(f);
        u32 len = fs_size(f), i = 0;
        while (i < len) {
            u32 e = i;
            while (e < len && d[e] != '\n') e++;
            char line[48];
            u32 n = e - i < sizeof line - 1 ? e - i : (u32)sizeof line - 1;
            kmemcpy(line, d + i, n); line[n] = 0;
            char *eq = (char *)kstrchr(line, '=');
            if (eq) {
                *eq = 0;
                const char *k = line, *v = eq + 1;
                if (!kstrcmp(k, "lang")) cur.lang = num(v, 0, LANG_COUNT - 1, 0);
                else if (!kstrcmp(k, "theme")) cur.theme = num(v, 0, 8, 0);
                else if (!kstrcmp(k, "login_gui")) cur.login_gui = num(v, 0, 1, 0);
                else if (!kstrcmp(k, "setup_done")) cur.setup_done = num(v, 0, 1, 0);
                else if (!kstrcmp(k, "repo_drive")) cur.repo_drive = num(v, 0, 3, 1);
                else if (!kstrcmp(k, "volume")) cur.volume = num(v, 0, 100, 70);
                else if (!kstrcmp(k, "win_alpha_frame")) cur.win_a_frame = num(v, 30, 100, 100);
                else if (!kstrcmp(k, "win_alpha_content")) cur.win_a_content = num(v, 30, 100, 100);
                else if (!kstrcmp(k, "wallpaper")) cur.wallpaper = num(v, 0, 12, -1);
                else if (!kstrcmp(k, "tab_color")) cur.tab_color = colnum(v);
                else if (!kstrcmp(k, "line_color")) cur.line_color = colnum(v);
                else if (!kstrcmp(k, "input_color")) cur.input_color = colnum(v);
                else if (!kstrcmp(k, "nf_logo1")) cur.nf_logo[0] = colnum(v);
                else if (!kstrcmp(k, "nf_logo2")) cur.nf_logo[1] = colnum(v);
                else if (!kstrcmp(k, "nf_logo3")) cur.nf_logo[2] = colnum(v);
                else if (!kstrcmp(k, "nf_info")) cur.nf_info = colnum(v);
                else if (!kstrcmp(k, "nf_png")) kstrncpy(cur.nf_png, v, sizeof cur.nf_png - 1);
                else if (!kstrcmp(k, "nf_txt")) kstrncpy(cur.nf_txt, v, sizeof cur.nf_txt - 1);
            }
            i = e + 1;
        }
    }
    fs_set_jail(o);
}

const as_settings *config_get(void) { return &cur; }

void config_save(void)
{
    int o = fs_get_jail();
    fs_set_jail(0);
    if (ensure_dir() > 0) {
        int f = fs_resolve("/.config/settings", 0);
        if (f == FS_ENOENT) f = fs_create("/.config/settings", 0, 0);
        if (f > 0) {
            char b[600], c[7][10];
            int vals[7] = { cur.tab_color, cur.line_color, cur.input_color, cur.nf_logo[0], cur.nf_logo[1], cur.nf_logo[2], cur.nf_info };
            for (int i = 0; i < 7; i++) { if (vals[i] < 0) kstrncpy(c[i], "default", 9); else ksnprintf(c[i], 10, "%06x", vals[i] & 0xFFFFFF); }
            int n = ksnprintf(b, sizeof b, "lang=%d\ntheme=%d\nlogin_gui=%d\nsetup_done=%d\nrepo_drive=%d\nvolume=%d\nwin_alpha_frame=%d\nwin_alpha_content=%d\nwallpaper=%d\n"
                              "tab_color=%s\nline_color=%s\ninput_color=%s\nnf_logo1=%s\nnf_logo2=%s\nnf_logo3=%s\nnf_info=%s\nnf_png=%s\nnf_txt=%s\n",
                              cur.lang, cur.theme, cur.login_gui, cur.setup_done, cur.repo_drive, cur.volume, cur.win_a_frame, cur.win_a_content, cur.wallpaper,
                              c[0], c[1], c[2], c[3], c[4], c[5], c[6], cur.nf_png, cur.nf_txt);
            fs_write(f, b, (u32)n, 0);
        }
    }
    fs_set_jail(o);
    persist_flush();
}

void config_set(const as_settings *s)
{
    cur = *s;
    config_save();
}

/* Text in the language chosen in the welcome app (English or Turkish, UTF-8). */
#include "lang_tab.h"

/* Text in the chosen language. Turkish comes with the call; Spanish, Russian and Chinese come from lang_tab.h
 * (looked up by the English text, cached per string address) and fall back to English when a text has no row. */
const char *config_tr(const char *en, const char *tr)
{
    if (cur.lang == LANG_EN) return en;
    if (cur.lang == LANG_TR && tr) return tr;
    static const char *ckey[64];
    static short crow[64];
    unsigned h = (unsigned)((unsigned long)en >> 2) & 63;
    int row = -1;
    if (ckey[h] == en && crow[h] >= 0 && !kstrcmp(lang_rows[crow[h]].en, en)) row = crow[h];
    else {
        for (int i = 0; i < LANG_NROWS; i++) if (lang_rows[i].en[0] == en[0] && !kstrcmp(lang_rows[i].en, en)) { row = i; break; }
        if (row >= 0) { ckey[h] = en; crow[h] = (short)row; }
    }
    if (row < 0) return en;
    const char *t = cur.lang == LANG_TR ? lang_rows[row].tr : cur.lang == LANG_ES ? lang_rows[row].es : cur.lang == LANG_RU ? lang_rows[row].ru : lang_rows[row].zh;
    return t && *t ? t : en;
}

/* nearest xterm-256 colour of a 0xRRGGBB value (cube or grey ramp) */
int con_rgb256(int rgb)
{
    static const int lv[6] = { 0, 95, 135, 175, 215, 255 };
    int c[3] = { rgb >> 16 & 255, rgb >> 8 & 255, rgb & 255 }, q[3], cd = 0;
    for (int i = 0; i < 3; i++) {
        int best = 0, bd = 1 << 30;
        for (int k = 0; k < 6; k++) { int d = (c[i] - lv[k]) * (c[i] - lv[k]); if (d < bd) { bd = d; best = k; } }
        q[i] = best; cd += bd;
    }
    int avg = (c[0] + c[1] + c[2]) / 3, gi = (avg - 8 + 5) / 10;
    if (gi < 0) gi = 0;
    if (gi > 23) gi = 23;
    int gv = 8 + 10 * gi, gd = 0;
    for (int i = 0; i < 3; i++) gd += (c[i] - gv) * (c[i] - gv);
    return gd < cd ? 232 + gi : 16 + 36 * q[0] + 6 * q[1] + q[2];
}
