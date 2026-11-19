/*
 * AST as a graphical application. The shell itself is unchanged: it still prints through the
 * console (which now fills a cell grid) and reads keys with kbd_getkey(). Here it runs as a
 * coroutine, receives only the keys sent to its window, and is killed when the window is closed.
 * Exactly one AST exists: launching it again just restores or raises the existing window.
 */
#include "gui.h"

#define AST_COLS 80
#define AST_ROWS 25
#define KQ 256

extern int aspi_wake;

/*
 * Terminal tabs. Every tab is its own AST session: its own shell coroutine (and stack), screen, folder and history.
 * The screen / folder / history of the visible tab live in the console and shell statics; the other tabs are saved
 * in their slot and are swapped in only for the moment their coroutine runs, so a command in a hidden tab keeps going
 * and writes into its own screen.
 */
#define MAXTAB   6
#define TAB_NAME 13
#define STRIP_H  26
typedef struct {
    int  used, alive, busy;
    char name[TAB_NAME + 1];
    coro co;
    int  keyq[KQ], kh, kt;
    u8   con[CON_STATE_BYTES], ast[AST_STATE_BYTES];
} tab_t;
static tab_t tabs[MAXTAB];
static u8    stacks[MAXTAB][192 * 1024] __attribute__((aligned(16)));
static u8    scr_con[CON_STATE_BYTES], scr_ast[AST_STATE_BYTES];
static int   vis, ntabs;
static int   req_op, req_tab;                  /* a tab command waits here for the next pump (it must not swap its own screen) */
static char  req_name[TAB_NAME + 1];
static char  pend[200];
static int   pend_on;
static u32   pend_t;
static int   blink_on = 1;
static u32   blink_t;
static int   bg_on;

static int tab_alive(int t) { return t == MAXTAB ? bg_on : (t >= 0 && t < MAXTAB && tabs[t].used && tabs[t].alive); }

static int ast_key_pop(void)
{
    int t = ast_cur_tab;
    if (t < 0 || t >= MAXTAB) return -1;
    tab_t *tb = &tabs[t];
    if (tb->kh == tb->kt) return -1;
    int k = tb->keyq[tb->kh];
    tb->kh = (tb->kh + 1) % KQ;
    return k;
}

static void shell_entry(void) { ast_session(); }

void app_ast_init(void)
{
    kbd_source = ast_key_pop;
    kbd_idle = coro_yield;
    ntabs = 0; vis = 0; req_op = 0;
    for (int i = 0; i < MAXTAB; i++) { tabs[i].used = tabs[i].alive = 0; tabs[i].kh = tabs[i].kt = 0; }
    ast_set_tab_alive(tab_alive);
}

int app_ast_running(void) { return ntabs > 0; }

/* Keys go to the visible tab only while AST's window is visible and focused. */
int app_ast_push_key(int key)
{
    window *w = wm_find_app(APP_AST);
    if (!ntabs || !w || w->state == WS_MINIMIZED || wm_focused() != w) return 0;
    tab_t *tb = &tabs[vis];
    int n = (tb->kt + 1) % KQ;
    if (n == tb->kh) return 0;
    tb->keyq[tb->kt] = key;
    tb->kt = n;
    blink_on = 1; blink_t = ui_ms();
    return 1;
}

/*
 * Background jobs: a command that needs no terminal window (starting an installed app, installing from the store)
 * runs on its own coroutine. Its output is captured while it runs and never reaches the terminal. One job at a
 * time; it waits while the terminal is busy with a command of its own.
 */
static u8   bg_stack[192 * 1024] __attribute__((aligned(16)));
static coro bg_co;
static int  bg_started;
static char bg_cmd[200], bg_out[3000];
static u32  bg_len, bg_serial, bg_t;

static void bg_entry(void) { if (ast_bg_exec(bg_cmd) < 0) ksnprintf(bg_out, sizeof bg_out, "Sign in first."); }

int app_ast_bg(const char *cmd)
{
    size_t n = kstrlen(cmd);
    if (bg_on || n == 0 || n >= sizeof bg_cmd) return 0;
    kmemcpy(bg_cmd, cmd, n + 1);
    bg_on = 1; bg_started = 0; bg_len = 0; bg_out[0] = 0; bg_t = ui_ms();
    return 1;
}
int app_ast_bg_busy(void) { return bg_on; }
u32 app_ast_bg_serial(void) { return bg_serial; }
const char *app_ast_bg_output(void) { return bg_out; }

static int terminal_busy(void)
{
    int typed;
    if (!ntabs) return 0;
    if (tabs[vis].co.started && !ast_prompt_state(&typed)) return 1;
    for (int t = 0; t < MAXTAB; t++) if (tabs[t].used && tabs[t].alive && tabs[t].busy) return 1;
    return 0;
}

static void bg_pump(void)
{
    if (!bg_on) return;
    if (!bg_started) {
        if (terminal_busy() && ui_ms() - bg_t < 60000u) return;      /* a terminal is running something: wait */
        coro_init(&bg_co, bg_entry, bg_stack, sizeof bg_stack);
        bg_started = 1; bg_t = ui_ms();
    }
    int prev = ast_cur_tab;
    ast_cur_tab = MAXTAB;                                            /* the background job is its own "tab" for the engine lock */
    con_capture_resume(bg_out, sizeof bg_out, bg_len);
    coro_resume(&bg_co);
    bg_len = con_capture_end();
    ast_cur_tab = prev;
    if (bg_co.done || ui_ms() - bg_t > 900000u) { bg_on = 0; bg_serial++; ast_engine_release(MAXTAB); }
}

/* ---- layout ---- */
static int strip_h(void) { return ntabs >= 2 ? STRIP_H : 0; }

static void resize_live(const window *w)
{
    gfx_rect cr = wm_content_rect(w);
    con_gui_resize((cr.w - 2 * WM_PAD) / 8, (cr.h - 2 * WM_PAD - strip_h()) / 16);
}

/* every tab gets the new size (the visible one is live, the others are swapped in for a moment) */
static void relayout(const window *w)
{
    gfx_rect cr = wm_content_rect(w);
    int cols = (cr.w - 2 * WM_PAD) / 8, rows = (cr.h - 2 * WM_PAD - strip_h()) / 16;
    con_gui_resize(cols, rows);
    for (int t = 0; t < MAXTAB; t++) {
        if (!tabs[t].used || t == vis) continue;
        con_state_save(scr_con);
        con_state_load(tabs[t].con);
        con_gui_resize(cols, rows);
        con_state_save(tabs[t].con);
        con_state_load(scr_con);
    }
    con_gui_dirty_all();
    wm_damage(cr);
}

/* ---- running a tab ---- */
static void run_tab(int t)
{
    tab_t *tb = &tabs[t];
    int prev = ast_cur_tab, typed;
    if (t != vis) {                                    /* swap the hidden tab's screen and shell state in */
        con_state_save(scr_con); ast_state_save(scr_ast);
        con_state_load(tb->con); ast_state_load(tb->ast);
    }
    ast_cur_tab = t;
    coro_resume(&tb->co);
    tb->busy = tb->co.started && !tb->co.done && !ast_prompt_state(&typed);
    if (t != vis) {
        con_state_save(tb->con); ast_state_save(tb->ast);
        con_state_load(scr_con); ast_state_load(scr_ast);
    }
    ast_cur_tab = prev;
    if (tb->co.done) { tb->alive = 0; ast_engine_release(t); }
}

static void switch_to(int n)
{
    if (n == vis || n < 0 || n >= MAXTAB || !tabs[n].used) return;
    con_state_save(tabs[vis].con); ast_state_save(tabs[vis].ast);
    con_state_load(tabs[n].con); ast_state_load(tabs[n].ast);
    vis = n;
    con_gui_dirty_all();
    blink_on = 1; blink_t = ui_ms();
    window *w = wm_find_app(APP_AST);
    if (w) wm_damage(wm_content_rect(w));
}

static int find_tab(const char *name)
{
    for (int t = 0; t < MAXTAB; t++) if (tabs[t].used && !kstrcmp(tabs[t].name, name)) return t;
    if (name[0] >= '1' && name[0] <= '9' && !name[1]) {                         /* "as tab 2": the 2nd tab in order */
        int k = name[0] - '0';
        for (int t = 0; t < MAXTAB; t++) if (tabs[t].used && --k == 0) return t;
    }
    return -1;
}

static int add_tab(const char *name)
{
    window *w = wm_find_app(APP_AST);
    int slot = -1;
    for (int t = 0; t < MAXTAB; t++) if (!tabs[t].used) { slot = t; break; }
    if (slot < 0 || !w) return -1;
    tab_t *tb = &tabs[slot];
    kmemset(tb->keyq, 0, sizeof tb->keyq);
    tb->kh = tb->kt = 0;
    ksnprintf(tb->name, sizeof tb->name, "%s", name);
    int cols, rows;
    con_gui_size(&cols, &rows);
    con_state_blank(tb->con, cols, rows);
    ast_state_blank(tb->ast);
    coro_init(&tb->co, shell_entry, stacks[slot], sizeof stacks[slot]);
    tb->used = tb->alive = 1; tb->busy = 0;
    ntabs++;
    relayout(w);                                        /* the strip appears with the second tab */
    switch_to(slot);
    return slot;
}

static void remove_tab(int t)
{
    window *w = wm_find_app(APP_AST);
    if (t < 0 || t >= MAXTAB || !tabs[t].used || ntabs <= 1) return;
    if (t == vis) {                                     /* show a neighbour first */
        int n = -1;
        for (int k = 1; k < MAXTAB && n < 0; k++) { int c = (t + MAXTAB - k) % MAXTAB; if (tabs[c].used) n = c; }
        switch_to(n);
    }
    tabs[t].used = tabs[t].alive = 0; tabs[t].busy = 0;  /* the shell's stack is simply abandoned */
    ast_engine_release(t);
    ntabs--;
    if (w) relayout(w);
}

/* `as tab ...` (terminal/ast.c): op 0 = list, 1 = add, 2 = remove, 3 = switch. Returns 0 and fills msg. Done on the next pump. */
int ast_tab_op(int op, const char *name, char *msg, int cap)
{
    if (!ntabs) { ksnprintf(msg, (size_t)cap, "Tabs need the graphical terminal window."); return -1; }
    if (op == 0) {
        int n = 0;
        for (int t = 0; t < MAXTAB; t++) if (tabs[t].used)
            n += ksnprintf(msg + n, (size_t)(cap - n), "%s%s%s\n", t == ast_cur_tab ? "* " : "  ", tabs[t].name, tabs[t].busy ? "  (running)" : "");
        return 0;
    }
    if (req_op) { ksnprintf(msg, (size_t)cap, "Another tab command is still being carried out."); return -1; }
    char nm[TAB_NAME + 1] = "";
    if (name && *name) {
        if (kstrlen(name) > TAB_NAME - 1) { ksnprintf(msg, (size_t)cap, "Tab names are at most %d characters.", TAB_NAME - 1); return -1; }
        for (const char *q = name; *q; q++)
            if (!((*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z') || (*q >= '0' && *q <= '9') || *q == '_' || *q == '-')) {
                ksnprintf(msg, (size_t)cap, "Tab names use letters, digits, - and _."); return -1;
            }
        kstrncpy(nm, name, TAB_NAME);
    }
    if (op == 1) {
        if (ntabs >= MAXTAB) { ksnprintf(msg, (size_t)cap, "At most %d tabs.", MAXTAB); return -1; }
        if (!nm[0]) for (int k = 1; k <= MAXTAB + 1; k++) { ksnprintf(nm, sizeof nm, "Tab%d", k); if (find_tab(nm) < 0) break; }
        if (find_tab(nm) >= 0) { ksnprintf(msg, (size_t)cap, "A tab named '%s' exists.", nm); return -1; }
        req_op = 1; kstrncpy(req_name, nm, TAB_NAME);
        ksnprintf(msg, (size_t)cap, "Tab '%s' opens now.", nm);
        return 0;
    }
    int t = nm[0] ? find_tab(nm) : ast_cur_tab;
    if (t < 0 || t >= MAXTAB || !tabs[t].used) { ksnprintf(msg, (size_t)cap, "No tab '%s' (as tab lists them).", nm); return -1; }
    if (op == 2) {
        if (ntabs <= 1) { ksnprintf(msg, (size_t)cap, "This is the only tab - close the window instead."); return -1; }
        req_op = 2; req_tab = t;
        ksnprintf(msg, (size_t)cap, "Tab '%s' closes now.", tabs[t].name);
        return 0;
    }
    req_op = 3; req_tab = t;
    ksnprintf(msg, (size_t)cap, "Switching to '%s'.", tabs[t].name);
    return 0;
}

static void do_request(void)
{
    int op = req_op;
    if (!op) return;
    req_op = 0;
    if (op == 1) add_tab(req_name);
    else if (op == 2) remove_tab(req_tab);
    else if (op == 3) switch_to(req_tab);
}

void app_ast_pump(void)
{
    bg_pump();
    if (!ntabs) return;
    do_request();
    int wake = app_script_wake() || aspi_wake;
    for (int t = 0; t < MAXTAB; t++) {
        tab_t *tb = &tabs[t];
        if (!tb->used || !tb->alive) continue;
        if (!tb->co.started || tb->kh != tb->kt || (wake && (t == vis || tb->busy))) run_tab(t);
    }
}

static gfx_rect cell_rect(const window *w, int c0, int r0, int c1, int r1)
{
    gfx_rect cr = wm_content_rect(w);
    return gfx_rect_make(cr.x + WM_PAD + c0 * 8, cr.y + WM_PAD + strip_h() + r0 * 16, (c1 - c0 + 1) * 8, (r1 - r0 + 1) * 16);
}

/* Terminal changes (and the blinking caret) become damage of the window. */
void app_ast_pump_damage(void)
{
    window *w = wm_find_app(APP_AST);
    int r0, r1;
    if (!w || w->state == WS_MINIMIZED) { (void)con_gui_take_dirty(&r0, &r1); return; }
    u32 now = ui_ms();
    if (now - blink_t >= 530) {
        blink_t = now; blink_on = !blink_on;
        int cx, cy;
        con_gui_cursor(&cx, &cy);
        wm_damage(cell_rect(w, cx, cy, cx, cy));
    }
    if (con_gui_take_dirty(&r0, &r1)) {
        gfx_rect cr = wm_content_rect(w);
        wm_damage(gfx_rect_make(cr.x, cr.y + WM_PAD + strip_h() + r0 * 16, cr.w, (r1 - r0 + 1) * 16));
    }
}

void app_ast_launch(void)
{
    window *w = wm_find_app(APP_AST);
    if (w) {                                              /* already running: never a second AST */
        if (w->state == WS_MINIMIZED) wm_restore(w);
        else wm_raise(w);
        return;
    }
    int sw, sh;
    ui_resolution(&sw, &sh);
    gfx_rect work = ui_work_rect();
    int avail_h = work.y + work.h;
    int cols = AST_COLS, rows = AST_ROWS;
    while (cols * 8 + 2 * WM_PAD + 2 * WM_BORDER > sw - 16 && cols > 40) cols--;
    while (rows * 16 + 2 * WM_PAD + WM_TITLE_H + WM_BORDER > avail_h - 16 && rows > 8) rows--;
    int ow = cols * 8 + 2 * WM_PAD + 2 * WM_BORDER;
    int oh = rows * 16 + 2 * WM_PAD + WM_TITLE_H + WM_BORDER;
    w = wm_create("AST - AstraSage Terminal", APP_AST, (sw - ow) / 2, (avail_h - oh) / 3, ow, oh);
    if (!w) return;
    gfx_rect cr = wm_content_rect(w);
    cols = (cr.w - 2 * WM_PAD) / 8; rows = (cr.h - 2 * WM_PAD) / 16;
    con_gui_enable(cols, rows);
    for (int i = 0; i < MAXTAB; i++) { tabs[i].used = tabs[i].alive = tabs[i].busy = 0; tabs[i].kh = tabs[i].kt = 0; }
    req_op = 0; vis = 0; ntabs = 1;
    ksnprintf(tabs[0].name, sizeof tabs[0].name, "Tab1");
    tabs[0].used = tabs[0].alive = 1;
    ast_cur_tab = 0;
    blink_on = 1; blink_t = ui_ms();
    coro_init(&tabs[0].co, shell_entry, stacks[0], sizeof stacks[0]);
    wm_damage(gfx_rect_make(w->r.x - 20, w->r.y - 20, w->r.w + 40, w->r.h + 44));
}

/* ---- window manager callbacks ---- */

/* ---- the tab strip (only with two or more tabs) ---- */
static int tab_x(int t, int *wd)
{
    int x = 0;
    for (int k = 0; k < MAXTAB; k++) {
        if (!tabs[k].used) continue;
        int tw = gfx_text_w(tabs[k].name) + 44;
        if (tw < 84) tw = 84;
        if (k == t) { *wd = tw; return x; }
        x += tw + 4;
    }
    *wd = 0;
    return x;
}

static u32 tab_col(int custom, u32 dflt) { return custom >= 0 ? COL(custom) : dflt; }

static void draw_strip(gfx_rect cr, gfx_rect clip)
{
    if (strip_h() == 0) return;
    gfx_rect sr = gfx_rect_make(cr.x, cr.y, cr.w, STRIP_H);
    if (gfx_rect_isect(sr, clip).h <= 0) return;
    const as_settings *cs = config_get();
    const ui_theme *th = ui_th();
    gfx_fill_rect(cr.x, cr.y, cr.w, STRIP_H, th->term_bg);
    for (int t = 0; t < MAXTAB; t++) {
        if (!tabs[t].used) continue;
        int wd, x = cr.x + WM_PAD + tab_x(t, &wd);
        int on = t == vis;
        u32 bgc = on ? tab_col(cs->tab_color, th->accent) : th->dim;
        gfx_round_rect_ex(x, cr.y + 3, wd, STRIP_H - 3, 7, 7, 0, 0, on ? bgc : (bgc & 0x00FFFFFF) | 0x66000000);
        gfx_text(x + 10, cr.y + 6, tabs[t].name, on ? th->term_cursor_txt : th->txt);
        gfx_text(x + wd - 18, cr.y + 6, "x", on ? th->term_cursor_txt : th->txt);
        if (tabs[t].busy) gfx_fill_rect(x + 3, cr.y + 12, 3, 3, on ? th->term_cursor_txt : th->accent);
    }
    gfx_fill_rect(cr.x, cr.y + STRIP_H - 2, cr.w, 2, tab_col(cs->line_color, th->accent));
}

/* ---- pictures in the terminal (neofetch logo): rendered once at full quality, then blitted over their cells ---- */
#define PIC 192
extern const u8 asset_boot_logo_start[], asset_boot_logo_end[];
static u32 pic_px[PIC * PIC], pic_src[128 * 128];
static gfx_surf pic_s;
static u32 pic_serial = 0xFFFFFFFFu, pic_ok;
static int pic_stack[PIC * PIC];

static void pic_scale(int w, int h)                              /* pic_src (w x h, ARGB) -> pic_px, bilinear, keeps the proportions */
{
    int dw = w >= h ? PIC : w * PIC / h, dh = h >= w ? PIC : h * PIC / w;
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    int ox = (PIC - dw) / 2, oy = (PIC - dh) / 2;
    for (int y = 0; y < dh; y++) {
        u32 fy = ((u32)(2 * y + 1) * (u32)h * 256u / (2u * (u32)dh));  /* source position in 1/256 pixels */
        fy = fy > 128 ? fy - 128 : 0;
        int y0 = (int)(fy >> 8), y1 = y0 + 1 < h ? y0 + 1 : y0; u32 wy = fy & 255;
        for (int x = 0; x < dw; x++) {
            u32 fx = ((u32)(2 * x + 1) * (u32)w * 256u / (2u * (u32)dw));
            fx = fx > 128 ? fx - 128 : 0;
            int x0 = (int)(fx >> 8), x1 = x0 + 1 < w ? x0 + 1 : x0; u32 wx = fx & 255;
            u32 c00 = pic_src[y0 * w + x0], c10 = pic_src[y0 * w + x1], c01 = pic_src[y1 * w + x0], c11 = pic_src[y1 * w + x1];
            u32 out = 0;
            for (int sh = 0; sh < 32; sh += 8) {
                u32 top = ((c00 >> sh & 255) * (256 - wx) + (c10 >> sh & 255) * wx) >> 8;
                u32 bot = ((c01 >> sh & 255) * (256 - wx) + (c11 >> sh & 255) * wx) >> 8;
                out |= (((top * (256 - wy) + bot * wy) >> 8) & 255) << sh;
            }
            pic_px[(oy + y) * PIC + ox + x] = out;
        }
    }
}

static int dark(u32 c) { return (c >> 16 & 255) < 40 && (c >> 8 & 255) < 40 && (c & 255) < 40; }

static void pic_build(int src, const u8 *data, u32 len)
{
    pic_ok = 0;
    kmemset(pic_px, 0, sizeof pic_px);
    pic_s.w = pic_s.h = pic_s.stride = PIC; pic_s.px = pic_px;
    if (src == 1) {
        int w = 0, h = 0;
        if (png_decode_small(data, len, &w, &h, pic_src, 128 * 128) < 0 || w < 1 || h < 1) return;
        pic_scale(w, h);
        pic_ok = 1;
        return;
    }
    bmp_view v;
    if (bmp_open(asset_boot_logo_start, (u32)(asset_boot_logo_end - asset_boot_logo_start), &v) != BMP_OK) return;
    for (int i = 0; i < PIC * PIC; i++) pic_px[i] = 0xFF000000u;
    gfx_draw_bmp_scaled(&v, &pic_s, 0, 0, PIC, PIC, 0);
    int sp = 0;                                                       /* the black around the logo becomes transparent */
    for (int i = 0; i < PIC; i++) { int e[4] = { i, (PIC - 1) * PIC + i, i * PIC, i * PIC + PIC - 1 }; for (int k = 0; k < 4; k++) if (dark(pic_px[e[k]])) pic_stack[sp++] = e[k]; }
    while (sp > 0) {
        int p = pic_stack[--sp];
        if (!(pic_px[p] >> 24) || !dark(pic_px[p])) continue;
        pic_px[p] = 0;
        int x = p % PIC, y = p / PIC;
        if (x > 0 && sp < PIC * PIC - 4) pic_stack[sp++] = p - 1;
        if (x < PIC - 1 && sp < PIC * PIC - 4) pic_stack[sp++] = p + 1;
        if (y > 0 && sp < PIC * PIC - 4) pic_stack[sp++] = p - PIC;
        if (y < PIC - 1 && sp < PIC * PIC - 4) pic_stack[sp++] = p + PIC;
    }
    for (int y = 1; y < PIC - 1; y++)                                 /* soft edge: dark pixels touching the cut-out fade out */
        for (int x = 1; x < PIC - 1; x++) {
            u32 c = pic_px[y * PIC + x];
            if (!(c >> 24)) continue;
            if (!(pic_px[y * PIC + x - 1] >> 24) || !(pic_px[y * PIC + x + 1] >> 24) || !(pic_px[(y - 1) * PIC + x] >> 24) || !(pic_px[(y + 1) * PIC + x] >> 24)) {
                u32 m = c >> 16 & 255; if ((c >> 8 & 255) > m) m = c >> 8 & 255; if ((c & 255) > m) m = c & 255;
                u32 a = m * 4 > 255 ? 255 : m * 4;
                if (a < 255) pic_px[y * PIC + x] = (a << 24) | (c & 0xFFFFFF);
            }
        }
    pic_ok = 1;
}

static void draw_pictures(gfx_rect clip, int ox, int oy, int cols, int rows)
{
    int n = con_image_count();
    if (!n) return;
    int src; u32 len, serial;
    const u8 *data = con_image_source(&src, &len, &serial);
    if (serial != pic_serial) { pic_build(src, data, len); pic_serial = serial; }
    if (!pic_ok) return;
    gfx_rect keep = gfx_get_clip();
    gfx_rect area = gfx_rect_isect(clip, gfx_rect_make(ox, oy, cols * 8, rows * 16));
    gfx_set_clip(&area);
    for (int i = 0; i < n; i++) {
        int r, c, pc, pr;
        con_image_get(i, &r, &c, &pc, &pr);
        gfx_blit(&pic_s, ox + c * 8 + (pc * 8 - PIC) / 2, oy + r * 16 + (pr * 16 - PIC) / 2);
    }
    gfx_set_clip(&keep);
}

static void ast_draw(window *w, gfx_rect cr, gfx_rect clip, int focused)
{
    (void)w;
    int cols, rows;
    con_gui_size(&cols, &rows);
    int ox = cr.x + WM_PAD, oy = cr.y + WM_PAD + strip_h();
    draw_strip(cr, clip);
    int r0 = clip.y <= oy ? 0 : (clip.y - oy) / 16, r1 = (clip.y + clip.h - 1 - oy) / 16;
    int c0 = clip.x <= ox ? 0 : (clip.x - ox) / 8, c1 = (clip.x + clip.w - 1 - ox) / 8;
    if (clip.y + clip.h - 1 < oy || clip.x + clip.w - 1 < ox) return;
    if (r1 >= rows) r1 = rows - 1;
    if (c1 >= cols) c1 = cols - 1;
    for (int r = r0; r <= r1; r++)
        for (int c = c0; c <= c1; c++) {
            u8 ch; u32 fg, bg;
            con_gui_cell(c, r, &ch, &fg, &bg);
            { u32 cpv; con_gui_cell_cp(c, r, &cpv); if (cpv) gfx_glyph_cp(ox + c * 8, oy + r * 16, cpv, ui_term_fg(fg, bg), ui_term_bg(bg)); else gfx_glyph(ox + c * 8, oy + r * 16, ch, ui_term_fg(fg, bg), ui_term_bg(bg)); }
        }
    draw_pictures(clip, ox, oy, cols, rows);
    int cx, cy;
    con_gui_cursor(&cx, &cy);
    if (cx >= c0 && cx <= c1 && cy >= r0 && cy <= r1) {
        u8 ch; u32 fg, bg;
        con_gui_cell(cx, cy, &ch, &fg, &bg);
        if (focused) {
            if (blink_on) gfx_glyph(ox + cx * 8, oy + cy * 16, ch, ui_th()->term_cursor_txt, ui_th()->term_cursor);
        } else {
            gfx_rect_outline(ox + cx * 8, oy + cy * 16, 8, 16, ui_th()->dim);
        }
    }
}

static void ast_resized(window *w, gfx_rect cr)
{
    relayout(w);
    wm_damage(cr);
}

static void ast_closed(window *w)
{
    (void)w;
    sgui_abort();
    for (int i = 0; i < MAXTAB; i++) { tabs[i].used = tabs[i].alive = tabs[i].busy = 0; tabs[i].kh = tabs[i].kt = 0; }
    ntabs = 0; vis = 0; req_op = 0;                       /* the shells' stacks are simply abandoned */
    ast_engine_release(0); for (int i = 1; i < MAXTAB; i++) ast_engine_release(i);
    /* the login is kept (saved on the drive): only `as logout` signs out */
    int a, b;
    (void)con_gui_take_dirty(&a, &b);
}

static void ast_clicked(window *w, int x, int y)
{
    blink_on = 1; blink_t = ui_ms();
    if (strip_h() == 0) return;
    gfx_rect cr = wm_content_rect(w);
    if (y < cr.y || y >= cr.y + STRIP_H) return;
    for (int t = 0; t < MAXTAB; t++) {
        if (!tabs[t].used) continue;
        int wd, tx = cr.x + WM_PAD + tab_x(t, &wd);
        if (x < tx || x >= tx + wd) continue;
        if (x >= tx + wd - 24) remove_tab(t); else switch_to(t);
        return;
    }
}

static void ast_key(window *w, int key) { (void)w; app_ast_push_key(key); }

const wm_app app_ast_ops = { ast_draw, ast_resized, ast_closed, ast_clicked, 0, ast_key, 0 };

/*
 * The File Explorer (and anything else on the desktop) asks AST to run one command, e.g. "as edit x.as".
 * AST is opened or restored first. The command is typed exactly like a user would: whatever is already
 * on the prompt line is erased with backspaces, then the text and Enter go through the normal key queue,
 * so every AST rule (login, guards, aliases) applies. It waits at most 4 s for the prompt.
 */
int app_ast_run(const char *cmd)
{
    size_t n = kstrlen(cmd);
    if (n == 0 || n >= sizeof pend) return 0;
    if (pend_on) return 0;                               /* one at a time */
    kmemcpy(pend, cmd, n + 1);
    pend_on = 1; pend_t = ui_ms();
    app_ast_launch();                                    /* opens, restores or raises */
    return 1;
}

void app_ast_pending_pump(void)
{
    if (!pend_on) return;
    window *w = wm_find_app(APP_AST);
    if (!ntabs || !w) { pend_on = 0; return; }
    if (w->state == WS_MINIMIZED) wm_restore(w);
    if (wm_focused() != w) wm_raise(w);
    int typed = 0;
    if (ntabs && tabs[vis].co.started && ast_prompt_state(&typed)) {
        int need = typed + (int)kstrlen(pend) + 1;
        if (need >= KQ - ((tabs[vis].kt - tabs[vis].kh + KQ) % KQ)) { pend_on = 0; return; }
        for (int i = 0; i < typed; i++) app_ast_push_key('\b');
        for (const char *p = pend; *p; p++) app_ast_push_key((unsigned char)*p);
        app_ast_push_key('\n');
        pend_on = 0;
        return;
    }
    if (ui_ms() - pend_t > 4000) {                       /* AST is busy with something else: give up quietly */
        pend_on = 0;
        ui_note("AST is busy - the command was not sent");
    }
}
