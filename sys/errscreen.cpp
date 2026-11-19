/*
 * AstraSageOS error screens (C++, freestanding: no libstdc++, no exceptions, no RTTI).
 *
 *   Green  "Kernel Panic!!"  - the system stopped; the logo top right, the reason in red bottom right.
 *                              It never restarts by itself (no panic -> reboot -> panic loop): the user reboots.
 *   Yellow "</>"             - a process puts a heavy load on the system (or /user was tampered with):
 *                              5 second countdown, then the machine restarts.
 *
 * C interface (kernel/astra.h): err_kernel_panic(), err_overload(), err_draw_panic(), err_draw_overload().
 * Without graphics (text mode) the same messages are printed on the console.
 */
extern "C" {
#include "../gui/gui.h"
extern const u8 asset_boot_logo_start[], asset_boot_logo_end[];
}

namespace {

constexpr u32 GREEN  = 0xFF138A3C;
constexpr u32 YELLOW = 0xFFF5C400;
constexpr u32 RED    = 0xFFFF2A2A;
constexpr u32 WHITE  = 0xFFFFFFFF;
constexpr u32 INK    = 0xFF1B1B1B;

int g_busy;                                   // a panic inside a panic just stops

struct Screen {
    int w, h;
    int scale(int big) const { return w >= 1024 ? big : (w >= 640 ? big * 2 / 3 : (big / 3 > 1 ? big / 3 : 1)); }
    int fit(const char *t, int want, int avail) const {          // the biggest scale <= want whose text still fits `avail` pixels
        while (want > 1 && gfx_text_w_scaled(t, want) > avail) want--;
        return want;
    }
    void text_c(int cx, int y, const char *s, u32 col, int sc) const { gfx_text_scaled(cx - gfx_text_w_scaled(s, sc) / 2, y, s, col, sc); }
    void text_r(int right, int y, const char *s, u32 col, int sc) const { gfx_text_scaled(right - gfx_text_w_scaled(s, sc), y, s, col, sc); }
};

bool open_screen(Screen &s)
{
    const video_info *vi = video_get();
    if (!vi) return false;
    if (gfx_width() <= 0 && !gfx_init(vi)) return false;
    s.w = gfx_width(); s.h = gfx_height();
    gfx_set_target(0);
    gfx_set_clip(0);
    return s.w > 0 && s.h > 0;
}

void present(const Screen &s) { gfx_present(gfx_rect_make(0, 0, s.w, s.h), 0, 0); }

// word-wrap `src` into lines of at most `maxc` characters; returns the number of lines
int wrap(const char *src, char (*lines)[96], int maxlines, int maxc)
{
    int n = 0, len = 0;
    char cur[96];
    const char *p = src;
    while (*p && n < maxlines) {
        const char *e = p;
        while (*e && *e != ' ') e++;
        int wl = (int)(e - p);
        if (len && len + 1 + wl > maxc) {
            cur[len] = 0;
            for (int i = 0; i <= len; i++) lines[n][i] = cur[i];
            n++; len = 0;
            if (n >= maxlines) break;
        }
        if (len) cur[len++] = ' ';
        for (int i = 0; i < wl && len < 94; i++) cur[len++] = p[i];
        p = *e ? e + 1 : e;
    }
    if (len && n < maxlines) {
        cur[len] = 0;
        for (int i = 0; i <= len; i++) lines[n][i] = cur[i];
        n++;
    }
    return n;
}

// The logo picture sits on a black square. On the coloured screens that square would clash, so the black that is
// connected to the picture's border is left out (flood fill); the letters inside the logo stay black.
constexpr int MAXLOGO = 256;
u8  g_outer[MAXLOGO * MAXLOGO];
u16 g_queue[MAXLOGO * MAXLOGO * 2];

bool near_black(const bmp_view &v, int x, int y)
{
    u32 c = bmp_get(&v, x, y);
    return ((c >> 16) & 255) < 40 && ((c >> 8) & 255) < 40 && (c & 255) < 40;
}

void mark_outer(const bmp_view &v)
{
    for (int i = 0; i < v.w * v.h; i++) g_outer[i] = 0;
    int qn = 0;
    auto push = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= v.w || y >= v.h || g_outer[y * v.w + x] || !near_black(v, x, y)) return;
        g_outer[y * v.w + x] = 1;
        if (qn + 2 <= (int)(sizeof g_queue / sizeof g_queue[0])) { g_queue[qn++] = (u16)x; g_queue[qn++] = (u16)y; }
    };
    for (int x = 0; x < v.w; x++) { push(x, 0); push(x, v.h - 1); }
    for (int y = 0; y < v.h; y++) { push(0, y); push(v.w - 1, y); }
    while (qn > 0) {
        int y = g_queue[--qn], x = g_queue[--qn];
        push(x + 1, y); push(x - 1, y); push(x, y + 1); push(x, y - 1);
    }
}

void draw_logo(const Screen &s, int size)
{
    bmp_view v;
    if (bmp_open(asset_boot_logo_start, (u32)(asset_boot_logo_end - asset_boot_logo_start), &v) != BMP_OK) return;
    if (v.w > MAXLOGO || v.h > MAXLOGO) { gfx_draw_bmp_scaled(&v, (gfx_surf *)gfx_back(), s.w - size - 24, 24, size, size, 0); return; }
    mark_outer(v);
    int x0 = s.w - size - 24, y0 = 24;
    for (int dy = 0; dy < size; dy++) {
        for (int dx = 0; dx < size; dx++) {
            // 2x2 samples inside the source pixel box; outer (black frame) samples do not count
            int sx0 = dx * v.w / size, sx1 = (dx + 1) * v.w / size, sy0 = dy * v.h / size, sy1 = (dy + 1) * v.h / size;
            if (sx1 <= sx0) sx1 = sx0 + 1;
            if (sy1 <= sy0) sy1 = sy0 + 1;
            u32 r = 0, g = 0, b = 0, n = 0, tot = 0;
            for (int yy = sy0; yy < sy1 && yy < v.h; yy++)
                for (int xx = sx0; xx < sx1 && xx < v.w; xx++) {
                    tot++;
                    if (g_outer[yy * v.w + xx]) continue;
                    u32 c = bmp_get(&v, xx, yy);
                    r += (c >> 16) & 255; g += (c >> 8) & 255; b += c & 255; n++;
                }
            if (!n) continue;
            u32 a = n * 255 / tot;                                   // partly covered edge pixels blend with the background
            gfx_pixel(x0 + dx, y0 + dy, (a << 24) | ((r / n) << 16) | ((g / n) << 8) | (b / n));
        }
    }
}

void fmt_secs(char *out, int secs)
{
    const char *a = "The system restarts in ";
    int i = 0;
    while (*a) out[i++] = *a++;
    out[i++] = (char)('0' + (secs % 10));
    const char *b = secs == 1 ? " second..." : " seconds...";
    while (*b) out[i++] = *b++;
    out[i] = 0;
}

[[noreturn]] void halt_forever()
{
#if defined(__i386__) || defined(__x86_64__)
    for (;;) __asm__ volatile("cli\n\thlt");
#elif defined(__aarch64__)
    for (;;) __asm__ volatile("msr daifset, #15\n\twfe");
#elif defined(__arm__)
    for (;;) __asm__ volatile("cpsid if\n\twfe");
#else
    for (;;) { }
#endif
}

}  // namespace

extern "C" {

/* The green screen. Draws into the back buffer and shows it. Returns 0 when there is no graphics. */
int err_draw_panic(const char *reason)
{
    Screen s;
    if (!open_screen(s)) return 0;
    gfx_clear(GREEN);
    int logo = s.h / 4 < 220 ? s.h / 4 : 220;
    draw_logo(s, logo);
    int big = s.fit("Kernel Panic!!", s.scale(9), s.w - 2 * (logo + 40));
    s.text_c(s.w / 2, s.h / 12, "Kernel Panic!!", WHITE, big);
    int mid = s.fit("Opss.. Your System is Panic Pls Reboot system", s.scale(3), s.w - 40);
    s.text_c(s.w / 2, s.h / 2 - 8 * mid, "Opss.. Your System is Panic Pls Reboot system", WHITE, mid);
    // reason: red, bottom right (a dark edge keeps it readable on the green)
    int rs = s.scale(2);
    char lines[4][96];
    char msg[160] = "Reason: ";
    int k = 8;
    for (const char *r = reason ? reason : "unknown"; *r && k < 150; r++) msg[k++] = *r;
    msg[k] = 0;
    int cols = (s.w - 48) / (8 * rs);
    int n = wrap(msg, lines, 4, cols > 90 ? 90 : cols);
    for (int i = 0; i < n; i++) {
        int y = s.h - 24 - (n - i) * (16 * rs + 4);
        s.text_r(s.w - 24 + 1, y + 1, lines[i], 0xFF000000, rs);
        s.text_r(s.w - 24, y, lines[i], RED, rs);
    }
    present(s);
    return 1;
}

/* The yellow screen with `secs` seconds still to go. */
int err_draw_overload(const char *reason, int secs)
{
    Screen s;
    if (!open_screen(s)) return 0;
    gfx_clear(YELLOW);
    // big "</>" logo, top right
    int ls = s.scale(14);
    s.text_r(s.w - 28, 20, "</>", INK, ls);
    // message, right side, vertically centred
    int ms = s.scale(3);
    char lines[8][96];
    int cols = (s.w * 3 / 5) / (8 * ms);
    int n = wrap("A process is placing a heavy load on the system. To protect it, the system is restarting.", lines, 6, cols);
    char cd[48];
    fmt_secs(cd, secs < 1 ? 1 : secs);
    int lh = 16 * ms + 6;
    int total = (n + 2) * lh;
    int y = s.h / 2 - total / 2;
    for (int i = 0; i < n; i++, y += lh) s.text_r(s.w - 28, y, lines[i], INK, ms);
    y += lh / 2;
    s.text_r(s.w - 28, y, cd, 0xFF8A1010, ms);
    if (reason && *reason) {
        int rs = s.scale(2);
        char m[160] = "Reason: ";
        int k = 8;
        for (const char *r = reason; *r && k < 150; r++) m[k++] = *r;
        m[k] = 0;
        s.text_r(s.w - 24, s.h - 24 - 16 * rs, m, INK, rs);
    }
    present(s);
    return 1;
}

/* Never returns and never restarts: a panic that restarted by itself could loop forever. */
void err_kernel_panic(const char *reason)
{
    if (g_busy) halt_forever();
    g_busy = 1;
    err_log("kernel panic", reason);
    if (!err_draw_panic(reason)) {
        kprintf("\n*** Kernel Panic!! ***\nOpss.. Your System is Panic Pls Reboot system\nReason: %s\n", reason ? reason : "unknown");
    }
    halt_forever();
}

/* Yellow screen, 5 second countdown, then the machine restarts. */
void err_overload(const char *reason)
{
    if (g_busy) halt_forever();
    g_busy = 1;
    err_log("heavy load", reason);
    for (int secs = 5; secs >= 1; secs--) {
        if (!err_draw_overload(reason, secs)) {
            kprintf("\n*** </> A process is placing a heavy load on the system. Restart in %d... (%s)\n", secs, reason ? reason : "");
        }
        u32 t0 = ui_ms();
        u32 spins = 0;
        while (ui_ms() - t0 < 1000 && ++spins < 20000000u) { }
    }
    g_busy = 0;
    hal_reboot();
    halt_forever();
}

}  // extern "C"
