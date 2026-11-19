/*
 * AstraSageOS graphics: VBE framebuffer driver interface, drawing library (gfx),
 * AstraGUI (window manager) and AstraUI (desktop, dock, icons).
 */
#ifndef AS_GUI_H
#define AS_GUI_H
#include "astra.h"

/* ---------------------------------------------------------------- video (drives/video.c) */
#define GFX_MAXW 1366
#define GFX_MAXH 768

typedef struct {
    u32 addr;                    /* physical (= linear, no paging) framebuffer address */
    u32 w, h, pitch, bpp;        /* pixels, bytes per row, bits per pixel (24 or 32) */
    u8  rpos, gpos, bpos;        /* bit position of each colour channel */
    const char *how;             /* where the parameters came from */
} video_info;

/* Looks for a framebuffer: Multiboot framebuffer info first, then the Bochs/QEMU VBE (DISPI)
 * interface of the standard VGA. Returns 1 and fills `out` when graphics is available,
 * otherwise 0 and a reason in video_reason(). mbi may be NULL. */
int         video_init(u32 magic, const void *mbi);
const char *video_reason(void);
int         video_cmdline_has(const char *word);     /* word on the Multiboot command line */
void        video_text_mode(void);                    /* leave graphics (Bochs path) */
void        video_stage(int n);                       /* boot progress marker (port 0xE9 + white square) */
const video_info *video_get(void);

/* ---------------------------------------------------------------- pointer (drives/mouse.c) */
typedef struct { int x, y; u8 buttons; } mouse_ev;      /* buttons: 1 left, 2 right, 4 middle */
int  mouse_init(void);                                   /* 1 = PS/2 mouse answered */
void mouse_set_bounds(int w, int h);
void mouse_feed(u8 byte);                                /* one byte from the 8042 aux port */
int  mouse_pop(mouse_ev *ev);                            /* 1 = event returned */
void mouse_position(int *x, int *y);
int  mouse_wheel_take(void);                             /* wheel notches since the last call (> 0 = scroll down) */
int  mouse_present(void);

/* keyboard / input plumbing (terminal/keyboard.c) */
int  kbd_hw_pending(void);                               /* a byte waits (key, mouse or serial) */
int  kbd_hw_poll(void);                                  /* non-blocking key, -1 = none (feeds the mouse too) */
extern int  (*kbd_source)(void);                         /* where kbd_getkey() takes keys from */
extern void (*kbd_idle)(void);                           /* called while kbd_getkey() waits */

/* ---------------------------------------------------------------- gfx (gui/gfx.c) */
typedef struct { int x, y, w, h; } gfx_rect;
typedef struct { int w, h, stride; u32 *px; } gfx_surf;   /* 0xAARRGGBB, straight alpha */

#define ARGB(a, r, g, b) ((u32)(a) << 24 | (u32)(r) << 16 | (u32)(g) << 8 | (u32)(b))
#define RGB(r, g, b)     ARGB(255, r, g, b)
#define COL(rgb)         ((u32)0xFF000000u | (u32)(rgb))

int  gfx_init(const video_info *vi);
const gfx_surf *gfx_back(void);                       /* the off-screen back buffer */
int  gfx_width(void);
int  gfx_height(void);
void gfx_set_target(gfx_surf *s);                     /* NULL = back buffer */
void gfx_set_clip(const gfx_rect *r);                 /* NULL = whole target */
gfx_rect gfx_get_clip(void);                          /* current clip (whole target when none) */
gfx_rect gfx_rect_make(int x, int y, int w, int h);
int  gfx_rect_empty(const gfx_rect *r);
gfx_rect gfx_rect_isect(gfx_rect a, gfx_rect b);
gfx_rect gfx_rect_union(gfx_rect a, gfx_rect b);
int  gfx_rect_has(const gfx_rect *r, int x, int y);

void gfx_clear(u32 color);                             /* whole clip */
void gfx_pixel(int x, int y, u32 color);               /* alpha-blended */
void gfx_fill_rect(int x, int y, int w, int h, u32 color);   /* alpha-blended when alpha < 255 */
void gfx_line(int x0, int y0, int x1, int y1, u32 color);
void gfx_rect_outline(int x, int y, int w, int h, u32 color);
/* Rounded rectangle with anti-aliased corners; r clamped to half the size. */
void gfx_round_rect(int x, int y, int w, int h, int r, u32 color);
void gfx_round_rect_ex(int x, int y, int w, int h, int rtl, int rtr, int rbr, int rbl, u32 color);
void gfx_round_outline(int x, int y, int w, int h, int r, u32 color);   /* 1 px ring */
void gfx_circle(int cx, int cy, int r, u32 color);                       /* anti-aliased disc */
void gfx_shadow(int x, int y, int w, int h, int r, int spread, int alpha);   /* soft shadow around the rect */
int  gfx_text_w(const char *s);
void gfx_snapshot(gfx_rect r, u32 *out);                                           /* window transparency helpers */
void gfx_mix_snapshot(gfx_rect r, const u32 *snap, gfx_rect inner, int a_outer, int a_inner);
const unsigned char *gfx_font_glyph(unsigned char ch);   /* 16 rows, MSB = left pixel */
void gfx_text(int x, int y, const char *s, u32 color);                  /* UTF-8: ASCII + Turkish letters */
void gfx_text_scaled(int x, int y, const char *s, u32 color, int scale);
int  gfx_text_w_scaled(const char *s, int scale);                  /* 8x16 AstraSage font */
void gfx_glyph_cp(int x, int y, u32 cp, u32 fg, u32 bg);              /* a terminal cell by Unicode value (bit 31: right half of a wide one) */
int  gfx_is_wide(u32 cp);
void gfx_glyph(int x, int y, unsigned char ch, u32 fg, u32 bg);         /* one opaque cell, bg 0 alpha = transparent */
void gfx_set_target_alpha(int on);                    /* 1: draw "over" a target that has its own alpha (sprites) */
void gfx_opaque_runs(const gfx_surf *src, short *rx0, short *rx1);                 /* per row: longest fully opaque run */
void gfx_blit_runs(const gfx_surf *src, int x, int y, const short *rx0, const short *rx1);   /* sprite blit using those runs */
void gfx_blit_opaque(const gfx_surf *src, int x, int y);   /* source has no transparency: fast row copy */
void gfx_blit(const gfx_surf *src, int x, int y);                        /* per-pixel alpha */
void gfx_blit_scaled(const gfx_surf *src, int x, int y, int w, int h, int alpha);
void gfx_blit_alpha(const gfx_surf *src, int x, int y, int alpha);       /* extra 0..255 opacity */
void gfx_present(gfx_rect r, int cx, int cy);                            /* back buffer -> screen, cursor drawn at cx,cy */
void gfx_cursor_set(int x, int y, int visible);

/* BMP decoding (gui/bmp.c) */
typedef struct {
    int w, h, bpp, topdown, has_alpha;
    u32 rowbytes, npal;
    const u8 *pix;                 /* first stored row */
    const u8 *pal;                 /* BGRA entries, 4 bytes each */
} bmp_view;
enum { BMP_OK = 0, BMP_ETRUNC = -1, BMP_EMAGIC = -2, BMP_EFORMAT = -3, BMP_ESIZE = -4 };
int         bmp_open(const u8 *data, u32 len, bmp_view *v);
const char *bmp_strerror(int e);
u32         bmp_get(const bmp_view *v, int x, int y);                    /* 0xAARRGGBB */
/* Scales a bitmap into a new surface area. Aspect ratio kept: cover = fill dest and crop,
 * otherwise fit inside and center (rest untouched). */
void        gfx_draw_bmp_scaled(const bmp_view *v, gfx_surf *dst, int dx, int dy, int dw, int dh, int cover);

/* ---------------------------------------------------------------- themes (gui/theme.c) */
enum { THEME_WHITE = 0, THEME_BLACK, THEME_GREEN, THEME_NAVY, THEME_YELLOW, THEME_RED, THEME_BLUE, THEME_PURPLE, THEME_AURO, THEME_COUNT };
typedef struct {
    const char *name; int light;
    u32 border_f, border_u, title_f, title_u, txt_f, txt_u, btn_off, content;     /* window frame */
    u32 dock_bg, dock_line, label;                                                   /* dock, desktop icon labels */
    u32 bg, side, bar, line, txt, dim, accent, accent_hi, sel, hov;                  /* apps */
    u32 field, btn2, btn_main, btn_main_txt, panel, menu, menu_hot, danger, txt_off, cell_sel, cell_hov;
    u32 scr_track, scr_thumb, dim_modal;
    u32 term_bg, term_cursor, term_cursor_txt;                                       /* terminal */
} ui_theme;
const ui_theme *ui_th(void);
int     ui_theme_id(void);
const ui_theme *ui_theme_by_id(int id);
void    ui_theme_set(int id);
const char *ui_theme_label(int id);              /* localised name */
const char *ui_theme_kind(int id);               /* "Light" / "Dark", localised */
void    ui_theme_preview(int x, int y, int w, int h, int id);   /* mini desktop picture of a theme */
u32     ui_theme_serial(void);                    /* changes whenever the theme does (everything is redrawn) */
u32     ui_term_bg(u32 bg);
u32     ui_term_fg(u32 fg, u32 bg);

/* ---------------------------------------------------------------- AstraGUI (gui/astragui.c) */
enum { WS_NORMAL = 0, WS_MAXIMIZED = 1, WS_MINIMIZED = 2 };
#define WM_MAX_WINDOWS 6
#define WM_TITLE_H     32
#define WM_BORDER      1
#define WM_PAD         6            /* terminal content padding */
typedef struct window {
    int  used, id, app;
    char title[40];
    gfx_rect r;                     /* outer rectangle (frame + title bar + content) */
    gfx_rect restore;               /* geometry before maximise / minimise */
    int  state, was_max;
    int  minw, minh;
    int  snapped;                   /* 1: it sits on one half of the screen (Alt+Left / Alt+Right); `restore` holds the old size */
} window;

void    wm_init(int screen_w, int screen_h, gfx_rect work_area);
void    wm_set_work_area(gfx_rect work);
int     wm_window_hits(gfx_rect r);            /* a visible window overlaps r */
window *wm_create(const char *title, int app, int x, int y, int w, int h);
void    wm_destroy(window *w);
void    wm_request_close(window *w);              /* like the close button: the application may refuse */
void    wm_close(window *w);                      /* app closes its own window (calls `closed`, no `can_close` veto) */
window *wm_find_app(int app);
window *wm_focused(void);
void    wm_raise(window *w);                      /* to the front + focus */
void    wm_minimize(window *w);
void    wm_toggle_maximize(window *w);
void    wm_restore(window *w);                    /* from minimised */
void    wm_snap(window *w, int side);             /* 0 = left half of the work area, 1 = right half */
void    wm_unsnap(window *w);                     /* back to the size from before the snap */
void    wm_cycle(void);                           /* Alt+Tab: the window that has been in the back longest comes to the front */
gfx_rect wm_content_rect(const window *w);
int     wm_visible_count(void);
int     wm_order(int idx);                        /* window slot at z position idx (0 = back) */
window *wm_slot(int slot);
void    wm_damage(gfx_rect r);
int     wm_take_damage(gfx_rect *r);
/* Pointer input (screen coordinates). `over_ui` is set by the UI when the pointer is on the dock. */
void    wm_pointer(int x, int y, u8 buttons, int over_ui);
void    wm_draw_all(gfx_rect clip);               /* back to front */
int     wm_drag_slot(void);                       /* window being moved with its cached sprite, else -1 */
void    wm_set_skip(int slot);                    /* wm_draw_all leaves this window out (-1 = none) */
int     wm_drag_foreign_take(void);               /* 1 when something other than the drag changed the screen meanwhile */
void    wm_draw_dragged(gfx_rect clip);           /* the moving window only */
int     wm_busy(void);                            /* dragging or resizing right now */
int     wm_hover_button(void);                    /* 0 none, 1 close, 2 minimise, 3 maximise */
window *wm_window_at(int x, int y);
/* the application behind a window */
typedef struct {
    void (*draw)(window *w, gfx_rect content, gfx_rect clip, int focused);
    void (*resized)(window *w, gfx_rect content);
    void (*closed)(window *w);
    void (*clicked)(window *w, int x, int y);
    /* optional, richer input (used when present instead of `clicked`): every pointer event over the content
     * area, all events while a button is held (the window keeps the pointer even outside it), and (-1,-1)
     * once when the pointer leaves. */
    void (*mouse)(window *w, int x, int y, u8 buttons, u8 prev);
    void (*key)(window *w, int key);          /* keyboard input while this window has the focus */
    int  (*can_close)(window *w);             /* optional: 0 = the red button is refused (the app shows its own question) */
} wm_app;
void    wm_register_app(int app, const wm_app *ops);
int     wm_key(int key);                          /* to the focused window's application; 1 = delivered */

/* ---------------------------------------------------------------- AstraUI (gui/astraui.c) */
enum { APP_AST = 0, APP_FILES, APP_WELCOME, APP_LOGIN, APP_SCRIPT, APP_STORE, APP_GRID, APP_SETTINGS, APP_TASKS, APP_COUNT };      /* APP_GRID is the 9-dot dock icon, not a window */
#define SHELL_TOP 28                              /* the strip at the top: notch + tray; windows and icons start below it */
void    ui_boot_splash(void);
int     ui_start(void);                           /* desktop + dock; 1 = ready */
void    ui_run(void);                             /* event loop, never returns */
void    ui_launch_app(int app);                   /* what a dock click does */
#define WP_COUNT 11
int     wp_render(int idx, int thumb, u32 *dst, int dw, int dh);   /* a built-in wallpaper (or its thumbnail) filling dw x dh; 0 = ok */
const char *wp_name(int i);
int     wp_effective(void);                       /* the picture that is on the desktop now (setting or the theme's own) */
void    wp_apply(int idx);                        /* -1 = the theme's own picture */
const gfx_surf *wp_preview(int idx);             /* 320x180 picture of a wallpaper */
void    wp_picker_toggle(void);                   /* Ctrl+Alt+B */
int     wp_picker_active(void);
void    wp_picker_tick(void);
void    wp_picker_draw(gfx_rect clip);
int     wp_picker_key(int k);
int     wp_picker_pointer(const mouse_ev *e, int down);
void    wp_picker_wheel(int notches);
int     ui_hotkey(int k);                         /* desktop shortcuts; 1 = handled */
int     ui_shortcut_count(void);
const char *ui_shortcut_keys(int i);
const char *ui_shortcut_text(int i);
void    ui_setup_reconcile(void);                 /* an existing account = setup done; welcome app removed */
void    ui_first_apps(void);                      /* what opens by itself at start (welcome / terminal / login) */
void    ui_anim_settle(void);                     /* finish every animation at once (tests) */
int     ui_desktop_drop(int node, int x, int y);  /* something dragged from the explorer onto the desktop; <0 = refused (error shown) */
int     ui_desktop_count(void);                   /* tests: visible desktop items */
int     ui_overlay_mode(void);                    /* tests: 0 none, 1 menu, 2 info, 3 error */
void    ui_step(void);                            /* one iteration of the desktop loop */
int     ui_active(void);                          /* graphical desktop running */
void    ui_resolution(int *w, int *h);
void    ui_note(const char *msg);                 /* queue a notice for the terminal banner */
int     ui_take_note(char *out, size_t cap);
/* the one icon pipeline used by dock and desktop */
void    draw_app_icon(int app, int x, int y, int size);
const gfx_surf *render_app_icon(int app, int size);
gfx_rect ui_dock_rect(void);                      /* empty (w = 0) while no app runs */
gfx_rect ui_work_rect(void);                      /* area windows may use (fixed, above the dock strip) */
int     ui_dock_hit(int x, int y);                /* app id under the point, -1 none */
int     ui_dock_count(void);
/* the shell (gui/shell.c): notch, tray, panels, app grid */
void    shell_init(int w, int h);
int     shell_tick(void);
void    shell_draw(gfx_rect clip);
int     shell_pointer(const mouse_ev *e, int ldown, int rdown, int lup);
int     shell_key(int k);
void    shell_notify(const char *msg);            /* notification centre (bell in the tray) */
int     shell_notif_count(void);
int     shell_notif_unread(void);
void    shell_notif_clear(void);
void    shell_open(int panel);                    /* 0 none, 1 clock, 2 power, 3 network, 4 volume, 5 app grid */
void    shell_toggle_grid(void);
int     shell_panel(void);
int     shell_sleeping(void);
int     shell_anim(int panel);
void    grid_icon_face(u32 *px, int size);        /* the 9-dot dock icon */
const char *ui_app_label(int app);
int     ui_desk_has_app(int app);
int     ui_desk_has_pkg(const char *name);
void    ui_desk_add_app(int app);
void    ui_desk_add_pkg(const char *name);
void    ui_launch_package(const char *name);
const gfx_surf *render_app_icon(int app, int size);
void    files_icon_face(u32 *px, int size);       /* File Explorer icon: green folder with S:/ */
void    login_icon_face(u32 *px, int size);       /* Login icon: grey user silhouette */
void    ui_user_glyph(int cx, int cy, int size, u32 body, u32 rim);   /* head + shoulders, drawn */

/* Login (gui/app_login.c) and Welcome / setup (gui/app_welcome.c) */
extern const wm_app app_welcome_ops, app_settings_ops;
void    app_settings_launch(void);
extern const wm_app app_tasks_ops;
void    app_tasks_launch(void);
void    app_tasks_pump(void);
void    tasks_icon_face(u32 *px, int size);          /* Task manager icon: dark tile with a green pulse line */
void    settings_icon_face(u32 *px, int size);       /* white and grey gear */
void    app_login_init(void);
int     ui_login_active(void);
void    app_login_test_close(void);               /* tests: take the login layer down */                    /* the full-screen login layer is up */
void    login_screen_draw(gfx_rect clip);
int     login_screen_pointer(const mouse_ev *e, int down);   /* 1 = taken (always while the layer is up) */
int     login_screen_key(int k);
void    login_screen_tick(void);
const gfx_surf *ui_wallpaper_surf(void);
u32     ui_wallpaper_serial(void);
void    shell_clock_strings(char *tm, size_t tc, char *dt, size_t dc);   /* "HH:MM", "Friday 8 May" */
void    app_login_launch(void);
int     ui_lock_now(void);                        /* put the signed-in user aside and ask for the password */
int     ui_boot_lock(void);                       /* ... at start, once the setup is done */
void    ui_boot_splash_finish(void);
unsigned wm_culled_count(void);                  /* windows skipped because another one covers them completely */
void    app_welcome_init(void);
void    app_welcome_launch(void);
void    app_store_pump(void);
void    app_welcome_pump(void);                   /* particles and the installer advance here, once per frame */
int     app_welcome_state(void);                  /* tests: page number */
int     app_welcome_done(void);                   /* tests: installer finished */
void    app_welcome_test_click(int what);         /* tests: 0 next, 1 back */
u32     ui_ms(void);                              /* monotonic milliseconds (PIT) */
extern u32 (*ui_ms_hook)(void);                   /* tests replace the clock */

/* AstraScript GUI window (gui/app_script.c) */
extern const wm_app app_script_ops;
int     app_script_wake(void);                    /* the shell coroutine should run (event / frame due) */
const bmp_view *sgui_icon(void);                   /* the script's own icon, 0 = the AstraScript logo */
const char *sgui_title(void);                      /* title of the script window ("App" when none) */
void    ui_icon_forget(int app);                  /* drop the cached icons of an app (its picture changed) */
void    sgui_abort(void);                         /* the shell is gone: close the script window */

/* AstraStore (gui/app_store.c) */
extern const wm_app app_store_ops;
void    app_store_init(void);
void    app_store_launch(void);
enum { NI_FROGGY = 0, NI_NETSURF, NI_SCRIPT, NI_COUNT };
int     ui_named_icon_id(const char *name);       /* picture icon of an app by name (FroggyCode, NetSurf), -1 = none */
const gfx_surf *ui_named_icon(int id, int size);   /* squircle-masked, cached */
void    store_icon_face(u32 *px, int size);       /* AstraStore dock / desktop icon */

/* AST application (gui/app_ast.c) */
void    app_ast_init(void);
void    app_ast_launch(void);
void    app_ast_pump(void);                       /* feed keys, resume the shell if it has input */
void    app_ast_pump_damage(void);                /* terminal changes -> window damage */
int     app_ast_running(void);
int     app_ast_push_key(int key);
extern const wm_app app_ast_ops;
/* Types a command into AST (opens/restores it first). 1 = accepted, 0 = AST busy/not at a prompt in time. */
int     app_ast_run(const char *cmd);
int     app_ast_bg(const char *cmd);                 /* runs a command in the background, no terminal window; 0 = another job is running */
int     app_ast_bg_busy(void);
u32     app_ast_bg_serial(void);                     /* grows by one each time a job ends */
const char *app_ast_bg_output(void);                 /* the text the last job printed */
void    app_ast_pending_pump(void);

/* File Explorer (gui/app_files.c) */
extern const wm_app app_files_ops;
void    app_files_open_node(int node);            /* show a file/folder in the explorer (folder opens, file is selected) */
void    app_files_draw_node_icon(int node, int x, int y);   /* the 40x40 type icon (desktop shortcuts) */
void    app_files_draw_ghost(void);               /* the icon carried by a drag (also outside the window) */
int     app_files_dragging(void);
void    app_files_init(void);
void    app_files_launch(void);
int     app_files_running(void);
void    app_files_pump(void);
/* read-only views for the self tests */
int     app_files_count(void);
int     app_files_cur(void);
int     app_files_thispc(void);
int     app_files_modal(void);                    /* 0 none, 1 menu, 2 rename, 3 new, 4 info, 5 confirm, 6 error */
int     app_files_item(int i);
int     app_files_selected(void);
int     app_files_dragging(void);

/* coroutine (gui/coro.c): the AST shell runs on its own stack and yields while waiting for keys */
typedef struct coro { size_t sp, resume_sp; void (*fn)(void); int started, done; u8 *stack; u32 stack_size; } coro;
void coro_init(coro *c, void (*fn)(void), u8 *stack, u32 size);
void coro_resume(coro *c);                        /* run until it yields or ends */
void coro_yield(void);                            /* from inside the coroutine */
coro *coro_current(void);

/* graphical console hooks (terminal/console.c) */
void con_gui_enable(int cols, int rows);
void con_gui_disable(void);
void con_gui_resize(int cols, int rows);
int  con_gui_active(void);
void con_gui_size(int *cols, int *rows);
void con_gui_cell(int x, int y, u8 *ch, u32 *fg, u32 *bg);
void con_gui_cell_cp(int x, int y, u32 *cp);                     /* Unicode value of a cell (0 = plain byte); bit 31 = right half of a wide char */
void con_gui_cursor(int *x, int *y);
int  con_gui_take_dirty(int *row0, int *row1);
/* tabs: the screen of a tab is saved to / loaded from a CON_STATE_BYTES buffer */
#define CON_STATE_BYTES 100000
void con_state_save(void *dst);
void con_state_load(const void *src);
void con_state_blank(void *dst, int cols, int rows);
u32  con_scroll_total(void);                       /* how many times the text has scrolled up (to anchor pictures) */
void con_image_place(int row, int col, int cols, int rows, int src, const u8 *data, u32 len);   /* src 0 logo, 1 PNG */
int  con_image_count(void);
void con_image_get(int i, int *row, int *col, int *cols, int *rows);
const u8 *con_image_source(int *src, u32 *len, u32 *serial);
void con_gui_dirty_all(void);                      /* redraw the whole terminal (after a tab switch) */

#endif
