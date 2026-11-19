/*
 * AstraSageOS - core definitions shared by as_kernel, the AST terminal and
 * the built-in AstraScript interpreter.
 */
#ifndef ASTRA_H
#define ASTRA_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;

#define AS_OS_NAME      "AstraSageOS"
#define AS_KERNEL_NAME  "as_kernel"
#define AS_TERM_NAME    "AST"
#define AS_VERSION      "0.9.29"

/* ---- string helpers (kstring.c) ---- */
size_t kstrlen(const char *s);
int    kstrcmp(const char *a, const char *b);
int    kstrncmp(const char *a, const char *b, size_t n);
char  *kstrchr(const char *s, int c);
char  *kstrrchr(const char *s, int c);
char  *kstrstr(const char *h, const char *n);
size_t kstrcspn(const char *s, const char *reject);
char  *kstrncpy(char *d, const char *s, size_t n);
char  *kstrcpy(char *d, const char *s);
void  *kmemcpy(void *dst, const void *src, size_t n);
void  *kmemset(void *dst, int c, size_t n);
int    kmemcmp(const void *a, const void *b, size_t n);
void  *kmemmove(void *dst, const void *src, size_t n);

/* ---- calendar time (kstring.c) ---- */
typedef struct { u32 year, month, day, hour, min, sec; } hal_time;
u32  ktime_pack(const hal_time *t);          /* seconds since 2000-01-01 00:00:00 */
void ktime_unpack(u32 secs, hal_time *t);

/* ---- formatted output (kprintf.c) ---- */
int  kvsnprintf(char *buf, size_t n, const char *fmt, va_list ap);
int  ksnprintf(char *buf, size_t n, const char *fmt, ...);
void kprintf(const char *fmt, ...);
void kputs(const char *s);

/* ---- hashing (sha256.c) ---- */
void sha256(const void *data, size_t len, u8 out[32]);
void sha256_hex(const void *data, size_t len, char out[65]);

/* ---- hardware abstraction layer ----
 * Implemented by console.c / keyboard.c / serial.c / sysinfo.c on real hardware
 * (QEMU) and by tests/hal_stub.c for host-side unit tests. */
void        con_init(void);
void        con_capture_resume(char *buf, u32 cap, u32 len);   /* continue a paused capture (con_capture_end returned len) */
void        con_capture_begin(char *buf, u32 cap);   /* output goes to buf (no escapes) until con_capture_end() */
u32         con_capture_end(void);
void        con_putc(char c);       /* understands a subset of ANSI SGR */
void        con_clear(void);
void        con_set_font(const u8 *glyphs, u32 len);  /* 8x16, ASCII 32..126 */
void        serial_init(void);
void        serial_putc(char c);
int         serial_getb_nb(void);
int         serial_pending(void);   /* 1 when COM1 has a byte waiting */
int         kbd_getc(void);         /* blocking; printable ASCII, '\n' and '\b' only */
int         kbd_poll_nb(void);      /* non-blocking key, -1 = none */
void        kbd_wait_hint(void);    /* yield to the desktop while polling (no-op in text mode) */
int         kbd_getkey(void);       /* blocking; ASCII, Ctrl-letters (1..26) or KEY_* */
int         hal_term_size(u32 *cols, u32 *rows);   /* 1 = host terminal answered */
int         serial_terminal_present(void);
void        hal_datetime(hal_time *t);
const char *hal_cpu_name(void);
u32         hal_cpu_cores(void);
int         hal_cpu_long_mode(void);   /* 1 if the CPU can run 64-bit code (CPUID long mode; always 1 on ARM64) */
const char *hal_gpu_name(void);
const char *hal_model(void);
u32         hal_mem_total_mib(void);
u32         hal_mem_used_mib(void);
u32         hal_uptime_sec(void);
void        hal_shutdown(void);     /* power off (QEMU/Bochs/VirtualBox ports); halts with a message when the machine cannot */
void        hal_reboot(void);

/* ---- error screens (sys/errscreen.cpp, C++) ---- */
int         err_draw_panic(const char *reason);                  /* green screen into the framebuffer; 0 = no graphics */
int         err_draw_overload(const char *reason, int secs);     /* yellow screen; secs = seconds left */
void        err_kernel_panic(const char *reason) __attribute__((noreturn));   /* green screen, stops (no reboot loop) */
void        err_overload(const char *reason);                    /* yellow screen, 5 s countdown, restart */
void        err_log(const char *kind, const char *reason);       /* appends to /.term/log/errors.log (best effort) */
void        err_idt_init(void);                                  /* CPU exceptions -> err_kernel_panic */
int         proc_run(const char *name, int sleep_s);             /* as proc: run /proc/<name> (panic / overload trigger) */
int         user_integrity_check(char *why, int cap);
int         user_seal_app(const char *name, const char *src, u32 len, char *msg, int cap);   /* compile-check + encrypt + seal -> /user/apps/<name>.asx */
int         user_app_run(const char *name);            /* 0 = fine; else the first offending item in /user */       /* restart the machine (never returns on hardware) */
u32         hal_ticks(void);        /* free-running counter, used as entropy */

enum {
    KEY_UP = 0x101, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
    KEY_HOME, KEY_END, KEY_PGUP, KEY_PGDN, KEY_DEL,
    /* desktop shortcuts (the keyboard driver builds them from the modifier keys) */
    KEY_ALT_TAB = 0x240, KEY_ALT_LEFT, KEY_ALT_RIGHT, KEY_ALT_UP, KEY_ALT_DOWN,
    KEY_HOT = 0x300                                  /* Ctrl+Alt+letter = KEY_HOT + (letter - 'a') */
};

/* ---- ATA disk, primary master, PIO (ata.c) ---- */
int  ata_init(void);                         /* 1 = disk present, 0 = none */
u32  ata_sectors(void);
int  ata_read(u32 lba, void *buf512);        /* 0 = ok */
int  ata_write(u32 lba, const void *buf512); /* 0 = ok */
int  ata_init_drive(int d);                  /* d: 0 = primary master, 1 = primary slave (aspi repo disk) */
u32  ata_sectors_drive(int d);
int  ata_read_drive(int d, u32 lba, void *buf512);
int  ata_write_drive(int d, u32 lba, const void *buf512);

/* ---- persistence of the file tree on the ATA disk (persist.c) ---- */
int         persist_init(void);              /* detect disk and restore the tree; returns files restored or -1 */
int         persist_save(void);              /* 0 = ok */
void        persist_flush(void);             /* save only when the tree changed */
int         persist_active(void);
const char *persist_status(void);

/* ---- OS file tree (fs.c) ----
 * "/" is the real OS root: boot, kernel, terminal, fs, drives, sys, font (system
 * only, hidden from users). Drive S: lives at /drives/S, keeps its own layout
 * (boot kernel terminal fs drives sys font usrs) and holds every user's data in
 * S:/usrs/<name>/home. */
#define FS_MAX_NODES 1024
#define FS_NAME_MAX  24
#define FS_FILE_MAX  16384          /* largest text file the editor / as write / as get handle */
#define FS_STORE_MAX 262144         /* files up to this size live in RAM; bigger ones live on the disk only (a "big file") */
#define FS_BIG_MAX   0xFFFE0000u    /* largest file: just under 4 GiB (the disk must have a free contiguous area) */

enum {
    FS_OK      = 0,
    FS_ENOENT  = -1,
    FS_EEXIST  = -2,
    FS_ENOTDIR = -3,
    FS_EISDIR  = -4,
    FS_EFULL   = -5,
    FS_EINVAL  = -6,
    FS_ENAME   = -7,
    FS_ETOOBIG = -8,
    FS_EPROTECT = -9,                       /* protected system item (root files): never removed or changed */
    FS_EOTHER  = -10,                       /* belongs to another user */
    FS_ELOGIN  = -11,                       /* login required */
    FS_EACS    = -12,
    FS_EIO     = -13                        /* disk read/write error */
};

void        fs_init(void);
void        fs_seed_system(void);           /* (re)create the OS root folders and files */
int         fs_dirty(void);
void        fs_clear_dirty(void);
u32         fs_mtime(int node);             /* ktime_pack() stamp */
u32         fs_ctime(int node);             /* creation stamp, 0 = unknown */
u32         fs_generation(void);            /* changes whenever anything in the tree changes */
int         fs_exists(int node);
int         fs_rename(int node, const char *newname);
int         fs_move(int node, int newdir);  /* reparent (name clash: "~2" is added) */
int         fs_guard_remove(int node);      /* delete: FS_OK, FS_EACS (password needed) or FS_EPROTECT */
int         fs_guard_rename(int node);
void        fs_acs_set(int on);             /* the user proved the password: root items may be added / removed */
int         fs_acs_get(void);
int         fs_in_own_area(int node);
int         fs_guard_edit(int node);
int         fs_is_system(int node);

/* boot log (sys/bootlog.c): levels */
#define BL_INFO 0
#define BL_OK   1
#define BL_WARN 2
#define BL_ERR  3
void        bl_add(int level, const char *fmt, ...);
int         bl_count(void);
const char *bl_text(int i);
int         bl_level(int i);
int         fs_guard_delete(int node);
int         fs_guard_create(const char *path, int cwd);
int         fs_guard_modify(int node);      /* may this node be deleted / renamed by the logged-in user? */
int         fs_guard_write_dir(int dir);    /* may files be created / copied into this directory? */
int         fs_is_inside(int node, int ancestor);   /* node == ancestor or below it */
typedef struct {
    u8 used, is_dir; u16 pad;
    int32_t parent; u32 size, mtime;
    char name[FS_NAME_MAX];
    u32 ctime;                              /* creation stamp (0 = unknown, older disks) */
} fs_rec;
void        fs_clear_all(void);
void        fs_get_rec(int node, fs_rec *r);
int         fs_put_rec(int node, const fs_rec *r, const char *data);
int         fs_pwrite(int node, u32 off, const void *data, u32 len);   /* write at an offset; the file grows (big files need the disk) */
int         fs_pread(int node, u32 off, void *buf, u32 len);          /* bytes read, 0 at the end, or an error */
u32         fs_size_real(int node);                                   /* fs_size() is 0 for big files */
int         fs_is_big(int node);
int         fs_copy_data(int dst, int src);                           /* replace dst by the contents of src (any size) */
int         persist_big_reserve(int node, u32 bytes);                 /* disk extent of at least that size (keeps contents) */
int         persist_big_rw(int node, u32 off, void *buf, u32 len, int write);
void        persist_big_free(int node);
u32         persist_capacity_bytes(void);
u32         persist_used_bytes(void);
char       *fs_rec_buf(int node);           /* writable contents of a node restored with fs_put_rec(.., NULL) */
int         fs_data_dirty(int node);        /* contents changed since the last save */
int         fs_mount_s(void);               /* node of the S: drive root (/drives/S) */
void        fs_set_jail(int node);          /* visible root of the current session */
int         fs_get_jail(void);
int         fs_resolve(const char *path, int cwd);
int         fs_create(const char *path, int cwd, int is_dir);
int         fs_write(int node, const char *data, u32 len, int append);
int         fs_delete(int node);            /* recursive */
int         fs_is_dir(int node);
int         fs_parent(int node);            /* parent node, -1 for the OS root */
u32         fs_size(int node);
const char *fs_name(int node);
const char *fs_data(int node);
int         fs_child(int dir, int after);
void        fs_getpath(int node, char *buf, size_t cap);   /* relative to the jail */
void        fs_stats(u32 *nodes_used, u32 *bytes_used, u32 *bytes_cap);
const char *fs_strerror(int err);

/* ---- OS configuration (config.c): /config/settings in the OS root ---- */
enum { LANG_EN = 0, LANG_TR = 1, LANG_ES = 2, LANG_RU = 3, LANG_ZH = 4, LANG_COUNT };
/* colours are 0xRRGGBB, -1 = the theme's own colour (as custom ...) */
typedef struct {
    int lang, theme, login_gui, setup_done, repo_drive;
    int volume;                                         /* 0..100, the tray's sound slider */
    int tab_color, line_color, input_color;            /* terminal tabs / the line under them / the text typed at the prompt */
    int nf_logo[3], nf_info;                           /* neofetch: the 3 logo colours, the info panel colour */
    char nf_png[24], nf_txt[24];                       /* neofetch logo picture (/bin/neofetch/custom_logo) and text logo (custom_logo_txt) */
    int win_a_frame, win_a_content;                    /* window frame / window inside opacity in percent, 30..100 (Settings) */
    int wallpaper;                                     /* -1 = the theme's own picture, else the picture number (Ctrl+Alt+B) */
} as_settings;
int  png_decode_small(const u8 *d, u32 n, int *w, int *h, u32 *px, u32 cap);   /* neofetch logos: tiny PNGs, 0xAARRGGBB out */
int  con_rgb256(int rgb);                                 /* nearest xterm-256 colour of 0xRRGGBB */
int  color_parse(const char *s, int *rgb);              /* name, #RRGGBB, RRGGBB or "default"; 0 = ok */
void        config_init(void);              /* ensure /config, read /config/settings */
const as_settings *config_get(void);
enum { SND_OPEN = 0, SND_CLOSE = 1 };
void snd_play(int kind);                     /* window sounds (Sound Blaster 16 or Intel AC97, silent without one) */
int  snd_present(void);
const char *snd_card(void);                 /* "Sound Blaster 16", "Intel AC97" or "none" (known after the first sound) */
int  snd_requested(void);
int  snd_started(void);
void        config_set(const as_settings *s);   /* store + save */
void        config_save(void);
#define TT(en) config_tr(en, 0)          /* terminal text: translated from sys/lang/terminal.tsv (es ru zh tr) */
const char *config_tr(const char *en, const char *tr);   /* picks the text of the chosen language */

/* ---- accounts and sessions (users.c) ---- */
enum {
    US_OK      = 0,
    US_EEXIST  = -1,
    US_ENOUSER = -2,
    US_EPASS   = -3,
    US_ENAME   = -4,
    US_EFULL   = -5,
    US_EPASSFMT = -6
};

int         user_create(const char *name, const char *pass);
int         user_remove(const char *name, const char *pass);
int         user_login(const char *name, const char *pass);
void        user_logout(void);
int         user_lock_session(void);
const char *user_locked_name(void);
void        user_unlock_clear(void);
int         user_session_restore(void);  /* log in again from the saved (encrypted) session; 1 = done */
int         user_verify_password(const char *pass);
int         user_account_count(void);   /* accounts on this machine (0 = nobody ever signed up) */
extern void (*user_rename_hook)(const char *oldname, const char *newname);
int         user_change_name(const char *pass, const char *newname);        /* as account -change --NAME */
int         user_change_password(const char *pass, const char *newpass);   /* as account -change --PASSWORD */
const char *user_name(void);        /* NULL when nobody is logged in */
int         user_home(void);        /* node of S:/usrs/<name>/home */
const char *user_strerror(int err);

/* ---- execute permission files: <script>.asbin (perm.c) ---- */
enum {
    PM_OK       = 0,
    PM_ENOLOGIN = -1,
    PM_EFS      = -2,
    PM_EEXT     = -3,
    PM_EMISSING = -4,    /* no .asbin next to the script */
    PM_EBAD     = -5,    /* corrupt or forged */
    PM_EOWNER   = -6,
    PM_ECHANGED = -7,    /* script modified after permission was granted */
    PM_ENOENT   = -8
};

int         perm_grant(const char *path, int cwd);    /* as cper +x */
int         perm_revoke(const char *path, int cwd);   /* as cper -x */
int         perm_check(int script_node, const char *path, int cwd);
int         perm_seal_system(const char *path);               /* ASPI: package script seal */
int         perm_check_system(int script_node, const char *path);
const char *perm_strerror(int err);

/* ---- AstraScript interpreter (astrascript.c) ---- */
int  asc_run(const char *source, u32 length);     /* 0 = ran to the end, 1 = errors */

/* Static syntax check used by the editor; never executes anything. */
typedef struct { int line; char msg[48]; } asc_diag;
const char *ast_clip(int *len);                    /* shared clipboard text (Ctrl+C / Ctrl+K in the editor fill it, Ctrl+V pastes) */
void        ast_clip_set(const char *t, int len);
const char *ast_script_template(void);            /* the text of a new .as file (start; func main ... end;) */
int  asc_check(const char *source, u32 length, asc_diag *out, int max);
/* Same, with extra function definitions (other package files) appended after the program. */
int  asc_run_ctx(const char *source, u32 length, const char *ctx, u32 ctxlen);
int  asc_check_ctx(const char *source, u32 length, const char *ctx, u32 ctxlen, asc_diag *out, int max);

/* ---- network (drives/e1000.c, sys/net.c) ---- */
typedef struct { int card, link; char name[56]; u8 mac[6]; u32 ip, mask, gw, dns; } net_info;
int  pci_wireless_scan(int kind, char *first, u32 cap);      /* PCI search for Wi-Fi (0) / Bluetooth (1) adapters */
void net_status(net_info *o);                              /* card, link and address for the network manager */
void net_fmt_ip(char *o, u32 cap, u32 v);
void weather_command(const char *args);                    /* as weather */
void wifi_command(const char *args);                       /* as wifi */
void bluetooth_command(const char *args);                  /* as bluetooth */
void net_command(const char *args);                       /* as net ... */
void ping_command(const char *args);                      /* as ping <ip> [count] */
int  net_http_get(const char *url, const char **body, u32 *blen, int *status, int *truncated, int verbose);
const char *net_http_error(void);
/* AstraScript GUI (gui/app_script.c; tests stub them). sgui_open returns 0 when there is no graphical desktop. */
int  sgui_open(const char *title, int w, int h);
void sgui_close(void);
int  sgui_is_open(void);
void sgui_clear(u32 rgb);
void sgui_rect(int x, int y, int w, int h, u32 rgb);
void sgui_pixel(int x, int y, u32 rgb);
void sgui_text(int x, int y, const char *s, u32 rgb, int scale);
void sgui_pal(int i, u32 rgb);
void sgui_sprite_row(int x, int y, int scale, const char *row);
int  sgui_set_icon(const u8 *bmp, u32 len);       /* Init(Icon=...): BMP of at most 4096 bytes, 0 = refused */
void sgui_show(void);
int  sgui_wait(int ms);
void sgui_mouse(int *x, int *y);
int  sgui_event(int *ev, int ms);                      /* native programs: [type, x, y, code] */
void sgui_blit(const u32 *src, int x, int y, int w, int h, int stride);
int  sgui_size(int *w, int *h);
/* installed packages of type "app" (desktop icons); aspi_app_command: first command of the package, "" when it has none */
int  aspi_app_packages(char (*names)[32], int max);
int  aspi_app_command(const char *name, char *out, int cap);
int  ast_native_node(int node, const char *args);          /* run an ELF file (any size) as a native program (used by native ASPI packages) */
/* for AstraScript programs (ast.c): run an AST command and capture its output; files of the signed-in user */
int  ast_bg_exec(const char *cmd);                                  /* runs one command for a background job (no terminal window); 0 ok, -1 not signed in */
int  ast_capture_command(const char *cmd, char *out, int cap);      /* 0 ok, -1 refused */
int  ast_file_write(const char *path, const void *data, unsigned len);   /* binary-safe whole-file write for native programs */
int  ast_file_put(const char *path, const char *text, int append);  /* 0 ok, <0 error */
int  ast_file_read(const char *path, char *out, int cap);           /* length, <0 error */
/* network helpers (net.c) */
void net_poll(void);
void net_my_ip(char *out, u32 cap);
int net_http_fetch(const char *url, u8 *buf, u32 cap, u32 *hdr_len, u32 *body_off, u32 *body_len, int *status);
int net_http_request(const char *method, const char *url, const char *xhdr, const void *body, u32 blen, u8 *buf, u32 cap, u32 *hdr_len, u32 *body_off, u32 *body_len, int *status);
int net_http_download(const char *url, u8 *buf, u32 cap, u32 *len, int *status);

/* ---- ASPI package installer (aspi.c) ---- */
void aspi_command(const char *args);                      /* aspi ... (and: as aspi ...) */
int  aspi_package_command(const char *word, const char *args);  /* as <word>: command declared by an installed package; 1 = handled */
int  aspi_installed(const char *name);
int  aspi_lib_source(const char *pkg, char *out, int cap);  /* use Name; (adds its dependencies first) */
void aspi_lib_reset(void);

/* ---- package archives (archive.c) ---- */
enum { ARC_ASPI, ARC_JAR, ARC_TGZ, ARC_TXZ };
#define ARC_MAX_FILES 96
typedef struct { char name[96]; const u8 *data; u32 size; u8 is_dir; } arc_entry;
typedef struct { arc_entry e[ARC_MAX_FILES]; int n; } arc_list;
int         arc_load(int kind, const u8 *src, u32 len, arc_list *out, const char **why);
int         arc_kind_from_text(const char *s);
const char *arc_kind_name(int kind);

/* ---- editor (editor.c) ---- */
void editor_run(const char *path, int cwd, const char *shown);

/* ---- AST terminal (ast.c / neofetch.c) ---- */
const char *ast_check_command(const char *line);  /* NULL if valid inside a script */
void ast_main(void);
void ast_system_init(void);                       /* disk, file tree, font (once) */
void ast_session(void);                           /* banner + prompt loop; never returns */                 /* text console: init + shell */
void ast_system_init(void);          /* disk, file tree, font (once) */
void ast_session(void);              /* banner + shell loop; runs in the AST window on the desktop */
void ast_exec(const char *line, int from_script);
int  ast_tab_op(int op, const char *name, char *msg, int cap);   /* as tab: 0 list, 1 add, 2 remove, 3 switch (gui/app_ast.c) */
extern int ast_cur_tab;                           /* terminal tabs: the tab whose shell is running now */
void ast_set_tab_alive(int (*fn)(int));           /* tab manager: is tab n still there? */
void ast_engine_release(int tab);
#define AST_STATE_BYTES 36000
void ast_state_save(void *dst);                   /* terminal tabs: the folder, history and prompt state of one AST session */
void ast_state_load(const void *src);
void ast_state_blank(void *dst);
int  ast_prompt_state(int *typed_len);            /* 1 while the shell waits at its prompt */
void ast_input(char *buf, size_t cap);            /* line input with echo */
int  ast_command_count(void);
int  ast_is_builtin_name(const char *w);                   /* AST's own command names (packages may not take them) */
void cmd_neofetch(void);

#endif
