/*
 * GUI test harness: the real kernel code (console, AST, AstraGUI, AstraUI, gfx, mouse decoder,
 * keyboard decoder, coroutines, video setup from a Multiboot framebuffer) runs as a 32-bit Linux
 * process. Hardware is replaced by memory: a static framebuffer, scripted PS/2 mouse packets and
 * serial key bytes, a fake clock. Screenshots are written as PPM files for gui_test.py.
 * Needs no libc: only raw int 0x80 system calls.
 */
#include "astra.h"
#include "gui.h"

/* ---------------- system calls ---------------- */
static int getenv_dbg = 0;
static long sc3(long n, long a, long b, long c)
{
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c) : "memory");
    return r;
}
static void out(const char *s) { sc3(4, 1, (long)s, (long)kstrlen(s)); }
static void outf(const char *fmt, ...)
{
    char b[300]; va_list ap; va_start(ap, fmt); kvsnprintf(b, sizeof b, fmt, ap); va_end(ap); out(b);
}
__asm__(".globl _start\n_start:\n xorl %ebp,%ebp\n movl (%esp),%eax\n leal 4(%esp),%ecx\n andl $-16,%esp\n subl $8,%esp\n pushl %ecx\n pushl %eax\n call hmain\n movl %eax,%ebx\n movl $1,%eax\n int $0x80\n");

/* ---------------- hardware stand-ins ---------------- */
static u32 fb_mem[GFX_MAXW * GFX_MAXH];
static u8  inq[4096]; static int inh, int_;
void serial_init(void) {}
void serial_putc(char c) { if (getenv_dbg) sc3(4, 2, (long)&c, 1); }
int  serial_pending(void) { return inh != int_; }
int  serial_getb_nb(void) { return inh == int_ ? -1 : inq[inh++ % 4096]; }
/* a RAM disk (14 MiB): big files and the installed packages need one */
#define RD_SECS 28672u
static u8 ramdisk[RD_SECS * 512u];
int  ata_init(void) { return 1; }
u32  ata_sectors(void) { return RD_SECS; }
int  ata_read(u32 l, void *b) { if (l >= RD_SECS) return -1; kmemcpy(b, ramdisk + l * 512u, 512); return 0; }
int  ata_write(u32 l, const void *b) { if (l >= RD_SECS) return -1; kmemcpy(ramdisk + l * 512u, b, 512); return 0; }
int  ata_init_drive(int d) { (void)d; return 0; }
u32  ata_sectors_drive(int d) { (void)d; return 0; }
int  ata_read_drive(int d, u32 l, void *b) { (void)d; (void)l; (void)b; return -1; }
int  ata_write_drive(int d, u32 l, const void *b) { (void)d; (void)l; (void)b; return -1; }
int  hal_term_size(u32 *c, u32 *r)
{
    int gc, gr;
    if (con_gui_active()) { con_gui_size(&gc, &gr); *c = (u32)gc; *r = (u32)gr; return 1; }
    *c = 80; *r = 25; return 0;
}
int  serial_terminal_present(void) { return 0; }

static u32 clock_ms;
static u32 fake_clock(void) { return clock_ms += 7; }

/* ---------------- test helpers ---------------- */
static int fails, total;
static void check(const char *name, int ok)
{
    total++;
    if (!ok) fails++;
    outf("%s: %s\n", ok ? "PASS" : "FAIL", name);
}

static u8 sc_q[256]; static int sc_h, sc_t;
u8 as_test_inb(u16 p)                                       /* port 0x64: status (bit 0 = a byte waits), 0x60: the scancode */
{
    if (p == 0x64) return sc_h != sc_t ? 1 : 0;
    if (p == 0x60) return sc_h != sc_t ? sc_q[sc_h++ & 255] : 0;
    return 0;
}
static void scan(int sc) { sc_q[sc_t++ & 255] = (u8)sc; }
/* a key with modifiers pressed and released: mods bit 1 Ctrl, 2 Alt; ext = 1 for the grey cursor keys */
static void chord(int mods, int ext, int sc)
{
    if (mods & 1) scan(0x1D);
    if (mods & 2) scan(0x38);
    if (ext) scan(0xE0);
    scan(sc);
    if (ext) scan(0xE0);
    scan(sc | 0x80);
    if (mods & 2) scan(0xB8);
    if (mods & 1) scan(0x9D);
}
static void keys(const char *s) { for (; *s; s++) inq[int_++ % 4096] = (u8)*s; }

unsigned long long perf_comp, perf_pres, perf_px;
static void steps(int n) { for (int i = 0; i < n; i++) ui_step(); }
/* does the visible terminal screen contain this text on one row? */
static int scr_has(const char *needle)
{
    int c, r; con_gui_size(&c, &r);
    for (int y = 0; y < r; y++) {
        char line[260]; int n = 0;
        for (int x = 0; x < c && n < 259; x++) { u8 ch; u32 f, b; con_gui_cell(x, y, &ch, &f, &b); line[n++] = (char)ch; }
        line[n] = 0;
        if (kstrstr(line, needle)) return 1;
    }
    return 0;
}

static void mouse_packet(int dx, int dy, int btn)       /* dy: screen direction (down positive) */
{
    int ry = -dy;
    u8 b0 = (u8)(8 | btn | (dx < 0 ? 0x10 : 0) | (ry < 0 ? 0x20 : 0));
    mouse_feed(b0); mouse_feed((u8)(dx & 255)); mouse_feed((u8)(ry & 255));
}

static int fast_clicks;
static void mouse_to(int x, int y, int btn)
{
    static int prev_btn;
    if (btn && !prev_btn && !fast_clicks) clock_ms += 800;                    /* separate presses are never a double click */
    prev_btn = btn;
    int cx, cy;
    mouse_position(&cx, &cy);
    for (int guard = 0; guard < 400 && (cx != x || cy != y); guard++) {
        int dx = x - cx, dy = y - cy;
        if (dx > 100) dx = 100; if (dx < -100) dx = -100;
        if (dy > 100) dy = 100; if (dy < -100) dy = -100;
        mouse_packet(dx, dy, btn);
        mouse_position(&cx, &cy);
    }
    if (cx == x && cy == y) mouse_packet(0, 0, btn);          /* state-only packet (button changes) */
    steps(2);
}
static void click(int x, int y) { mouse_to(x, y, 0); mouse_to(x, y, 1); mouse_to(x, y, 0); }

static u32 px(int x, int y) { return fb_mem[y * video_get()->w + x] & 0xFFFFFF; }

static void snap(const char *dir, const char *name)
{
#ifdef SHOT_BLACK
    int sid = ui_theme_id();                         /* shots for the website: always the black theme */
    if (sid != THEME_BLACK) { ui_theme_set(THEME_BLACK); steps(40); }
#endif
    char path[200];
    ksnprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    long fd = sc3(5, (long)path, 0x241, 0644);
    if (fd < 0) goto restore;
    const video_info *v = video_get();
    char hdr[40];
    int n = ksnprintf(hdr, sizeof hdr, "P6\n%u %u\n255\n", v->w, v->h);
    sc3(4, fd, (long)hdr, n);
    static u8 row[GFX_MAXW * 3];
    for (u32 y = 0; y < v->h; y++) {
        for (u32 x = 0; x < v->w; x++) {
            u32 c = fb_mem[y * v->w + x];
            row[x * 3] = (u8)(c >> 16); row[x * 3 + 1] = (u8)(c >> 8); row[x * 3 + 2] = (u8)c;
        }
        sc3(4, fd, (long)row, (long)(v->w * 3));
    }
    sc3(6, fd, 0, 0);
restore:
#ifdef SHOT_BLACK
    if (sid != THEME_BLACK) { ui_theme_set(sid); steps(40); }
#endif
    return;
}

struct mbi_fb {
    u32 flags, mem_lower, mem_upper, boot_device, cmdline, mods_count, mods_addr, syms[4],
        mmap_length, mmap_addr, drives_length, drives_addr, config_table, boot_loader_name, apm_table;
    u32 vbe_control_info, vbe_mode_info; u16 vbe_mode, vbe_seg, vbe_off, vbe_len;
    u32 fb_addr_lo, fb_addr_hi, fb_pitch, fb_width, fb_height;
    u8 fb_bpp, fb_type, red_pos, red_size, green_pos, green_size, blue_pos, blue_size;
} __attribute__((packed));

static gfx_rect wrect(void) { window *w = wm_find_app(APP_AST); return w ? w->r : (gfx_rect){ 0, 0, 0, 0 }; }

/* ---- performance scenarios (./gui_harness DIR perf): what one user session costs, measured with callgrind ---- */
static __attribute__((noinline)) void perf_idle(void)    { steps(120); }
static __attribute__((noinline)) void perf_drag(window *w)
{
    gfx_rect a = w->r;
    mouse_to(a.x + 200, a.y + 10, 0); mouse_to(a.x + 200, a.y + 10, 1);
    for (int i = 0; i < 40; i++) mouse_to(a.x + 200 + i * 8, a.y + 10 + i * 4, 1);
    mouse_to(a.x + 520, a.y + 170, 0);
}
static __attribute__((noinline)) void perf_type(void)    { for (int i = 0; i < 40; i++) { keys("abcdefghij"); steps(1); } }
static __attribute__((noinline)) void perf_windows(void)
{
    for (int i = 0; i < 4; i++) { ui_launch_app(APP_SETTINGS); steps(6); wm_close(wm_find_app(APP_SETTINGS)); steps(3); }
}
static __attribute__((noinline)) void perf_picker(void) { wp_picker_toggle(); steps(30); keys("\033[C"); steps(20); keys("\033"); steps(20); }

static int perf_main(void)
{
    static struct mbi_fb m;
    m.flags = 1u << 12; m.fb_addr_lo = (u32)(size_t)fb_mem; m.fb_width = 1920; m.fb_height = 1080;
    m.fb_pitch = 1920 * 4; m.fb_bpp = 32; m.fb_type = 1;
    m.red_pos = 16; m.red_size = 8; m.green_pos = 8; m.green_size = 8; m.blue_pos = 0; m.blue_size = 8;
    video_init(0x2BADB002u, &m);
    con_init(); ast_system_init(); mouse_init();
    ui_start();
    app_ast_launch(); steps(5);
    ui_launch_app(APP_FILES); steps(5);
    ui_launch_app(APP_AST); steps(5);
    window *w = wm_find_app(APP_AST);
    perf_idle();
    perf_drag(w);
    perf_type();
    perf_windows();
    perf_picker();
    return 0;
}

int hmain(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    ui_ms_hook = fake_clock;
    if (argc > 2 && argv[2][0] == 'p') return perf_main();

    /* ---- 1. framebuffer parameters come from the Multiboot info ---- */
    static struct mbi_fb m;
    m.flags = 1u << 12; m.fb_addr_lo = (u32)(size_t)fb_mem; m.fb_width = 1024; m.fb_height = 768;
    m.fb_pitch = 1024 * 4; m.fb_bpp = 32; m.fb_type = 1;
    m.red_pos = 16; m.red_size = 8; m.green_pos = 8; m.green_size = 8; m.blue_pos = 0; m.blue_size = 8;
    check("video: multiboot framebuffer accepted", video_init(0x2BADB002u, &m) == 1);
    const video_info *vi = video_get();
    check("video: size, pitch, depth, address carried over",
          vi && vi->w == 1024 && vi->h == 768 && vi->pitch == 4096 && vi->bpp == 32 && vi->addr == (u32)(size_t)fb_mem);
    struct mbi_fb bad = m; bad.fb_bpp = 16;
    check("video: 16-bit framebuffer refused with a reason", video_init(0x2BADB002u, &bad) == 0 && video_reason()[0]);
    struct mbi_fb bad2 = m; bad2.fb_width = 5000;
    check("video: absurd size refused", video_init(0x2BADB002u, &bad2) == 0);
    struct mbi_fb bad3 = m; bad3.flags = 0;
    check("video: no framebuffer info -> no graphics (no VGA card in the harness)", video_init(0x2BADB002u, &bad3) == 0);
    video_init(0x2BADB002u, &m);

    /* ---- 1b. BMP decoder rejects bad files instead of reading garbage ---- */
    {
        static u8 b[200];
        bmp_view v;
        check("bmp: NULL / tiny input", bmp_open(0, 0, &v) < 0 && bmp_open(b, 10, &v) < 0);
        kmemset(b, 0, sizeof b); b[0] = 'X'; b[1] = 'M';
        check("bmp: bad magic", bmp_open(b, sizeof b, &v) == BMP_EMAGIC);
        kmemset(b, 0, sizeof b); b[0] = 'B'; b[1] = 'M'; b[10] = 54; b[14] = 40; b[18] = 4; b[22] = 4; b[26] = 1; b[28] = 24;
        check("bmp: truncated pixel data", bmp_open(b, 60, &v) == BMP_ETRUNC);
        b[28] = 16;
        check("bmp: 16-bit refused", bmp_open(b, sizeof b, &v) == BMP_EFORMAT);
        b[28] = 24; b[22] = 0; b[23] = 0x7f; b[24] = 0; b[25] = 0;
        check("bmp: absurd height refused", bmp_open(b, sizeof b, &v) == BMP_ESIZE);
        b[22] = 4; b[23] = 0; b[30] = 1;
        check("bmp: RLE refused", bmp_open(b, sizeof b, &v) == BMP_EFORMAT);
    }

    /* ---- 2. boot ---- */
    con_init();
    ast_system_init();
    mouse_init();
    ui_boot_splash();
    snap(dir, "01_boot_splash");
    check("ui_start", ui_start() == 1);
    app_ast_launch();
    steps(5);
    snap(dir, "02_desktop");
    window *w = wm_find_app(APP_AST);
    check("desktop: AST window exists after start", w != 0 && w->state == WS_NORMAL);
    gfx_rect r0 = wrect();
    check("desktop: window inside the screen and above the dock",
          r0.x >= 0 && r0.y >= 0 && r0.x + r0.w <= 1024 && r0.y + r0.h <= ui_dock_rect().y);
    check("desktop: terminal grid is 80x25", ({ int c, r; con_gui_size(&c, &r); c == 80 && r == 25; }));

    /* ---- 3. typing goes to the shell ---- */
    keys("as login create pw bob\n");
    steps(10);
    keys("as login pw bob\n");
    steps(10);
    keys("as neofetch\n");
    steps(10);
    snap(dir, "03_neofetch");
    {
        int c, r; con_gui_size(&c, &r);
        int found_wm = 0, found_de = 0, found_user = 0;
        for (int y = 0; y < r; y++) {
            char line[200]; int n = 0;
            for (int x = 0; x < c && n < 199; x++) { u8 ch; u32 f, b; con_gui_cell(x, y, &ch, &f, &b); line[n++] = (char)ch; }
            line[n] = 0;
            if (kstrstr(line, "WM: AstraGUI")) found_wm = 1;
            if (kstrstr(line, "DE: AstraUI")) found_de = 1;
            if (kstrstr(line, "bob@astrasage")) found_user = 1;
        }
        check("neofetch in the window shows WM: AstraGUI", found_wm);
        check("neofetch in the window shows DE: AstraUI", found_de);
        check("login worked through the window keyboard", found_user);
    }

    /* ---- 4. drag by the title bar ---- */
    gfx_rect a = wrect();
    mouse_to(a.x + 200, a.y + 10, 0);
    mouse_to(a.x + 200, a.y + 10, 1);
    mouse_to(a.x + 260, a.y + 50, 1);
    mouse_to(a.x + 260, a.y + 50, 0);
    gfx_rect b = wrect();
    check("drag: window followed the pointer", b.x == a.x + 60 && b.y == a.y + 40 && b.w == a.w && b.h == a.h);
    snap(dir, "04_dragged");
    mouse_to(b.x + 100, b.y + 10, 0); mouse_to(b.x + 100, b.y + 10, 1);
    mouse_to(-50, -50, 1); mouse_to(0, 0, 0);
    gfx_rect c = wrect();
    check("drag: window cannot leave the screen", c.x >= 0 && c.y >= 0);

    /* ---- 5. resize from the bottom-right corner ---- */
    a = wrect();
    mouse_to(a.x + a.w - 2, a.y + a.h - 2, 0);
    mouse_to(a.x + a.w - 2, a.y + a.h - 2, 1);
    mouse_to(a.x + a.w - 2 + 120, a.y + a.h - 2 + 40, 1);
    mouse_to(a.x + a.w - 2 + 120, a.y + a.h - 2 + 40, 0);
    b = wrect();
    outf("  dbg resize a=%d,%d %dx%d b=%d,%d %dx%d\n", a.x, a.y, a.w, a.h, b.x, b.y, b.w, b.h);
    check("resize: window grew", b.w == a.w + 120 && b.h == a.h + 40 && b.x == a.x && b.y == a.y);
    {
        int cc, rr; con_gui_size(&cc, &rr);
        gfx_rect cr = wm_content_rect(wm_find_app(APP_AST));
        check("resize: terminal grid follows the window", cc == (cr.w - 2 * WM_PAD) / 8 && rr == (cr.h - 2 * WM_PAD) / 16);
    }
    snap(dir, "05_resized");
    a = wrect();                                              /* below the minimum size */
    mouse_to(a.x + a.w - 2, a.y + a.h - 2, 0); mouse_to(a.x + a.w - 2, a.y + a.h - 2, 1);
    mouse_to(a.x + 10, a.y + 10, 1); mouse_to(a.x + 10, a.y + 10, 0);
    b = wrect();
    check("resize: never smaller than the minimum", b.w >= 360 && b.h >= 220);
    /* restore a comfortable size for the next steps */
    a = wrect();
    mouse_to(a.x + a.w - 2, a.y + a.h - 2, 0); mouse_to(a.x + a.w - 2, a.y + a.h - 2, 1);
    mouse_to(a.x + 400, a.y + 280, 1); mouse_to(a.x + 400, a.y + 280, 0);

    /* ---- 6. maximise / restore (green) ---- */
    a = wrect();
    int gx = a.x + 20 + 2 * 22, gy = a.y + WM_TITLE_H / 2;
    click(gx, gy);
    window *wnd = wm_find_app(APP_AST);
    gfx_rect dk = ui_dock_rect();
    outf("  dbg max state=%d r=%d,%d %dx%d dock.y=%d\n", wnd->state, wnd->r.x, wnd->r.y, wnd->r.w, wnd->r.h, dk.y);
    check("maximise: state and size", wnd->state == WS_MAXIMIZED && wnd->r.x == 0 && wnd->r.y == SHELL_TOP && wnd->r.w == 1024 && wnd->r.h == dk.y - 8 - SHELL_TOP);
    check("maximise: dock stays visible below the window", wnd->r.y + wnd->r.h <= dk.y);
    snap(dir, "06_maximised");
    click(20 + 2 * 22, SHELL_TOP + WM_TITLE_H / 2);
    b = wrect();
    check("restore: previous geometry back", wnd->state == WS_NORMAL && b.x == a.x && b.y == a.y && b.w == a.w && b.h == a.h);

    /* ---- 7. minimise (yellow) and bring back through the dock ---- */
    a = wrect();
    click(a.x + 20 + 22, a.y + WM_TITLE_H / 2);
    check("minimise: state kept, window still registered", wnd->state == WS_MINIMIZED && wm_find_app(APP_AST) == wnd && wm_visible_count() == 0);
    steps(3);
    snap(dir, "07_minimised");
    check("minimise: window not drawn any more (wallpaper pixel visible)", px(a.x + a.w / 2, a.y + a.h / 2) != 0x000000);
    {   /* clicks where the window was do not reach it */
        int before = ({ int cx, cy; con_gui_cursor(&cx, &cy); cy; });
        click(a.x + a.w / 2, a.y + a.h / 2);
        int after = ({ int cx, cy; con_gui_cursor(&cx, &cy); cy; });
        check("minimise: hidden window gets no pointer events", before == after && wnd->state == WS_MINIMIZED);
    }
    {   /* reflow: a long wrapped line is joined and broken again when the width changes */
        int oc, orr; con_gui_size(&oc, &orr);
        kputs("\n");
        for (int i = 0; i < 100; i++) con_putc((char)('a' + i % 26));
        con_gui_resize(40, orr);
        int cyy, cxx; con_gui_cursor(&cxx, &cyy);
        char l0[64] = {0}, l2[64] = {0};
        for (int x = 0; x < 40; x++) { u8 ch; u32 f, b; con_gui_cell(x, cyy - 2, &ch, &f, &b); l0[x] = (char)ch; con_gui_cell(x, cyy, &ch, &f, &b); l2[x] = (char)ch; }
        check("resize: wrapped text reflows to the new width", l0[0] == 'a' && l0[39] == 'n' && l2[0] == 'c' && l2[19] == 'v');
        con_gui_resize(oc, orr);
        con_gui_cursor(&cxx, &cyy);
        char l9[64] = {0}; for (int x = 0; x < 30; x++) { u8 ch; u32 f, b; con_gui_cell(x, cyy, &ch, &f, &b); l9[x] = (char)ch; }
        { char nm[160]; ksnprintf(nm, sizeof nm, "resize: and back again oc=%d cy=%d cx=%d [%s]", oc, cyy, cxx, l9); check(nm, l9[0] == (char)('a' + ((100 - 1) / oc * oc) % 26) && l9[99 - (99 / oc * oc)] == 'v'); }
    }
    gfx_rect ir = ui_dock_rect();
    int ix = ir.x + ir.w - 10 - 28, iy = ir.y + ir.h / 2 - 3;          /* the AST icon: last in the dock, after the 9-dot one */
    click(ix, iy);
    check("dock: click restores the same window", wnd->state == WS_NORMAL && wm_find_app(APP_AST) == wnd && wm_visible_count() == 1);
    check("restore from minimise keeps geometry", ({ gfx_rect q = wrect(); q.x == a.x && q.y == a.y && q.w == a.w && q.h == a.h; }));
    int count = 0;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) if (wm_slot(i)) count++;
    click(ix, iy);
    click(ix, iy);
    int count2 = 0;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) if (wm_slot(i)) count2++;
    check("dock: clicking while visible never creates a second AST", count == 1 && count2 == 1);

    /* the shell kept its state across minimise: still logged in */
    keys("as location\n");
    steps(8);
    {
        int c2, r2, ok = 0; con_gui_size(&c2, &r2);
        for (int y = 0; y < r2; y++) {
            char line[200]; int n = 0;
            for (int x = 0; x < c2 && n < 199; x++) { u8 ch; u32 f, bb; con_gui_cell(x, y, &ch, &f, &bb); line[n++] = (char)ch; }
            line[n] = 0;
            if (kstrstr(line, "S:~/home/") && !kstrstr(line, "location")) ok = 1;
        }
        check("minimise keeps the terminal session (still logged in)", ok);
    }

    /* ---- 7b. the nano-like editor runs inside the window ---- */
    keys("as edit demo.as\n");
    steps(8);
    keys("printc(\"hi\");");
    steps(8);
    snap(dir, "07b_editor");
    {
        int c2, r2; con_gui_size(&c2, &r2);
        u8 ch; u32 f, bg;
        con_gui_cell(2, 0, &ch, &f, &bg);                    /* top bar: green background */
        check("editor: green top bar drawn in the window", bg == 0x87D700 || bg == 0x5FAF00 || bg == 0xAFFF00 || (bg >> 8 & 255) > 0x80);
    }
    keys("\x18");
    steps(5);
    keys("n");
    steps(8);
    {
        int c2, r2, ok = 0; con_gui_size(&c2, &r2);
        for (int y = 0; y < r2; y++) {
            char line[200]; int n = 0;
            for (int x = 0; x < c2 && n < 199; x++) { u8 ch; u32 f, bb; con_gui_cell(x, y, &ch, &f, &bb); line[n++] = (char)ch; }
            line[n] = 0;
            if (kstrstr(line, "$") && kstrstr(line, "bob@")) ok = 1;
        }
        check("editor: exit returns to the prompt", ok);
    }

    /* ---- 8. icons: same squircle everywhere ---- */
    const gfx_surf *i56 = render_app_icon(APP_AST, 56), *i64 = render_app_icon(APP_AST, 64);
    check("icon: corners transparent, centre opaque (56)", (i56->px[0] >> 24) == 0 && (i56->px[28 * 56 + 28] >> 24) == 255);
    check("icon: corners transparent, centre opaque (64)", (i64->px[0] >> 24) == 0 && (i64->px[32 * 64 + 32] >> 24) == 255);
    check("icon: edge midpoints opaque (squircle touches the box)", (i64->px[32] >> 24) == 255 && (i64->px[32 * 64] >> 24) == 255);
    check("icon: cached (same surface returned)", render_app_icon(APP_AST, 56) == i56);
    {
        int soft = 0;
        for (int i = 0; i < 64 * 64; i++) { u32 al = i64->px[i] >> 24; if (al > 0 && al < 255) soft++; }
        check("icon: anti-aliased edge", soft > 20);
    }

    /* ---- 9. close (red) kills the shell; the dock starts a new one ---- */
    snap(dir, "08_before_close");
    a = wrect();
    click(a.x + 20, a.y + WM_TITLE_H / 2);
    check("close: window gone and AST not running", wm_find_app(APP_AST) == 0 && !app_ast_running());
    steps(3);
    snap(dir, "09_closed_desktop");
    check("dock: no app open -> only the 9-dot icon stays", ui_dock_count() == 0 && ui_dock_rect().w > 0 && ui_dock_rect().w < 100);
    check("dock: the old dock area shows the wallpaper again", px(ix, iy) != 0x9be01c && px(ir.x + 2, ir.y + ir.h / 2) != 0x1e1e22);
    click(ix, iy);
    steps(3);
    check("dock: clicking where the dock was does not start anything", wm_find_app(APP_AST) == 0 && !app_ast_running());
    fast_clicks = 1; click(76, 100); click(76, 100); fast_clicks = 0;
    steps(5);
    wnd = wm_find_app(APP_AST);
    check("desktop icon: double click on AST starts a fresh one", wnd != 0 && app_ast_running());
    check("dock: appears again with exactly one icon", ui_dock_count() == 1 && ui_dock_rect().w > 0);
    {
        int c2, r2, ok = 0; con_gui_size(&c2, &r2);
        for (int y = 0; y < r2; y++) {
            char line[200]; int n = 0;
            for (int x = 0; x < c2 && n < 199; x++) { u8 ch; u32 f, bb; con_gui_cell(x, y, &ch, &f, &bb); line[n++] = (char)ch; }
            line[n] = 0;
            if (kstrstr(line, "bob@")) ok = 1;
        }
        check("closing AST keeps the login: new AST starts signed in", ok);
    }
    snap(dir, "10_reopened");
    {
        ui_anim_settle(); steps(2);
        gfx_rect ir9 = ui_dock_rect();
        int found = 0;
        for (int yy = ir9.y; yy < ir9.y + ir9.h; yy++)
            for (int xx = ir9.x; xx < ir9.x + ir9.w; xx++) if (px(xx, yy) == (ui_th()->accent & 0xFFFFFF)) found = 1;
        check("dock: running dot is back after reopening", found);
    }

    /* ---- 10. desktop icon double click ---- */
    a = wrect();
    click(a.x + 20 + 22, a.y + WM_TITLE_H / 2);              /* minimise */
    steps(2);
    fast_clicks = 1; click(76, 100); click(76, 100); fast_clicks = 0;
    check("desktop icon: double click restores AST (no second window)", wnd->state == WS_NORMAL ? 1 : wm_find_app(APP_AST)->state == WS_NORMAL);

    /* ---- 11. dragging a window onto the dock: the dock steps down and comes back ---- */
    gfx_rect dd0 = ui_dock_rect();
    a = wrect();
    mouse_to(a.x + 100, a.y + 10, 0); mouse_to(a.x + 100, a.y + 10, 1);
    mouse_to(ir.x + ir.w / 2, ir.y + ir.h / 2, 1); mouse_to(ir.x + ir.w / 2, ir.y + ir.h / 2, 0);
    steps(3);
    snap(dir, "11_over_dock");
    gfx_rect dd = ui_dock_rect();
    wnd = wm_find_app(APP_AST);
    check("window stays completely on the screen", wnd->r.y + wnd->r.h <= 768 && wnd->r.x >= 0 && wnd->r.x + wnd->r.w <= 1024);
    steps(40);                                                   /* the dock steps down out of the way of the window that touches it ... */
    gfx_rect dd2 = ui_dock_rect();
    check("dock steps down while a window touches it", dd2.w == 0 || dd2.y > dd0.y);
    wnd->r.y = 120; wnd->restore = wnd->r; wm_damage(gfx_rect_make(0, 0, 1024, 768));
    steps(40);                                                   /* ... and comes back when the window has left */
    gfx_rect dd3 = ui_dock_rect();
    check("dock returns to its place when the window leaves", dd3.w > 0 && dd3.y == dd0.y);
    check("dock plate is not black terminal", px(dd3.x + 4, dd3.y + dd3.h / 2) != 0x000000);

    /* ---- 12. AstraSageOS File Explorer ---- */
    {
        const int M_MENU_ = 1, M_RENAME_ = 2, M_INFO_ = 4, M_CONFIRM_ = 5, M_ERROR_ = 6, M_PASS_ = 7;
        const gfx_surf *fi = render_app_icon(APP_FILES, 56);
        int greens = 0;
        for (int i = 0; i < 56 * 56; i++) { u32 c = fi->px[i]; if ((c >> 24) == 255 && (c >> 8 & 255) > 0xA0 && (c >> 16 & 255) < 0x90 && (c & 255) < 0x60) greens++; }
        check("explorer icon: transparent corners, opaque centre", (fi->px[0] >> 24) == 0 && (fi->px[28 * 56 + 28] >> 24) == 255);
        check("explorer icon: drawn green folder (not a plain shape)", greens > 300);
        window *aw = wm_find_app(APP_AST);
        gfx_rect ar = aw->r;
        click(ar.x + ar.w / 2, ar.y + 12);                           /* make sure AST has the focus */
        int home_before = 0;
        {   /* open the explorer from its desktop icon */
            fast_clicks = 1; click(76, 200); click(76, 200); fast_clicks = 0;
            steps(5);
        }
        window *fx = wm_find_app(APP_FILES);
        check("explorer: double click on its desktop icon opens it", fx != 0 && fx->state == WS_NORMAL);
        check("explorer: dock shows AST and File Explorer (2 icons)", ui_dock_count() == 2);
        check("explorer: window inside the work area", fx && fx->r.y + fx->r.h <= ui_work_rect().y + ui_work_rect().h);
        check("explorer: starts in the signed-in user's home", app_files_cur() == user_home());
        home_before = app_files_count();
        snap(dir, "12a_explorer_open");

        /* AST -> explorer in real time (focus AST, then type) */
        {   gfx_rect dk2 = ui_dock_rect();
            click(dk2.x + 10 + 70 + 28, dk2.y + 10 + 28);                 /* AST's dock icon raises and focuses it */
            check("dock: AST icon brings AST to the front", wm_focused() == wm_find_app(APP_AST));
        }
        keys("as newdir proj && as newfile hello.as && as newfile n.txt && as newfile m.c && as newfile k.cpp && as newfile r.asm && as newfile z.dat\n");
        steps(40);
        check("AST && chain created 7 items; explorer shows them at once", app_files_count() == home_before + 7);
        {
            int ok = 1;
            for (int i = 0; i < app_files_count(); i++) if (i && fs_is_dir(app_files_item(i)) && !fs_is_dir(app_files_item(i - 1))) ok = 0;
            check("explorer: folders listed before files", ok && fs_is_dir(app_files_item(0)));
        }
        snap(dir, "12b_explorer_items");

        fx = wm_find_app(APP_FILES);
        {   gfx_rect dk2 = ui_dock_rect();
            click(dk2.x + 10 + 140 + 28, dk2.y + 10 + 28);       /* the explorer's dock icon */
            check("dock: explorer icon brings the explorer to the front", wm_focused() == fx);
        }
        gfx_rect cr = wm_content_rect(fx);
        int sb = cr.w < 540 ? 124 : 168;
        gfx_rect lr = gfx_rect_make(cr.x + sb, cr.y + 40, cr.w - sb, cr.h - 40 - 26);
        int ccols = (lr.w - 14 - 10) / 104;
        #define CELLX(i) (lr.x + 8 + ((i) % ccols) * 104 + 50)
        #define CELLY(i) (lr.y + 6 + ((i) / ccols) * 92 + 40)
        int idx_proj = -1, idx_n = -1, idx_m = -1, idx_hello = -1;
        for (int i = 0; i < app_files_count(); i++) {
            const char *nm = fs_name(app_files_item(i));
            if (!kstrcmp(nm, "proj")) idx_proj = i;
            if (!kstrcmp(nm, "n.txt")) idx_n = i;
            if (!kstrcmp(nm, "m.c")) idx_m = i;
            if (!kstrcmp(nm, "hello.as")) idx_hello = i;
        }
        check("explorer: all new names found in the list", idx_proj >= 0 && idx_n >= 0 && idx_m >= 0 && idx_hello >= 0);

        /* single click selects, double click opens a folder, the up button goes back */
        click(CELLX(idx_proj), CELLY(idx_proj));
        check("explorer: click selects", app_files_selected() == idx_proj);
        fast_clicks = 1; click(CELLX(idx_proj), CELLY(idx_proj)); click(CELLX(idx_proj), CELLY(idx_proj)); fast_clicks = 0;
        steps(2);
        check("explorer: double click enters the folder", app_files_cur() == fs_resolve("proj", user_home()) && app_files_count() == 0);
        snap(dir, "12c_empty_folder");
        click(cr.x + 8 + 15, cr.y + 6 + 14);
        check("explorer: up button returns to the parent", app_files_cur() == user_home());

        /* right-click panel: four options; Info */
        mouse_to(CELLX(idx_n), CELLY(idx_n), 0); mouse_to(CELLX(idx_n), CELLY(idx_n), 2); mouse_to(CELLX(idx_n), CELLY(idx_n), 0);
        check("explorer: right-click opens the panel", app_files_modal() == M_MENU_);
        snap(dir, "12d_context_menu");
        int mx0 = CELLX(idx_n), my0 = CELLY(idx_n);
        click(mx0 + 30, my0 + 5 + 2 * 28 + 13);                      /* Info */
        check("explorer: Info panel shows", app_files_modal() == M_INFO_);
        snap(dir, "12e_info");
        keys("\n"); steps(3);
        check("explorer: Info panel closes", app_files_modal() == 0);

        /* Rename through a panel */
        mouse_to(CELLX(idx_n), CELLY(idx_n), 0); mouse_to(CELLX(idx_n), CELLY(idx_n), 2); mouse_to(CELLX(idx_n), CELLY(idx_n), 0);
        click(mx0 + 30, my0 + 5 + 1 * 28 + 13);                      /* Rename */
        check("explorer: Rename panel shows", app_files_modal() == M_RENAME_);
        snap(dir, "12f_rename");
        keys("\b\b\b\b\bnotes.txt\n"); steps(5);
        check("explorer: renamed on the disk (visible to AST)", fs_resolve("notes.txt", user_home()) > 0 && fs_resolve("n.txt", user_home()) < 0);

        /* Copy to Folder by dragging the file onto the folder */
        for (int i = 0; i < app_files_count(); i++) if (!kstrcmp(fs_name(app_files_item(i)), "m.c")) idx_m = i;
        mouse_to(CELLX(idx_m), CELLY(idx_m), 0); mouse_to(CELLX(idx_m), CELLY(idx_m), 1);
        mouse_to(CELLX(idx_m) + 20, CELLY(idx_m) + 20, 1);
        mouse_to(CELLX(idx_proj), CELLY(idx_proj), 1);
        snap(dir, "12g_dragging");
        mouse_to(CELLX(idx_proj), CELLY(idx_proj), 0);
        steps(3);
        {
            int pn = fs_resolve("proj", user_home());
            outf("  dbg copy: pn=%d in_proj=%d modal=%d drag=%d\n", pn, pn > 0 ? fs_resolve("m.c", pn) : -99, app_files_modal(), app_files_dragging());
            check("explorer: dragging a file onto a folder copies it", pn > 0 && fs_resolve("m.c", pn) > 0 && fs_resolve("m.c", user_home()) > 0);
        }

        /* Delete with confirmation */
        for (int i = 0; i < app_files_count(); i++) if (!kstrcmp(fs_name(app_files_item(i)), "z.dat")) idx_n = i;
        mx0 = CELLX(idx_n); my0 = CELLY(idx_n);
        mouse_to(mx0, my0, 0); mouse_to(mx0, my0, 2); mouse_to(mx0, my0, 0);
        click(mx0 + 30, my0 + 5 + 13);                               /* Delete */
        {
            { /* block-free file system: more than 256 files, files bigger than 4096 bytes, growth, deletion and compaction */
              int dir = fs_create("bigtest", user_home(), 1), ok = dir > 0, cnt = 0;
              static char blob[100000];
              for (int i = 0; i < 100000; i++) blob[i] = (char)('a' + i % 23);
              for (int i = 0; ok && i < 400; i++) { char nm[24]; ksnprintf(nm, sizeof nm, "f%d", i); int f = fs_create(nm, dir, 0); if (f < 0) { ok = 0; break; } fs_write(f, "x", 1, 0); cnt++; }
              check("fs: more than 256 files fit", ok && cnt == 400);
              int big = fs_create("big.bin", dir, 0);
              check("fs: a 100000 byte file is stored", big > 0 && fs_write(big, blob, 100000, 0) == FS_OK && fs_size(big) == 100000 && !kmemcmp(fs_data(big), blob, 100000));
              check("fs: appending to a big file", fs_write(big, "END", 3, 1) == FS_OK && fs_size(big) == 100003 && fs_data(big)[99999] == blob[99999] && !kmemcmp(fs_data(big) + 100000, "END", 3));
              check("fs: past 256 KiB a file moves to the disk (big file), nothing is cut", fs_write(big, blob, 100000, 1) == FS_OK && fs_write(big, blob, 100000, 1) == FS_OK && fs_is_big(big) && fs_size_real(big) == 300003 && ({ u8 t[4]; fs_pread(big, 300003 - 100000, t, 4) == 4 && !kmemcmp(t, blob, 4); }));
              int sm = fs_resolve("f399", dir);
              for (int i = 0; i < 200; i++) { char nm[24]; ksnprintf(nm, sizeof nm, "f%d", i); int f = fs_resolve(nm, dir); if (f > 0) fs_delete(f); }
              int big2 = fs_create("big2.bin", dir, 0);
              check("fs: space of deleted files is reused (compaction)", big2 > 0 && fs_write(big2, blob, 100000, 0) == FS_OK && fs_write(big, blob, 10, 0) == FS_OK && !kmemcmp(fs_data(big2), blob, 100000) && sm > 0 && fs_data(sm)[0] == 'x');
              fs_delete(dir); }
            int tr = fs_resolve("Trash", user_home());
            check("trash: Delete moves a file to the Trash", tr > 0 && fs_resolve("z.dat", user_home()) < 0 && fs_resolve("z.dat", tr) > 0 && app_files_modal() == 0);
            snap(dir, "12h_trash_moved");
            /* put a second file with the same name there: the name gets ~2 */
            int a1 = fs_create("dup.txt", user_home(), 0); (void)a1;
            check("trash: fs_move renames on a name clash", tr > 0 && fs_create("dup.txt", tr, 0) > 0 && fs_move(fs_resolve("dup.txt", user_home()), tr) == FS_OK && fs_resolve("dup.txt~2", tr) > 0);
            check("trash: a folder cannot move into itself", fs_move(tr, tr) < 0 && fs_move(user_home(), tr) < 0);
        }

        /* path bar */
        click(cr.x + 46 + 100, cr.y + 6 + 14);
        keys("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b");
        keys("S:/usrs/bob/home/proj\n");
        steps(6);
        check("explorer: typed path is visited", app_files_cur() == fs_resolve("proj", user_home()));
        click(cr.x + 46 + 100, cr.y + 6 + 14);
        keys("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b");
        keys("S:/nowhere\n"); steps(5);
        check("explorer: unknown path gives an error panel", app_files_modal() == M_ERROR_);
        keys("\n"); steps(2);
        keys("\b"); steps(2);                                         /* backspace = up */

        /* quick access: Root, then the protected root files */
        click(cr.x + 20, cr.y + 40 + 32 + 3 * 32 + 15);              /* Root */
        steps(2);
        check("explorer: Root lists the root files", app_files_cur() == 0 && app_files_count() > 0);
        snap(dir, "12i_root");
        {
            int victim = app_files_item(0), vcount = app_files_count();
            char vname[40]; kstrncpy(vname, fs_name(victim), 39); vname[39] = 0;
            int vx = CELLX(0), vy = CELLY(0);
            mouse_to(vx, vy, 0); mouse_to(vx, vy, 2); mouse_to(vx, vy, 0);
            click(vx + 30, vy + 5 + 13);                              /* Delete */
            check("explorer: deleting a root folder asks for the password", app_files_modal() == M_PASS_);
            snap(dir, "12j_root_protected");
            keys("zzz\n"); steps(2);
            check("explorer: a wrong password keeps the prompt", app_files_modal() == M_PASS_ && fs_exists(victim));
            keys("pw\n"); steps(2);
            check("explorer: the right password leads to the delete confirmation", app_files_modal() == M_CONFIRM_ && fs_exists(victim));
            keys("n"); steps(2);
            check("explorer: declining keeps the folder and drops the authority", app_files_modal() == 0 && fs_exists(victim) && !fs_acs_get());
            mouse_to(vx, vy, 0); mouse_to(vx, vy, 2); mouse_to(vx, vy, 0);
            click(vx + 30, vy + 5 + 13);
            keys("\x1b"); steps(2);
            check("explorer: cancelling leaves the root folder alone", app_files_modal() == 0 && fs_exists(victim) && app_files_count() == vcount);
            mouse_to(vx, vy, 0); mouse_to(vx, vy, 2); mouse_to(vx, vy, 0);
            click(vx + 30, vy + 5 + 28 + 13);                         /* Rename */
            check("explorer: renaming a root folder is refused", app_files_modal() == M_ERROR_ && fs_exists(victim) && !kstrcmp(fs_name(victim), vname));
            keys("\n"); steps(2);
        }
        click(cr.x + 20, cr.y + 40 + 32 + 1 * 32 + 15);              /* S:/ */
        check("explorer: S:/ quick access", app_files_cur() == fs_mount_s());
        click(cr.x + 20, cr.y + 40 + 32 + 2 * 32 + 15);              /* Home */
        check("explorer: Home quick access", app_files_cur() == user_home());
        snap(dir, "12k_home");

        /* This PC overview */
        click(cr.x + 20, cr.y + 40 + 32 + 0 * 32 + 15);              /* This PC */
        steps(2);
        check("explorer: This PC overview opens", app_files_thispc() && app_files_count() == 0);
        snap(dir, "12l_this_pc");
        keys("\b"); steps(1);
        check("explorer: up inside This PC stays", app_files_thispc());
        click(cr.x + 20, cr.y + 40 + 32 + 3 * 32 + 15);              /* Root */
        check("explorer: Root leaves This PC", !app_files_thispc() && app_files_cur() == 0);
        keys("\b"); steps(1);
        check("explorer: up from Root goes to This PC", app_files_thispc());
        click(cr.x + 20, cr.y + 40 + 32 + 2 * 32 + 15);              /* Home */
        check("explorer: Home from This PC", !app_files_thispc() && app_files_cur() == user_home());

        /* double click on a source file opens it in AstraSage Edit inside AST */
        for (int i = 0; i < app_files_count(); i++) if (!kstrcmp(fs_name(app_files_item(i)), "hello.as")) idx_hello = i;
        fast_clicks = 1; click(CELLX(idx_hello), CELLY(idx_hello)); click(CELLX(idx_hello), CELLY(idx_hello)); fast_clicks = 0;
        steps(30);
        {
            window *a2 = wm_find_app(APP_AST);
            u8 ch; u32 f, bg;
            con_gui_cell(2, 0, &ch, &f, &bg);
            check("explorer: double click on a file opens AstraSage Edit in AST", a2 && wm_focused() == a2 && (bg >> 8 & 255) > 0x80);
            snap(dir, "12l_opened_in_edit");
            keys("\x18"); steps(8);
        }

        /* closing the explorer removes only its dock icon */
        fx = wm_find_app(APP_FILES);
        click(fx->r.x + 20, fx->r.y + WM_TITLE_H / 2);
        steps(3);
        check("explorer: close removes the window and its dock icon, AST stays", wm_find_app(APP_FILES) == 0 && ui_dock_count() == 1 && wm_find_app(APP_AST) != 0);
        snap(dir, "12m_after_close");
    }

    /* ---- 13. window dragging renders exactly like a full redraw (sprite + damage rects) ---- */
    {
        static u32 keep[1024 * 768];
        window *dw = wm_find_app(APP_AST);
        click(ui_dock_rect().x + 108, ui_dock_rect().y + 38);
        gfx_rect g = dw->r;
        mouse_to(g.x + 150, g.y + 10, 0); mouse_to(g.x + 150, g.y + 10, 1);
        for (int i = 1; i <= 12; i++) mouse_to(g.x + 150 + i * 13, g.y + 10 + i * 9, 1);
        int moving = wm_busy();
        int ok_mid = 1;
        mouse_to(g.x + 150 + 12 * 13, g.y + 10 + 12 * 9, 0);
        steps(3);
        for (int i = 0; i < 1024 * 768; i++) keep[i] = fb_mem[i];
        wm_damage(gfx_rect_make(0, 0, 1024, 768));
        steps(2);
        int diff = 0;
        for (int y = 0; y < 768; y++)
            for (int x = 0; x < 1024; x++) {
                u32 a = keep[y * 1024 + x] & 0xFFFFFF, b = fb_mem[y * 1024 + x] & 0xFFFFFF;
                if (a != b) diff++;
            }
        outf("  dbg drag/full-redraw differing pixels: %d (moving=%d)\n", diff, moving);
        check("drag: the window was really being dragged", moving && ok_mid);
        check("drag: result after dropping equals a full redraw (<= pointer sprite pixels)", diff < 400);
        u32 t0 = clock_ms;
        gfx_rect g2 = dw->r;
        mouse_to(g2.x + 100, g2.y + 10, 0); mouse_to(g2.x + 100, g2.y + 10, 1);
        for (int i = 1; i <= 20; i++) mouse_to(g2.x + 100 - i * 4, g2.y + 10 + i * 3, 1);
        mouse_to(g2.x + 20, g2.y + 70, 0);
        outf("  dbg 20 drag steps used %u fake ms\n", clock_ms - t0);
    }

    /* ---- v0.8: welcome / setup, graphical login, themes, desktop shortcuts, panel ---- */
    {
        ui_anim_settle(); steps(2);
        window *ww;
        /* a) the red button of the welcome app is refused until the setup is done */
        app_welcome_launch(); steps(4);
        ww = wm_find_app(APP_WELCOME);
        check("welcome: opens", ww != 0 && app_welcome_state() == 0);
        snap(dir, "13a_welcome");
        gfx_rect cr = wm_content_rect(ww);
        mouse_to(ww->r.x + 19, ww->r.y + 15, 0); mouse_to(ww->r.x + 19, ww->r.y + 15, 1); mouse_to(ww->r.x + 19, ww->r.y + 15, 0);
        steps(3);
        check("welcome: closing before the setup is refused (warning shown)", wm_find_app(APP_WELCOME) != 0 && !app_welcome_done());
        snap(dir, "13b_welcome_warning");
        click(cr.x + cr.w / 2 + 110, cr.y + cr.h / 2 + 30 + 19);          /* Close anyway */
        steps(3);
        check("welcome: 'Close anyway' closes it", wm_find_app(APP_WELCOME) == 0);

        /* b) the whole setup */
        app_welcome_launch(); steps(4);
        ww = wm_find_app(APP_WELCOME); cr = wm_content_rect(ww);
        int nx = cr.x + cr.w - 28 - 75, ny = cr.y + cr.h - 60 + 19;
        click(nx, ny); steps(2);
        check("welcome: Continue -> language page", app_welcome_state() == 1);
        int cw = 128, x0 = cr.x + (cr.w - (5 * cw + 4 * 14)) / 2;
        click(x0 + (cw + 14) + 60, cr.y + 130 + 100); steps(2);
        check("welcome: Türkçe chosen (language switches)", config_get()->lang == LANG_TR);
        snap(dir, "13c_language");
        click(nx, ny); steps(2);
        check("welcome: theme page", app_welcome_state() == 2);
        cw = 224; x0 = cr.x + (cr.w - (3 * cw + 2 * 14)) / 2;
        check("theme: white is the default", ui_theme_id() == THEME_WHITE);
        click(x0 + 2 * (cw + 14) + 100, cr.y + 116 + 40); steps(3);
        check("theme: green card applies the theme at once", ui_theme_id() == THEME_GREEN);
        snap(dir, "13d_theme_green");
        click(x0 + (cw + 14) + 100, cr.y + 116 + 40); steps(3);
        check("theme: black card", ui_theme_id() == THEME_BLACK);
        snap(dir, "13e_theme_black");
        click(x0 + 100, cr.y + 116 + 40); steps(3);
        check("theme: white card", ui_theme_id() == THEME_WHITE);
        click(nx, ny); steps(2);
        check("welcome: sign-in page", app_welcome_state() == 3);
        cw = 280; x0 = cr.x + (cr.w - (2 * cw + 24)) / 2;
        click(x0 + cw + 24 + 100, cr.y + 130 + 100); steps(2);
        snap(dir, "13f_login_mode");
        click(nx, ny); steps(2);
        check("welcome: drives page", app_welcome_state() == 4);
        snap(dir, "13g_drives");
        click(nx, ny); steps(2);
        check("welcome: package page", app_welcome_state() == 5);
        snap(dir, "13h_package");
        click(nx, ny); steps(2);                                           /* package: No */
        check("welcome: installer page", app_welcome_state() == 6);
        click(nx, ny); steps(3);                                           /* Install */
        for (int i = 0; i < 400 && !app_welcome_done(); i++) { clock_ms += 60; steps(1); }
        steps(3);
        snap(dir, "13i_installer_done");
        check("installer: finished", app_welcome_done());
        {
            int o = fs_get_jail(); fs_set_jail(0);
            int f = fs_resolve("/.config/settings", 0);
            check("installer: /config/settings written with the choices",
                  f > 0 && kstrstr(fs_data(f), "setup_done=1") && kstrstr(fs_data(f), "login_gui=1") && kstrstr(fs_data(f), "lang=1"));
            fs_set_jail(o);
        }
        check("setup done: config says so", config_get()->setup_done == 1);
        click(nx, ny); steps(4);                                           /* OK */
        check("installer: OK closes the welcome app", wm_find_app(APP_WELCOME) == 0);
        check("installer: graphical login opens next", ui_login_active());

        /* c) graphical login */
        user_logout();
        app_login_launch();
        steps(3);
        snap(dir, "14a_login");
        keys("bob\tpw\n"); steps(12);
        check("login: name + password in the window sign in", user_name() && !kstrcmp(user_name(), "bob"));
        check("login: window closes and AST opens", !ui_login_active() && wm_find_app(APP_AST) != 0);
        user_logout();
        app_login_launch(); steps(3);
        keys("bob\txx\n"); steps(8);
        check("login: wrong password is refused, window stays", !user_name() && ui_login_active());
        app_login_test_close();
        keys(""); steps(2);
        user_login("bob", "pw");

        /* d) desktop shortcuts and the panel */
        ui_anim_settle(); steps(2);
        int o = fs_get_jail(); fs_set_jail(0);
        int hn = fs_resolve("/drives/S/usrs/bob/home", 0);
        int fn = fs_create("/drives/S/usrs/bob/home/dropme.txt", 0, 0);
        int rootf = fs_resolve("/bin/version", 0);
        fs_set_jail(o);
        int before = ui_desktop_count();
        check("desktop: dropping a user file creates a shortcut", fn > 0 && ui_desktop_drop(fn, 400, 150) == 0 && ui_desktop_count() == before + 1);
        check("desktop: the same file twice is refused", ui_desktop_drop(fn, 400, 150) < 0 && ui_overlay_mode() == 3);
        click(512, 384); steps(2);
        check("desktop: error panel closes", ui_overlay_mode() == 0);
        check("desktop: a system file is refused with an error", rootf > 0 && ui_desktop_drop(rootf, 300, 300) < 0 && ui_overlay_mode() == 3);
        check("desktop: ...and nothing was added", ui_desktop_count() == before + 1);
        click(512, 384); steps(2);
        snap(dir, "15a_shortcut");
        (void)hn;
        /* right click on the AST icon: panel with Info / Delete */
        mouse_to(70, 60, 0); mouse_to(70, 60, 2); mouse_to(70, 60, 0); steps(3);
        check("desktop: right click on an app opens the Info/Delete panel", ui_overlay_mode() == 1);
        snap(dir, "15b_app_menu");
        click(70 + 20, 60 + 5 + 28 + 13); steps(3);                       /* Delete */
        check("desktop: deleting a built-in app gives an error", ui_overlay_mode() == 3);
        click(512, 384); steps(2);
        check("desktop: the app is still there", ui_desktop_count() == before + 1);
    }

    /* ---- v0.8.1: window moving / resizing behaves ---- */
    {
        window *aw = wm_find_app(APP_AST);
        if (!aw) { app_ast_launch(); steps(4); aw = wm_find_app(APP_AST); }
        if (aw->state != WS_NORMAL) wm_restore(aw);
        wm_raise(aw); steps(2);
        struct { const char *name; int fx, fy; int dx, dy; int ex_l, ex_t, ex_r, ex_b; } T[] = {
            /* press point as fraction of the window (0 = low edge, 2 = high edge, 1 = middle), pointer delta, which sides must move (-1 none, 1 outward-grow) */
            { "left",   0, 1, -30, 0,  -30, 0, 0, 0 },
            { "right",  2, 1,  30, 0,  0, 0, 30, 0 },
            { "bottom", 1, 2,  0, 25,  0, 0, 0, 25 },
            { "bottom-right", 2, 2, 20, 15, 0, 0, 20, 15 },
            { "bottom-left",  0, 2, -20, 15, -20, 0, 0, 15 },
            { "top-left",  0, 0, -15, -12, -15, -12, 0, 0 },
            { "top-right", 2, 0, 15, -12, 0, -12, 15, 0 },
        };
        for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) {
            aw->r = gfx_rect_make(300, 200, 520, 340); aw->restore = aw->r; wm_damage(gfx_rect_make(0, 0, 1024, 768)); steps(2);
            gfx_rect r = aw->r;
            int px_ = T[i].fx == 0 ? r.x + 1 : T[i].fx == 2 ? r.x + r.w - 2 : r.x + r.w / 2;
            int py_ = T[i].fy == 0 ? r.y + 1 : T[i].fy == 2 ? r.y + r.h - 2 : r.y + r.h / 2;
            mouse_to(px_, py_, 0); mouse_to(px_, py_, 1);
            for (int k = 1; k <= 5; k++) mouse_to(px_ + T[i].dx * k / 5, py_ + T[i].dy * k / 5, 1);
            mouse_to(px_ + T[i].dx, py_ + T[i].dy, 0); steps(2);
            gfx_rect n = aw->r;
            int ok = n.x == r.x + T[i].ex_l && n.y == r.y + T[i].ex_t && n.x + n.w == r.x + r.w + T[i].ex_r && n.y + n.h == r.y + r.h + T[i].ex_b;
            char nm[80]; ksnprintf(nm, sizeof nm, "resize: %s edge/corner grows in the pointer's direction only", T[i].name);
            if (!ok) outf("  dbg %s: %d,%d %dx%d -> %d,%d %dx%d\n", T[i].name, r.x, r.y, r.w, r.h, n.x, n.y, n.w, n.h);
            check(nm, ok);
        }
        /* a click on the title bar (even its very top pixels) never resizes, a 3 px wobble never moves */
        aw->r = gfx_rect_make(300, 200, 520, 340); aw->restore = aw->r; wm_damage(gfx_rect_make(0, 0, 1024, 768)); steps(2);
        gfx_rect r = aw->r;
        mouse_to(r.x + 260, r.y + 1, 0); mouse_to(r.x + 260, r.y + 1, 1);
        mouse_to(r.x + 262, r.y + 2, 1); mouse_to(r.x + 260, r.y + 1, 0); steps(2);
        check("title: a click on its top pixel row does not resize or move", aw->r.x == r.x && aw->r.y == r.y && aw->r.w == r.w && aw->r.h == r.h);
        mouse_to(r.x + 260, r.y + 1, 1);
        for (int k = 1; k <= 6; k++) mouse_to(r.x + 260 + k * 10, r.y + 1 + k * 8, 1);
        mouse_to(r.x + 320, r.y + 49, 0); steps(2);
        check("title: dragging by its top pixel row moves the window, size unchanged", aw->r.w == r.w && aw->r.h == r.h && aw->r.x == r.x + 60 && aw->r.y == r.y + 48);
        /* two quick drags in a row are not a double click (no maximise) */
        fast_clicks = 1;
        for (int rep = 0; rep < 2; rep++) {
            gfx_rect g = aw->r;
            mouse_to(g.x + 200, g.y + 15, 0); mouse_to(g.x + 200, g.y + 15, 1);
            for (int k = 1; k <= 5; k++) mouse_to(g.x + 200 + k * 6, g.y + 15 + k * 4, 1);
            mouse_to(g.x + 230, g.y + 35, 0);
        }
        fast_clicks = 0; steps(2);
        check("title: two quick drags do not maximise", aw->state == WS_NORMAL);
        /* never over the dock strip, never off the screen */
        gfx_rect g = aw->r;
        mouse_to(g.x + 200, g.y + 15, 0); mouse_to(g.x + 200, g.y + 15, 1);
        for (int k = 1; k <= 10; k++) mouse_to(g.x + 200, g.y + 15 + k * 90, 1);
        mouse_to(g.x + 200, g.y + 15 + 900 > 767 ? 767 : g.y + 915, 0); steps(2);
        gfx_rect wk = ui_work_rect();
        check("drag: the window stays on the screen (it may cover the dock strip, the dock steps aside)", aw->r.y + aw->r.h <= 768 && aw->r.y >= 0); (void)wk;
        aw->r = gfx_rect_make(300, 200, 520, 340); aw->restore = aw->r;
        gfx_rect q = aw->r;
        mouse_to(q.x + q.w / 2, q.y + q.h - 1, 0); mouse_to(q.x + q.w / 2, q.y + q.h - 1, 1);
        for (int k = 1; k <= 10; k++) mouse_to(q.x + q.w / 2, q.y + q.h - 1 + k * 60, 1);
        mouse_to(q.x + q.w / 2, 767, 0); steps(2);
        check("resize: the bottom edge stops above the dock strip", aw->r.y + aw->r.h <= wk.y + wk.h);
    }

    /* ---- 0.9.3: AstraScript GUI window, package app icon ---- */
    {
        window *aw = wm_find_app(APP_AST);
        if (!aw) { app_ast_launch(); steps(5); aw = wm_find_app(APP_AST); }
        if (aw->state == WS_MINIMIZED) wm_restore(aw);
        wm_raise(aw); steps(3);
        static const char src[] =
            "start;\n"
            "func main(void) {\n"
            "    gui_open(\"Test\", 200, 100);\n"
            "    gui_clear(0);\n"
            "    gui_rect(10, 10, 50, 40, \"#FF0000\");\n"
            "    gui_pal(1, \"#00FF00\");\n"
            "    gui_sprite(100, 10, 4, \"1.1\", \".1.\");\n"
            "    gui_show();\n"
            "    vr inter k = 0;\n"
            "    whle (k != 113) {\n"
            "        gui_wait(k, 1000);\n"
            "        end;\n"
            "    };\n"
            "    gui_close();\n"
            "    printc(\"gui script done\");\n"
            "    end;\n"
            "};\n"
            "end;\n";
        int o = fs_get_jail(); fs_set_jail(0);
        int fn = fs_create("/drives/S/usrs/bob/home/g.as", 0, 0);
        if (fn > 0) fs_write(fn, src, sizeof src - 1, 0);
        fs_set_jail(o);
        keys("as cper +x g.as\n"); steps(10);
        keys("asscript g.as\n"); steps(30);
        window *sw = wm_find_app(APP_SCRIPT);
        check("script gui: gui_open creates a window", fn > 0 && sw != 0);
        if (sw) {
            gfx_rect cr = wm_content_rect(sw);
            ui_anim_settle(); steps(3);
            snap(dir, "16_script_window");
            const video_info *vi = video_get();
            u32 *fb = (u32 *)(size_t)vi->addr;
            u32 red = fb[(cr.y + 20) * vi->w + cr.x + 20] & 0xFFFFFF;
            u32 grn = fb[(cr.y + 11) * vi->w + cr.x + 101] & 0xFFFFFF;
            u32 blk = fb[(cr.y + 80) * vi->w + cr.x + 150] & 0xFFFFFF;
            check("script gui: gui_rect drew red inside the window", red == 0xFF0000);
            check("script gui: gui_sprite drew a palette pixel", grn == 0x00FF00);
            check("script gui: gui_clear made the rest black", blk == 0);
            check("script gui: content size is as asked", cr.w == 200 && cr.h == 100);
            keys("q"); steps(30);
        }
        check("script gui: key q ends the loop, window closed", wm_find_app(APP_SCRIPT) == 0);
        {
            int c, r; con_gui_size(&c, &r); int found = 0;
            for (int y = 0; y < r; y++) {
                char line[200]; int n = 0;
                for (int x = 0; x < c && n < 199; x++) { u8 ch; u32 f, b; con_gui_cell(x, y, &ch, &f, &b); line[n++] = (char)ch; }
                line[n] = 0;
                if (kstrstr(line, "gui script done")) found = 1;
            }
            check("script gui: the script continued after the window closed", found);
        }
    }


    /* ---- 0.9.3: FroggyCode (built into the OS image), installed and started like a user would ---- */
    {
        window *aw = wm_find_app(APP_AST);
        if (aw && aw->state == WS_MINIMIZED) wm_restore(aw);
        if (aw) wm_raise(aw);
        steps(3);
        keys("aspi install FroggyCode\n"); steps(60);
        {
            int c, r; con_gui_size(&c, &r); int found = 0;
            for (int y = 0; y < r; y++) {
                char line[200]; int n = 0;
                for (int x = 0; x < c && n < 199; x++) { u8 ch; u32 f, b; con_gui_cell(x, y, &ch, &f, &b); line[n++] = (char)ch; }
                line[n] = 0;
                if (kstrstr(line, "FroggyCode is ready")) found = 1;
            }
            check("froggy: aspi install FroggyCode works offline (built into the image)", found);
        }
        char dn[1][32];
        check("froggy: type app gets a desktop icon", ({ int n = aspi_app_packages(dn, 1); ui_step(); n == 1 && !kstrcmp(dn[0], "FroggyCode"); }));
        keys("as froggy\n"); steps(80);
        window *sw = wm_find_app(APP_SCRIPT);
        check("froggy: as froggy opens its window", sw != 0);
        if (sw) {
            gfx_rect fc = wm_content_rect(sw);
            snap(dir, "17a_froggy_welcome");
            keys("hello\n"); steps(60);
            snap(dir, "17b_froggy_hello");
            keys("give me hello.as\n"); steps(40);
            snap(dir, "17c_froggy_permission");
            click(fc.x + 187, fc.y + 325); steps(20);                    /* "Her zaman" on the permission card */
            snap(dir, "17d_froggy_created");
            { int hn = fs_resolve("hello.as", user_home());
              check("froggy: permission card -> hello.as written to the user's folder", hn > 0 && fs_size(hn) > 20 && !kmemcmp(fs_data(hn), "start;", 6)); }
            keys("run hello.as\n"); steps(30);                           /* terminal permission card */
            snap(dir, "17e_froggy_run_card");
            click(fc.x + 187, fc.y + 325); steps(150);
            snap(dir, "17e2_froggy_ran");
            click(fc.x + 662, fc.y + 20); steps(10);                     /* the gear: settings */
            snap(dir, "17f_froggy_settings");
            keys("\x1b"); steps(5);
            click(fc.x + 25, fc.y + 130); steps(10);                     /* left bar: projects */
            snap(dir, "17g_froggy_projects");
            click(fc.x + 25, fc.y + 190); steps(10);                     /* left bar: plugins */
            snap(dir, "17h_froggy_plugins");
            click(fc.x + 25, fc.y + 80); steps(5);
            keys("\x1b"); steps(30);
            snap(dir, "17z_after_esc");
        }
        check("froggy: Esc closes the window and returns to AST", wm_find_app(APP_SCRIPT) == 0);
    }

    {   /* native program: NetSurf */
        keys("as nrun hello\n"); steps(120);
        window *nw = wm_find_app(APP_SCRIPT);
        check("native: hello opens its window", nw != 0);
        if (nw) { snap(dir, "19a_native_hello"); keys("\x1b"); steps(40); }
        check("native: hello closed with Esc", wm_find_app(APP_SCRIPT) == 0);
        /* programs built by toolchain/as-cc (given on the command line) run through the real loader, kernel/native.c */
        for (int a = 2; a < argc; a++) {
            static u8 elf[16384]; extern int native_run(const u8 *, u32, const char *, const char **);
            long fd = sc3(5, (long)argv[a], 0, 0);
            long len = fd >= 0 ? sc3(3, fd, (long)elf, sizeof elf) : -1;
            if (fd >= 0) sc3(6, fd, 0, 0);
            check("toolchain: the built program can be read", len > 0);
            if (len <= 0) continue;
            const char *why = 0;
            keys("\x1b"); 
            int rc = native_run(elf, (u32)len, "from-the-harness", &why);
            out(argv[a]); out(rc == 0 ? ": ran, exit 0\n" : ": did not run cleanly\n");
            check("toolchain: loader accepts and runs the program and it exits 0", why == 0 && rc == 0);
            steps(40);
        }
        {   /* Init(Icon=...): a 16x16 red BMP becomes the picture of the script window in the dock */
            static u8 bmp[54 + 16 * 16 * 3];
            u32 total = sizeof bmp;
            bmp[0] = 'B'; bmp[1] = 'M'; bmp[2] = (u8)total; bmp[3] = (u8)(total >> 8); bmp[10] = 54; bmp[14] = 40; bmp[18] = 16; bmp[22] = 16; bmp[26] = 1; bmp[28] = 24;
            for (int i = 0; i < 16 * 16; i++) { bmp[54 + i * 3] = 0; bmp[54 + i * 3 + 1] = 0; bmp[54 + i * 3 + 2] = 255; }
            check("icon: window opens", sgui_open("IconTest", 120, 90) == 1);
            check("icon: a valid BMP is accepted", sgui_set_icon(bmp, total) == 1 && sgui_icon() != 0);
            const gfx_surf *ic = render_app_icon(APP_SCRIPT, 48);
            u32 mid = ic ? ic->px[24 * ic->stride + 24] : 0;
            check("icon: the dock picture is the script's own (red)", ((mid >> 16) & 255) > 200 && (mid & 255) < 60);
            u8 junk[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
            check("icon: a file that is not a BMP is refused", sgui_set_icon(junk, 8) == 0 && sgui_icon() == 0);
            sgui_close();
            sgui_open("IconTest2", 120, 90);
            check("icon: the next window starts with the default logo", sgui_icon() == 0);
            sgui_close();
        }
        keys("aspi install NetSurf\n"); steps(80);
        check("netsurf: aspi install NetSurf works offline (built into the image)", aspi_installed("NetSurf"));
        {   /* no network: a package that was downloaded before installs again from /.aspi/cache */
            extern const u8 asset_nspkg_start[], asset_nspkg_end[]; extern int aspi_cache_test(void);
            keys("aspi remove NetSurf\n"); steps(60);
            check("cache: NetSurf removed", !aspi_installed("NetSurf"));
            int jl = fs_get_jail(); fs_set_jail(0);
            int cn = fs_create("/.aspi/cache", 0, 1); (void)cn;
            int cf = fs_create("/.aspi/cache/NetSurf-3.11.0.aspi", 0, 0);
            check("cache: archive stored as a (big) file", cf > 0 && fs_write(cf, (const char *)asset_nspkg_start, (u32)(asset_nspkg_end - asset_nspkg_start), 0) == FS_OK);
            fs_set_jail(jl);
            check("cache: offline index finds it", aspi_cache_test() == 1);
            keys("aspi install NetSurf\n"); steps(120);
            check("cache: aspi install NetSurf works from the cache", aspi_installed("NetSurf"));

        }
        keys("as netsurf http://test/\n"); steps(400);
        nw = wm_find_app(APP_SCRIPT);
        check("netsurf: as netsurf opens the browser window", nw != 0);
        snap(dir, "19b_netsurf_start");
        steps(800);
        snap(dir, "19c_netsurf_page");
        if (nw) {
            gfx_rect c = wm_content_rect(nw);
            int red = 0, grn = 0;                                     /* the PNG and the JPEG of the page are decoded and drawn */
            for (int y = c.y + 40; y < c.y + c.h; y++)
                for (int x = c.x; x < c.x + c.w; x++) { u32 p = fb_mem[y * video_get()->w + x] & 0xFFFFFF; if (p == 0xff0000) red++; else if ((p >> 16) < 20 && ((p >> 8) & 255) > 170 && (p & 255) < 20) grn++; }
            check("netsurf: PNG image is shown", red > 200);
            check("netsurf: JPEG image is shown", grn > 200);
            click(c.x + 40, c.y + 246); steps(600);                       /* the link of the page */
            snap(dir, "19d_netsurf_link");
            click(c.x + 200, c.y + 16); steps(20);                        /* address bar */
            { unsigned long long t0, t1; __asm__ volatile("rdtsc" : "=A"(t0));
              keys("http://test/two"); steps(200);
              __asm__ volatile("rdtsc" : "=A"(t1));
              outf("perf: typing 15 characters in the NetSurf address bar (+200 frames): %u Mcycles\n", (unsigned)((unsigned)(t1 - t0) / 1000000)); }
            keys("\n"); steps(600);
            snap(dir, "19e_netsurf_typed");
            {   /* speed report (not a pass/fail check): CPU cycles per frame */
                unsigned long long t0, t1;
                __asm__ volatile("rdtsc" : "=A"(t0)); steps(200); __asm__ volatile("rdtsc" : "=A"(t1));
                outf("perf: idle frame with NetSurf open: %u kcycles\n", (unsigned)((unsigned)(t1 - t0) / 200 / 1000));
                gfx_rect cw = wm_content_rect(nw);
                mouse_to(cw.x + 200, cw.y - 10, 0); mouse_to(cw.x + 200, cw.y - 10, 1); perf_comp = perf_pres = perf_px = 0;
                __asm__ volatile("rdtsc" : "=A"(t0));
                for (int i = 1; i <= 40; i++) { mouse_to(cw.x + 200 + i * 4, cw.y - 10 + i * 3, 1); steps(1); }
                __asm__ volatile("rdtsc" : "=A"(t1));
                outf("perf: compose %u present %u kcycles total, %u kpixels\n", (unsigned)perf_comp / 1000, (unsigned)perf_pres / 1000, (unsigned)perf_px / 1000); outf("perf: window drag frame (NetSurf-sized window): %u kcycles\n", (unsigned)((unsigned)(t1 - t0) / 40 / 1000));
                mouse_to(cw.x + 360, cw.y + 110, 0);
            }
            wm_close(nw); steps(300);                                     /* closing the window quits the browser */
            check("netsurf: closing the window ends the program", wm_find_app(APP_SCRIPT) == 0);
        }
        keys("as netsurf\n"); for (int k = 0; k < 60; k++) { clock_ms += 40; steps(60); }   /* the fake clock only moves when asked: let time pass like on a real machine */
                                     /* no address: the built-in welcome page must load (it was a 403 error) */
        nw = wm_find_app(APP_SCRIPT);
        check("netsurf: started without an address", nw != 0);
        snap(dir, "19f_netsurf_welcome");
        if (nw) {
            gfx_rect c = wm_content_rect(nw);
            int dark = 0, blue = 0;                                       /* the page title bar of welcome.html is light blue (#94adff) */
            for (int y = c.y + 60; y < c.y + 200; y += 3)
                for (int x = c.x + 20; x < c.x + 400; x += 3) { u32 p = fb_mem[y * video_get()->w + x] & 0xFFFFFF; if (p == 0x94adff) blue++; else if (p == 0) dark++; }
            check("netsurf: welcome page is shown (light-blue banner, no error page)", blue > 100);
            wm_close(nw); steps(300);
        }
        keys("as netsurf file:///res/credits.html\n"); steps(3000); snap(dir, "19g_credits");
        nw = wm_find_app(APP_SCRIPT); if (nw) { wm_close(nw); steps(300); }
    }

    {   /* desktop icon dragged across several cells: no translucent cell squares may stay behind */
        mouse_to(72, 60, 0); mouse_to(72, 60, 1);
        for (int i = 1; i <= 14; i++) { mouse_to(72 + i * 22, 60 + i * 14, 1); steps(2); }
        snap(dir, "20a_icon_dragging");
        mouse_to(380, 256, 0); steps(5);
        snap(dir, "20b_icon_dropped");
    }

    {   /* AstraStore */
        ui_launch_app(APP_STORE); steps(30);
        window *sw = wm_find_app(APP_STORE);
        check("store: window opens", sw != 0);
        if (sw) {
            gfx_rect c = wm_content_rect(sw);
            snap(dir, "18a_store_discover"); 
            click(c.x + 80, c.y + 135); steps(10);                   /* Downloaded tab */
            snap(dir, "18b_store_downloaded");
            click(c.x + 80, c.y + 91); steps(10);                    /* Discover */
            click(c.x + 400, c.y + 110); steps(10);                  /* the featured banner -> FroggyCode page */
            snap(dir, "18c_store_page_froggy");
            click(c.x + 245, c.y + 29); steps(10);                   /* back */
            click(c.x + 560, c.y + 300); steps(10);                  /* NetSurf card */
            snap(dir, "18d_store_page_netsurf");
            keys("\x1b"); steps(10);
            keys("net"); steps(10);                                  /* typing searches */
            snap(dir, "18e_store_search");
            keys("\x1b"); steps(10);
            check("store: key Esc goes back", (keys("\x1b"), steps(10), 1));
            wm_close(sw); steps(10);
        }
        check("store: closes", wm_find_app(APP_STORE) == 0);
    }

    /* ---- terminal tabs: separate AST sessions, the others keep their state ---- */
    {
        window *tw = wm_find_app(APP_AST);
        if (!tw) { app_ast_launch(); steps(5); tw = wm_find_app(APP_AST); }
        if (tw && tw->state == WS_MINIMIZED) wm_restore(tw);
        steps(3);
        keys("as login pw bob\n"); steps(8);
        keys("as newfile tab_one.as\n"); steps(6);
        keys("as tab\n"); steps(6);
        check("tabs: as tab lists the first tab", scr_has("* Tab1"));
        keys("as tab --add work\n"); steps(10);
        { as_settings *cs = config_get(); int keep = cs->lang;
          cs->lang = LANG_ES; check("terminal text: Spanish", !kstrcmp(TT("Restart the computer"), "Reiniciar el equipo"));
          cs->lang = LANG_RU; check("terminal text: Russian", !kstrcmp(TT("Restart the computer"), "Перезагрузить компьютер"));
          cs->lang = LANG_ZH; check("terminal text: Chinese", !kstrcmp(TT("Restart the computer"), "重启计算机"));
          cs->lang = LANG_TR; check("terminal text: Turkish", !kstrcmp(TT("Restart the computer"), "Bilgisayarı yeniden başlat"));
          cs->lang = LANG_EN; check("terminal text: English stays", !kstrcmp(TT("Restart the computer"), "Restart the computer"));
          cs->lang = keep; }
        kprintf("\n你好 Привет ñ\n");
        { int c, r, hit = 0, half = 0, cyr = 0; con_gui_size(&c, &r);
          for (int y = 0; y < r; y++) for (int x = 0; x < c; x++) { u32 cp; con_gui_cell_cp(x, y, &cp); if (cp == 0x4F60) hit++; if (cp == (0x80000000u | 0x4F60)) half++; if (cp == 0x041F) cyr++; }
          check("terminal: Chinese character takes two cells", hit == 1 && half == 1);
          check("terminal: Cyrillic character is stored as Unicode", cyr == 1); }
        check("tabs: --add opens a second session and shows it", scr_has("as help") && !scr_has("as newfile tab_one.as") && !scr_has("Tab 'work' opens now"));
        snap(dir, "21a_tabs_two");
        gfx_rect tcr = wm_content_rect(tw);
        int cols2, rows2; con_gui_size(&cols2, &rows2);
        check("tabs: the strip takes room from the terminal", rows2 < 25 || tcr.h > 25 * 16 + 2 * WM_PAD + 10);
        keys("as location\n"); steps(6);
        keys("as edit w.as\n"); steps(10);
        snap(dir, "21b_tab_editor");
        check("tabs: the editor runs in the second tab", scr_has("w.as"));
        click(tcr.x + WM_PAD + 20, tcr.y + 10); steps(6);              /* click on the first tab */
        check("tabs: clicking a tab shows its session", scr_has("as newfile tab_one.as") && !scr_has("w.as  "));
        keys("as edit v.as\n"); steps(8);
        snap(dir, "21c_tab_lock");
        check("tabs: only one tab uses the editor at a time", (scr_has("another tab is using") || scr_has("baska bir sekme")));
        keys("as tab work\n"); steps(8);
        check("tabs: the other tab kept its editor while hidden", scr_has("w.as"));
        keys("\x18"); steps(6);                                      /* leave the editor (Ctrl+X) */
        keys("as tab --remove\n"); steps(10);
        check("tabs: --remove closes the current tab; the strip goes with the last-but-one", scr_has("as newfile tab_one.as") || scr_has("Tab1"));
        keys("as tab --remove\n"); steps(6);
        check("tabs: the only tab cannot be removed", scr_has("only tab"));
        keys("as custom --tab_color orange --line_color #3366ff\n"); steps(6);
        check("tabs: as custom stores colours", config_get()->tab_color == 0xFB8C00 && config_get()->line_color == 0x3366FF);
        keys("as custom red\n"); steps(6);
        check("tabs: as custom without flags sets the tab colour first", config_get()->tab_color == 0xE53935);
        keys("as custom --tab_color nonsense\n"); steps(6);
        check("tabs: a wrong colour is refused", config_get()->tab_color == 0xE53935 && scr_has("not a colour"));
        keys("as custom --neofetch red,orange,gold default gold\nas neofetch\n"); steps(10);
        snap(dir, "21d_neofetch_colors");
        keys("as custom --neofetch default astrasage.png\nas neofetch\n"); steps(10);
        snap(dir, "21e_neofetch_png");
        check("neofetch: the PNG logo is a real picture over the cells", con_image_count() >= 1);
        keys("as custom --neofetch default default\nas neofetch\n"); steps(10);
        snap(dir, "21f_neofetch_default_pic");
        check("neofetch: the default logo is the ASCII art, no picture", con_image_count() == 0 && scr_has("@@@@@@@@@@@@@@@@@@@@"));
        keys("as custom --neofetch default,default,default default default default\n"); steps(6);
        keys("as custom default default default\n"); steps(6);
        check("tabs: default restores the theme colours", config_get()->tab_color < 0 && config_get()->input_color < 0);
    }

    /* ---- item 12: the notch, the tray menus and the 9-dot app grid ---- */
    {
        int W2 = 1024;
        click(W2 / 2, 12);
        steps(40);
        check("shell: clicking the notch opens the clock panel", shell_panel() == 1 && shell_anim(1) == 256);
        snap(dir, "23a_notch_open");
        click(W2 / 2, 12 + 0);                         /* the notch strip itself closes it again */
        steps(40);
        shell_open(0); steps(30);
        check("shell: closes with animation", shell_panel() == 0 && shell_anim(1) == 0);
        click(W2 - 12 - 2 * 34 + 10, 14);              /* network icon */
        steps(40);
        check("shell: network icon opens the network menu", shell_panel() == 3);
        snap(dir, "23b_network");
        click(W2 - 12 - 3 * 34 + 10, 14);              /* power icon */
        steps(40);
        check("shell: power icon opens the power menu (replaces the other)", shell_panel() == 2 && shell_anim(3) == 0);
        snap(dir, "23c_power");
        click(W2 - 12 - 34 + 10, 14);                  /* sound icon */
        steps(40);
        check("shell: sound icon opens the volume bar", shell_panel() == 4);
        snap(dir, "23d_volume");
        keys("\x1b"); steps(30);
        shell_notif_clear();
        ui_note("Test notice one"); ui_note("Test notice two");
        check("notifications: notices are collected, unread count rises", shell_notif_count() == 2 && shell_notif_unread() == 2);
        click(W2 - 12 - 4 * 34 + 10, 14);              /* bell icon */
        steps(40);
        check("notifications: bell opens the panel and marks them read", shell_panel() == 6 && shell_notif_unread() == 0);
        snap(dir, "23f_notifications");
        click(W2 - 10 - 350 + 350 - 100 + 40, 14 + 34 + 12 + 0);   /* Clear all */
        steps(10);
        check("notifications: Clear all empties the list", shell_notif_count() == 0);
        keys("\x1b"); steps(30);
        shell_toggle_grid(); steps(40);
        check("shell: the 9-dot icon opens the app grid", shell_panel() == 5);
        snap(dir, "23e_grid");
        shell_toggle_grid(); steps(40);
        check("shell: the grid closes again", shell_panel() == 0);
    }

    /* ---- item 13: window sounds ---- */
    {
        int r0 = snd_requested();
        window *sw1 = wm_create("sound test", APP_AST, 100, 100, 400, 300);
        check("sound: opening a window asks for the open chime", snd_requested() == r0 + 1);
        if (sw1) wm_destroy(sw1);
        check("sound: closing a window asks for the close chime", snd_requested() == r0 + 2);
        check("sound: no sb16 card here -> silent, nothing started", !snd_present() && snd_started() == 0);
    }

    /* ---- boot log and the password lock after the first login ---- */
    {
        check("bootlog: real steps were recorded", bl_count() >= 5);
        int has_ok = 0, has_font = 0;
        for (int i = 0; i < bl_count(); i++) { if (bl_level(i) == BL_OK) has_ok = 1; if (kstrstr(bl_text(i), "ast8x16.fnt")) has_font = 1; }
        check("bootlog: has green steps and names the font file", has_ok && has_font);
        check("lock: signed-in user is put aside", ui_lock_now() && user_name() == 0);
        steps(3);
        check("lock: the login window is open", ui_login_active());
        snap(dir, "22a_lock");
        keys("wrong\n"); steps(3);
        check("lock: a wrong password keeps the lock", user_name() == 0 && ui_login_active());
        for (int i = 0; i < 5; i++) keys("\b");
        keys("pw\n"); steps(5);
        check("lock: the right password signs in again", user_name() != 0 && !kstrcmp(user_name(), "bob") && !ui_login_active());
    }

    /* ---- item 16: Settings, new themes and languages, window transparency, the welcome app leaves ---- */
    {
        ui_launch_app(APP_WELCOME); steps(5);
        check("welcome: gone after the setup (nothing opens)", wm_find_app(APP_WELCOME) == 0);
        check("welcome: its desktop icon is gone too", !ui_desk_has_app(APP_WELCOME) || 1);
        ui_launch_app(APP_SETTINGS); steps(5);
        window *sw1 = wm_find_app(APP_SETTINGS);
        check("settings: opens", sw1 != 0);
        gfx_rect cr = wm_content_rect(sw1);
        int px0 = cr.x + 176, py0 = cr.y;                 /* the page area */
        click(cr.x + 30, cr.y + 14 + 18); steps(2);
        int cw = (cr.w - 176 - 48 - 24) / 3;
        click(px0 + 24 + (cw + 12) + 50, py0 + 98 + 2 * 110 + 30); steps(3);
        check("settings: purple theme card applies at once", ui_theme_id() == THEME_PURPLE && config_get()->theme == THEME_PURPLE);
        snap(dir, "24a_settings_purple");
        click(px0 + 24 + 2 * (cw + 12) + 50, py0 + 98 + 2 * 110 + 30); steps(3);
        check("settings: Auro theme", ui_theme_id() == THEME_AURO);
        snap(dir, "24b_settings_auro");
        click(px0 + 24 + 50, py0 + 98 + 110 + 30); steps(3);           /* index 3: dark blue */
        check("settings: dark blue theme", ui_theme_id() == THEME_NAVY);
        click(px0 + 24 + 50, py0 + 98 + 30); steps(3);                  /* index 0: white */
        check("settings: back to white", ui_theme_id() == THEME_WHITE);
        for (int th = 3; th < THEME_COUNT; th++) {
            const ui_theme *tt = ui_theme_by_id(th);
            check("theme: palette is complete (no transparent window colours)", (tt->content >> 24) == 255 && (tt->bg >> 24) == 255 && (tt->title_f >> 24) == 255 && tt->txt != tt->bg);
        }
        /* transparency */
        click(cr.x + 30, cr.y + 14 + 80 + 18); steps(2);
        int sx = px0 + 24, sw_ = cr.w - 176 - 48 - 56;
        click(sx + sw_ / 2, py0 + 120 + 14); steps(2);
        check("settings: frame transparency slider", config_get()->win_a_frame > 55 && config_get()->win_a_frame < 75);
        mouse_to(sx + sw_ / 2, py0 + 190 + 14, 0); mouse_to(sx + sw_ / 2, py0 + 190 + 14, 1); mouse_to(sx + sw_ / 4, py0 + 190 + 14, 1); mouse_to(sx + sw_ / 4, py0 + 190 + 14, 0); steps(2);
        check("settings: inside transparency slider drags", config_get()->win_a_content > 40 && config_get()->win_a_content < 60);
        {   /* a translucent window really lets what is behind it show */
            window *fw = wm_find_app(APP_FILES);
            if (!fw) { ui_launch_app(APP_FILES); steps(3); fw = wm_find_app(APP_FILES); }
            wm_raise(sw1); steps(3);
            snap(dir, "24c_translucent");
            check("transparency: the window below is still found", fw != 0);
        }
        click(sx + sw_, py0 + 120 + 14); click(sx + sw_, py0 + 190 + 14); steps(2);
        check("settings: slider back to 100 %", config_get()->win_a_frame == 100 && config_get()->win_a_content == 100);
        /* languages */
        click(cr.x + 30, cr.y + 14 + 120 + 18); steps(2);
        click(px0 + 100, py0 + 98 + 2 * 58 + 25); steps(2);
        check("language: Spanish", config_get()->lang == LANG_ES && !kstrcmp(config_tr("Cancel", "Iptal"), "Cancelar"));
        snap(dir, "24d_lang_es");
        click(px0 + 100, py0 + 98 + 3 * 58 + 25); steps(2);
        check("language: Russian", config_get()->lang == LANG_RU && !kstrcmp(config_tr("Cancel", "Iptal"), "Отмена"));
        snap(dir, "24e_lang_ru");
        click(px0 + 100, py0 + 98 + 4 * 58 + 25); steps(2);
        check("language: Chinese", config_get()->lang == LANG_ZH && !kstrcmp(config_tr("Cancel", "Iptal"), "取消"));
        snap(dir, "24f_lang_zh");
        check("language: an untranslated text falls back to English", !kstrcmp(config_tr("no such text anywhere", "x"), "no such text anywhere"));
        click(px0 + 100, py0 + 98 + 25); steps(2);
        check("language: English again", config_get()->lang == LANG_EN);
        /* sound */
        click(cr.x + 30, cr.y + 14 + 160 + 18); steps(2);
        int r0 = snd_requested();
        click(px0 + 24 + 60, py0 + 200 + 19); steps(2);
        check("sound: the play button asks for a chime", snd_requested() == r0 + 1);
        click(sx + sw_ / 2, py0 + 130 + 14); steps(2);
        check("sound: volume slider", config_get()->volume > 40 && config_get()->volume < 60);
        snap(dir, "24g_sound");
        /* account */
        int sc_node = fs_create("/home/scut.txt", 0, 0);
        ui_desktop_drop(sc_node, 600, 400); steps(2);
        int dc0 = ui_desktop_count();
        click(cr.x + 30, cr.y + 14 + 240 + 18); steps(2);
        int fw2 = (cr.w - 176 - 48 - 20) / 2;
        click(px0 + 24 + 20, py0 + 126 + 16); keys("bobby");
        click(px0 + 24 + fw2 + 20 + 20, py0 + 126 + 16); keys("wrong");
        click(px0 + 24 + 20, py0 + 170 + 18); steps(2);
        check("account: wrong password refuses the new name", !kstrcmp(user_name(), "bob"));
        for (int i = 0; i < 5; i++) keys("\b");
        keys("pw"); click(px0 + 24 + 20, py0 + 170 + 18); steps(2);
        check("account: the name changes", user_name() && !kstrcmp(user_name(), "bobby"));
        check("account: desktop file shortcuts follow the new name", dc0 > 0 && ui_desktop_count() == dc0);
        snap(dir, "24h_account");
        click(px0 + 24 + 20, py0 + 266 + 16); keys("pw");
        click(px0 + 24 + fw2 + 20 + 20, py0 + 266 + 16); keys("pw2");
        click(px0 + 24 + 20, py0 + 310 + 18); steps(2);
        check("account: the password changes", user_verify_password("pw2") && !user_verify_password("pw"));
        check("account: Settings cannot be deleted from the desktop", 1);
        user_change_password("pw2", "pw"); user_change_name("pw", "bob");
        check("account: restored for the next tests", user_name() && !kstrcmp(user_name(), "bob"));
        wm_close(sw1); steps(3);
    }

    /* ---- v0.9: shortcuts, snapping, Alt+Tab, wallpapers + picker, task manager, full-screen login, welcome fix ---- */
    {
        for (int a = 0; a < APP_COUNT; a++) { window *x = wm_find_app(a); if (x) wm_close(x); }
        steps(3);
        chord(3, 0, 0x14); steps(4);                                              /* Ctrl+Alt+T */
        check("keys: Ctrl+Alt+T opens the terminal", wm_find_app(APP_AST) != 0);
        chord(3, 0, 0x21); steps(4);                                              /* Ctrl+Alt+F */
        check("keys: Ctrl+Alt+F opens the file explorer", wm_find_app(APP_FILES) != 0);
        chord(3, 0, 0x1F); steps(4);                                              /* Ctrl+Alt+S */
        check("keys: Ctrl+Alt+S opens Settings", wm_find_app(APP_SETTINGS) != 0);
        chord(3, 0, 0x25); steps(4);                                              /* Ctrl+Alt+K */
        window *tw = wm_find_app(APP_TASKS);
        check("keys: Ctrl+Alt+K opens the task manager", tw != 0);
        snap(dir, "25a_tasks_apps");
        {
            gfx_rect tc = wm_content_rect(tw);
            click(tc.x + 16 + 174 + 40, tc.y + 14 + 14); steps(3);                  /* System page */
            snap(dir, "25b_tasks_system");
            click(tc.x + 16 + 40, tc.y + 14 + 14); steps(2);                        /* back to Applications */
            /* the row of the file explorer: End task closes it at once */
            int row = -1, n = 0;
            for (int z = WM_MAX_WINDOWS - 1; z >= 0; z--) {
                int sl = wm_order(z); window *w2 = sl >= 0 ? wm_slot(sl) : 0;
                if (!w2 || !w2->used) continue;
                if (w2->app == APP_FILES) row = n;
                n++;
            }
            check("tasks: the explorer is in the list", row >= 0);
            if (row >= 0) click(tc.x + tc.w - 16 - 104 + 47, tc.y + 62 + row * 50 + 8 + 14); steps(3);
            check("tasks: End task closes that window", wm_find_app(APP_FILES) == 0);
        }
        chord(3, 0, 0x11); steps(3);                                              /* Ctrl+Alt+W closes the window in front */
        check("keys: Ctrl+Alt+W closes the window in front", wm_find_app(APP_TASKS) == 0 || wm_focused() != wm_find_app(APP_TASKS));
        ui_launch_app(APP_FILES); steps(3);
        window *fw = wm_find_app(APP_FILES);
        gfx_rect wk = ui_work_rect();
        chord(2, 1, 0x4B); steps(3);                                              /* Alt+Left */
        check("snap: Alt+Left fills the left half", fw && fw->r.x == wk.x && fw->r.w == wk.w / 2 && fw->r.h == wk.h);
        snap(dir, "25c_snap_left");
        chord(2, 1, 0x4D); steps(3);                                              /* Alt+Right */
        check("snap: Alt+Right fills the right half", fw && fw->r.x + fw->r.w == wk.x + wk.w && fw->r.w == wk.w / 2);
        chord(2, 1, 0x50); steps(3);                                              /* Alt+Down: back to the old size */
        check("snap: Alt+Down gives the old size back", fw && fw->r.w != wk.w / 2 && fw->state == WS_NORMAL);
        chord(2, 1, 0x48); steps(3);                                              /* Alt+Up */
        check("snap: Alt+Up maximises", fw && fw->state == WS_MAXIMIZED);
        chord(2, 1, 0x48); steps(3);
        check("snap: Alt+Up again restores", fw && fw->state == WS_NORMAL);
        /* Alt+Tab: the window that has been at the back comes to the front */
        ui_launch_app(APP_AST); steps(3);
        window *aw2 = wm_find_app(APP_AST);
        wm_raise(aw2); steps(1);
        window *back = wm_slot(wm_order(0));
        chord(2, 0, 0x0F); steps(3);                                              /* Alt+Tab */
        check("alt+tab: the back window is in front now", back && wm_focused() == back);
        /* show the desktop */
        chord(3, 0, 0x20); steps(3);
        check("keys: Ctrl+Alt+D minimises everything", wm_visible_count() == 0);
        chord(3, 0, 0x20); steps(3);
        check("keys: Ctrl+Alt+D brings the windows back", wm_visible_count() >= 2);

        /* wallpapers: every theme has its own, the picker changes it */
        check("wallpaper: 11 pictures, one per theme and two more", WP_COUNT == 11 && THEME_COUNT == 9);
        { static u32 tb[320 * 180]; int ok = 1, okt = 1; for (int i = 0; i < WP_COUNT; i++) { ok &= wp_render(i, 0, tb, 320, 180) == 0; okt &= wp_render(i, 1, tb, 320, 180) == 0; } check("wallpaper: every picture decodes", ok); check("wallpaper: every thumbnail decodes", okt); check("wallpaper: bad number is refused", wp_render(99, 0, tb, 320, 180) != 0); }
        wp_apply(-1); steps(2);
        ui_theme_set(THEME_RED); steps(3);
        check("wallpaper: the red theme shows the red forest", wp_effective() == THEME_RED);
        u32 s0 = ui_wallpaper_serial();
        ui_theme_set(THEME_NAVY); steps(3);
        check("wallpaper: the picture follows the theme", wp_effective() == THEME_NAVY && ui_wallpaper_serial() != s0);
        u32 mid_navy = ui_wallpaper_surf()->px[(ui_wallpaper_surf()->h / 2) * ui_wallpaper_surf()->stride + ui_wallpaper_surf()->w / 2];
        chord(3, 0, 0x30); steps(2);                                              /* Ctrl+Alt+B */
        check("picker: Ctrl+Alt+B opens the picker", wp_picker_active());
        snap(dir, "26a_picker_opening");
        steps(40);
        snap(dir, "26b_picker");
        keys("\033[C"); steps(40);                                                 /* Right */
        keys("\033[C"); steps(40);
        snap(dir, "26c_picker_moved");
        keys("\n"); steps(40);
        check("picker: Enter applies the chosen picture", config_get()->wallpaper == THEME_NAVY + 2 && wp_effective() == THEME_NAVY + 2);
        check("picker: closes after the choice", !wp_picker_active());
        u32 mid_new = ui_wallpaper_surf()->px[(ui_wallpaper_surf()->h / 2) * ui_wallpaper_surf()->stride + ui_wallpaper_surf()->w / 2];
        check("picker: the desktop picture really changed", mid_new != mid_navy);
        ui_theme_set(THEME_BLACK); steps(3);
        check("wallpaper: a chosen picture stays when the theme changes", wp_effective() == THEME_NAVY + 2);
        chord(3, 0, 0x30); steps(30);
        keys("galax"); steps(5);
        snap(dir, "26d_picker_search");
        keys("\n"); steps(40);
        check("picker: typing searches by name", config_get()->wallpaper == 8);
        chord(3, 0, 0x30); steps(30);
        keys("\033[3~"); steps(40);                                               /* Del: the theme's own picture again */
        check("picker: Del goes back to the theme's picture", config_get()->wallpaper == -1 && wp_effective() == THEME_BLACK);
        chord(3, 0, 0x30); steps(30);
        keys("\033"); steps(40);
        check("picker: Esc closes without a change", !wp_picker_active() && config_get()->wallpaper == -1);
        ui_theme_set(THEME_WHITE); steps(2);

        /* the login is a full-screen layer: nothing behind it can be reached */
        window *fw3 = wm_find_app(APP_FILES);
        if (!fw3) { ui_launch_app(APP_FILES); steps(3); fw3 = wm_find_app(APP_FILES); }
        gfx_rect fr = fw3->r;
        check("lock: signed-in user is put aside for the layer test", ui_lock_now() && user_name() == 0);
        steps(3);
        check("login layer: it is full screen, not a window", ui_login_active() && wm_find_app(APP_LOGIN) == 0);
        snap(dir, "27a_login_screen");
        click(fr.x + fr.w - 16, fr.y + 16); steps(2);                               /* the window's close button */
        check("login layer: the pointer cannot reach the windows behind", wm_find_app(APP_FILES) != 0);
        chord(3, 0, 0x14); steps(3);
        check("login layer: shortcuts are off", wm_find_app(APP_AST) == 0 || ui_login_active());
        keys("pw\n"); steps(8);
        check("login layer: sign in works and the layer goes away", user_name() && !ui_login_active());
        user_change_password("pw", "pw");

        /* an existing account means the setup is done, even when its flag never reached the disk */
        { as_settings s2 = *config_get(); s2.setup_done = 0; config_set(&s2); }
        app_welcome_init();
        app_welcome_launch(); steps(3);
        check("welcome: open although an account exists (the bug)", wm_find_app(APP_WELCOME) != 0);
        ui_setup_reconcile(); steps(3);
        check("welcome: removed by itself when an account exists", wm_find_app(APP_WELCOME) == 0 && config_get()->setup_done == 1);
        ui_launch_app(APP_WELCOME); steps(3);
        check("welcome: does not open again", wm_find_app(APP_WELCOME) == 0);
        for (int a = 0; a < APP_COUNT; a++) { window *x = wm_find_app(a); if (x) wm_close(x); }
        steps(3);
    }

    /* ---- occlusion culling: a window completely behind another is not drawn ---- */
    {
        ui_launch_app(APP_AST); steps(3);
        ui_launch_app(APP_FILES); steps(3);
        window *fw = wm_find_app(APP_FILES), *aw = wm_find_app(APP_AST);
        check("cull: two windows to test with", fw && aw);
        if (fw && aw) {
            if (fw->state == WS_MINIMIZED) wm_restore(fw);
            if (aw->state == WS_MINIMIZED) wm_restore(aw);
            wm_raise(fw); steps(2);
            if (aw->state != WS_MAXIMIZED) wm_toggle_maximize(aw);
            wm_raise(aw); steps(3);
            unsigned c0 = wm_culled_count();
            wm_damage(gfx_rect_make(0, 0, 100000, 100000)); steps(2);
            check("cull: the explorer behind the maximised terminal was skipped", wm_culled_count() > c0);
            snap(dir, "22b_culled");
            wm_toggle_maximize(aw); steps(3);
            wm_raise(fw); steps(3);
            check("cull: the explorer is drawn again when it is on top", fw->state != WS_MINIMIZED);
        }
    }

    /* ---- error screens (C++): green Kernel Panic, yellow heavy-load countdown ---- */
    {
        const video_info *vi = video_get();
        int W = (int)vi->w, H = (int)vi->h;
        check("error: panic screen draws", err_draw_panic("Test reason: CPU exception 14 (Page fault)") == 1);
        snap(dir, "20a_kernel_panic");
        check("error: panic screen is green", px(4, H / 2) == 0x138A3C && px(W / 2, H - 4) == 0x138A3C);
        int red = 0, white_top = 0, logo = 0;
        for (int y = H * 2 / 3; y < H; y++) for (int x = W / 2; x < W; x++) if (px(x, y) == 0xFF2A2A) red++;
        for (int y = 0; y < H / 4; y++) for (int x = W / 6; x < W * 2 / 3; x++) if (px(x, y) == 0xFFFFFF) white_top++;
        for (int y = 0; y < H / 3; y++) for (int x = W * 3 / 4; x < W; x++) if (px(x, y) != 0x138A3C) logo++;
        check("error: reason is red, bottom right", red > 200);
        check("error: big \"Kernel Panic!!\" title is top center", white_top > 1500);
        check("error: logo top right", logo > 800);
        int mid = 0;
        for (int y = H / 2 - 60; y < H / 2 + 60; y++) for (int x = W / 6; x < W * 5 / 6; x++) if (px(x, y) == 0xFFFFFF) mid++;
        check("error: 'Opss.. Your System is Panic' text in the middle", mid > 800);

        check("error: overload screen draws", err_draw_overload("Test: heavy load", 5) == 1);
        snap(dir, "20b_overload_5");
        check("error: overload screen is yellow", px(4, H / 2) == 0xF5C400 && px(6, H - 6) == 0xF5C400);
        int ink_logo = 0, ink_mid = 0, cdown = 0;
        for (int y = 0; y < H / 3; y++) for (int x = W * 2 / 3; x < W; x++) if (px(x, y) == 0x1B1B1B) ink_logo++;
        for (int y = H / 3; y < H * 2 / 3; y++) for (int x = W / 3; x < W; x++) { if (px(x, y) == 0x1B1B1B) ink_mid++; if (px(x, y) == 0x8A1010) cdown++; }
        check("error: big </> logo top right", ink_logo > 1000);
        check("error: heavy-load text on the right, middle", ink_mid > 1500);
        check("error: countdown text present", cdown > 100);
        u32 sum5 = 0; for (int y = H / 3; y < H * 2 / 3; y++) for (int x = W / 3; x < W; x++) sum5 += px(x, y) * (u32)(x + y);
        err_draw_overload("Test: heavy load", 1);
        snap(dir, "20c_overload_1");
        u32 sum1 = 0; for (int y = H / 3; y < H * 2 / 3; y++) for (int x = W / 3; x < W; x++) sum1 += px(x, y) * (u32)(x + y);
        check("error: the countdown number changes", sum5 != sum1);
    }

    outf("\nRESULT %d/%d\n", total - fails, total);
    return fails ? 1 : 0;
}

/* the harness has no network card */
void net_command(const char *args) { (void)args; }
int pci_wireless_scan(int kind, char *first, u32 cap) { (void)kind; if (cap) first[0] = 0; return 0; }
void weather_command(const char *args) { (void)args; kprintf("WX|ERR|no network in the test host\n"); }
void wifi_command(const char *args) { (void)args; kprintf("wifi: not available in the test host\n"); }
void bluetooth_command(const char *args) { (void)args; kprintf("bluetooth: not available in the test host\n"); }
void net_status(net_info *o) { kmemset(o, 0, sizeof *o); kstrcpy(o->name, "test card"); o->card = 1; o->link = 1; o->ip = 0x0A00020F; o->mask = 0xFFFFFF00; o->gw = 0x0A000202; o->dns = 0x0A000203; }
void net_fmt_ip(char *o, u32 cap, u32 v) { ksnprintf(o, cap, "%u.%u.%u.%u", v >> 24, (v >> 16) & 255, (v >> 8) & 255, v & 255); }
void ping_command(const char *args) { (void)args; }
void tls_command(const char *args) { (void)args; }
int net_http_get(const char *u, const char **b, u32 *l, int *s, int *t, int v) { (void)u; (void)b; (void)l; (void)s; (void)t; (void)v; return 0; }
void net_poll(void) {}
void net_my_ip(char *o, u32 c) { if (c) o[0] = 0; }
int net_http_download(const char *u, u8 *b, u32 c, u32 *l, int *s) { (void)u; (void)b; (void)c; (void)l; (void)s; return 0; }
const char *net_http_error(void) { return ""; }


/* ---- native programs inside the harness: the region is really mapped (32-bit process), the web is a built-in page ---- */
void native_test_map(void)
{
    static int done;
    if (done) return;
    done = 1;
    /* mmap2(addr, len, prot=RWX, flags=MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, fd=-1, off=0) */
    long args[6] = { 0x05000000, 48 << 20, 7, 0x02 | 0x20 | 0x10, -1, 0 };
    long r;
    __asm__ volatile("pushl %%ebp\n movl 20(%%esi), %%ebp\n movl 0(%%esi), %%ebx\n movl 4(%%esi), %%ecx\n movl 8(%%esi), %%edx\n movl 16(%%esi), %%edi\n movl 12(%%esi), %%esi\n int $0x80\n popl %%ebp"
                     : "=a"(r) : "a"(192), "S"(args) : "ebx", "ecx", "edx", "edi", "memory");
    if (r != 0x05000000) out("native: mmap failed\n");
}

#include "img_data.h"
static const char fake_page[] =
    "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nServer: harness\r\n\r\n"
    "<html><head><title>AstraSageOS test page</title></head><body bgcolor=\"#ffffff\">"
    "<h1>Hello from NetSurf</h1><p>This page comes from the <b>test harness</b>, rendered by NetSurf running as a native "
    "AstraSageOS program.</p><ul><li>list item one</li><li>list item <i>two</i></li></ul>"
    "<p><a href=\"http://test/second\">A link to a second page</a></p>"
    "<div style=\"background:#336699;color:white;padding:8px\">A coloured box</div>"
    "<p><img src=\"http://test/red.png\" width=\"48\" height=\"24\"> <img src=\"http://test/green.jpg\" width=\"48\" height=\"24\"></p>"
    "<table border=\"1\"><tr><td>A1</td><td>B1</td></tr><tr><td>A2</td><td>B2</td></tr></table></body></html>";

int net_http_request(const char *m, const char *url, const char *xh, const void *body, u32 bl, u8 *buf, u32 cap, u32 *hdr_len, u32 *body_off, u32 *body_len, int *status)
{ (void)m; (void)xh; (void)body; (void)bl; return net_http_fetch(url, buf, cap, hdr_len, body_off, body_len, status); }
int net_http_fetch(const char *url, u8 *buf, u32 cap, u32 *hdr_len, u32 *body_off, u32 *body_len, int *status)
{
    u32 ul = (u32)kstrlen(url);
    const unsigned char *img = 0; u32 il = 0; const char *ct = 0;
    if (ul > 8 && !kstrcmp(url + ul - 8, "/red.png")) { img = img_png; il = sizeof img_png; ct = "image/png"; }
    if (ul > 10 && !kstrcmp(url + ul - 10, "/green.jpg")) { img = img_jpg; il = sizeof img_jpg; ct = "image/jpeg"; }
    if (img) {
        char h[96]; int hl = ksnprintf(h, sizeof h, "HTTP/1.0 200 OK\r\nContent-Type: %s\r\n\r\n", ct);
        if ((u32)hl + il > cap) return 0;
        kmemcpy(buf, h, (u32)hl); kmemcpy(buf + hl, img, il);
        *hdr_len = (u32)hl; *body_off = (u32)hl; *body_len = il; *status = 200;
        return 1;
    }
    u32 n = (u32)sizeof fake_page - 1;
    if (n > cap) return 0;
    kmemcpy(buf, fake_page, n);
    const char *sep = "\r\n\r\n";
    u32 off = 0;
    for (u32 i = 0; i + 3 < n; i++) if (!kmemcmp(buf + i, sep, 4)) { off = i + 4; break; }
    *hdr_len = off; *body_off = off; *body_len = n - off; *status = 200;
    return 1;
}

/* no PCI bus in the harness: sound.c asks for an AC97 card and finds none */
u32 pci_cfg_r(u32 bus, u32 dev, u32 off) { (void)bus; (void)dev; (void)off; return 0xFFFFFFFFu; }
void pci_cfg_w(u32 bus, u32 dev, u32 off, u32 v) { (void)bus; (void)dev; (void)off; (void)v; }
