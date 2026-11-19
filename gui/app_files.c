/*
 * AstraSageOS File Explorer - the second graphical application.
 *
 * It works on the very same file tree as AST (fs.c): a change made in either is seen by the other at once
 * (the explorer follows fs_generation(), AST re-checks its folder before every prompt). Everything is drawn
 * with the gfx primitives - the icons are vector drawings made for this system, no bitmaps.
 *
 *   top bar    "up" button + the path of the open folder; click it and type a path to go there
 *   left       quick access: S:/  Home  Root  and, below, how full the drive is (text + bar)
 *   main       the folders (green) and files (white, with a mark for .txt .as .c .cpp .asm) of the folder
 *   right-click on an item: Delete / Rename / Info / Copy to Folder (drag it or use "Paste here")
 *   double-click: folders open, files open in AstraSage Edit (the command is typed into AST)
 *
 * Rules: modifications are only possible inside the signed-in user's own folder on S:. The OS root files
 * (boot, kernel, ...), the drive's system folders and other users' folders can be looked at but never
 * deleted or changed - the explorer shows an error (see fs_guard_modify in fs.c).
 */
#include "gui.h"
#define T(en, tr) config_tr(en, tr)

/* ---------------------------------------------------------------- layout */
#define TB      40            /* top bar */
#define ST      26            /* status line */
#define CW      104           /* grid cell */
#define CH      92
#define SCRW    10            /* scroll bar */
#define MAXI    256

#define C_BG (ui_th()->bg)
#define C_SIDE (ui_th()->side)
#define C_BAR (ui_th()->bar)
#define C_LINE (ui_th()->line)
#define C_TXT (ui_th()->txt)
#define C_DIM (ui_th()->dim)
#define C_GREEN (ui_th()->accent)
#define C_SEL (ui_th()->sel)
#define C_HOV (ui_th()->hov)

/* kinds of entries */
enum { K_FOLDER, K_TXT, K_AS, K_C, K_CPP, K_ASM, K_OTHER };

enum { M_NONE, M_MENU, M_RENAME, M_NEW, M_INFO, M_CONFIRM, M_ERROR, M_PASS };
/* what the password prompt (acs) was asked for */
enum { PA_DELETE, PA_RENAME, PA_NEW, PA_COPY };
static int  pw_act, pw_node, pw_dir, pw_isdir;
static char pw_buf[48];
static int  pw_len, pw_bad;
static int  acs_granted;                    /* the password was entered for the operation in progress */
enum { MI_DELETE, MI_RENAME, MI_INFO, MI_COPY, MI_NEWDIR, MI_NEWFILE, MI_RESTORE, MI_EMPTY };

static int cur;                       /* open folder (node) */
static int thispc;                    /* 1: the "This PC" overview is shown instead of a folder */
static int items[MAXI], nitems;
static int sel = -1, hover = -1;
static int scroll;                    /* first visible row */
static u32 seen_gen = 0xFFFFFFFFu;
static int seen_logged = -1;
static char status[110];

static int path_edit, path_len;
static char path_buf[100];

static int modal;
static int menu_x, menu_y, menu_target = -1, menu_n, menu_ids[6], menu_hot = -1;
static char text_buf[FS_NAME_MAX + 1];
static int text_len, text_new_dir;
static int modal_node = -1;
static char info_lines[8][96];
static int info_n;
static char err_msg[160];

static int copy_src = -1;             /* "Copy to Folder" picked: waiting for the destination folder */
static int press_idx = -1, press_x, press_y, drag_on, drag_node = -1, drag_x, drag_y;
static int last_click_idx = -1;
static u32 last_click_t;

/* ---------------------------------------------------------------- small helpers */

static int logged(void) { return user_name() != 0; }

static int ieq_cmp(const char *a, const char *b)       /* case-insensitive order */
{
    for (;; a++, b++) {
        char x = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a;
        char y = *b >= 'A' && *b <= 'Z' ? (char)(*b + 32) : *b;
        if (x != y) return (u8)x < (u8)y ? -1 : 1;
        if (!x) return 0;
    }
}

static int ext_is(const char *name, const char *ext)
{
    const char *dot = 0;
    for (const char *p = name; *p; p++) if (*p == '.') dot = p;
    if (!dot || dot == name) return 0;
    return !ieq_cmp(dot + 1, ext);
}

static int kind_of(int node)
{
    if (fs_is_dir(node)) return K_FOLDER;
    const char *n = fs_name(node);
    if (ext_is(n, "txt")) return K_TXT;
    if (ext_is(n, "as")) return K_AS;
    if (ext_is(n, "c") || ext_is(n, "h")) return K_C;
    if (ext_is(n, "cpp") || ext_is(n, "cc") || ext_is(n, "cxx") || ext_is(n, "hpp") || ext_is(n, "hh")) return K_CPP;
    if (ext_is(n, "asm") || ext_is(n, "s")) return K_ASM;
    return K_OTHER;
}

static const char *kind_text(int k)
{
    switch (k) {
    case K_FOLDER: return T("Folder", "Klasör");
    case K_TXT:    return T("Text file (.txt)", "Metin dosyası (.txt)");
    case K_AS:     return T("AstraScript file (.as)", "AstraScript dosyası (.as)");
    case K_C:      return T("C source file", "C kaynak dosyası");
    case K_CPP:    return T("C++ source file", "C++ kaynak dosyası");
    case K_ASM:    return T("Assembly source file", "Assembly kaynak dosyası");
    default:       return T("File", "Dosya");
    }
}

static void set_status(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    kvsnprintf(status, sizeof status, fmt, ap);
    va_end(ap);
}

static void size_text(char *o, size_t cap, u32 b)
{
    if (b < 1024) ksnprintf(o, cap, "%u B", b);
    else if (b < 1024 * 1024) ksnprintf(o, cap, "%u.%u KB", b / 1024, (b % 1024) * 10 / 1024);
    else ksnprintf(o, cap, "%u.%u MB", b / (1024 * 1024), (b % (1024 * 1024)) * 10 / (1024 * 1024));
}

static void date_text(char *o, size_t cap, u32 secs)
{
    if (!secs) { ksnprintf(o, cap, "unknown"); return; }
    hal_time t;
    ktime_unpack(secs, &t);
    ksnprintf(o, cap, "%u-%02u-%02u %02u:%02u", t.year, t.month, t.day, t.hour, t.min);
}

/* "S:/usrs/bob/home" or "Root:/boot" */
static void node_path(int node, char *out, size_t cap)
{
    int chain[24], n = 0;
    int s = fs_mount_s();
    for (int x = node; x > 0 && n < 24; x = fs_parent(x)) {
        if (x == s) break;
        chain[n++] = x;
    }
    int in_s = fs_is_inside(node, s);
    size_t o = (size_t)ksnprintf(out, cap, "%s", in_s ? "S:" : "Root:");
    if (!n) { ksnprintf(out + o, cap - o, "/"); return; }
    for (int i = n - 1; i >= 0 && o < cap; i--) o += (size_t)ksnprintf(out + o, cap - o, "/%s", fs_name(chain[i]));
}

/* ---------------------------------------------------------------- the list */

static gfx_rect content(void)
{
    window *w = wm_find_app(APP_FILES);
    return w ? wm_content_rect(w) : gfx_rect_make(0, 0, 0, 0);
}

static int side_w(gfx_rect cr) { return cr.w < 540 ? 124 : 168; }

static gfx_rect list_rect(gfx_rect cr)
{
    int sb = side_w(cr);
    return gfx_rect_make(cr.x + sb, cr.y + TB, cr.w - sb, cr.h - TB - ST);
}

static int cols_of(gfx_rect lr)
{
    int c = (lr.w - 14 - SCRW) / CW;
    return c < 1 ? 1 : c;
}

static int rows_total(gfx_rect lr) { int c = cols_of(lr); return (nitems + c - 1) / c; }
static int rows_vis(gfx_rect lr) { int v = (lr.h - 6) / CH; return v < 1 ? 1 : v; }

static void clamp_scroll(gfx_rect lr)
{
    int max = rows_total(lr) - rows_vis(lr);
    if (max < 0) max = 0;
    if (scroll > max) scroll = max;
    if (scroll < 0) scroll = 0;
}

static gfx_rect cell_rect(gfx_rect lr, int idx)
{
    int c = cols_of(lr);
    return gfx_rect_make(lr.x + 8 + (idx % c) * CW, lr.y + 6 + (idx / c - scroll) * CH, CW - 4, CH - 4);
}

static int cell_at(gfx_rect lr, int x, int y)
{
    if (x < lr.x || y < lr.y || x >= lr.x + lr.w - SCRW || y >= lr.y + lr.h) return -1;
    for (int i = 0; i < nitems; i++) {
        gfx_rect r = cell_rect(lr, i);
        if (r.y + r.h < lr.y) continue;
        if (r.y > lr.y + lr.h) break;
        if (gfx_rect_has(&r, x, y)) return i;
    }
    return -1;
}

static void relist(void)
{
    nitems = 0;
    if (!logged() || thispc) return;
    if (!fs_exists(cur) || !fs_is_dir(cur)) cur = user_home() > 0 ? user_home() : fs_mount_s();
    for (int c = fs_child(cur, 0); c > 0 && nitems < MAXI; c = fs_child(cur, c)) items[nitems++] = c;
    for (int i = 1; i < nitems; i++) {                         /* folders first, then by name */
        int v = items[i], j = i - 1;
        while (j >= 0) {
            int a = items[j];
            int after = (fs_is_dir(a) ? 0 : 1) > (fs_is_dir(v) ? 0 : 1) ||
                        ((fs_is_dir(a) == fs_is_dir(v)) && ieq_cmp(fs_name(a), fs_name(v)) > 0);
            if (!after) break;
            items[j + 1] = a; j--;
        }
        items[j + 1] = v;
    }
    if (sel >= nitems) sel = -1;
}

static void damage_all(void)
{
    gfx_rect cr = content();
    if (!gfx_rect_empty(&cr)) wm_damage(cr);
}

static void damage_cell(int idx)
{
    gfx_rect cr = content();
    if (gfx_rect_empty(&cr) || idx < 0 || idx >= nitems) return;
    gfx_rect r = cell_rect(list_rect(cr), idx);
    wm_damage(gfx_rect_make(r.x - 2, r.y - 2, r.w + 4, r.h + 4));
}

static void damage_status(void)
{
    gfx_rect cr = content();
    if (!gfx_rect_empty(&cr)) wm_damage(gfx_rect_make(cr.x + side_w(cr), cr.y + cr.h - ST, cr.w - side_w(cr), ST));
}

static void refresh(void)
{
    relist();
    gfx_rect cr = content();
    if (!gfx_rect_empty(&cr)) clamp_scroll(list_rect(cr));
    seen_gen = fs_generation();
    seen_logged = logged();
    damage_all();
}

static void navigate(int node)
{
    if (!fs_exists(node) || !fs_is_dir(node)) return;
    cur = node; thispc = 0;
    sel = -1; hover = -1; scroll = 0; press_idx = -1; last_click_idx = -1;
    path_edit = 0;
    set_status("");
    refresh();
}

static void go_up(void)
{
    int p = fs_parent(cur);
    if (thispc) { set_status(T("This is the top of the file tree.", "Dosya ağacının en üstündesiniz.")); damage_status(); return; }
    if (cur == 0) { thispc = 1; nitems = 0; sel = -1; hover = -1; scroll = 0; path_edit = 0; set_status(""); damage_all(); return; }
    if (p >= 0) navigate(p);
    else set_status(T("This is the top of the file tree.", "Dosya ağacının en üstündesiniz."));
    damage_status();
}

/* ---------------------------------------------------------------- operations */

static void show_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    kvsnprintf(err_msg, sizeof err_msg, fmt, ap);
    va_end(ap);
    modal = M_ERROR;
    menu_target = -1;
    damage_all();
}

static void report_fs_error(const char *what, const char *name, int e)
{
    if (e == FS_EACS)
        show_error("%s", T("Permission denied", "İzin Reddedildi"));
    else if (e == FS_EPROTECT)
        show_error(T("Error: '%s' is a protected root file. Root files can never be deleted or changed.", "Hata: '%s' korunan bir kök dosyasıdır. Kök dosyaları asla silinemez veya değiştirilemez."), name);
    else if (e == FS_ELOGIN)
        show_error("%s", T("Error: you must be signed in. Use AST: as login <PASSWORD> <NAME>", "Hata: giriş yapmalısınız. AST'de: as login <ŞİFRE> <İSİM>"));
    else if (e == FS_EOTHER)
        show_error(T("Error: '%s' belongs to another user. You can only change your own folder.", "Hata: '%s' başka bir kullanıcıya ait. Yalnızca kendi klasörünüzü değiştirebilirsiniz."), name);
    else
        show_error(T("Cannot %s '%s': %s", "%s yapılamadı '%s': %s"), T(what, what), name, fs_strerror(e));
}

static void open_file(int node)
{
    int p = fs_get_jail();
    if (!logged() || !fs_is_inside(node, p) || node == p) {
        set_status(T("AST can only open files inside your own folder (S:/usrs/%s).", "AST yalnızca kendi klasörünüzdeki dosyaları açabilir (S:/usrs/%s)."), user_name() ? user_name() : "?");
        damage_status();
        return;
    }
    char path[160];
    fs_getpath(node, path, sizeof path);                     /* relative to the user's folder: /home/x.as */
    char cmd[200];
    ksnprintf(cmd, sizeof cmd, "as edit %s", path);
    if (app_ast_run(cmd)) set_status(T("Opening %s in AstraSage Edit (AST)...", "%s AstraSage Edit'te (AST) açılıyor..."), fs_name(node));
    else set_status("%s", T("AST is busy - finish what is running there, then try again.", "AST meşgul - orada çalışanı bitirip tekrar deneyin."));
    damage_status();
}

static void open_item(int node)
{
    if (fs_is_dir(node)) navigate(node);
    else open_file(node);
}

static void ask_password(int act, int node, int dir, int isdir)
{
    pw_act = act; pw_node = node; pw_dir = dir; pw_isdir = isdir;
    pw_buf[0] = 0; pw_len = 0; pw_bad = 0;
    modal = M_PASS; modal_node = node;
    damage_all();
}

/* ---- Trash: S:/usrs/<name>/home/Trash. Deleting something of your own moves it there; deleting inside the Trash is for good. ---- */
static int trash_node(int create)
{
    int h = user_home();
    if (h <= 0) return -1;
    for (int c = fs_child(h, 0); c > 0; c = fs_child(h, c))
        if (fs_is_dir(c) && !kstrcmp(fs_name(c), "Trash")) return c;
    return create ? (fs_create("Trash", h, 1) > 0 ? trash_node(0) : -1) : -1;
}

static int in_trash(int node) { int t = trash_node(0); return t > 0 && node != t && fs_is_inside(node, t); }

static void to_trash(int node)
{
    int t = trash_node(1);
    char nm[FS_NAME_MAX + 1];
    kstrncpy(nm, fs_name(node), FS_NAME_MAX); nm[FS_NAME_MAX] = 0;
    if (t < 0 || fs_move(node, t) < 0) { report_fs_error("move to Trash", nm, FS_EFULL); return; }
    persist_flush();
    set_status(T("Moved '%s' to Trash", "'%s' çöp kutusuna taşındı"), nm);
    sel = -1; refresh();
}

static void restore_item(int node)
{
    char nm[FS_NAME_MAX + 1];
    kstrncpy(nm, fs_name(node), FS_NAME_MAX); nm[FS_NAME_MAX] = 0;
    if (fs_move(node, user_home()) < 0) { report_fs_error("put back", nm, FS_EFULL); return; }
    persist_flush();
    set_status(T("Put '%s' back in your home folder", "'%s' ana klasöre geri konuldu"), nm);
    sel = -1; refresh();
}

static void empty_trash(void)
{
    int t = trash_node(0), n = 0;
    if (t < 0) return;
    for (int c = fs_child(t, 0); c > 0; c = fs_child(t, 0)) { fs_delete(c); n++; }
    persist_flush();
    set_status(T("Trash emptied (%d items)", "Çöp boşaltıldı (%d öğe)"), n);
    sel = -1; refresh();
}

static void start_delete(int node)
{
    if (logged() && node != trash_node(0) && user_home() > 0 && fs_is_inside(node, user_home()) && !in_trash(node)) { to_trash(node); return; }
    if (node == trash_node(0)) { set_status(T("The Trash folder stays; use Empty Trash", "Çöp klasörü kalır; 'Çöpü boşalt' kullanın")); damage_all(); return; }
    int g = fs_guard_remove(node);
    if (g == FS_EACS && !acs_granted) { ask_password(PA_DELETE, node, 0, 0); return; }
    if (g < 0 && !(g == FS_EACS && acs_granted)) { report_fs_error("delete", fs_name(node), g); return; }
    modal = M_CONFIRM; modal_node = node;
    damage_all();
}

static void start_rename(int node)
{
    int g = fs_guard_rename(node);
    if (g == FS_EACS && !acs_granted) { ask_password(PA_RENAME, node, 0, 0); return; }
    if (g < 0 && !(g == FS_EACS && acs_granted)) { report_fs_error("rename", fs_name(node), g); return; }
    modal = M_RENAME; modal_node = node;
    kstrncpy(text_buf, fs_name(node), FS_NAME_MAX);
    text_buf[FS_NAME_MAX] = 0;
    text_len = (int)kstrlen(text_buf);
    damage_all();
}

static void start_new(int is_dir)
{
    int g = fs_guard_write_dir(cur);
    if (g == FS_EACS && !acs_granted) { ask_password(PA_NEW, -1, cur, is_dir); return; }
    if (g < 0 && !(g == FS_EACS && acs_granted)) { report_fs_error("create in", fs_name(cur) && cur ? fs_name(cur) : "Root", g); return; }
    modal = M_NEW; text_new_dir = is_dir;
    text_buf[0] = 0; text_len = 0;
    damage_all();
}

static int tree_stats(int dir, u32 *count, u32 *bytes)
{
    for (int c = fs_child(dir, 0); c > 0; c = fs_child(dir, c)) {
        (*count)++;
        if (fs_is_dir(c)) tree_stats(c, count, bytes);
        else *bytes += fs_size_real(c);
    }
    return 0;
}

static void start_info(int node)
{
    char a[40], b[40], p[120];
    info_n = 0;
    int k = kind_of(node);
    ksnprintf(info_lines[info_n++], 96, T("Name:      %s", "Ad:        %s"), fs_name(node));
    ksnprintf(info_lines[info_n++], 96, T("Type:      %s", "Tür:       %s"), kind_text(k));
    node_path(fs_parent(node) >= 0 ? fs_parent(node) : 0, p, sizeof p);
    ksnprintf(info_lines[info_n++], 96, T("Location:  %.80s", "Konum:     %.80s"), p);
    if (fs_is_dir(node)) {
        u32 n = 0, by = 0;
        tree_stats(node, &n, &by);
        size_text(a, sizeof a, by);
        ksnprintf(info_lines[info_n++], 96, T("Size:      %u item(s), %s inside", "Boyut:     %u öğe, içinde %s"), n, a);
    } else {
        size_text(a, sizeof a, fs_size_real(node));
        ksnprintf(info_lines[info_n++], 96, T("Size:      %s (%u bytes)", "Boyut:     %s (%u bayt)"), a, fs_size_real(node));
    }
    date_text(a, sizeof a, fs_ctime(node));
    date_text(b, sizeof b, fs_mtime(node));
    ksnprintf(info_lines[info_n++], 96, T("Created:   %s", "Oluşturma: %s"), a);
    ksnprintf(info_lines[info_n++], 96, T("Modified:  %s", "Değişiklik:%s"), b);
    int g = logged() ? fs_guard_modify(node) : FS_ELOGIN;
    ksnprintf(info_lines[info_n++], 96, T("Access:    %s", "Erişim:    %s"),
              g == FS_OK ? T("yours - can be changed", "sizin - değiştirilebilir") : g == FS_EOTHER ? T("another user's - read only", "başka kullanıcının - salt okunur") : T("protected - read only", "korumalı - salt okunur"));
    modal = M_INFO; modal_node = node;
    damage_all();
}

/* ---- copy ---- */

static int copy_into(int src, int dst)                       /* contents of src into the existing dst */
{
    if (!fs_is_dir(src)) return fs_copy_data(dst, src);
    for (int c = fs_child(src, 0); c > 0; c = fs_child(src, c)) {
        if (fs_is_inside(dst, c)) continue;                  /* never recurse into the copy itself */
        int n = fs_resolve(fs_name(c), dst);
        if (n == FS_ENOENT) n = fs_create(fs_name(c), dst, fs_is_dir(c));
        if (n < 0) return n;
        if (fs_is_dir(c) != fs_is_dir(n)) return FS_ENOTDIR;
        int r = copy_into(c, n);
        if (r < 0) return r;
    }
    return FS_OK;
}

static void do_copy(int src, int dstdir)
{
    if (!fs_exists(src) || !fs_exists(dstdir) || !fs_is_dir(dstdir)) return;
    if (src > 0 && fs_parent(src) == 0 && fs_is_dir(src)) { show_error("%s", T("Root folders cannot be copied.", "Kök klasörleri kopyalanamaz.")); return; }
    int g = fs_guard_write_dir(dstdir);
    if (g == FS_EACS && !acs_granted) { ask_password(PA_COPY, src, dstdir, 0); return; }
    if (g < 0 && !(g == FS_EACS && acs_granted)) { report_fs_error("copy into", dstdir ? fs_name(dstdir) : "Root", g); return; }
    if (fs_is_dir(src) && fs_is_inside(dstdir, src)) { show_error("%s", T("Cannot copy a folder into itself.", "Klasör kendi içine kopyalanamaz.")); return; }
    if (fs_resolve(fs_name(src), dstdir) >= 0) {
        show_error(T("'%s' already exists in that folder.", "'%s' bu klasörde zaten var."), fs_name(src));
        return;
    }
    int n = fs_create(fs_name(src), dstdir, fs_is_dir(src));
    if (n < 0) { report_fs_error("copy", fs_name(src), n); return; }
    int r = copy_into(src, n);
    if (r < 0) { fs_delete(n); report_fs_error("copy", fs_name(src), r); persist_flush(); return; }
    persist_flush();
    char p[100];
    node_path(dstdir, p, sizeof p);
    set_status(T("Copied '%s' to %.70s", "'%s' şuraya kopyalandı: %.70s"), fs_name(src), p);
    refresh();
}

/* ---- path bar ---- */

static int find_child(int dir, const char *name, size_t n)
{
    for (int c = fs_child(dir, 0); c > 0; c = fs_child(dir, c))
        if (kstrlen(fs_name(c)) == n && !kstrncmp(fs_name(c), name, n)) return c;
    return -1;
}

static int starts_ci(const char *s, const char *pre)
{
    for (; *pre; s++, pre++) {
        char a = *s >= 'A' && *s <= 'Z' ? (char)(*s + 32) : *s;
        char b = *pre >= 'A' && *pre <= 'Z' ? (char)(*pre + 32) : *pre;
        if (a != b) return 0;
    }
    return 1;
}

/* A typed location -> folder node, or -1 */
static int parse_path(const char *in)
{
    while (*in == ' ') in++;
    int node = cur;
    const char *p = in;
    if (starts_ci(p, "s:")) { node = fs_mount_s(); p += 2; }
    else if (starts_ci(p, "root:")) { node = 0; p += 5; }
    else if (*p == '/') { node = 0; }
    else if (starts_ci(p, "home")) { node = user_home(); p += 4; }
    else if (*p == '~') { node = user_home(); p++; }
    if (node < 0) return -1;
    while (*p) {
        while (*p == '/' || *p == ' ') p++;
        if (!*p) break;
        const char *e = p;
        while (*e && *e != '/') e++;
        size_t n = (size_t)(e - p);
        while (n && p[n - 1] == ' ') n--;
        if (n == 1 && p[0] == '.') { }
        else if (n == 2 && p[0] == '.' && p[1] == '.') { if (node > 0) node = fs_parent(node); }
        else if (n) {
            int c = find_child(node, p, n);
            if (c < 0 || !fs_is_dir(c)) return -1;
            node = c;
        }
        p = e;
    }
    return node;
}

static void commit_path(void)
{
    path_buf[path_len] = 0;
    int n = parse_path(path_buf);
    path_edit = 0;
    if (n < 0) {
        show_error(T("No such folder: %s", "Böyle bir klasör yok: %s"), path_buf);
        return;
    }
    navigate(n);
}

/* ---------------------------------------------------------------- drawing: icons */

static void tri(int x, int y, int s, u32 col)               /* filled right triangle, corner at the top-right */
{
    for (int dy = 0; dy < s; dy++)
        for (int dx = 0; dx < s; dx++)
            if (dx + dy >= s - 1) gfx_pixel(x + dx, y + dy, col);
}

static void ring_c(int cx, int cy, int ro, int ri, u32 col)       /* a "C": ring open to the right */
{
    for (int dy = -ro; dy <= ro; dy++)
        for (int dx = -ro; dx <= ro; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 > ro * ro + ro || d2 < ri * ri - ri) continue;
            if (dx > 0 && dy > -(dx * 3) / 4 - 1 && dy < (dx * 3) / 4 + 1) continue;       /* the opening */
            /* edge softening */
            int o = d2 > ro * ro - ro ? 150 : 255;
            int inn = d2 < ri * ri + ri ? 150 : 255;
            int a = o < inn ? o : inn;
            gfx_pixel(cx + dx, cy + dy, (col & 0x00FFFFFFu) | (u32)a << 24);
        }
}

static void plus(int cx, int cy, u32 col)
{
    gfx_fill_rect(cx - 2, cy, 5, 1, col);
    gfx_fill_rect(cx, cy - 2, 1, 5, col);
    gfx_fill_rect(cx - 2, cy + 1 - 1, 5, 2, col);
    gfx_fill_rect(cx - 1 + 1, cy - 2, 2, 5, col);
}

static void bold_text(int x, int y, const char *s, u32 col)
{
    gfx_text(x, y, s, col);
    gfx_text(x + 1, y, s, col);
}

/* a 40x40 icon; `bg` repaints the page corner so the fold looks cut */
static void draw_kind_icon(int k, int x, int y, u32 bg)
{
    if (!bg && k != K_FOLDER) {                                                  /* on the desktop: the cut corner shows the wallpaper */
        const gfx_surf *b = gfx_back();
        int sx = x + 30, sy = y + 6;
        bg = sx >= 0 && sy >= 0 && sx < b->w && sy < b->h ? (b->px[sy * b->stride + sx] | 0xFF000000u) : 0xFF808080u;
    }
    if (k == K_FOLDER) {
        gfx_round_rect(x + 3, y + 7, 17, 9, 3, 0xFF3f8f24);                      /* tab */
        gfx_round_rect(x + 2, y + 11, 36, 26, 4, 0xFF4aa32b);                    /* back */
        gfx_round_rect_ex(x + 2, y + 15, 36, 22, 4, 4, 4, 4, 0xFF63c13c);        /* front */
        gfx_fill_rect(x + 4, y + 16, 32, 2, 0x40FFFFFF);                         /* light edge */
        return;
    }
    gfx_round_rect(x + 6, y + 3, 28, 34, 3, 0xFF9AA0A8);                         /* page: thin grey rim ... */
    gfx_round_rect(x + 7, y + 4, 26, 32, 2, 0xFFF9F9F9);                         /* ... and the white sheet */
    gfx_fill_rect(x + 26, y + 3, 8, 8, bg);                                      /* cut corner ... */
    tri(x + 26, y + 3, 8, 0xFFC7C7C7);                                           /* ... and its fold */
    switch (k) {
    case K_TXT:
        for (int i = 0; i < 3; i++) gfx_fill_rect(x + 11, y + 16 + i * 6, 18, 2, 0xFF6e6e74);
        break;
    case K_AS:
        bold_text(x + 11, y + 14, "As", 0xFF1c8c1c);
        break;
    case K_C:
        ring_c(x + 19, y + 23, 9, 5, 0xFF00A9C9);
        break;
    case K_CPP:
        ring_c(x + 16, y + 24, 8, 4, 0xFF00A9C9);
        plus(x + 25, y + 13, 0xFF00A9C9);
        plus(x + 31, y + 13, 0xFF00A9C9);
        break;
    case K_ASM:
        bold_text(x + 8, y + 14, "ASM", 0xFF1b2a7a);
        break;
    default:
        break;
    }
}

/* ---- scalable vector icons: which 0 This PC, 1 S:/ drive, 2 Home, 3 Root, 4 RAM; sz is the side in pixels ---- */
#define SC(v) ((v) * sz / 64)

static void ic_disk(int x, int y, int sz)
{
    gfx_round_rect(x + SC(4), y + SC(8), SC(56), SC(26), SC(8) + 1, 0xFFD9DCE0);                   /* lid (top face) */
    gfx_round_rect(x + SC(10), y + SC(10), SC(44), SC(12), SC(5), 0xFFF3F4F6);
    gfx_round_rect(x + SC(2), y + SC(26), SC(60), SC(30), SC(5) + 1, 0xFFA6ABB2);                   /* silver rim */
    gfx_round_rect(x + SC(4), y + SC(28), SC(56), SC(26), SC(4) + 1, 0xFF2B2E33);                   /* dark front */
    gfx_round_rect_ex(x + SC(4), y + SC(28), SC(56), SC(10), SC(4) + 1, SC(4) + 1, 0, 0, 0xFF45494F);  /* sheen */
    int lr = sz >= 40 ? 3 : 2;
    gfx_circle(x + SC(14), y + SC(44), lr + 1, 0xFF1d6b2a);                                           /* green LED */
    gfx_circle(x + SC(14), y + SC(44), lr, 0xFF3CE05A);
    if (sz >= 40) gfx_text(x + SC(24), y + SC(36), "S:/", 0xFFE6E8EB);
}

static void ic_pc(int x, int y, int sz)
{
    gfx_round_rect(x + SC(6), y + SC(4), SC(52), SC(36), SC(3) + 1, 0xFFB8BEC6);                    /* monitor frame */
    gfx_round_rect(x + SC(9), y + SC(7), SC(46), SC(30), SC(2), 0xFF1E2430);
    gfx_round_rect(x + SC(10), y + SC(8), SC(44), SC(28), SC(2), 0xFF3AA6F2);                       /* screen */
    gfx_round_rect_ex(x + SC(10), y + SC(8), SC(44), SC(11), SC(2), SC(2), 0, 0, 0xFF6CC3FA);       /* glare */
    gfx_fill_rect(x + SC(28), y + SC(40), SC(8), SC(6), 0xFF9AA1AB);                                /* neck */
    gfx_round_rect(x + SC(20), y + SC(45), SC(24), SC(5), SC(2), 0xFF7E8590);                       /* foot */
    gfx_round_rect(x + SC(2), y + SC(52), SC(60), SC(9), SC(3), 0xFFC9CED5);                        /* keyboard */
    if (sz >= 40) for (int k = 0; k < 9; k++) gfx_fill_rect(x + SC(6) + k * SC(6), y + SC(55), SC(4), 2, 0xFF7E8590);
}

static void ic_home(int x, int y, int sz)
{
    for (int i = 0; i < SC(28); i++) {                                                               /* roof */
        int hw = SC(30) * (i + 1) / SC(28);
        gfx_fill_rect(x + SC(32) - hw, y + SC(4) + i, hw * 2, 1, 0xFFE0573C);
    }
    gfx_fill_rect(x + SC(11), y + SC(30), SC(42), SC(28), 0xFFF3E3C3);                              /* walls */
    gfx_fill_rect(x + SC(11), y + SC(30), SC(42), 1 + SC(2), 0xFFd9c49c);
    gfx_round_rect(x + SC(27), y + SC(40), SC(11), SC(18), 2, 0xFF8A5A2B);                          /* door */
    if (sz >= 40) gfx_round_rect(x + SC(15), y + SC(37), SC(8), SC(8), 1, 0xFF7CC4F5);              /* window */
    if (sz >= 40) gfx_round_rect(x + SC(41), y + SC(37), SC(8), SC(8), 1, 0xFF7CC4F5);
}

static void ic_root(int x, int y, int sz)
{
    gfx_round_rect(x + SC(2), y + SC(2), SC(60), SC(60), SC(10), 0xFF3B4A66);                        /* plate */
    int lw = sz >= 40 ? 2 : 1;
    gfx_fill_rect(x + SC(31), y + SC(22), lw, SC(14), 0xFFcfd6e4);                                   /* trunk */
    gfx_fill_rect(x + SC(16), y + SC(36), SC(32), lw, 0xFFcfd6e4);                                   /* bar */
    gfx_fill_rect(x + SC(16), y + SC(36), lw, SC(10), 0xFFcfd6e4);
    gfx_fill_rect(x + SC(47), y + SC(36), lw, SC(10), 0xFFcfd6e4);
    gfx_round_rect(x + SC(24), y + SC(10), SC(16), SC(13), 2, 0xFF63c13c);                           /* top node */
    gfx_round_rect(x + SC(9), y + SC(46), SC(15), SC(12), 2, 0xFF8fa3c8);
    gfx_round_rect(x + SC(40), y + SC(46), SC(15), SC(12), 2, 0xFF8fa3c8);
}

static void ic_ram(int x, int y, int sz)
{
    for (int i = 0; i < 8; i++) gfx_fill_rect(x + SC(6) + i * SC(7), y + SC(46), SC(4), SC(10), 0xFFE2B84A);   /* gold contacts */
    gfx_round_rect(x + SC(2), y + SC(12), SC(60), SC(36), SC(3), 0xFF2E8B57);                        /* module */
    for (int i = 0; i < 4; i++) gfx_round_rect(x + SC(8) + i * SC(13), y + SC(18), SC(10), SC(16), 1, 0xFF22262B);   /* chips */
    gfx_fill_rect(x + SC(8), y + SC(38), SC(46), 1 + SC(1), 0xFF6fd39b);
}

static void ic_bin(int x, int y, int sz)
{
    gfx_fill_rect(x + SC(14), y + SC(10), SC(36), SC(5), 0xFF9AA3B2);                               /* lid */
    gfx_fill_rect(x + SC(26), y + SC(6), SC(12), SC(5), 0xFF9AA3B2);
    gfx_round_rect(x + SC(16), y + SC(16), SC(32), SC(42), 3, 0xFF6C7686);                          /* bin */
    for (int k = 0; k < 3; k++) gfx_fill_rect(x + SC(23) + k * SC(9), y + SC(22), 1 + SC(1), SC(30), 0xFF3C4350);
}

static void draw_side_icon(int which, int x, int y)               /* 18x18 */
{
    switch (which) {
    case 0: ic_pc(x, y, 18); break;
    case 1: ic_disk(x, y, 18); break;
    case 2: ic_home(x, y, 18); break;
    case 4: ic_bin(x, y, 18); break;
    default: ic_root(x, y, 18); break;
    }
}

/* the application icon (dock, desktop): a green folder with "S:/" on it, drawn for a size x size squircle */
void files_icon_face(u32 *px, int size)
{
    gfx_surf t = { size, size, size, px };
    for (int i = 0; i < size * size; i++) px[i] = 0xFF111113u;
    for (int y = 0; y < size; y++) {                                  /* soft vertical shade of the plate */
        u32 sh = (u32)(y * 18 / size);
        for (int x = 0; x < size; x++) px[y * size + x] = 0xFF000000u | (0x1c - sh) << 16 | (0x1c - sh) << 8 | (0x20 - sh);
    }
    gfx_set_target(&t);
    gfx_set_clip(0);
    int u = size;                                                      /* unit = whole icon */
    int fx = u * 11 / 100, fw = u * 78 / 100;
    int tabw = fw * 40 / 100, tabh = u * 10 / 100;
    int by = u * 30 / 100, bh = u * 52 / 100;
    gfx_round_rect(fx + fw * 6 / 100, by - tabh + 2, tabw, tabh + 6, u * 4 / 100 + 1, 0xFF3f8f24);
    gfx_round_rect(fx, by, fw, bh, u * 6 / 100 + 1, 0xFF4aa32b);
    gfx_round_rect_ex(fx, by + bh * 14 / 100, fw, bh * 86 / 100, u * 6 / 100 + 1, u * 6 / 100 + 1, u * 6 / 100 + 1, u * 6 / 100 + 1, 0xFF6fcf45);
    gfx_fill_rect(fx + u * 3 / 100, by + bh * 14 / 100 + 1, fw - u * 6 / 100, 1 + u / 64, 0x50FFFFFF);
    /* "S:/" on the folder front, scaled font */
    int k = size >= 80 ? 2 : 1;
    int sc = k * (size >= 112 ? 2 : 1);
    const char *lab = "S:/";
    int tw = 3 * 8 * sc, th = 16 * sc;
    int tx = fx + (fw - tw) / 2, ty = by + bh * 14 / 100 + (bh * 86 / 100 - th) / 2;
    for (int i = 0; lab[i]; i++) {
        const unsigned char *g = gfx_font_glyph((unsigned char)lab[i]);
        for (int gy = 0; gy < 16; gy++)
            for (int gx = 0; gx < 8; gx++)
                if (g[gy] >> (7 - gx) & 1)
                    gfx_fill_rect(tx + (i * 8 + gx) * sc, ty + gy * sc, sc, sc, 0xFF0c2a0a);
    }
    gfx_set_target(0);
}

/* ---------------------------------------------------------------- drawing: window */

static void label_fit(int x, int y, int w, const char *s, u32 col)         /* centred, shortened with ".." */
{
    char t[40];
    int max = w / 8;
    if (max > 38) max = 38;
    size_t n = kstrlen(s);
    if ((int)n <= max) { kmemcpy(t, s, n + 1); }
    else { kmemcpy(t, s, (size_t)max - 2); t[max - 2] = '.'; t[max - 1] = '.'; t[max] = 0; n = (size_t)max; }
    int tw = (int)kstrlen(t) * 8;
    gfx_text(x + (w - tw) / 2, y, t, col);
}

static void field(int x, int y, int w, int h, const char *text, int focus, int caret)
{
    gfx_round_rect(x, y, w, h, 6, focus ? C_GREEN : C_LINE);
    gfx_round_rect(x + 1, y + 1, w - 2, h - 2, 5, ui_th()->field);
    int max = (w - 16) / 8;
    int n = (int)kstrlen(text);
    const char *t = text;
    if (n > max) { t = text + (n - max); n = max; }
    gfx_text(x + 8, y + (h - 16) / 2, t, C_TXT);
    if (caret) gfx_fill_rect(x + 8 + n * 8, y + (h - 16) / 2, 2, 16, C_GREEN);
}

static void button(int x, int y, int w, int h, const char *label, int primary)
{
    gfx_round_rect(x, y, w, h, 6, primary ? ui_th()->btn_main : ui_th()->btn2);
    gfx_text(x + (w - (int)kstrlen(label) * 8) / 2, y + (h - 16) / 2, label, primary ? ui_th()->btn_main_txt : C_TXT);
}

/* modal panel geometry (shared by drawing and hit testing) */
typedef struct { gfx_rect panel, ok, cancel, field; } mlay;

static mlay modal_layout(gfx_rect cr)
{
    mlay l;
    int w = 380, h = 150;
    if (modal == M_INFO) h = 70 + info_n * 20 + 44;
    if (modal == M_ERROR) h = 150;
    if (w > cr.w - 20) w = cr.w - 20;
    if (h > cr.h - 10) h = cr.h - 10;
    l.panel = gfx_rect_make(cr.x + (cr.w - w) / 2, cr.y + (cr.h - h) / 2, w, h);
    l.field = gfx_rect_make(l.panel.x + 16, l.panel.y + 58, w - 32, 30);
    l.ok = gfx_rect_make(l.panel.x + w - 16 - 96 - 8 - 96, l.panel.y + h - 46, 96, 30);
    l.cancel = gfx_rect_make(l.panel.x + w - 16 - 96, l.panel.y + h - 46, 96, 30);
    if (modal == M_INFO || modal == M_ERROR) l.ok = gfx_rect_make(l.panel.x + w - 16 - 96, l.panel.y + h - 46, 96, 30);
    return l;
}

static void wrap_text(int x, int y, int w, const char *s, u32 col, int max_lines)
{
    int per = w / 8;
    if (per < 8) return;
    size_t n = kstrlen(s), i = 0;
    for (int line = 0; line < max_lines && i < n; line++) {
        size_t e = i + (size_t)per;
        if (e >= n) e = n;
        else {
            size_t b = e;
            while (b > i && s[b] != ' ') b--;
            if (b > i) e = b;
        }
        char t[96];
        size_t len = e - i;
        if (len > 95) len = 95;
        kmemcpy(t, s + i, len); t[len] = 0;
        gfx_text(x, y + line * 20, t, col);
        i = e;
        while (i < n && s[i] == ' ') i++;
    }
}

static void draw_modal(gfx_rect cr)
{
    gfx_fill_rect(cr.x, cr.y, cr.w, cr.h, ui_th()->dim_modal);
    mlay l = modal_layout(cr);
    gfx_shadow(l.panel.x, l.panel.y + 3, l.panel.w, l.panel.h, 10, 14, 120);
    gfx_round_rect(l.panel.x, l.panel.y, l.panel.w, l.panel.h, 10, C_LINE);
    gfx_round_rect(l.panel.x + 1, l.panel.y + 1, l.panel.w - 2, l.panel.h - 2, 9, ui_th()->panel);
    int tx = l.panel.x + 16, ty = l.panel.y + 14;
    switch (modal) {
    case M_RENAME:
        gfx_text(tx, ty, T("Rename", "Yeniden adlandır"), C_GREEN);
        gfx_text(tx, ty + 22, T("New name:", "Yeni ad:"), C_DIM);
        field(l.field.x, l.field.y, l.field.w, l.field.h, text_buf, 1, 1);
        button(l.ok.x, l.ok.y, l.ok.w, l.ok.h, T("Rename", "Adlandır"), 1);
        button(l.cancel.x, l.cancel.y, l.cancel.w, l.cancel.h, T("Cancel", "İptal"), 0);
        break;
    case M_NEW:
        gfx_text(tx, ty, text_new_dir ? T("New folder", "Yeni klasör") : T("New file", "Yeni dosya"), C_GREEN);
        gfx_text(tx, ty + 22, T("Name:", "Ad:"), C_DIM);
        field(l.field.x, l.field.y, l.field.w, l.field.h, text_buf, 1, 1);
        button(l.ok.x, l.ok.y, l.ok.w, l.ok.h, T("Create", "Oluştur"), 1);
        button(l.cancel.x, l.cancel.y, l.cancel.w, l.cancel.h, T("Cancel", "İptal"), 0);
        break;
    case M_CONFIRM: {
        gfx_text(tx, ty, T("Delete", "Sil"), ui_th()->danger);
        char t[100];
        ksnprintf(t, sizeof t, T("Delete '%s'%s?", "'%s'%s silinsin mi?"), modal_node >= 0 && fs_exists(modal_node) ? fs_name(modal_node) : "?",
                  modal_node >= 0 && fs_exists(modal_node) && fs_is_dir(modal_node) ? T(" and everything inside it", " ve içindekilerin hepsi") : "");
        wrap_text(tx, ty + 30, l.panel.w - 32, t, C_TXT, 2);
        button(l.ok.x, l.ok.y, l.ok.w, l.ok.h, T("Delete", "Sil"), 1);
        button(l.cancel.x, l.cancel.y, l.cancel.w, l.cancel.h, T("Cancel", "İptal"), 0);
        break;
    }
    case M_INFO:
        gfx_text(tx, ty, T("Info", "Bilgi"), C_GREEN);
        for (int i = 0; i < info_n; i++) gfx_text(tx, ty + 30 + i * 20, info_lines[i], C_TXT);
        button(l.ok.x, l.ok.y, l.ok.w, l.ok.h, T("Close", "Kapat"), 1);
        break;
    case M_PASS: {
        gfx_text(tx, ty, T("Password required", "Parola gerekli"), ui_th()->danger);
        gfx_text(tx, ty + 22, pw_bad ? T("Wrong password - try again:", "Yanlış parola - tekrar deneyin:") : T("[acs] Pls Enter your Password:", "[acs] Lütfen parolanızı girin:"), pw_bad ? ui_th()->danger : C_DIM);
        char st[48]; int i = 0;
        for (; i < pw_len && i < 46; i++) st[i] = '*';
        st[i] = 0;
        field(l.field.x, l.field.y, l.field.w, l.field.h, st, 1, 1);
        button(l.ok.x, l.ok.y, l.ok.w, l.ok.h, "OK", 1);
        button(l.cancel.x, l.cancel.y, l.cancel.w, l.cancel.h, T("Cancel", "İptal"), 0);
        break;
    }
    case M_ERROR:
        gfx_text(tx, ty, T("Error", "Hata"), ui_th()->danger);
        wrap_text(tx, ty + 30, l.panel.w - 32, err_msg, C_TXT, 4);
        button(l.ok.x, l.ok.y, l.ok.w, l.ok.h, "OK", 1);
        break;
    default:
        break;
    }
}

#define MENU_TEXT(i) menu_txt_(i)
static const char *menu_txt_(int i)
{
    switch (i) {
    case 0: return T("Delete", "Sil");
    case 1: return T("Rename", "Yeniden adlandır");
    case 2: return T("Info", "Bilgi");
    case 3: return T("Copy to Folder", "Klasöre kopyala");
    case 4: return T("New Folder", "Yeni klasör");
    case 6: return T("Put back", "Geri koy");
    case 7: return T("Empty Trash", "Çöpü boşalt");
    default: return T("New File", "Yeni dosya");
    }
}

static gfx_rect menu_rect(gfx_rect cr)
{
    int w = 168, h = menu_n * 28 + 10;
    int x = menu_x, y = menu_y;
    if (x + w > cr.x + cr.w) x = cr.x + cr.w - w;
    if (y + h > cr.y + cr.h) y = cr.y + cr.h - h;
    if (x < cr.x) x = cr.x;
    if (y < cr.y) y = cr.y;
    return gfx_rect_make(x, y, w, h);
}

static void draw_menu(gfx_rect cr)
{
    gfx_rect m = menu_rect(cr);
    gfx_shadow(m.x, m.y + 3, m.w, m.h, 8, 12, 120);
    gfx_round_rect(m.x, m.y, m.w, m.h, 8, C_LINE);
    gfx_round_rect(m.x + 1, m.y + 1, m.w - 2, m.h - 2, 7, ui_th()->menu);
    for (int i = 0; i < menu_n; i++) {
        gfx_rect r = gfx_rect_make(m.x + 5, m.y + 5 + i * 28, m.w - 10, 26);
        if (menu_hot == i) gfx_round_rect(r.x, r.y, r.w, r.h, 5, ui_th()->menu_hot);
        gfx_text(r.x + 10, r.y + 5, MENU_TEXT(menu_ids[i]), menu_ids[i] == MI_DELETE ? ui_th()->danger : C_TXT);
    }
}

static void usage_bar(int x, int y, int w, u32 used, u32 total, int any)
{
    gfx_round_rect(x, y, w, 8, 4, ui_th()->btn2);
    u32 pct = total ? used * 100 / total : 0;
    int fill = total ? (int)((u32)w * used / total) : 0;
    if (fill < 4 && any) fill = 4;
    if (fill > w) fill = w;
    if (fill > 0) gfx_round_rect(x, y, fill, 8, 4, pct > 85 ? 0xFFE0573c : ui_th()->accent_hi);
}

/* the "This PC" tiles: 0 S: drive, 1 memory, 2 Root, 3 Home */
static gfx_rect pc_tile(gfx_rect lr, int i)
{
    int cols = lr.w >= 520 ? 2 : 1;
    int tw = (lr.w - 16 - SCRW - (cols - 1) * 10) / cols, th = 92;
    return gfx_rect_make(lr.x + 8 + (i % cols) * (tw + 10), lr.y + 34 + (i / cols) * (th + 10), tw, th);
}

static void draw_pc_view(gfx_rect lr)
{
    gfx_text(lr.x + 12, lr.y + 10, T("Devices and folders", "Aygıtlar ve klasörler"), C_DIM);
    u32 nu, bu, bc;
    fs_stats(&nu, &bu, &bc);
    char a[24], b[24], t[64];
    size_text(a, sizeof a, bu); size_text(b, sizeof b, bc);
    u32 sbu = bu, sbc = bc;
    while (sbc > 0x100000u) { sbc >>= 1; sbu >>= 1; }
    u32 mu = hal_mem_used_mib(), mt = hal_mem_total_mib();
    for (int i = 0; i < 4; i++) {
        gfx_rect r = pc_tile(lr, i);
        gfx_round_rect(r.x, r.y, r.w, r.h, 8, C_HOV);
        int ix = r.x + 12, iy = r.y + 14;
        int ic = i == 0 ? 1 : i == 1 ? 4 : i == 2 ? 3 : 2;
        if (ic == 1) ic_disk(ix, iy, 64);
        else if (ic == 4) ic_ram(ix, iy, 64);
        else if (ic == 3) ic_root(ix, iy, 64);
        else ic_home(ix, iy, 64);
        int tx = r.x + 88;
        if (i == 0) {
            gfx_text(tx, r.y + 12, T("Local Disk (S:/)", "Yerel Disk (S:/)"), C_TXT);
            usage_bar(tx, r.y + 38, r.w - 100, sbu, sbc, bu != 0);
            ksnprintf(t, sizeof t, T("%s used of %s", "%s kullanıldı / %s"), a, b);
            gfx_text(tx, r.y + 54, t, C_DIM);
        } else if (i == 1) {
            gfx_text(tx, r.y + 12, T("Memory (RAM)", "Bellek (RAM)"), C_TXT);
            usage_bar(tx, r.y + 38, r.w - 100, mu, mt, mu != 0);
            ksnprintf(t, sizeof t, T("%u MiB used of %u MiB", "%u MiB kullanıldı / %u MiB"), mu, mt);
            gfx_text(tx, r.y + 54, t, C_DIM);
        } else if (i == 2) {
            gfx_text(tx, r.y + 12, T("Root", "Kök"), C_TXT);
            gfx_text(tx, r.y + 36, T("System folders", "Sistem klasörleri"), C_DIM);
            gfx_text(tx, r.y + 54, "Root:/", C_DIM);
        } else {
            gfx_text(tx, r.y + 12, T("Home", "Ana klasör"), C_TXT);
            gfx_text(tx, r.y + 36, T("Your personal files", "Kişisel dosyalarınız"), C_DIM);
            gfx_text(tx, r.y + 54, T("Opens your home folder", "Ana klasörünüzü açar"), C_DIM);
        }
    }
}

static int pc_tile_at(gfx_rect lr, int x, int y)
{
    for (int i = 0; i < 4; i++) { gfx_rect r = pc_tile(lr, i); if (gfx_rect_has(&r, x, y)) return i; }
    return -1;
}

static void files_draw(window *w, gfx_rect cr, gfx_rect clip, int focused)
{
    (void)w; (void)focused;
    int sb = side_w(cr);
    gfx_rect lr = list_rect(cr);
    clamp_scroll(lr);

    /* backgrounds */
    gfx_fill_rect(cr.x, cr.y, cr.w, cr.h, C_BG);
    gfx_fill_rect(cr.x, cr.y, cr.w, TB, C_BAR);
    gfx_fill_rect(cr.x, cr.y + TB - 1, cr.w, 1, C_LINE);
    gfx_fill_rect(cr.x, cr.y + TB, sb, cr.h - TB, C_SIDE);
    gfx_fill_rect(cr.x + sb - 1, cr.y + TB, 1, cr.h - TB, C_LINE);

    /* top bar: up button and path */
    gfx_round_rect(cr.x + 8, cr.y + 6, 30, 28, 6, ui_th()->btn2);
    for (int i = 0; i < 6; i++) gfx_fill_rect(cr.x + 23 - i, cr.y + 12 + i, 2 * i + 2, 1, C_TXT);       /* arrow head */
    gfx_fill_rect(cr.x + 21, cr.y + 18, 4, 9, C_TXT);
    char pth[110];
    if (path_edit) { path_buf[path_len] = 0; kstrncpy(pth, path_buf, sizeof pth - 1); pth[sizeof pth - 1] = 0; }
    else if (logged() && thispc) ksnprintf(pth, sizeof pth, "%s", T("This PC", "Bu Bilgisayar"));
    else if (logged()) node_path(cur, pth, sizeof pth);
    else ksnprintf(pth, sizeof pth, "(not signed in)");
    field(cr.x + 46, cr.y + 6, cr.w - 54, 28, pth, path_edit, path_edit);

    /* sidebar */
    gfx_text(cr.x + 12, cr.y + TB + 10, T("Quick access", "Hızlı erişim"), C_DIM);
    const char *names[5] = { T("This PC", "Bu Bilgisayar"), "S:/", T("Home", "Ana klasör"), T("Root", "Kök"), T("Trash", "Çöp kutusu") };
    int s = fs_mount_s(), home = logged() ? user_home() : -1;
    int nodes4[5] = { -1, s, home, 0, logged() ? trash_node(0) : -1 };
    for (int i = 0; i < 5; i++) {
        gfx_rect r = gfx_rect_make(cr.x + 6, cr.y + TB + 32 + i * 32, sb - 12, 30);
        int on = logged() && (i == 0 ? thispc : (!thispc && nodes4[i] >= 0 && nodes4[i] == cur));
        if (on) gfx_round_rect(r.x, r.y, r.w, r.h, 6, C_SEL);
        draw_side_icon(i, r.x + 8, r.y + 6);
        gfx_text(r.x + 34, r.y + 7, names[i], logged() && (i == 0 || nodes4[i] >= 0) ? C_TXT : ui_th()->txt_off);
    }
    {   /* how full the drive and the memory are */
        u32 nu, bu, bc;
        fs_stats(&nu, &bu, &bc);
        char a[24], b[24], t[48];
        size_text(a, sizeof a, bu); size_text(b, sizeof b, bc);
        int by = cr.y + cr.h - 104;
        int bw = sb - 28;
        gfx_text(cr.x + 12, by, T("Drive S:", "Sürücü S:"), C_DIM);
        ksnprintf(t, sizeof t, "%s / %s", a, b);
        gfx_text(cr.x + 12, by + 16, t, C_TXT);
        u32 sbu = bu, sbc = bc;                          /* scale down so that the products fit in 32 bits */
        while (sbc > 0x100000u) { sbc >>= 1; sbu >>= 1; }
        usage_bar(cr.x + 12, by + 36, bw, sbu, sbc, bu != 0);
        u32 mu = hal_mem_used_mib(), mt = hal_mem_total_mib();
        gfx_text(cr.x + 12, by + 52, "RAM", C_DIM);
        ksnprintf(t, sizeof t, "%u / %u MiB", mu, mt);
        gfx_text(cr.x + 12, by + 68, t, C_TXT);
        usage_bar(cr.x + 12, by + 88, bw, mu, mt, mu != 0);
    }

    /* main area */
    gfx_rect lc = gfx_rect_isect(lr, clip);
    if (!gfx_rect_empty(&lc)) {
        gfx_set_clip(&lc);
        if (!logged()) {
            gfx_text(lr.x + 24, lr.y + 30, T("Sign in to browse your files.", "Dosyalara bakmak için giriş yapın."), C_TXT);
            gfx_text(lr.x + 24, lr.y + 54, T("In AST type:  as login <PASSWORD> <NAME>", "AST'de yazın:  as login <ŞİFRE> <İSİM>"), C_DIM);
        } else if (thispc) {
            draw_pc_view(lr);
        } else if (!nitems) {
            gfx_text(lr.x + 24, lr.y + 30, T("This folder is empty.", "Bu klasör boş."), C_DIM);
        }
        for (int i = 0; i < nitems; i++) {
            gfx_rect r = cell_rect(lr, i);
            if (r.y + r.h < lr.y) continue;
            if (r.y > lr.y + lr.h) break;
            u32 bg = C_BG;
            if (i == sel) { gfx_round_rect(r.x, r.y, r.w, r.h, 8, C_SEL); bg = ui_th()->cell_sel; }
            else if (i == hover) { gfx_round_rect(r.x, r.y, r.w, r.h, 8, C_HOV); bg = ui_th()->cell_hov; }
            int node = items[i];
            draw_kind_icon(kind_of(node), r.x + (r.w - 40) / 2, r.y + 8, bg);
            if (cur == 0) {                                      /* root folders: a root badge, and "!" for the important ones */
                int bx = r.x + (r.w - 40) / 2;
                ic_root(bx - 3, r.y + 8 + 26, 16);
                if (fs_is_system(node)) {
                    gfx_circle(bx + 40, r.y + 12, 7, 0xFFE0573C);
                    gfx_text(bx + 37, r.y + 4, "!", 0xFFFFFFFF);
                }
            }
            label_fit(r.x, r.y + 56, r.w, fs_name(node), C_TXT);
            if (copy_src == node) gfx_rect_outline(r.x, r.y, r.w, r.h, C_GREEN);
        }
        int total = rows_total(lr), vis = rows_vis(lr);
        if (total > vis) {                                       /* scroll bar */
            int th = lr.h - 8;
            gfx_round_rect(lr.x + lr.w - SCRW, lr.y + 4, SCRW - 3, th, 3, ui_th()->scr_track);
            int hh = th * vis / total;
            if (hh < 16) hh = 16;
            int hy = lr.y + 4 + (th - hh) * scroll / (total - vis);
            gfx_round_rect(lr.x + lr.w - SCRW, hy, SCRW - 3, hh, 3, ui_th()->scr_thumb);
        }
    }

    /* status line / copy banner */
    gfx_rect st = gfx_rect_make(cr.x + sb, cr.y + cr.h - ST, cr.w - sb, ST);
    gfx_rect sc = gfx_rect_isect(st, clip);
    if (!gfx_rect_empty(&sc)) {
        gfx_set_clip(&sc);
        gfx_fill_rect(st.x, st.y, st.w, st.h, C_BAR);
        gfx_fill_rect(st.x, st.y, st.w, 1, C_LINE);
        if (copy_src >= 0 && fs_exists(copy_src)) {
            char t[80];
            ksnprintf(t, sizeof t, T("Copy '%.30s': open the target folder", "'%.30s' kopyalanıyor: hedef klasörü açın"), fs_name(copy_src));
            gfx_text(st.x + 10, st.y + 5, t, C_GREEN);
            button(st.x + st.w - 188, st.y + 2, 100, 22, T("Paste here", "Buraya yapıştır"), 1);
            button(st.x + st.w - 82, st.y + 2, 76, 22, T("Cancel", "İptal"), 0);
        } else if (status[0]) {
            gfx_text(st.x + 10, st.y + 5, status, C_TXT);
        } else if (thispc) {
            gfx_text(st.x + 10, st.y + 5, T("This PC - click a tile to open it", "Bu Bilgisayar - açmak için bir kutucuğa tıklayın"), C_DIM);
        } else if (sel >= 0 && sel < nitems) {
            char t[100], z[24];
            int n = items[sel];
            if (fs_is_dir(n)) ksnprintf(t, sizeof t, "%s - %s", fs_name(n), T("folder", "klasör"));
            else { size_text(z, sizeof z, fs_size_real(n)); ksnprintf(t, sizeof t, "%s - %s", fs_name(n), z); }
            gfx_text(st.x + 10, st.y + 5, t, C_DIM);
        } else {
            char t[40];
            ksnprintf(t, sizeof t, T("%d item(s)", "%d öğe"), nitems);
            gfx_text(st.x + 10, st.y + 5, t, C_DIM);
        }
    }
    gfx_set_clip(&clip);

    if (modal == M_MENU) draw_menu(cr);
    else if (modal != M_NONE) draw_modal(cr);
    gfx_set_clip(&clip);
}

/* ---------------------------------------------------------------- input */

static void close_modal(void)
{
    modal = M_NONE; modal_node = -1; menu_target = -1; menu_hot = -1;
    acs_granted = 0; pw_buf[0] = 0; pw_len = 0;
    damage_all();
}

/* the password was typed: check it, then continue with what it was asked for (with acs on for that one step) */
static void do_copy(int src, int dstdir);
static void confirm_password(void)
{
    pw_buf[pw_len] = 0;
    if (!user_verify_password(pw_buf)) { pw_bad = 1; pw_buf[0] = 0; pw_len = 0; damage_all(); return; }
    int act = pw_act, node = pw_node, dir = pw_dir, isdir = pw_isdir;
    pw_buf[0] = 0; pw_len = 0;
    modal = M_NONE; acs_granted = 1;
    switch (act) {
    case PA_DELETE: start_delete(node); break;
    case PA_RENAME: start_rename(node); break;
    case PA_NEW:    start_new(isdir); (void)dir; break;
    case PA_COPY:   fs_acs_set(1); do_copy(node, dir); fs_acs_set(0); acs_granted = 0; break;
    }
    damage_all();
}

static void confirm_text(void)
{
    text_buf[text_len] = 0;
    if (modal == M_RENAME) {
        int node = modal_node;
        fs_acs_set(acs_granted);
        int r = fs_rename(node, text_buf);
        fs_acs_set(0);
        if (r < 0) { report_fs_error("rename", fs_name(node), r); return; }
        persist_flush();
        close_modal();
        set_status(T("Renamed to '%s'", "Yeni ad: '%s'"), text_buf);
        refresh();
    } else if (modal == M_NEW) {
        int r = fs_create(text_buf, cur, text_new_dir);
        if (r < 0) { report_fs_error("create", text_buf, r); return; }
        {   /* a new .as file starts from the AstraScript template, like in AST */
            size_t tl = kstrlen(text_buf);
            if (!text_new_dir && tl > 3 && !kstrcmp(text_buf + tl - 3, ".as")) { const char *t = ast_script_template(); fs_write(r, t, (u32)kstrlen(t), 0); }
        }
        persist_flush();
        close_modal();
        set_status(T("Created '%s'", "Oluşturuldu: '%s'"), text_buf);
        refresh();
    }
}

static void do_menu(int id, int target)
{
    close_modal();
    switch (id) {
    case MI_DELETE: if (target >= 0) start_delete(target); break;
    case MI_RENAME: if (target >= 0) start_rename(target); break;
    case MI_INFO:   if (target >= 0) start_info(target); break;
    case MI_COPY:
        if (target >= 0) {
            copy_src = target;
            set_status("");
            damage_all();
        }
        break;
    case MI_RESTORE: if (target >= 0) restore_item(target); break;
    case MI_EMPTY:   empty_trash(); break;
    case MI_NEWDIR:  start_new(1); break;
    case MI_NEWFILE: start_new(0); break;
    }
}

static int side_hit(gfx_rect cr, int x, int y)
{
    int sb = side_w(cr);
    for (int i = 0; i < 5; i++) {
        gfx_rect r = gfx_rect_make(cr.x + 6, cr.y + TB + 32 + i * 32, sb - 12, 30);
        if (gfx_rect_has(&r, x, y)) return i;
    }
    return -1;
}

static int side_node(int i)
{
    if (!logged()) return -1;
    return i == 0 ? -1 : i == 1 ? fs_mount_s() : i == 2 ? user_home() : i == 3 ? 0 : trash_node(1);
}

static void files_mouse(window *w, int x, int y, u8 buttons, u8 prev)
{
    (void)w;
    gfx_rect cr = content();
    gfx_rect lr = list_rect(cr);
    int ldown = (buttons & 1) && !(prev & 1), rdown = (buttons & 2) && !(prev & 2);

    if (x < 0) {                                                        /* pointer left the window */
        if (hover >= 0) { int h = hover; hover = -1; damage_cell(h); }
        return;
    }

    if (modal == M_MENU) {
        gfx_rect m = menu_rect(cr);
        int hot = -1;
        if (gfx_rect_has(&m, x, y)) {
            hot = (y - m.y - 5) / 28;
            if (hot < 0 || hot >= menu_n) hot = -1;
        }
        if (hot != menu_hot) { menu_hot = hot; wm_damage(m); }
        if (ldown || rdown) {
            int id = hot >= 0 ? menu_ids[hot] : -1, tg = menu_target;
            if (id >= 0 && ldown) do_menu(id, tg);
            else close_modal();
        }
        return;
    }
    if (modal != M_NONE) {
        if (!ldown) return;
        mlay l = modal_layout(cr);
        int ok = gfx_rect_has(&l.ok, x, y), cancel = gfx_rect_has(&l.cancel, x, y);
        if (modal == M_PASS) {
            if (ok) confirm_password(); else if (cancel) close_modal();
        } else if (modal == M_RENAME || modal == M_NEW) {
            if (ok) confirm_text(); else if (cancel) close_modal();
        } else if (modal == M_CONFIRM) {
            if (ok) {
                int node = modal_node;
                char nm[FS_NAME_MAX + 1];
                kstrncpy(nm, fs_name(node), FS_NAME_MAX); nm[FS_NAME_MAX] = 0;
                fs_acs_set(acs_granted);
                int g = fs_guard_remove(node);
                if (g < 0) { fs_acs_set(0); report_fs_error("delete", nm, g); return; }
                int r = fs_delete(node);
                fs_acs_set(0);
                persist_flush();
                close_modal();
                if (r < 0) report_fs_error("delete", nm, r);
                else { set_status(T("Deleted '%s'", "Silindi: '%s'"), nm); sel = -1; refresh(); }
            } else if (cancel) close_modal();
        } else {                                                        /* info, error */
            if (ok || cancel || !gfx_rect_has(&l.panel, x, y)) close_modal();
        }
        return;
    }

    /* dragging an item */
    if ((buttons & 1) && press_idx >= 0 && !ldown) {
        int dx = x - press_x, dy = y - press_y;
        if (!drag_on && dx * dx + dy * dy > 36 && press_idx < nitems) { drag_on = 1; drag_node = items[press_idx]; }
        if (drag_on) {
            wm_damage(gfx_rect_make(drag_x - 44, drag_y - 24, 88, 64));
            drag_x = x; drag_y = y;
            wm_damage(gfx_rect_make(drag_x - 44, drag_y - 24, 88, 64));
        }
        return;
    }
    if (!(buttons & 1) && (prev & 1)) {                                  /* released */
        if (drag_on) {
            int target = -1;
            int side = side_hit(cr, x, y);
            if (side >= 0) target = side_node(side);
            else {
                int i = cell_at(lr, x, y);
                if (i >= 0 && fs_is_dir(items[i])) target = items[i];
                else if (gfx_rect_has(&lr, x, y)) target = cur;
            }
            int src = drag_node;
            window *me = wm_find_app(APP_FILES);
            int outside = me && !gfx_rect_has(&me->r, x, y);
            drag_on = 0; drag_node = -1; press_idx = -1;
            wm_damage(gfx_rect_make(0, 0, 100000, 100000));
            damage_all();
            if (target >= 0 && src >= 0) do_copy(src, target);
            else if (outside && src >= 0 && !wm_window_at(x, y)) {
                if (ui_desktop_drop(src, x, y) == 0) set_status(T("Placed on the desktop: %s", "Masaüstüne bırakıldı: %s"), fs_name(src));
            }
            else set_status("%s", T("Dropped outside: nothing was copied.", "Dışarı bırakıldı: hiçbir şey kopyalanmadı."));
        }
        press_idx = -1;
        return;
    }

    /* hover highlight */
    if (!buttons) {                                                      /* (a release was handled above) */
        int h = cell_at(lr, x, y);
        if (h != hover) { int o = hover; hover = h; damage_cell(o); damage_cell(h); }
        return;
    }

    if (rdown) {                                                         /* right-click: the panel */
        path_edit = 0;
        int i = cell_at(lr, x, y);
        if (i >= 0) {
            if (sel != i) { int o = sel; sel = i; damage_cell(o); damage_cell(i); }
            menu_target = items[i];
            menu_n = 4;
            menu_ids[0] = MI_DELETE; menu_ids[1] = MI_RENAME; menu_ids[2] = MI_INFO; menu_ids[3] = MI_COPY;
            if (in_trash(items[i])) { menu_n = 3; menu_ids[0] = MI_RESTORE; menu_ids[1] = MI_DELETE; menu_ids[2] = MI_INFO; }
        } else if (gfx_rect_has(&lr, x, y) && logged() && !thispc) {
            menu_target = -1;
            menu_n = 2;
            menu_ids[0] = MI_NEWDIR; menu_ids[1] = MI_NEWFILE;
            if (cur == trash_node(0)) { menu_n = 1; menu_ids[0] = MI_EMPTY; }
        } else return;
        modal = M_MENU; menu_x = x; menu_y = y; menu_hot = -1;
        damage_all();
        return;
    }

    if (!ldown) return;

    /* left click */
    set_status("");
    gfx_rect up = gfx_rect_make(cr.x + 8, cr.y + 6, 30, 28);
    gfx_rect pb = gfx_rect_make(cr.x + 46, cr.y + 6, cr.w - 54, 28);
    if (gfx_rect_has(&up, x, y)) { path_edit = 0; if (logged()) go_up(); damage_all(); return; }
    if (gfx_rect_has(&pb, x, y)) {
        if (logged()) {
            node_path(cur, path_buf, sizeof path_buf);
            path_len = (int)kstrlen(path_buf);
            path_edit = 1;
        }
        damage_all();
        return;
    }
    if (path_edit) { path_edit = 0; damage_all(); }
    int sd = side_hit(cr, x, y);
    if (sd >= 0) {
        if (sd == 0) {
            if (logged()) { thispc = 1; nitems = 0; sel = -1; hover = -1; scroll = 0; set_status(""); damage_all(); }
            return;
        }
        int n = side_node(sd);
        if (n >= 0) navigate(n);
        return;
    }
    if (thispc && logged()) {                                            /* the This PC tiles */
        int t = pc_tile_at(lr, x, y);
        if (t == 0) navigate(fs_mount_s());
        else if (t == 2) navigate(0);
        else if (t == 3 && user_home() >= 0) navigate(user_home());
        else if (t == 1) set_status(T("Memory: %u of %u MiB in use", "Bellek: %u / %u MiB kullanımda"), hal_mem_used_mib(), hal_mem_total_mib());
        return;
    }
    if (copy_src >= 0) {                                                 /* the banner buttons */
        int sb = side_w(cr);
        gfx_rect st = gfx_rect_make(cr.x + sb, cr.y + cr.h - ST, cr.w - sb, ST);
        gfx_rect paste = gfx_rect_make(st.x + st.w - 188, st.y + 2, 100, 22);
        gfx_rect canc = gfx_rect_make(st.x + st.w - 82, st.y + 2, 76, 22);
        if (gfx_rect_has(&paste, x, y)) {
            int src = copy_src;
            int dst = cur;
            copy_src = -1;
            damage_all();
            do_copy(src, dst);
            return;
        }
        if (gfx_rect_has(&canc, x, y)) { copy_src = -1; damage_all(); return; }
    }
    int total = rows_total(lr), vis = rows_vis(lr);
    if (total > vis && x >= lr.x + lr.w - SCRW && gfx_rect_has(&lr, x, y)) {       /* scroll bar track */
        scroll += y < lr.y + lr.h / 2 ? -vis : vis;
        clamp_scroll(lr);
        damage_all();
        return;
    }
    int i = cell_at(lr, x, y);
    if (i < 0) {
        if (sel >= 0) { int o = sel; sel = -1; damage_cell(o); }
        damage_status();
        return;
    }
    u32 now = ui_ms();
    if (i == last_click_idx && now - last_click_t < 450) {               /* double click */
        last_click_idx = -1;
        press_idx = -1;
        open_item(items[i]);
        return;
    }
    last_click_idx = i; last_click_t = now;
    if (sel != i) { int o = sel; sel = i; damage_cell(o); damage_cell(i); }
    damage_status();
    press_idx = i; press_x = x; press_y = y;
}

static void keep_visible(gfx_rect lr)
{
    if (sel < 0) return;
    int c = cols_of(lr), row = sel / c, vis = rows_vis(lr);
    if (row < scroll) scroll = row;
    if (row >= scroll + vis) scroll = row - vis + 1;
    clamp_scroll(lr);
}

static void files_key(window *w, int k)
{
    (void)w;
    gfx_rect cr = content();
    gfx_rect lr = list_rect(cr);

    if (modal == M_PASS) {
        if (k == 27) close_modal();
        else if (k == '\n') confirm_password();
        else if (k == '\b') { if (pw_len) pw_len--; pw_buf[pw_len] = 0; damage_all(); }
        else if (k >= 32 && k < 127 && pw_len < (int)sizeof pw_buf - 2) { pw_buf[pw_len++] = (char)k; pw_buf[pw_len] = 0; damage_all(); }
        return;
    }
    if (modal == M_RENAME || modal == M_NEW) {
        if (k == 27) close_modal();
        else if (k == '\n') confirm_text();
        else if (k == '\b') { if (text_len) text_len--; text_buf[text_len] = 0; damage_all(); }
        else if (k >= 33 && k < 127 && text_len < FS_NAME_MAX - 1) { text_buf[text_len++] = (char)k; text_buf[text_len] = 0; damage_all(); }
        return;
    }
    if (modal == M_CONFIRM) {
        if (k == 27 || k == 'n' || k == 'N') close_modal();
        else if (k == '\n' || k == 'y' || k == 'Y') {
            int node = modal_node;
            char nm[FS_NAME_MAX + 1];
            kstrncpy(nm, fs_name(node), FS_NAME_MAX); nm[FS_NAME_MAX] = 0;
            fs_acs_set(acs_granted);
            int g = fs_guard_remove(node);
            if (g < 0) { fs_acs_set(0); report_fs_error("delete", nm, g); return; }
            int r = fs_delete(node);
            fs_acs_set(0);
            persist_flush();
            close_modal();
            if (r < 0) report_fs_error("delete", nm, r);
            else { set_status(T("Deleted '%s'", "Silindi: '%s'"), nm); sel = -1; refresh(); }
        }
        return;
    }
    if (modal != M_NONE) { if (k == 27 || k == '\n') close_modal(); return; }

    if (path_edit) {
        if (k == 27) { path_edit = 0; damage_all(); }
        else if (k == '\n') commit_path();
        else if (k == '\b') { if (path_len) path_len--; path_buf[path_len] = 0; damage_all(); }
        else if (k >= 32 && k < 127 && path_len < (int)sizeof path_buf - 2) { path_buf[path_len++] = (char)k; path_buf[path_len] = 0; damage_all(); }
        return;
    }
    if (!logged()) return;
    int c = cols_of(lr), old = sel;
    switch (k) {
    case 27: if (copy_src >= 0) { copy_src = -1; damage_all(); } return;
    case KEY_LEFT:  sel = sel < 0 ? (nitems ? 0 : -1) : (sel > 0 ? sel - 1 : sel); break;
    case KEY_RIGHT: sel = sel < 0 ? (nitems ? 0 : -1) : (sel + 1 < nitems ? sel + 1 : sel); break;
    case KEY_UP:    sel = sel < 0 ? (nitems ? 0 : -1) : (sel - c >= 0 ? sel - c : sel); break;
    case KEY_DOWN:  sel = sel < 0 ? (nitems ? 0 : -1) : (sel + c < nitems ? sel + c : sel); break;
    case KEY_HOME:  sel = nitems ? 0 : -1; break;
    case KEY_END:   sel = nitems - 1; break;
    case KEY_PGUP:  scroll -= rows_vis(lr); clamp_scroll(lr); damage_all(); return;
    case KEY_PGDN:  scroll += rows_vis(lr); clamp_scroll(lr); damage_all(); return;
    case '\n':      if (sel >= 0 && sel < nitems) open_item(items[sel]); return;
    case '\b':      go_up(); return;
    case KEY_DEL:   if (sel >= 0 && sel < nitems) start_delete(items[sel]); return;
    default: return;
    }
    if (sel != old) { keep_visible(lr); set_status(""); damage_all(); }
}

/* ---------------------------------------------------------------- lifecycle */

static void files_resized(window *w, gfx_rect cr)
{
    (void)w;
    clamp_scroll(list_rect(cr));
    wm_damage(cr);
}

static void files_closed(window *w)
{
    (void)w;
    modal = M_NONE; path_edit = 0; copy_src = -1; drag_on = 0; drag_node = -1; press_idx = -1;
    hover = -1; sel = -1; status[0] = 0;
}

const wm_app app_files_ops = { files_draw, files_resized, files_closed, 0, files_mouse, files_key, 0 };

void app_files_init(void)
{
    cur = logged() && user_home() > 0 ? user_home() : fs_mount_s();
    seen_gen = 0xFFFFFFFFu;
    seen_logged = -1;
}

void app_files_launch(void)
{
    window *w = wm_find_app(APP_FILES);
    if (w) {
        if (w->state == WS_MINIMIZED) wm_restore(w);
        else wm_raise(w);
        return;
    }
    int sw, sh;
    ui_resolution(&sw, &sh);
    gfx_rect work = ui_work_rect();
    int ow = 780, oh = 500;
    if (ow > sw - 24) ow = sw - 24;
    if (oh > work.h - 24) oh = work.h - 24;
    cur = logged() && user_home() > 0 ? user_home() : fs_mount_s();          /* a new window always starts at home */
    sel = -1; hover = -1; scroll = 0; modal = M_NONE; copy_src = -1; status[0] = 0;
    w = wm_create("AstraSageOS File Explorer", APP_FILES, (sw - ow) / 2 + 40, work.y + (work.h - oh) / 3 + 24, ow, oh);
    if (!w) return;
    refresh();
}

int app_files_running(void) { return wm_find_app(APP_FILES) != 0; }

/* Called every desktop step: follows what AST (or anything else) did to the files, and the login state. */
void app_files_pump(void)
{
    window *w = wm_find_app(APP_FILES);
    if (!w) return;
    int lg = logged();
    if (fs_generation() != seen_gen || lg != seen_logged) {
        if (lg && !seen_logged) cur = user_home() > 0 ? user_home() : fs_mount_s();
        if (!lg) { modal = M_NONE; path_edit = 0; copy_src = -1; }
        refresh();
    }
}

/* read-only views used by the self tests */
int app_files_count(void) { return nitems; }
int app_files_cur(void) { return cur; }
int app_files_thispc(void) { return thispc; }
int app_files_modal(void) { return modal; }
int app_files_item(int i) { return i >= 0 && i < nitems ? items[i] : -1; }
int app_files_selected(void) { return sel; }
int app_files_dragging(void) { return drag_on; }

/* the type icon for a desktop shortcut */
void app_files_draw_node_icon(int node, int x, int y) { draw_kind_icon(kind_of(node), x, y, 0); }

/* the icon carried by a drag; drawn by the desktop on top of everything, so it also shows outside the window */
void app_files_draw_ghost(void)
{
    if (!drag_on || drag_node < 0 || !fs_exists(drag_node)) return;
    gfx_rect keep = gfx_get_clip();
    gfx_rect g = gfx_rect_make(drag_x - 44, drag_y - 24, 88, 64);
    gfx_rect c = gfx_rect_isect(g, keep);
    if (gfx_rect_empty(&c)) return;
    gfx_set_clip(&c);
    draw_kind_icon(kind_of(drag_node), drag_x - 20, drag_y - 20, 0);
    label_fit(drag_x - 40, drag_y + 22, 80, fs_name(drag_node), 0xFFFFFFFF);
    gfx_set_clip(&keep);
}

/* the desktop opens something in the explorer: a folder is entered, a file's folder is shown with it selected */
void app_files_open_node(int node)
{
    if (!logged() || !fs_exists(node)) return;
    int dir = fs_is_dir(node) ? node : fs_parent(node);
    app_files_launch();
    window *w = wm_find_app(APP_FILES);
    if (!w) return;
    navigate(dir);
    if (!fs_is_dir(node))
        for (int i = 0; i < nitems; i++) if (items[i] == node) { sel = i; keep_visible(list_rect(content())); break; }
    damage_all();
}
