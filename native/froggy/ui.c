/* FroggyCode 2.0 - everything that is drawn: left bar, top bar, chat bubbles, mascot, bottom Terminal / Console, right history, settings. */
#include "fg.h"
#include "templates.h"

int ui_W = 900, ui_H = 620;

/* ---------------------------------------------------------------- themes */
typedef struct { unsigned bg, side, bar, text, dim, accent, on_accent, bub_f, bub_u, on_bub_u, input, line, ok, err, hover; } Theme;
static const Theme TH[3] = {
    /* dark  */ { 0x1b1f23, 0x13161a, 0x13161a, 0xe8ebee, 0x8b949e, 0x5bbf5a, 0x0d2410, 0x2a3138, 0x2f7d3b, 0xf4fff4, 0x242a31, 0x37414a, 0x6be675, 0xff7a70, 0x2d353d },
    /* light */ { 0xf5f7f9, 0xe9edf1, 0xe9edf1, 0x1c2430, 0x667180, 0x2e9a55, 0xffffff, 0xe3e8ee, 0x2e9a55, 0xffffff, 0xffffff, 0xc9d1da, 0x1f8a43, 0xc63c32, 0xdce3ea },
    /* green */ { 0x0c2518, 0x071a10, 0x071a10, 0xdff5e4, 0x86b895, 0x8be07a, 0x0b2a00, 0x16402a, 0x3f8f2a, 0xf4fff0, 0x123a25, 0x24593a, 0x9bf08c, 0xff8e84, 0x1c4d33 },
};
static const Theme *T;

/* ---------------------------------------------------------------- hit areas */
static Btn btns[200];
static int nb, modal_from;

static void hit(int x, int y, int w, int h, int id, int arg)
{
    if (nb < 200) btns[nb++] = (Btn){ x, y, w, h, id, arg };
}

int ui_hit(int x, int y, int *arg)
{
    int from = S.settings_open ? modal_from : 0;
    for (int i = nb - 1; i >= from; i--)
        if (x >= btns[i].x && y >= btns[i].y && x < btns[i].x + btns[i].w && y < btns[i].y + btns[i].h) { *arg = btns[i].arg; return btns[i].id; }
    if (S.settings_open) { *arg = 0; return B_CLOSE; }              /* a click outside the settings panel closes it */
    return B_NONE;
}

/* ---------------------------------------------------------------- primitives */
static void rect(int x, int y, int w, int h, unsigned c) { if (w > 0 && h > 0) A->win_rect(x, y, w, h, c); }

static void rrect(int x, int y, int w, int h, int r, unsigned c)
{
    if (w <= 0 || h <= 0) return;
    if (r * 2 > h) r = h / 2;
    if (r * 2 > w) r = w / 2;
    if (r <= 0) { rect(x, y, w, h, c); return; }
    rect(x, y + r, w, h - 2 * r, c);
    for (int i = 0; i < r; i++) {
        int dy = r - i, dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= r * r + r) dx++;       /* quarter circle: how far the row reaches in */
        int inset = r - dx;
        if (inset < 0) inset = 0;
        rect(x + inset, y + i, w - 2 * inset, 1, c);
        rect(x + inset, y + h - 1 - i, w - 2 * inset, 1, c);
    }
}

static void circle(int cx, int cy, int r, unsigned c)
{
    for (int dy = -r; dy <= r; dy++) {
        int dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= r * r + r) dx++;
        rect(cx - dx, cy + dy, 2 * dx + 1, 1, c);
    }
}

static void text(int x, int y, const char *s, unsigned c, int sc) { A->win_text(x, y, s, c, sc); }

static int ulen(const char *s) { int n = 0; for (; *s; s++) if ((*s & 0xC0) != 0x80) n++; return n; }

static void text_clip(int x, int y, const char *s, int maxch, unsigned c, int sc)    /* at most maxch letters, ".." when cut */
{
    char b[260];
    int n = 0, k = 0;
    const char *p = s;
    while (*p && n < maxch && k < 250) {
        b[k++] = *p++;
        while ((*p & 0xC0) == 0x80 && k < 255) b[k++] = *p++;
        n++;
    }
    if (*p && maxch > 2) { int back = 0; while (k > 0 && back < 2) { k--; if ((b[k] & 0xC0) != 0x80) back++; } b[k++] = '.'; b[k++] = '.'; }
    b[k] = 0;
    text(x, y, b, c, sc);
}

const char *tr(const char *t, const char *e) { return S.lang_en ? e : t; }

/* ---------------------------------------------------------------- the frog (pixel art of the original Froggy) */
static const unsigned FP[6] = { 0x1F3D2B, 0x5BBF5A, 0x8BE07A, 0xEAF6C9, 0xF28B82, 0xFFFFFF };
static const char *EYES_OPEN[4]  = { "...000....000...", "..05550..05550..", "..05050..05050..", "..050000005000.." };
static const char *EYES_BLINK[4] = { "...000....000...", "..01110..01110..", "..00000..00000..", "..011100001110.." };
static const char *BODY1[3]      = { ".01111111111110.", "0112211111122110", "0144111001114410" };
static const char *BODY2[4]      = { ".01333333333310.", ".01333333333310.", ".01100000000110.", ".0000......0000." };
static const char *MOUTH[3][2]   = { { "0110111111110110", "0111000000001110" }, { "0110000000000110", "0111004444001110" }, { "0111111111111110", "0111110000011110" } };

static void rows(int x, int y, int sc, const char **r, int n, int row0)
{
    for (int j = 0; j < n; j++)
        for (int i = 0; r[j][i]; i++) {
            int p = r[j][i] - '0';
            if (p >= 0 && p <= 5) rect(x + i * sc, y + (row0 + j) * sc, sc, sc, FP[p]);
        }
}

static void frog(int x, int y, int sc, int blink, int mouth)
{
    rows(x, y, sc, blink ? EYES_BLINK : EYES_OPEN, 4, 0);
    rows(x, y, sc, BODY1, 3, 4);
    rows(x, y, sc, MOUTH[mouth], 2, 7);
    rows(x, y, sc, BODY2, 4, 9);
}

static int tick, mx_off = 0, mx_dir = 1;

void ui_tick(void)
{
    tick++;
    mx_off += mx_dir * 4;
    if (mx_off > 160 + (tick * 7) % 90) mx_dir = -1;
    if (mx_off < 0) { mx_off = 0; mx_dir = 1; }
}

static const int HOP[8] = { 0, 4, 8, 10, 10, 8, 4, 0 };

/* ---------------------------------------------------------------- icons */
static void ic_chat(int x, int y, unsigned c, unsigned bg)
{
    rrect(x, y + 2, 26, 18, 6, c);
    rrect(x + 2, y + 4, 22, 14, 4, bg);
    rect(x + 6, y + 20, 4, 3, c); rect(x + 5, y + 18, 4, 2, c);
    rect(x + 7, y + 9, 12, 2, c); rect(x + 7, y + 13, 8, 2, c);
}
static void ic_paper(int x, int y, unsigned c, unsigned bg)
{
    rect(x + 3, y, 20, 26, c);
    rect(x + 5, y + 2, 16, 22, bg);
    rect(x + 17, y, 6, 6, bg); rect(x + 17, y + 6, 6, 1, c); rect(x + 17, y, 1, 6, c);
    rect(x + 8, y + 10, 10, 2, c); rect(x + 8, y + 15, 10, 2, c); rect(x + 8, y + 20, 6, 2, c);
}
static void ic_puzzle(int x, int y, unsigned c, unsigned bg)
{
    rect(x + 2, y + 6, 20, 20, c);
    circle(x + 12, y + 4, 4, c);
    circle(x + 24, y + 16, 4, c);
    circle(x + 2, y + 16, 4, bg);
    rect(x + 6, y + 10, 4, 4, bg);
}
static void ic_gear(int x, int y, unsigned c, unsigned bg)
{
    int cx = x + 12, cy = y + 12;
    circle(cx, cy, 8, c);
    rect(cx - 2, cy - 12, 4, 5, c); rect(cx - 2, cy + 8, 4, 5, c);
    rect(cx - 12, cy - 2, 5, 4, c); rect(cx + 8, cy - 2, 5, 4, c);
    rect(cx - 9, cy - 9, 4, 4, c); rect(cx + 6, cy - 9, 4, 4, c); rect(cx - 9, cy + 6, 4, 4, c); rect(cx + 6, cy + 6, 4, 4, c);
    circle(cx, cy, 3, bg);
}
static void ic_clock(int x, int y, unsigned c, unsigned bg)
{
    circle(x + 12, y + 12, 11, c);
    circle(x + 12, y + 12, 9, bg);
    rect(x + 11, y + 5, 2, 8, c);
    rect(x + 11, y + 12, 7, 2, c);
}
static void ic_plus(int x, int y, unsigned c)
{
    rect(x + 10, y + 3, 4, 18, c);
    rect(x + 3, y + 10, 18, 4, c);
}

static void avatar_user(int x, int y)
{
    circle(x + 16, y + 16, 16, T->accent);
    circle(x + 16, y + 12, 5, T->on_accent);
    rrect(x + 7, y + 19, 18, 9, 4, T->on_accent);
}
static void avatar_frog(int x, int y)
{
    circle(x + 16, y + 16, 16, T->bub_f);
    frog(x + 4, y + 7, 1, 0, 0);
}

/* ---------------------------------------------------------------- text layout */
#define MAXL 48
static int wrap(const char *s, int maxch, int *st, int *ln)
{
    int n = 0, i = 0, len = (int)strlen(s);
    while (i < len && n < MAXL) {
        int cols = 0, j = i, last_sp = -1;
        while (j < len && s[j] != '\n') {
            if ((s[j] & 0xC0) != 0x80) {
                if (cols >= maxch) break;
                cols++;
            }
            if (s[j] == ' ') last_sp = j;
            j++;
            while (j < len && (s[j] & 0xC0) == 0x80) j++;
        }
        int e = j;
        if (j < len && s[j] != '\n' && last_sp > i) e = last_sp;          /* cut at the last space */
        st[n] = i; ln[n] = e - i; n++;
        i = e;
        if (i < len && (s[i] == ' ' || s[i] == '\n')) i++;
        if (i == len && len > 0 && s[len - 1] == '\n') break;
    }
    if (n == 0) { st[0] = 0; ln[0] = 0; n = 1; }
    return n;
}

static void draw_lines(int x, int y, const char *s, const int *st, const int *ln, int n, unsigned c, int sc)
{
    char b[260];
    for (int i = 0; i < n; i++) {
        int l = ln[i] < 255 ? ln[i] : 255;
        memcpy(b, s + st[i], (size_t)l); b[l] = 0;
        text(x, y + i * 20 * sc, b, c, sc);
    }
}

/* ---------------------------------------------------------------- pieces */
static int x0, x1, y0, yd, LB = 56, TOP = 40, RPw, dockH;

static int button(int x, int y, int w, int h, const char *label, int id, int arg, int style)       /* style 0 plain, 1 accent */
{
    rrect(x, y, w, h, 8, style ? T->accent : T->input);
    if (!style) { rrect(x, y, w, 1, 0, T->line); }
    int tw = ulen(label) * 8;
    text(x + (w - tw) / 2, y + (h - 16) / 2, label, style ? T->on_accent : T->text, 1);
    hit(x, y, w, h, id, arg);
    return w;
}

static void input_box(int x, int y, int w, int focused_field, const char *value, const char *placeholder, int send)
{
    int h = 44;
    rrect(x, y, w, h, 22, T->line);
    rrect(x + 1, y + 1, w - 2, h - 2, 21, T->input);
    int maxch = (w - 90) / 8;
    if (value[0]) {
        int n = ulen(value);
        const char *p = value;
        if (n > maxch) { int skip = n - maxch; while (skip > 0) { p++; while ((*p & 0xC0) == 0x80) p++; skip--; } }
        text(x + 20, y + 14, p, T->text, 1);
        if (focused_field && (tick / 5) % 2 == 0) rect(x + 20 + ulen(p) * 8, y + 12, 2, 20, T->accent);
    } else {
        text(x + 20, y + 14, placeholder, T->dim, 1);
        if (focused_field && (tick / 5) % 2 == 0) rect(x + 18, y + 12, 2, 20, T->accent);
    }
    hit(x, y, w, h, focused_field ? B_INPUT : B_TINPUT, 0);
    if (send) {
        circle(x + w - 26, y + h / 2, 15, value[0] ? T->accent : T->line);
        rect(x + w - 33, y + h / 2 - 1, 12, 3, value[0] ? T->on_accent : T->dim);
        rect(x + w - 25, y + h / 2 - 5, 3, 11, value[0] ? T->on_accent : T->dim);       /* arrow */
        rect(x + w - 28, y + h / 2 - 3, 3, 7, value[0] ? T->on_accent : T->dim);
        hit(x + w - 44, y + 4, 40, 36, B_SEND, 0);
    }
}

/* the question of a permission card is stored as  <category digit><text> */
static void draw_perm_card(int x, int y, int w, const char *s)
{
    int cat = s[0] - '0';
    const char *q = s + 1;
    int st[MAXL], ln[MAXL];
    int n = wrap(q, (w - 28) / 8, st, ln);
    int h = n * 20 + 16 + 44;
    rrect(x, y, w, h, 14, T->line);
    rrect(x + 1, y + 1, w - 2, h - 2, 13, T->bub_f);
    rect(x + 14, y + 12, 3, n * 20 - 4, T->accent);
    draw_lines(x + 24, y + 8, q, st, ln, n, T->text, 1);
    int by = y + 12 + n * 20, bx = x + 14;
    bx += button(bx, by, 110, 28, tr("Her zaman", "Always"), B_PERM, cat << 4 | 2, 1) + 8;
    bx += button(bx, by, 150, 28, tr("Sadece Bu Sefer", "Only This Time"), B_PERM, cat << 4 | 1, 0) + 8;
    button(bx, by, 80, 28, tr("Reddet", "Deny"), B_PERM, cat << 4, 0);
}

static int perm_card_h(const char *s, int w)
{
    int st[MAXL], ln[MAXL];
    int n = wrap(s + 1, (w - 28) / 8, st, ln);
    return n * 20 + 16 + 44;
}

/* ---------------------------------------------------------------- chat */
static int mascot_x, mascot_y, mascot_ok;

static void draw_welcome(void)
{
    int cx = (x0 + x1) / 2;
    int cy = y0 + (yd - y0) / 2;
    int bw = x1 - x0 - 80; if (bw > 560) bw = 560;
    int hop = HOP[(tick / 2) % 8];
    frog(cx - 48, cy - 150 - hop, 6, (tick % 40) < 2, (tick / 6) % 3 == 0 ? 0 : 0);
    const char *h1 = tr("Merhaba! Ben Froggy.", "Hello! I am Froggy.");
    text(cx - ulen(h1) * 8, cy - 62, h1, T->text, 2);
    const char *h2 = tr("Kodlama arkadaşınım. Bana bir şey sor ya da bir iş ver.", "Your coding buddy. Ask me something or give me a job.");
    text(cx - ulen(h2) * 4, cy - 26, h2, T->dim, 1);
    input_box(cx - bw / 2, cy + 6, bw, 1, S.inp, tr("Froggy'ye yaz...", "Write to Froggy..."), 1);
    static const char *CH_TR[4] = { "Merhaba", "Dosyalarımı göster", "hello.as örneği ver", "Python nedir?" };
    static const char *CH_EN[4] = { "Hello", "Show my files", "give me hello.as", "What is Python?" };
    int bx = cx - bw / 2, by = cy + 62;
    for (int i = 0; i < 4; i++) {
        const char *l = S.lang_en ? CH_EN[i] : CH_TR[i];
        int w = ulen(l) * 8 + 24;
        if (bx + w > cx + bw / 2) { bx = cx - bw / 2; by += 36; }
        rrect(bx, by, w, 28, 14, T->input); rrect(bx, by, w, 1, 0, T->line);
        text(bx + 12, by + 6, l, T->dim, 1);
        hit(bx, by, w, 28, B_SAMPLE, i);
        bx += w + 8;
    }
}

static void draw_messages(void)
{
    Chat *c = chat_cur();
    int sc = S.big ? 2 : 1;
    int lh = 20 * sc;
    int inY = yd - 10 - 44;
    int top = y0 + 6, bot = inY - 10;
    int areaW = x1 - x0;
    int maxB = areaW - 2 * 20 - 2 * 40;
    if (maxB > 620) maxB = 620;
    int per = (maxB - 28) / (8 * sc);
    int total = 0;
    int hs[MAX_MSGS];
    static int st[MAXL], ln[MAXL];
    for (int i = 0; i < c->n; i++) {
        if (c->m[i].role == R_ASK) hs[i] = perm_card_h(c->m[i].text, maxB);
        else hs[i] = wrap(c->m[i].text, per, st, ln) * lh + 16;
        total += hs[i] + 12;
    }
    int view = bot - top;
    if (S.scroll > total - view) S.scroll = total - view;
    if (S.scroll < 0) S.scroll = 0;
    int y = bot + S.scroll;
    mascot_ok = 0;
    for (int i = c->n - 1; i >= 0; i--) {
        y -= hs[i] + 12;
        int by = y + 6;
        if (by + hs[i] < top - 40 || by > bot + 4) continue;
        Msg *m = &c->m[i];
        int r = m->role;
        int n = r == R_ASK ? 0 : wrap(m->text, per, st, ln);
        int tw = 0;
        for (int k = 0; k < n; k++) { int w = 0; for (int j = st[k]; j < st[k] + ln[k]; j++) if ((m->text[j] & 0xC0) != 0x80) w++; if (w > tw) tw = w; }
        int bw = tw * 8 * sc + 28;
        if (r == R_USER) {
            int ax = x1 - 20 - 32;
            int bx = ax - 10 - bw;
            rrect(bx, by, bw, hs[i], 16, T->bub_u);
            draw_lines(bx + 14, by + 8, m->text, st, ln, n, T->on_bub_u, sc);
            avatar_user(ax, by);
            if (i == c->n - 1) { mascot_x = bx; mascot_y = by; mascot_ok = bw; }
        } else if (r == R_FROG) {
            int ax = x0 + 20;
            int bx = ax + 32 + 10;
            rrect(bx, by, bw, hs[i], 16, T->bub_f);
            draw_lines(bx + 14, by + 8, m->text, st, ln, n, T->text, sc);
            avatar_frog(ax, by);
            if (i == c->n - 1) { mascot_x = bx; mascot_y = by; mascot_ok = bw; }
        } else {
            draw_perm_card(x0 + 20 + 42, by, maxB < areaW - 2 * 62 ? maxB : areaW - 2 * 62, m->text);
            avatar_frog(x0 + 20, by);
            if (i == c->n - 1) { mascot_x = x0 + 62; mascot_y = by; mascot_ok = 200; }
        }
    }
    /* scroll buttons */
    if (S.scroll > 0 || total > view) {
        rrect(x1 - 36, top + 4, 26, 26, 13, T->input); text(x1 - 28, top + 8, "^", T->dim, 1);
        hit(x1 - 36, top + 4, 26, 26, B_SCROLL, 1);
        rrect(x1 - 36, bot - 30, 26, 26, 13, T->input); text(x1 - 28, bot - 26, "v", T->dim, 1);
        hit(x1 - 36, bot - 30, 26, 26, B_SCROLL, -1);
    }
    /* Froggy hops along the top edge of the newest bubble */
    if (mascot_ok && S.scroll < 80) {
        int span = mascot_ok - 52; if (span < 20) span = 20;
        int px = mascot_x + (mx_off % (2 * span) > span ? 2 * span - mx_off % (2 * span) : mx_off % (2 * span));
        int hop = HOP[(tick / 2) % 8];
        int py = mascot_y - 28 - hop;
        if (py > top - 20) frog(px, py, 2 > 0 ? 1 : 1, (tick % 35) < 2, S.thinking ? (tick / 3) % 3 : 0);
    }
    /* the input box at the bottom */
    rect(x0, inY - 10, x1 - x0, 10 + 44 + 10, T->bg);
    input_box(x0 + 20, inY, x1 - x0 - 40, 1, S.inp, tr("Froggy'ye yaz...", "Write to Froggy..."), 1);
}

/* ---------------------------------------------------------------- other views */
static void draw_projects(void)
{
    text(x0 + 24, y0 + 18, tr("Projeler", "Projects"), T->text, 2);
    text(x0 + 24, y0 + 56, tr("Klasöründeki kod dosyaları. Birine tıklarsan Froggy'ye bakmasını söylersin.", "Code files in your folder. Click one to ask Froggy to look at it."), T->dim, 1);
    int y = y0 + 92;
    if (S.nproj == 0) text(x0 + 24, y, tr("Henüz .as / .c / .cpp / .txt dosyası yok. Sohbette \"hello.as örneği ver\" yaz.", "No .as / .c / .cpp / .txt files yet. In the chat say \"give me hello.as\"."), T->dim, 1);
    for (int i = 0; i < S.nproj && y + 44 < yd; i++, y += 50) {
        rrect(x0 + 24, y, x1 - x0 - 48, 42, 10, T->input);
        ic_paper(x0 + 36, y + 8, T->accent, T->input);
        text(x0 + 76, y + 13, S.proj[i], T->text, 1);
        text(x1 - 150, y + 13, tr("Froggy bak ->", "Ask Froggy ->"), T->dim, 1);
        hit(x0 + 24, y, x1 - x0 - 48, 42, B_PROJ, i);
    }
}

static void draw_plugins(void)
{
    text(x0 + 24, y0 + 18, tr("Eklentiler (ASPI)", "Plugins (ASPI)"), T->text, 2);
    text(x0 + 24, y0 + 56, tr("Kurulu paketler. İşaretlediklerini Froggy'ye gönderirsin; o da onlar hakkında konuşabilir.", "Installed packages. Tick the ones to send to Froggy; it can then talk about them."), T->dim, 1);
    int y = y0 + 92, sel = 0;
    if (S.nplug == 0) text(x0 + 24, y, tr("Kurulu paket yok. AST'de: aspi online  ->  aspi install Ad", "No packages installed. In AST: aspi online  ->  aspi install Name"), T->dim, 1);
    for (int i = 0; i < S.nplug && y + 44 < yd; i++, y += 50) {
        rrect(x0 + 24, y, x1 - x0 - 48, 42, 10, T->input);
        rrect(x0 + 38, y + 11, 20, 20, 5, T->line);
        if (S.plug_sel[i]) { rrect(x0 + 40, y + 13, 16, 16, 4, T->accent); sel++; }
        ic_puzzle(x0 + 70, y + 8, T->dim, T->input);
        text(x0 + 108, y + 13, S.plug_name[i], T->text, 1);
        text(x1 - 190, y + 13, S.plug_sel[i] ? tr("Froggy'ye gönderildi", "sent to Froggy") : tr("gönderilmedi", "not sent"), T->dim, 1);
        hit(x0 + 24, y, x1 - x0 - 48, 42, B_PLUG, i);
    }
    char b[80]; snprintf(b, sizeof b, tr("%d eklenti Froggy'ye gönderildi", "%d plugin(s) sent to Froggy"), sel);
    text(x0 + 24, yd - 30, b, T->accent, 1);
}

/* ---------------------------------------------------------------- bars and panels */
static void draw_topbar(void)
{
    rect(x0, 0, x1 - x0, TOP, T->bar);
    rect(x0, TOP - 1, x1 - x0, 1, T->line);
    const char *t = S.view == V_CHAT ? chat_cur()->title : S.view == V_PROJECTS ? tr("Projeler", "Projects") : tr("Eklentiler", "Plugins");
    text_clip(x0 + 20, 12, t, (x1 - x0 - 200) / 8, T->text, 1);
    int x = x1 - 44;
    ic_gear(x + 4, 8, S.settings_open ? T->accent : T->dim, T->bar); hit(x, 0, 44, TOP, B_SETTINGS, 0);
    x -= 44;
    if (RPw == 0 || 1) { ic_clock(x + 8, 8, S.panel ? T->accent : T->dim, T->bar); hit(x, 0, 44, TOP, B_PANEL, 0); }
    x -= 44;
    ic_plus(x + 8, 8, T->dim); hit(x, 0, 44, TOP, B_NEWCHAT, 0);
}

static void draw_leftbar(void)
{
    rect(0, 0, LB, ui_H, T->side);
    rect(LB - 1, 0, 1, ui_H, T->line);
    circle(LB / 2, 24, 15, T->bub_f);
    frog(LB / 2 - 8, 17, 1, (tick % 40) < 2, 0);
    struct { int v; void (*f)(int, int, unsigned, unsigned); } it[3] = { { V_CHAT, ic_chat }, { V_PROJECTS, ic_paper }, { V_PLUGINS, ic_puzzle } };
    for (int i = 0; i < 3; i++) {
        int y = 64 + i * 56;
        if (S.view == it[i].v) { rrect(6, y - 8, LB - 12, 44, 10, T->hover); rect(0, y - 4, 3, 36, T->accent); }
        it[i].f(15, y, S.view == it[i].v ? T->accent : T->dim, S.view == it[i].v ? T->hover : T->side);
        hit(0, y - 8, LB, 44, B_VIEW, it[i].v);
    }
}

static void draw_dock(void)
{
    int w = x1 - x0;
    rect(x0, yd, w, dockH, T->side);
    rect(x0, yd, w, 1, T->line);
    const char *tab[2] = { "Terminal", "Console" };
    int bx = x0 + 14;
    for (int i = 0; i < 2; i++) {
        int tw = (int)strlen(tab[i]) * 8 + 24;
        if (S.dock_tab == i) { rrect(bx, yd + 4, tw, 22, 8, T->hover); rect(bx + 8, yd + 25, tw - 16, 2, T->accent); }
        text(bx + 12, yd + 8, tab[i], S.dock_tab == i ? T->text : T->dim, 1);
        hit(bx, yd + 2, tw, 26, B_TAB, i);
        bx += tw + 6;
    }
    text(x1 - 30, yd + 6, S.dock_open ? "v" : "^", T->dim, 1);
    hit(x1 - 44, yd, 44, 30, B_DOCK, 0);
    if (!S.dock_open) return;
    int cy = yd + 32, ch = dockH - 32;
    int lines = (ch - (S.dock_tab == 0 ? 34 : 6)) / 16;
    if (S.dock_tab == 0) {
        int first = S.nterm > lines ? S.nterm - lines : 0;
        for (int i = first; i < S.nterm; i++) text_clip(x0 + 14, cy + (i - first) * 16, S.term[i], (w - 28) / 8, S.term[i][0] == '$' ? T->accent : T->text, 1);
        int iy = yd + dockH - 30;
        rrect(x0 + 10, iy, w - 20, 24, 8, T->input);
        text(x0 + 20, iy + 4, "$", T->accent, 1);
        int maxch = (w - 60) / 8, n = ulen(S.tinp);
        const char *p = S.tinp;
        if (n > maxch) { int skip = n - maxch; while (skip-- > 0) { p++; while ((*p & 0xC0) == 0x80) p++; } }
        text(x0 + 36, iy + 4, p, T->text, 1);
        if (S.focus == 1 && (tick / 5) % 2 == 0) rect(x0 + 36 + ulen(p) * 8, iy + 3, 2, 18, T->accent);
        hit(x0 + 10, iy, w - 20, 24, B_TINPUT, 0);
    } else {
        int first = S.ncon > lines ? S.ncon - lines : 0;
        for (int i = first; i < S.ncon; i++) {
            unsigned col = S.conk[i] == L_OK ? T->ok : S.conk[i] == L_ERR ? T->err : T->dim;
            text(x0 + 14, cy + (i - first) * 16, S.conk[i] == L_OK ? "+" : S.conk[i] == L_ERR ? "!" : "-", col, 1);
            text_clip(x0 + 30, cy + (i - first) * 16, S.con[i], (w - 48) / 8, col, 1);
        }
    }
}

static void draw_panel(void)
{
    rect(x1, 0, RPw, ui_H, T->side);
    rect(x1, 0, 1, ui_H, T->line);
    text(x1 + 16, 12, tr("Geçmiş", "History"), T->text, 1);
    int y = TOP + 8, shown = 0;
    for (int i = S.nchats - 1; i >= 0 && y + 36 < ui_H; i--) {
        Chat *c = &S.chats[i];
        if (c->n == 0 && i != S.cur) continue;
        if (i == S.cur) rrect(x1 + 8, y, RPw - 16, 32, 8, T->hover);
        text_clip(x1 + 18, y + 8, c->title, (RPw - 36) / 8, i == S.cur ? T->text : T->dim, 1);
        hit(x1 + 8, y, RPw - 16, 32, B_OPENCHAT, i);
        y += 36; shown++;
    }
    if (!shown) text(x1 + 16, y, tr("Henüz sohbet yok", "No chats yet"), T->dim, 1);
}

static void draw_settings(void)
{
    int w = 540, h = 470;
    if (w > ui_W - 20) w = ui_W - 20;
    if (h > ui_H - 20) h = ui_H - 20;
    int x = (ui_W - w) / 2, y = (ui_H - h) / 2;
    rrect(x - 1, y - 1, w + 2, h + 2, 16, T->line);
    rrect(x, y, w, h, 15, T->bg);
    modal_from = nb;
    hit(x, y, w, h, B_NONE, 0);
    text(x + 24, y + 18, tr("Ayarlar", "Settings"), T->text, 2);
    int r = y + 62;
    text(x + 24, r + 6, tr("Tema", "Theme"), T->dim, 1);
    const char *th[3] = { tr("Koyu", "Dark"), tr("Açık", "Light"), tr("Yeşil", "Green") };
    for (int i = 0; i < 3; i++) button(x + 190 + i * 100, r, 92, 28, th[i], B_THEME, i, S.theme == i);
    r += 40;
    text(x + 24, r + 6, tr("Metin boyutu", "Text size"), T->dim, 1);
    button(x + 190, r, 92, 28, tr("Normal", "Normal"), B_SIZE, 0, !S.big);
    button(x + 290, r, 92, 28, tr("Büyük", "Large"), B_SIZE, 1, S.big);
    r += 40;
    text(x + 24, r + 6, tr("Dil", "Language"), T->dim, 1);
    button(x + 190, r, 92, 28, "Türkçe", B_LANG, 0, !S.lang_en);
    button(x + 290, r, 92, 28, "English", B_LANG, 1, S.lang_en);
    r += 46;
    text(x + 24, r, tr("İzinler", "Permissions"), T->text, 1);
    r += 24;
    for (int p = 0; p < P_COUNT; p++, r += 26) {
        text(x + 24, r + 4, brain_perm_name(p), T->text, 1);
        text(x + 250, r + 4, S.always[p] ? tr("her zaman izinli", "always allowed") : tr("her sohbette sorulur", "asked in every chat"), S.always[p] ? T->ok : T->dim, 1);
        if (S.always[p]) button(x + w - 112, r - 2, 88, 24, tr("Kaldır", "Revoke"), B_REVOKE, p, 0);
    }
    r += 8;
    char b[60]; snprintf(b, sizeof b, tr("Bellek (%d kayıt)", "Memory (%d entries)"), S.nmem);
    text(x + 24, r, b, T->text, 1);
    r += 22;
    int room = (y + h - 56 - r) / 22;
    if (S.nmem == 0) text(x + 24, r, tr("Boş. Bilmediğim bir şeyi bana anlatırsan buraya kaydederim.", "Empty. When you explain something I do not know, I save it here."), T->dim, 1);
    for (int i = 0; i < S.nmem && i < room; i++, r += 22) {
        char l[160]; snprintf(l, sizeof l, "%s: %s", S.mem[i].key, S.mem[i].val);
        text_clip(x + 24, r + 3, l, (w - 100) / 8, T->text, 1);
        button(x + w - 56, r - 1, 32, 20, "x", B_MEMDEL, i, 0);
    }
    if (S.nmem > room && room > 0) { snprintf(b, sizeof b, "+%d ...", S.nmem - room); text(x + 24, r + 3, b, T->dim, 1); }
    button(x + w - 124, y + h - 44, 100, 30, tr("Kapat", "Close"), B_CLOSE, 0, 1);
}

/* ---------------------------------------------------------------- the whole picture */
void ui_draw(void)
{
    A->win_size(&ui_W, &ui_H);
    T = &TH[S.theme];
    nb = 0; modal_from = 0;
    RPw = (S.panel && ui_W >= 760) ? 210 : 0;
    dockH = S.dock_open ? (ui_H >= 560 ? 190 : 140) : 30;
    x0 = LB; x1 = ui_W - RPw; y0 = TOP; yd = ui_H - dockH;
    rect(0, 0, ui_W, ui_H, T->bg);
    if (S.view == V_CHAT) { if (chat_cur()->n == 0) draw_welcome(); else draw_messages(); }
    else if (S.view == V_PROJECTS) draw_projects();
    else draw_plugins();
    draw_topbar();
    draw_dock();
    draw_leftbar();
    if (RPw) draw_panel();
    if (S.settings_open) draw_settings();
}
