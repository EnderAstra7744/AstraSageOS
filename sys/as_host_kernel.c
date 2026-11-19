/* AstraScript engine 2 - the kernel side: connects the engine's as_h_* host calls to the AstraSageOS services. */
#include "astra.h"
#include "../AstraScript/include/as_host.h"

void asc_host_error(int line, const char *msg);     /* sys/astrascript.c: prints, or collects for the editor's check */
int  asc_host_check_mode(void);
int  asc_host_module(const char *name, char *buf, int cap);   /* user folder, packages */

void as_h_puts(const char *s) { kputs(s); }
void as_h_error(int line, const char *msg) { asc_host_error(line, msg); }
int  as_h_check_mode(void) { return asc_host_check_mode(); }

int  as_h_gui_open(const char *t, int w, int h) { return sgui_open(t, w, h); }
void as_h_gui_close(void) { sgui_close(); }
void as_h_gui_clear(unsigned c) { sgui_clear(c); }
void as_h_gui_rect(int x, int y, int w, int h, unsigned c) { sgui_rect(x, y, w, h, c); }
void as_h_gui_pixel(int x, int y, unsigned c) { sgui_pixel(x, y, c); }
void as_h_gui_text(int x, int y, const char *s, unsigned c, int sc) { sgui_text(x, y, s, c, sc); }
void as_h_gui_pal(int i, unsigned c) { sgui_pal(i, c); }
void as_h_gui_sprite_row(int x, int y, int sc, const char *r) { sgui_sprite_row(x, y, sc, r); }
int as_h_gui_icon(const char *path)
{
    static char buf[4200];
    int n = ast_file_read(path, buf, sizeof buf);
    return n > 0 && sgui_set_icon((const u8 *)buf, (u32)n);
}
void as_h_gui_show(void) { sgui_show(); }
int  as_h_gui_wait(int ms) { return sgui_wait(ms); }
void as_h_gui_mouse(int *x, int *y) { sgui_mouse(x, y); }

int as_h_file_read(const char *p, char *b, int cap) { return ast_file_read(p, b, cap); }
int as_h_file_put(const char *p, const char *t, int app) { return ast_file_put(p, t, app); }
int as_h_exec(const char *c, char *o, int cap) { return ast_capture_command(c, o, cap); }
int as_h_readline(const char *prompt, char *b, int cap)
{
    if (asc_host_check_mode()) { b[0] = 0; return 1; }
    if (prompt && *prompt) kputs(prompt);
    ast_input(b, (size_t)cap);
    return 1;
}
int as_h_module(const char *name, char *buf, int cap) { return asc_host_module(name, buf, cap); }
int as_h_package(const char *name, char *buf, int cap) { return aspi_lib_source(name, buf, cap); }
unsigned as_h_ms(void) { return 0; }

int as_h_check_command(const char *line, char *msg, int cap)
{
    const char *e = ast_check_command(line);
    if (!e) return 0;
    ksnprintf(msg, (size_t)cap, "%s", e);
    return 1;
}
