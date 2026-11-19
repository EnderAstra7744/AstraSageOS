/*
 * VGA text console (80x25) with an ANSI/VT100 subset and the AstraSage palette.
 *
 * Supported: SGR (16/256 colours, reverse), cursor movement and positioning
 * (A B C D G H f s u), erase (J K). Everything written here is also sent raw to
 * the serial port, where a real terminal draws it with exact 256 colours.
 */
#include "astra.h"
#include "io.h"

#include "gui.h"

/*
 * Two back ends share one ANSI state machine:
 *  - VGA text mode, fixed 80x25 (the original console, also the fallback), and
 *  - the graphical terminal: a cell grid (character + two xterm-256 colours) that the AST
 *    window of AstraGUI draws with the 8x16 font. Its size follows the window.
 */
#define VGA_COLS 80
#define VGA_ROWS 25
#define GRID_C   200
#define GRID_R   80
#define DEF_ATTR 0x07
#define COLS ncols
#define ROWS nrows

typedef struct { u8 ch, fg, bg, std; u16 cp; } cell;       /* std: bg is an ANSI colour 0-15 drawn in the standard terminal palette (neofetch's palette rows) */       /* cp: 0 = plain byte in ch; 0xFFFF = right half of a wide character; else a Unicode character */
static cell grid[GRID_R][GRID_C];
static u8   row_dirty[GRID_R];
static u8   wrapped[GRID_R];                 /* row continues on the next one (automatic wrap): the resize reflows such lines */
static int  gui_on, ncols = VGA_COLS, nrows = VGA_ROWS, last_cy;

/* ---- picture layer (GUI only): real pixels anchored to cell rows, so a picture scrolls with the text around it ---- */
#define IMG_MAX 4
typedef struct { int row, col, cols, rows; } img_pl;
static img_pl im[IMG_MAX];
static int    nim;
static u32    scroll_total;
static u8     img_src, img_data[4100];
static u32    img_len, img_serial;

static void img_dirty(const img_pl *p) { for (int y = p->row; y < p->row + p->rows; y++) if (y >= 0 && y < GRID_R) row_dirty[y] = 1; }
static void img_clear(void) { for (int i = 0; i < nim; i++) img_dirty(&im[i]); nim = 0; }
static void img_shift(int n)                                  /* the text moved up by n rows */
{
    int k = 0;
    for (int i = 0; i < nim; i++) {
        img_dirty(&im[i]);
        im[i].row -= n;
        if (im[i].row + im[i].rows > 0) im[k++] = im[i];
    }
    nim = k;
}

u32 con_scroll_total(void) { return scroll_total; }

/* src 0: the AstraSage logo; src 1: a PNG file (data, len <= 4096). The picture is drawn over cols x rows cells. */
void con_image_place(int row, int col, int cols, int rows, int src, const u8 *data, u32 len)
{
    if (!gui_on) return;
    if (len > sizeof img_data) len = sizeof img_data;
    if (src != img_src || len != img_len || (len && kmemcmp(img_data, data, len))) {
        img_src = (u8)src; img_len = len;
        if (len) kmemcpy(img_data, data, len);
        img_serial++;
    }
    if (nim == IMG_MAX) { for (int i = 1; i < IMG_MAX; i++) im[i - 1] = im[i]; nim--; }
    im[nim].row = row; im[nim].col = col; im[nim].cols = cols; im[nim].rows = rows;
    img_dirty(&im[nim]);
    nim++;
}
int  con_image_count(void) { return nim; }
void con_image_get(int i, int *row, int *col, int *cols, int *rows) { *row = im[i].row; *col = im[i].col; *cols = im[i].cols; *rows = im[i].rows; }
const u8 *con_image_source(int *src, u32 *len, u32 *serial) { *src = img_src; *len = img_len; *serial = img_serial; return img_data; }

#ifdef AS_NO_IO
static volatile u16 vram_stub[VGA_COLS * VGA_ROWS];
static volatile u16 *const vram = vram_stub;
#else
#if defined(__i386__) || defined(__x86_64__)
static volatile u16 *const vram = (volatile u16 *)0xB8000;
#else
static u16 vram_dummy[80 * 25];                        /* no VGA text memory on other CPUs: the text console draws into RAM only */
static volatile u16 *const vram = vram_dummy;
#endif
#endif
static int cx, cy, scx, scy;
static u8  fg = 7, bg = 0, rev;
static u8  fg256 = 7, bg256 = 0, bg_std = 0;
static u8  attr = DEF_ATTR;

/*
 * The 16 attribute colours. VGA text mode cannot show more at once, so the
 * palette is reprogrammed: the four AstraSage greens of the logo and prompt are
 * exact, the rest is tuned for the editor (dark red error background, orange
 * strings, violet numbers, grey comments).
 */
static const u32 pal_rgb[16] = {
    0x000000, 0x0000AA, 0x87D700, 0x5FAF00,    /* black  blue    154-ish greens: 112, 70 */
    0x5F0000, 0xAF87FF, 0xFFAF00, 0xAAAAAA,    /* faint red, violet, orange, grey          */
    0x808080, 0x5555FF, 0xAFFF00, 0x5F8700,    /* dark grey, bright blue, 154, 64          */
    0xFF5555, 0xFF55FF, 0x55FFFF, 0xFFFFFF
};

/* ANSI colour index (0-7) -> VGA colour index: red and blue swap places. */
static u8 ansi2vga(int c)
{
    return (u8)((c & 2) | ((c & 1) << 2) | ((c & 4) >> 2));
}

static u32 xterm_rgb(int n)
{
    static const u8 lv[6] = { 0, 95, 135, 175, 215, 255 };
    if (n >= 232) { u32 g = 8u + 10u * (u32)(n - 232); return g << 16 | g << 8 | g; }
    n -= 16;
    return (u32)lv[n / 36] << 16 | (u32)lv[(n / 6) % 6] << 8 | lv[n % 6];
}

/* xterm-256 colour number -> nearest of the 16 programmed VGA colours. */
static u8 map256(int n)
{
    if (n < 0) return 7;
    if (n < 8)  return ansi2vga(n);
    if (n < 16) return (u8)(ansi2vga(n - 8) | 8);
    if (n > 255) return 7;
    u32 c = xterm_rgb(n);
    int cr = (int)(c >> 16 & 255), cg = (int)(c >> 8 & 255), cb = (int)(c & 255);
    u8 best = 7;
    long bd = 1L << 30;
    for (u8 i = 0; i < 16; i++) {
        int dr = cr - (int)(pal_rgb[i] >> 16 & 255);
        int dg = cg - (int)(pal_rgb[i] >> 8 & 255);
        int db = cb - (int)(pal_rgb[i] & 255);
        long d = (long)dr * dr + (long)dg * dg + (long)db * db;
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

static void dac(u8 idx, u8 r, u8 g, u8 b)
{
    outb(0x3C8, idx);
    outb(0x3C9, r); outb(0x3C9, g); outb(0x3C9, b);   /* 6-bit components */
}

static void program_palette(void)
{
    for (int i = 0; i < 16; i++) {
        u8 slot = (u8)(i < 6 ? i : i == 6 ? 0x14 : i == 7 ? 0x07 : 0x38 + (i - 8));
        u32 c = pal_rgb[i];
        dac(slot, (u8)((c >> 16 & 255) >> 2), (u8)((c >> 8 & 255) >> 2), (u8)((c & 255) >> 2));
    }
}

static void set_cursor(void)
{
    if (gui_on) { row_dirty[last_cy < nrows ? last_cy : 0] = 1; row_dirty[cy] = 1; last_cy = cy; return; }
    int x = cx >= COLS ? COLS - 1 : cx;
    u16 pos = (u16)(cy * COLS + x);
    outb(0x3D4, 0x0F); outb(0x3D5, (u8)(pos & 0xFF));
    outb(0x3D4, 0x0E); outb(0x3D5, (u8)(pos >> 8));
}

static void recompute_attr(void)
{
    u8 f = fg & 0x0F, b = bg & 0x0F;
    attr = rev ? (u8)(b | (f << 4)) : (u8)(f | (b << 4));
}

/* One screen cell, whichever back end is active. */
static void put_cell(int x, int y, u8 ch)
{
    if (gui_on) {
        cell *c = &grid[y][x];
        c->ch = ch; c->cp = 0;
        c->fg = rev ? bg256 : fg256;
        c->bg = rev ? fg256 : bg256;
        c->std = (u8)(bg_std && !rev);
        row_dirty[y] = 1;
    } else {
        vram[y * COLS + x] = (u16)(ch | (attr << 8));
    }
}

/* a Unicode character (GUI terminal only): wide ones (Chinese) take two cells */
extern int gfx_is_wide(u32 cp);
static void put_cp(int x, int y, u32 cp, int wide)
{
    if (!gui_on) { put_cell(x, y, '?'); return; }
    put_cell(x, y, '?'); grid[y][x].cp = (u16)cp;
    if (wide && x + 1 < GRID_C) { put_cell(x + 1, y, ' '); grid[y][x + 1].cp = 0xFFFF; }
}

static void blank_cells(int from, int to, int use_attr)         /* linear cell range [from, to) */
{
    if (from < 0) from = 0;
    if (to > COLS * ROWS) to = COLS * ROWS;
    for (int i = from; i < to; i++) {
        if (gui_on) {
            cell *c = &grid[i / COLS][i % COLS];
            c->ch = ' '; c->cp = 0;
            c->fg = use_attr ? (rev ? bg256 : fg256) : 7;
            c->bg = use_attr ? (rev ? fg256 : bg256) : 0;
            c->std = (u8)(use_attr && bg_std && !rev);
            row_dirty[i / COLS] = 1;
        } else {
            vram[i] = (u16)(' ' | ((use_attr ? attr : DEF_ATTR) << 8));
        }
    }
}

void con_clear(void)
{
    const char *seq = "\033[0m\033[2J\033[H";
    for (const char *p = seq; *p; p++) serial_putc(*p);
    fg = 7; bg = 0; rev = 0; attr = DEF_ATTR; fg256 = 7; bg256 = 0;
    blank_cells(0, COLS * ROWS, 0);
    img_clear();
    cx = cy = 0;
    set_cursor();
}

void con_init(void)
{
    program_palette();
    serial_init();
    fg = 7; bg = 0; rev = 0; attr = DEF_ATTR; fg256 = 7; bg256 = 0;
    /* attribute bit 7 = bright background instead of blinking */
    (void)inb(0x3DA);
    outb(0x3C0, 0x30);
    u8 mode = inb(0x3C1);
    outb(0x3C0, (u8)(mode & ~0x08));
    con_clear();
}

static void scroll(void)
{
    if (gui_on) {
        for (int y = 0; y < ROWS - 1; y++) { kmemcpy(grid[y], grid[y + 1], sizeof grid[y]); wrapped[y] = wrapped[y + 1]; row_dirty[y] = 1; }
        wrapped[ROWS - 1] = 0;
        blank_cells(COLS * (ROWS - 1), COLS * ROWS, 0);
        scroll_total++;
        img_shift(1);
    } else {
        for (int i = 0; i < COLS * (ROWS - 1); i++) vram[i] = vram[i + COLS];
        blank_cells(COLS * (ROWS - 1), COLS * ROWS, 0);
    }
    cy = ROWS - 1;
}

static void newline(void)
{
    cx = 0;
    if (++cy >= ROWS) scroll();
}

/* ---- ANSI escape handling: ESC [ [?] p;p;p <final> ---- */
static int st, np, par[8], curp, havep, priv;

static void sgr(void)
{
    if (havep) { if (np < 8) par[np++] = curp; }
    if (np == 0) par[np++] = 0;
    for (int i = 0; i < np; i++) {
        int v = par[i];
        if (v == 0)                  { fg = 7; bg = 0; rev = 0; fg256 = 7; bg256 = 0; bg_std = 0; }
        else if (v == 7)             rev = 1;
        else if (v == 27)            rev = 0;
        else if (v == 39)            { fg = 7; fg256 = 7; }
        else if (v == 49)            { bg = 0; bg256 = 0; bg_std = 0; }
        else if (v >= 160 && v <= 175) { int k = v - 160; bg = (u8)(ansi2vga(k & 7) | (k & 8)); bg256 = (u8)k; bg_std = 1; }   /* AstraSage extension: background in the standard terminal palette (used by neofetch) */
        else if (v >= 40 && v <= 47)   { bg = ansi2vga(v - 40); bg256 = (u8)(v - 40); bg_std = 0; }
        else if (v >= 100 && v <= 107) { bg = (u8)(ansi2vga(v - 100) | 8); bg256 = (u8)(v - 100 + 8); bg_std = 0; }
        else if (v >= 30 && v <= 37) { fg = ansi2vga(v - 30); fg256 = (u8)(v - 30); }
        else if (v >= 90 && v <= 97) { fg = (u8)(ansi2vga(v - 90) | 8); fg256 = (u8)(v - 90 + 8); }
        else if ((v == 38 || v == 48) && i + 2 < np && par[i + 1] == 5) {
            u8 n8 = (u8)(par[i + 2] & 255);
            if (v == 38) { fg = map256(par[i + 2]); fg256 = n8; } else { bg = map256(par[i + 2]); bg256 = n8; bg_std = 0; }
            i += 2;
        }
        else if ((v == 38 || v == 48) && i + 4 < np && par[i + 1] == 2) i += 4;   /* truecolour: ignored */
        /* 1 (bold) and everything else: ignored on VGA */
    }
    recompute_attr();
}

static int arg(int i, int def)
{
    return (i < np && par[i] > 0) ? par[i] : def;
}

static void fill(int from, int to)           /* cells [from, to) of the screen */
{
    blank_cells(from, to, 1);
}

static void csi(char fin)
{
    if (havep) { if (np < 8) par[np++] = curp; }
    int x = cx >= COLS ? COLS - 1 : cx;
    if (priv) return;                                   /* ?25l, ?7h ...: ignored */
    switch (fin) {
    case 'm': sgr(); return;
    case 'H': case 'f': {
        int r = arg(0, 1), c = arg(1, 1);
        cy = r - 1 >= ROWS ? ROWS - 1 : r - 1;
        cx = c - 1 >= COLS ? COLS - 1 : c - 1;
        break;
    }
    case 'A': cy -= arg(0, 1); if (cy < 0) cy = 0; cx = x; break;
    case 'B': cy += arg(0, 1); if (cy >= ROWS) cy = ROWS - 1; cx = x; break;
    case 'C': cx = x + arg(0, 1); if (cx >= COLS) cx = COLS - 1; break;
    case 'D': cx = x - arg(0, 1); if (cx < 0) cx = 0; break;
    case 'G': cx = arg(0, 1) - 1; if (cx >= COLS) cx = COLS - 1; break;
    case 'J': {
        int m = np ? par[0] : 0;
        int pos = cy * COLS + x;
        if (m == 0) fill(pos, COLS * ROWS);
        else if (m == 1) fill(0, pos + 1);
        else { fill(0, COLS * ROWS); img_clear(); }
        break;
    }
    case 'K': {
        int m = np ? par[0] : 0;
        int row = cy * COLS;
        if (m == 0) fill(row + x, row + COLS);
        else if (m == 1) fill(row, row + x + 1);
        else fill(row, row + COLS);
        break;
    }
    case 's': scx = x; scy = cy; return;
    case 'u': cx = scx; cy = scy; break;
    default: return;
    }
}

static int ansi_feed(char c)
{
    if (st == 0) { if (c == 033) { st = 1; return 1; } return 0; }
    if (st == 1) {
        if (c == '[') { st = 2; np = 0; curp = 0; havep = 0; priv = 0; }
        else st = 0;
        return 1;
    }
    if (c >= '0' && c <= '9') { curp = curp * 10 + (c - '0'); havep = 1; }
    else if (c == ';') { if (np < 8) par[np++] = curp; curp = 0; havep = 0; }
    else if (c == '?' || c == '>' || c == '=' || c == '<') priv = 1;
    else if (c >= 0x40 && c <= 0x7E) { csi(c); st = 0; }
    /* other intermediate bytes: skipped */
    return 1;
}

static u32 u_cp; static int u_need;                  /* UTF-8 sequence being collected */

void con_putc(char c)
{
    if (c != '\t') serial_putc(c);      /* raw stream incl. ANSI escapes */
    if (ansi_feed(c)) { set_cursor(); return; }
    if (u_need && ((u8)c & 0xC0) == 0x80) {
        u_cp = u_cp << 6 | ((u8)c & 0x3F);
        if (--u_need == 0) {
            int wide = gfx_is_wide(u_cp);
            if (cx + (wide ? 2 : 1) > COLS) { if (gui_on && cy < GRID_R) wrapped[cy] = 1; newline(); }
            put_cp(cx, cy, u_cp, wide);
            cx += wide ? 2 : 1;
            set_cursor();
        }
        return;
    }
    u_need = 0;
    if ((u8)c >= 0xC2 && (u8)c <= 0xEF) { u_need = (u8)c >= 0xE0 ? 2 : 1; u_cp = (u8)c & (u_need == 2 ? 0x0F : 0x1F); return; }
    switch (c) {
    case '\n': newline(); break;
    case '\r': cx = 0; break;
    case '\b':
        if (cx > 0) {
            cx = cx >= COLS ? COLS - 1 : cx - 1;
            if (gui_on && cx > 0 && grid[cy][cx].cp == 0xFFFF) { grid[cy][cx].cp = 0; cx--; }
            int sr = rev, sf = fg256, sb = bg256;
            rev = 0; fg256 = 7; bg256 = 0;
            put_cell(cx, cy, ' ');
            rev = (u8)sr; fg256 = (u8)sf; bg256 = (u8)sb;
        }
        break;
    case '\t': do con_putc(' '); while (cx % 4); return;
    default:
        if ((u8)c < 32) break;
        if (cx >= COLS) { if (gui_on && cy < GRID_R) wrapped[cy] = 1; newline(); }   /* deferred wrap */
        put_cell(cx, cy, (u8)c);
        cx++;
    }
    set_cursor();
}

/*
 * Load the AstraSage 8x16 bitmap font into VGA plane 2. Glyphs cover ASCII
 * 32..126, every other character keeps the BIOS shape.
 */
void con_set_font(const u8 *glyphs, u32 len)
{
    if (len < 95 * 16) return;
#ifdef AS_NO_IO
    return;                                    /* test harness: no VGA memory */
#endif
    volatile u8 *plane = (volatile u8 *)0xA0000;

    outb(0x3C4, 0x02); outb(0x3C5, 0x04);      /* write plane 2 only */
    outb(0x3C4, 0x04); outb(0x3C5, 0x07);      /* sequential access */
    outb(0x3CE, 0x04); outb(0x3CF, 0x02);      /* read plane 2 */
    outb(0x3CE, 0x05); outb(0x3CF, 0x00);      /* write mode 0, no odd/even */
    outb(0x3CE, 0x06); outb(0x3CF, 0x00);      /* map at A0000 */

    for (int c = 0; c < 95; c++)
        for (int y = 0; y < 16; y++)
            plane[(32 + c) * 32 + y] = glyphs[c * 16 + y];

    outb(0x3C4, 0x02); outb(0x3C5, 0x03);      /* back to text mode */
    outb(0x3C4, 0x04); outb(0x3C5, 0x03);
    outb(0x3CE, 0x04); outb(0x3CF, 0x00);
    outb(0x3CE, 0x05); outb(0x3CF, 0x10);
    outb(0x3CE, 0x06); outb(0x3CF, 0x0E);
}


/* ---------------- graphical terminal hooks (used by the AST window) ---------------- */

static void grid_reset(void)
{
    for (int y = 0; y < GRID_R; y++) {
        for (int x = 0; x < GRID_C; x++) { grid[y][x].ch = ' '; grid[y][x].cp = 0; grid[y][x].fg = 7; grid[y][x].bg = 0; }
        row_dirty[y] = 1; wrapped[y] = 0;
    }
}

void con_gui_enable(int cols, int rows)
{
    if (cols < 20) cols = 20;
    if (rows < 5) rows = 5;
    if (cols > GRID_C) cols = GRID_C;
    if (rows > GRID_R) rows = GRID_R;
    gui_on = 1;
    ncols = cols; nrows = rows;
    st = 0;
    grid_reset();
    nim = 0;
    fg = 7; bg = 0; rev = 0; attr = DEF_ATTR; fg256 = 7; bg256 = 0;
    cx = cy = scx = scy = last_cy = 0;
}

void con_gui_disable(void)
{
    gui_on = 0;
    ncols = VGA_COLS; nrows = VGA_ROWS;
    cx = cy = 0;
}

static cell rf[900][GRID_C];
static u8   rf_wrap[900];

static int blank_cell(const cell *c) { return c->ch == ' ' && c->bg == 0; }

/* Re-wrap the text for a new width: lines that were wrapped automatically are joined and broken again. */
static void reflow(int cols, int rows)
{
    int cur_row = cy < nrows ? cy : nrows - 1, cur_col = cx > ncols ? ncols : cx;
    int out = 0, ncy = 0, ncx = 0, have_cur = 0;
    int y = 0;
    while (y <= cur_row && out < 900) {
        int y0 = y, len = 0, cpos = -1;
        static cell line[GRID_C * 8];
        for (;;) {
            int take = ncols;
            if (len + take > GRID_C * 8) take = GRID_C * 8 - len;
            for (int x = 0; x < take; x++) line[len + x] = grid[y][x];
            if (y == cur_row) cpos = len + cur_col;
            len += take;
            if (!wrapped[y] || y >= nrows - 1) { y++; break; }
            y++;
        }
        (void)y0;
        int keep = len;                                   /* trailing blanks of the last row are not text */
        while (keep > 0 && blank_cell(&line[keep - 1]) && keep > cpos) keep--;
        int n = keep ? (keep + cols - 1) / cols : 1;
        if (cpos >= 0 && cpos / cols + 1 > n) n = cpos / cols + 1;
        for (int r = 0; r < n && out < 900; r++, out++) {
            for (int x = 0; x < GRID_C; x++) { rf[out][x].ch = ' '; rf[out][x].cp = 0; rf[out][x].fg = 7; rf[out][x].bg = 0; }
            int from = r * cols, cnt = keep - from;
            if (cnt > cols) cnt = cols;
            for (int x = 0; x < cnt; x++) rf[out][x] = line[from + x];
            rf_wrap[out] = r < n - 1;
            if (cpos >= 0 && cpos / cols == r) { ncy = out; ncx = cpos % cols; have_cur = 1; }
        }
    }
    if (!have_cur) { ncy = out > 0 ? out - 1 : 0; ncx = 0; }
    int total = ncy + 1, start = total > rows ? total - rows : 0;
    for (int r = 0; r < GRID_R; r++) {
        if (r < rows && start + r < total) { kmemcpy(grid[r], rf[start + r], sizeof grid[r]); wrapped[r] = rf_wrap[start + r]; }
        else { for (int x = 0; x < GRID_C; x++) { grid[r][x].ch = ' '; grid[r][x].cp = 0; grid[r][x].fg = 7; grid[r][x].bg = 0; } wrapped[r] = 0; }
    }
    cy = ncy - start; cx = ncx;
    if (cy < 0) cy = 0;
}

void con_gui_resize(int cols, int rows)
{
    if (!gui_on) return;
    if (cols < 20) cols = 20;
    if (rows < 5) rows = 5;
    if (cols > GRID_C) cols = GRID_C;
    if (rows > GRID_R) rows = GRID_R;
    if (cols != ncols) {
        img_clear();
        reflow(cols, rows);
        ncols = cols; nrows = rows;
        if (cx > ncols) cx = ncols;
        for (int y = 0; y < GRID_R; y++) row_dirty[y] = 1;
        return;
    }
    if (cy >= rows) {                                   /* keep the cursor line: scroll the old text up */
        int shift = cy - rows + 1;
        for (int y = 0; y + shift < nrows && y < GRID_R; y++) kmemcpy(grid[y], grid[y + shift], sizeof grid[y]);
        for (int y = nrows - shift; y < GRID_R && y >= 0 && y < nrows; y++)
            for (int x = 0; x < GRID_C; x++) { grid[y][x].ch = ' '; grid[y][x].cp = 0; grid[y][x].fg = 7; grid[y][x].bg = 0; }
        cy -= shift;
        img_shift(shift);
    }
    for (int y = nrows; y < rows; y++)                  /* rows that appear start blank */
        for (int x = 0; x < GRID_C; x++) { grid[y][x].ch = ' '; grid[y][x].cp = 0; grid[y][x].fg = 7; grid[y][x].bg = 0; }
    for (int y = 0; y < rows; y++)                      /* columns that appear start blank */
        for (int x = ncols; x < cols; x++) { grid[y][x].ch = ' '; grid[y][x].cp = 0; grid[y][x].fg = 7; grid[y][x].bg = 0; }
    ncols = cols; nrows = rows;
    if (cx > ncols) cx = ncols;
    for (int y = 0; y < GRID_R; y++) row_dirty[y] = 1;
}

int con_gui_active(void) { return gui_on; }
void con_gui_size(int *c, int *r) { *c = ncols; *r = nrows; }

void con_gui_cell(int x, int y, u8 *ch, u32 *fgc, u32 *bgc)
{
    if (x < 0 || y < 0 || x >= ncols || y >= nrows) { *ch = ' '; *fgc = 0xAAAAAA; *bgc = 0; return; }
    const cell *c = &grid[y][x];
    u32 f = c->fg < 16 ? pal_rgb[(c->fg & 8) | ansi2vga(c->fg & 7)] : xterm_rgb(c->fg);
    u32 b = c->bg < 16 ? pal_rgb[(c->bg & 8) | ansi2vga(c->bg & 7)] : xterm_rgb(c->bg);
    if (c->std && c->bg < 16) {
        static const u32 stdpal[16] = { 0x010101, 0xCD0000, 0x00CD00, 0xCDCD00, 0x6495ED, 0xCD00CD, 0x00CDCD, 0xE5E5E5,
                                        0x7F7F7F, 0xFF0000, 0x00FF00, 0xFFFF00, 0x5C5CFF, 0xFF00FF, 0x00FFFF, 0xFFFFFF };
        b = stdpal[c->bg];
    }
    *ch = c->ch; *fgc = f; *bgc = b;
}

/* the Unicode value of a cell: bit 31 set = the right half of a wide character (cp in the low bits) */
void con_gui_cell_cp(int x, int y, u32 *cp)
{
    *cp = 0;
    if (x < 0 || y < 0 || x >= ncols || y >= nrows) return;
    const cell *c = &grid[y][x];
    if (c->cp == 0xFFFF) { *cp = x > 0 ? 0x80000000u | grid[y][x - 1].cp : 0; }
    else *cp = c->cp;
}

void con_gui_cursor(int *x, int *y) { *x = cx >= ncols ? ncols - 1 : cx; *y = cy; }

/* Range of rows changed since the last call (inclusive); 0 when nothing changed. */
int con_gui_take_dirty(int *row0, int *row1)
{
    int a = -1, b = -1;
    for (int y = 0; y < nrows; y++)
        if (row_dirty[y]) { if (a < 0) a = y; b = y; }
    for (int y = 0; y < GRID_R; y++) row_dirty[y] = 0;
    if (a < 0) return 0;
    *row0 = a; *row1 = b;
    return 1;
}

/* ---- terminal tabs: one saved copy of everything that makes up a screen (grid, cursor, colours, ANSI parser) ---- */
typedef struct {
    cell grid[GRID_R][GRID_C];
    u8   wrapped[GRID_R];
    int  ncols, nrows, last_cy, cx, cy, scx, scy;
    u8   fg, bg, rev, fg256, bg256, attr;
    int  st, np, par[8], curp, havep, priv;
    img_pl im[IMG_MAX]; int nim;
} con_tab_state;

_Static_assert(sizeof(con_tab_state) <= CON_STATE_BYTES, "CON_STATE_BYTES too small");

void con_state_save(void *dst)
{
    con_tab_state *t = (con_tab_state *)dst;
    kmemcpy(t->grid, grid, sizeof grid);
    kmemcpy(t->wrapped, wrapped, sizeof wrapped);
    t->ncols = ncols; t->nrows = nrows; t->last_cy = last_cy; t->cx = cx; t->cy = cy; t->scx = scx; t->scy = scy;
    t->fg = fg; t->bg = bg; t->rev = rev; t->fg256 = fg256; t->bg256 = bg256; t->attr = attr;
    t->st = st; t->np = np; t->curp = curp; t->havep = havep; t->priv = priv;
    for (int i = 0; i < 8; i++) t->par[i] = par[i];
    kmemcpy(t->im, im, sizeof im); t->nim = nim;
}

void con_state_load(const void *src)
{
    const con_tab_state *t = (const con_tab_state *)src;
    kmemcpy(grid, t->grid, sizeof grid);
    kmemcpy(wrapped, t->wrapped, sizeof wrapped);
    ncols = t->ncols; nrows = t->nrows; last_cy = t->last_cy; cx = t->cx; cy = t->cy; scx = t->scx; scy = t->scy;
    fg = t->fg; bg = t->bg; rev = t->rev; fg256 = t->fg256; bg256 = t->bg256; attr = t->attr;
    st = t->st; np = t->np; curp = t->curp; havep = t->havep; priv = t->priv;
    for (int i = 0; i < 8; i++) par[i] = t->par[i];
    kmemcpy(im, t->im, sizeof im); nim = t->nim;
}

void con_gui_dirty_all(void) { for (int y = 0; y < GRID_R; y++) row_dirty[y] = 1; }

/* a fresh, blank screen of cols x rows for a new tab */
void con_state_blank(void *dst, int cols, int rows)
{
    con_tab_state *t = (con_tab_state *)dst;
    for (int y = 0; y < GRID_R; y++) for (int x = 0; x < GRID_C; x++) { t->grid[y][x].ch = ' '; t->grid[y][x].cp = 0; t->grid[y][x].fg = 7; t->grid[y][x].bg = 0; }
    for (int y = 0; y < GRID_R; y++) t->wrapped[y] = 0;
    t->ncols = cols; t->nrows = rows; t->last_cy = 0; t->cx = t->cy = t->scx = t->scy = 0;
    t->fg = 7; t->bg = 0; t->rev = 0; t->fg256 = 7; t->bg256 = 0; t->attr = DEF_ATTR;
    t->st = t->np = t->curp = t->havep = t->priv = 0;
    for (int i = 0; i < 8; i++) t->par[i] = 0;
    t->nim = 0;
}
