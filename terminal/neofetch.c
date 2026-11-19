/*
 * as neofetch - the AstraSage asfetch banner, reproduced exactly.
 *
 * Layout (taken from the reference binary):
 *   - one empty line before and one after the banner
 *   - 20 rows; the info column starts on row 0, the 17-line logo starts on row 1
 *   - every row is   <logo line> <padding to the widest logo line> 4 spaces <info>
 *   - info: user@host title, a green dash rule, 15 fields, an empty row and two
 *     palette rows (8 normal + 8 bright colour blocks)
 */
#include "astra.h"
#include "gui.h"
#include "logo.h"

#define ROWS_TOTAL 20
#define PIC_COLS 24                    /* the picture logo: 24 x 12 cells = 192 x 192 pixels */
#define PIC_ROWS 12
#define INFO_MAX   256

/* the info panel colours follow `as custom --neofetch ... <INFO_PANEL_COLOR>` (title = bright, label and rule = the same colour) */
static char TITLE[24] = "\033[1;38;5;154m", LABEL[24] = "\033[1;38;5;112m", RULE[24] = "\033[38;5;112m";
#define VALUE "\033[97m"
#define RST   "\033[0m"
#define LOGO_MAX 700

/* Printable width of a string, ignoring ANSI SGR sequences. */
static int vislen(const char *s)
{
    int n = 0;
    while (*s) {
        if (*s == '\033' && s[1] == '[') {
            s += 2;
            while (*s && *s != 'm') s++;
            if (*s) s++;
            continue;
        }
        n++; s++;
    }
    return n;
}

/* asfetch human_size(): bytes below 1024 as plain digits, else K/M/G with rounding up. */
static void human_size(char *o, size_t cap, u32 bytes)
{
    static const char suf[] = "KMGTPE";
    if (bytes < 1024) { ksnprintf(o, cap, "%u", bytes); return; }
    for (int idx = 0; idx < 6; idx++) {
        int sh = 10 * (idx + 1);
        if (sh >= 32) { ksnprintf(o, cap, "%u%c", 1u, suf[idx]); return; }
        u32 mask = (1u << sh) - 1;
        u32 t10 = (u32)((((unsigned long long)bytes * 10ull) + mask) >> sh);   /* ceil(v * 10) */
        if (t10 < 100) { ksnprintf(o, cap, "%u.%u%c", t10 / 10, t10 % 10, suf[idx]); return; }
        u32 t1 = (u32)((((unsigned long long)bytes) + mask) >> sh);            /* ceil(v) */
        if (t1 < 1024 || idx == 5) { ksnprintf(o, cap, "%u%c", t1, suf[idx]); return; }
    }
}

static void uptime_text(char *o, size_t cap, u32 secs)
{
    u32 days = secs / 86400, rest = secs % 86400, hours = rest / 3600, mins = (rest % 3600) / 60;
    size_t n = 0;
    o[0] = 0;
    if (days)  n += (size_t)ksnprintf(o + n, cap - n, "%ud ", days);
    if (rest >= 3600) n += (size_t)ksnprintf(o + n, cap - n, "%uh ", hours);
    ksnprintf(o + n, cap - n, "%um", mins);
}

/* ---- the logo: the AstraSage text logo (optionally in the 3 chosen colours), a text file or a small PNG ---- */
static char logo_lines[ROWS_TOTAL][LOGO_MAX];
static u32  png_px[128 * 128];
static char txt_buf[4100];

static int build_default(const as_settings *cs)
{
    int cc[3] = { cs->nf_logo[0], cs->nf_logo[1], cs->nf_logo[2] };
    for (int i = 0; i < AS_LOGO_LINES; i++) {
        const char *p = as_logo[i];
        char *o = logo_lines[i];
        size_t n = 0;
        while (*p && n + 24 < LOGO_MAX) {
            if (p[0] == '\033' && p[1] == '[') {
                int num = 0; const char *q = p + 7;                 /* "\033[38;5;" then the colour number */
                while (*q >= '0' && *q <= '9') num = num * 10 + (*q++ - '0');
                int slot = num == 154 ? 0 : num == 112 ? 1 : (num == 64 || num == 70) ? 2 : -1;
                if (slot >= 0 && cc[slot] >= 0) n += (size_t)ksnprintf(o + n, LOGO_MAX - n, "\033[38;5;%dm", con_rgb256(cc[slot]));
                else { for (const char *k = p; k <= q; k++) o[n++] = *k; }
                p = *q ? q + 1 : q;
                continue;
            }
            o[n++] = *p++;
        }
        o[n] = 0;
    }
    return AS_LOGO_LINES;
}

/* a text file in /bin/neofetch/custom_logo_txt: coloured by thirds (left, middle, right) like the default logo */
static int build_txt(const as_settings *cs, const char *name)
{
    int o = fs_get_jail();
    char path[96];
    ksnprintf(path, sizeof path, "/bin/neofetch/custom_logo_txt/%s", name);
    fs_set_jail(0);
    int f = fs_resolve(path, 0);
    u32 len = 0;
    if (f > 0 && !fs_is_dir(f)) { len = fs_size(f); if (len > sizeof txt_buf - 1) len = sizeof txt_buf - 1; kmemcpy(txt_buf, fs_data(f), len); }
    fs_set_jail(o);
    if (!len) return 0;
    txt_buf[len] = 0;
    int dflt[3] = { 154, 112, 64 };
    int rows = 0, width = 0;
    for (u32 i = 0, w = 0; i <= len; i++) {
        if (i == len || txt_buf[i] == '\n') { if ((int)w > width) width = (int)w; w = 0; } else w++;
    }
    if (width > 44) width = 44;
    if (width < 3) width = 3;
    u32 i = 0;
    while (i < len && rows < ROWS_TOTAL - 1) {
        char *o2 = logo_lines[rows];
        size_t n = 0;
        int x = 0, last = -1;
        while (i < len && txt_buf[i] != '\n') {
            unsigned char ch = (unsigned char)txt_buf[i++];
            if (ch == '\r' || x >= 44) continue;
            if (ch < 32 || ch > 126) ch = ' ';
            int third = x * 3 / width; if (third > 2) third = 2;
            if (third != last && n + 24 < LOGO_MAX) {
                n += (size_t)ksnprintf(o2 + n, LOGO_MAX - n, "\033[38;5;%dm", cs->nf_logo[third] >= 0 ? con_rgb256(cs->nf_logo[third]) : dflt[third]);
                last = third;
            }
            if (n + 2 < LOGO_MAX) o2[n++] = (char)ch;
            x++;
        }
        o2[n] = 0;
        if (i < len) i++;
        rows++;
    }
    return rows;
}

/* a PNG in /bin/neofetch/custom_logo: 17 rows, every pixel two cells wide with its own background colour */
static int build_png(const char *name)
{
    int o = fs_get_jail(), w = 0, h = 0, rc;
    char path[96];
    ksnprintf(path, sizeof path, "/bin/neofetch/custom_logo/%s", name);
    fs_set_jail(0);
    int f = fs_resolve(path, 0);
    if (f <= 0 || fs_is_dir(f)) { fs_set_jail(o); return 0; }
    rc = png_decode_small((const u8 *)fs_data(f), fs_size(f), &w, &h, png_px, sizeof png_px / sizeof png_px[0]);
    fs_set_jail(o);
    if (rc < 0 || w < 1 || h < 1) return 0;
    int rows = AS_LOGO_LINES, cols = w * rows / h;
    if (cols < 1) cols = 1;
    if (cols > 24) cols = 24;
    for (int r = 0; r < rows; r++) {
        char *p = logo_lines[r];
        size_t n = 0;
        for (int c = 0; c < cols && n + 24 < LOGO_MAX; c++) {
            int y0 = r * h / rows, y1 = (r + 1) * h / rows, x0 = c * w / cols, x1 = (c + 1) * w / cols;   /* box average of the covered pixels */
            if (y1 <= y0) y1 = y0 + 1;
            if (x1 <= x0) x1 = x0 + 1;
            int sr = 0, sg = 0, sb = 0, sa = 0, cnt = 0, vis = 0;
            for (int yy = y0; yy < y1 && yy < h; yy++)
                for (int xx = x0; xx < x1 && xx < w; xx++) {
                    u32 q = png_px[yy * w + xx];
                    cnt++; sa += (int)(q >> 24);
                    if ((q >> 24) >= 128) { sr += (int)(q >> 16 & 255); sg += (int)(q >> 8 & 255); sb += (int)(q & 255); vis++; }
                }
            int a = cnt ? sa / cnt : 0, rr = vis ? sr / vis : 0, gg = vis ? sg / vis : 0, bb = vis ? sb / vis : 0;
            if (a < 100) n += (size_t)ksnprintf(p + n, LOGO_MAX - n, "\033[0m  ");
            else n += (size_t)ksnprintf(p + n, LOGO_MAX - n, "\033[48;5;%dm  ", con_rgb256(rr << 16 | gg << 8 | bb));
        }
        n += (size_t)ksnprintf(p + n, LOGO_MAX - n, "\033[0m");
    }
    return rows;
}

void cmd_neofetch(void)
{
    static char info[ROWS_TOTAL][INFO_MAX];
    const as_settings *cs = config_get();
    if (cs->nf_info >= 0) {                              /* the info panel colour */
        int c = con_rgb256(cs->nf_info);
        ksnprintf(TITLE, sizeof TITLE, "\033[1;38;5;%dm", c);
        ksnprintf(LABEL, sizeof LABEL, "\033[1;38;5;%dm", c);
        ksnprintf(RULE, sizeof RULE, "\033[38;5;%dm", c);
    } else {
        kstrcpy(TITLE, "\033[1;38;5;154m"); kstrcpy(LABEL, "\033[1;38;5;112m"); kstrcpy(RULE, "\033[38;5;112m");
    }
    int nlogo = 0;
    /* In the graphical terminal the logo is a real picture (full quality, drawn over blank cells). The text logo with its own
     * three colours and the text-file logos stay character art, and so does the text mode. */
    int pic = 0;
    const u8 *pic_data = 0; u32 pic_len = 0;
    if (con_gui_active()) {
        if (cs->nf_png[0]) {
            char path[96];
            int o = fs_get_jail();
            ksnprintf(path, sizeof path, "/bin/neofetch/custom_logo/%s", cs->nf_png);
            fs_set_jail(0);
            int f = fs_resolve(path, 0);
            if (f > 0 && !fs_is_dir(f)) { pic_data = (const u8 *)fs_data(f); pic_len = fs_size(f); pic = 1; }
            fs_set_jail(o);
        } /* the default logo is the ASCII art of asfetch (no picture any more) */
    }
    if (pic) { nlogo = PIC_ROWS; for (int i = 0; i < PIC_ROWS; i++) logo_lines[i][0] = 0; }
    else if (cs->nf_png[0]) nlogo = build_png(cs->nf_png);
    if (!nlogo && cs->nf_txt[0]) nlogo = build_txt(cs, cs->nf_txt);
    if (!nlogo) nlogo = build_default(cs);
    for (int i = nlogo; i < ROWS_TOTAL; i++) logo_lines[i][0] = 0;
    int row = 0;
    int logo_w = 0;
    for (int i = 0; i < nlogo; i++) {
        int w = vislen(logo_lines[i]);
        if (w > logo_w) logo_w = w;
    }
    if (pic) logo_w = PIC_COLS;

    int term_cols = 80;
    if (con_gui_active()) { int rr; con_gui_size(&term_cols, &rr); }
    int avail = term_cols - logo_w - 4 - 1;                /* longer info is cut instead of wrapping into the logo */
    if (avail < 12) avail = 12;

    const char *user = user_name() ? user_name() : "Unknown";
    const char *host = "astrasage";

    ksnprintf(info[row++], INFO_MAX, "%s%s" RST "@%s%s" RST, TITLE, user, TITLE, host);

    {
        char *p = info[row++];
        size_t o = (size_t)ksnprintf(p, INFO_MAX, "%s", RULE);
        int dashes = (int)kstrlen(user) + 1 + (int)kstrlen(host);
        for (int i = 0; i < dashes && o + 8 < INFO_MAX; i++) p[o++] = '-';
        ksnprintf(p + o, INFO_MAX - o, RST);
    }

    char up[48], pk[24], kern[40], cpu[120], mem[40], disk[80], used[16], total[16], pct[16], tmp[24];
    u32 nu, bu, bc;
    fs_stats(&nu, &bu, &bc);
    uptime_text(up, sizeof up, hal_uptime_sec());
    ksnprintf(pk, sizeof pk, "%d (%s)", ast_command_count(), "as");
    ksnprintf(kern, sizeof kern, "%s-%s", AS_VERSION, AS_KERNEL_NAME);
    {   /* the cores and the bit width always stay visible: a long model name is shortened to fit the row instead */
        char tail[24], name[96];
        ksnprintf(tail, sizeof tail, " (%u cores, %u-bit)", hal_cpu_cores(), (u32)(sizeof(void *) * 8));
        ksnprintf(name, sizeof name, "%s", hal_cpu_name());
        int room = avail - 5 - (int)kstrlen(tail);          /* "CPU: " takes 5 */
        if (room < 8) room = 8;
        if ((int)kstrlen(name) > room) name[room] = 0;
        ksnprintf(cpu, sizeof cpu, "%s%s", name, tail);
    }
    ksnprintf(mem, sizeof mem, "%uMiB / %uMiB", hal_mem_used_mib(), hal_mem_total_mib());
    human_size(used, sizeof used, bu);
    human_size(total, sizeof total, bc);
    if (bc) { ksnprintf(tmp, sizeof tmp, "%u", (bu * 100 + bc - 1) / bc); ksnprintf(pct, sizeof pct, "%s%%", tmp); }
    else    ksnprintf(pct, sizeof pct, "-");
    ksnprintf(disk, sizeof disk, "%s / %s (%s)", used, total, pct);

    char res[24];
    const char *wm = "None (text mode)", *de = "None (text mode)";
    kstrcpy(res, "720x400");
    if (ui_active()) {                                   /* only what is really running */
        int gw, gh;
        ui_resolution(&gw, &gh);
        ksnprintf(res, sizeof res, "%dx%d", gw, gh);
        wm = "AstraGUI"; de = "AstraUI";
    }
    static const char *const labels[15] = {
        "OS", "Model", "Kernel", "Uptime", "Packages", "Shell", "Resolution", "DE", "WM",
        "Terminal", "CPU", "GPU", "Memory", "Disk (/)", "Battery"
    };
    const char *values[15] = {
        "AstraSage OS", hal_model(), kern, up, pk, AS_TERM_NAME " " AS_VERSION, res,
        de, wm, AS_TERM_NAME, cpu, hal_gpu_name(), mem, disk, "Unknown"
    };
    for (int i = 0; i < 15; i++)
        ksnprintf(info[row++], INFO_MAX, "%s%s" RST ": " VALUE "%s" RST, LABEL, labels[i], values[i]);

    info[row++][0] = 0;                                  /* empty row */
    for (int pass = 0; pass < 2; pass++) {               /* palette: 40..47, then 100..107 */
        char *p = info[row++];
        size_t o = 0;
        p[0] = 0;
        for (int i = 0; i < 8; i++)
            o += (size_t)ksnprintf(p + o, INFO_MAX - o, "\033[%dm   \033[0m", 160 + (pass ? 8 : 0) + i);   /* the standard terminal palette in the graphical terminal; the text console shows its nearest colours */
    }

    for (int r = 0; r < ROWS_TOTAL; r++) {
        int vis = 0;
        for (char *q = info[r]; *q; ) {
            if (*q == '\033' && q[1] == '[') { while (*q && *q != 'm') q++; if (*q) q++; continue; }
            if (++vis > avail) { kstrcpy(q, RST); break; }
            q++;
        }
    }
    con_putc('\n');
    int pic_row = 0, pic_scr = 0, pic_set = 0;
    for (int r = 0; r < ROWS_TOTAL; r++) {
        if (pic && r == 1) { int px, py; con_gui_cursor(&px, &py); pic_row = py; pic_scr = (int)con_scroll_total(); pic_set = 1; }
        const char *logo = (r >= 1 && r <= nlogo) ? logo_lines[r - 1] : "";
        kputs(logo);
        for (int s = vislen(logo); s < logo_w; s++) con_putc(' ');
        kputs("    ");
        kputs(info[r]);
        con_putc('\n');
    }
    con_putc('\n');
    if (pic_set) con_image_place(pic_row - ((int)con_scroll_total() - pic_scr), 0, PIC_COLS, PIC_ROWS, pic == 2 ? 0 : 1, pic_data, pic_len);
}
