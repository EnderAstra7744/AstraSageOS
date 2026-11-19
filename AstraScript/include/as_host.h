/* AstraScript engine - what the host (the AstraSageOS kernel, or the test program) must provide. */
#ifndef AS_HOST_H
#define AS_HOST_H

void as_h_puts(const char *s);                               /* text to the console */
void as_h_error(int line, const char *msg);                  /* report an error (the host knows about check mode) */
int  as_h_check_mode(void);                                  /* 1 while the editor only checks the program */

int  as_h_gui_open(const char *title, int w, int h);         /* 0 = no desktop */
void as_h_gui_close(void);
void as_h_gui_clear(unsigned rgb);
void as_h_gui_rect(int x, int y, int w, int h, unsigned rgb);
void as_h_gui_pixel(int x, int y, unsigned rgb);
void as_h_gui_text(int x, int y, const char *s, unsigned rgb, int scale);
void as_h_gui_pal(int i, unsigned rgb);
void as_h_gui_sprite_row(int x, int y, int scale, const char *row);
void as_h_gui_show(void);
int  as_h_gui_icon(const char *path);                         /* window icon from a BMP file; 0 = cannot be read or is not a valid BMP */
int  as_h_gui_wait(int ms);
void as_h_gui_mouse(int *x, int *y);

int  as_h_file_read(const char *path, char *buf, int cap);   /* length or <0 */
int  as_h_file_put(const char *path, const char *text, int append);
int  as_h_exec(const char *cmd, char *out, int cap);         /* AST command, output captured; <0 not allowed */
int  as_h_readline(const char *prompt, char *buf, int cap);  /* 0 = no input available */
int  as_h_module(const char *name, char *buf, int cap);      /* source of "name.ash": the user's folder, installed packages, then the built-in lib; length or <0 */
int  as_h_package(const char *name, char *buf, int cap);     /* "use Name;": the .as sources of an installed package; <0 = not installed */
int  as_h_check_command(const char *line, char *msg, int cap);   /* validates "as <command> ..." without running it; 0 = fine, else a message in msg */
unsigned as_h_ms(void);

#endif
