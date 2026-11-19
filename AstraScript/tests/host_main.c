/* Test host: runs a .as file with the engine on a normal computer. Usage: as2_host file.as */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../include/astrascript.h"
#include "../include/as_host.h"

static char dir[256] = ".";
void as_h_puts(const char *s) { fputs(s, stdout); }
void as_h_error(int line, const char *msg) { printf("[Error] Line %d: %s\n", line, msg); }
int as_h_check_mode(void) { return 0; }
int as_h_gui_open(const char *t, int w, int h) { printf("<gui_open %s %dx%d>\n", t, w, h); return 1; }
void as_h_gui_close(void) { puts("<gui_close>"); }
void as_h_gui_clear(unsigned c) { printf("<gui_clear %06x>\n", c); }
void as_h_gui_rect(int x, int y, int w, int h, unsigned c) { printf("<gui_rect %d %d %d %d %06x>\n", x, y, w, h, c); }
void as_h_gui_pixel(int x, int y, unsigned c) { (void)x; (void)y; (void)c; }
void as_h_gui_text(int x, int y, const char *s, unsigned c, int sc) { printf("<gui_text %d %d '%s' %06x %d>\n", x, y, s, c, sc); }
void as_h_gui_pal(int i, unsigned c) { (void)i; (void)c; }
void as_h_gui_sprite_row(int x, int y, int s, const char *r) { (void)x; (void)y; (void)s; (void)r; }
void as_h_gui_show(void) {}
int as_h_gui_icon(const char *p) { printf("<gui_icon %s>\n", p); return 1; }
int as_h_gui_wait(int ms) { (void)ms; return -1; }
void as_h_gui_mouse(int *x, int *y) { *x = 5; *y = 6; }
int as_h_file_read(const char *p, char *b, int cap) { FILE *f = fopen(p, "rb"); if (!f) return -1; int n = (int)fread(b, 1, (size_t)cap, f); fclose(f); b[n] = 0; return n; }
int as_h_file_put(const char *p, const char *t, int a) { FILE *f = fopen(p, a ? "ab" : "wb"); if (!f) return -1; fputs(t, f); fclose(f); return 0; }
int as_h_exec(const char *c, char *o, int cap) { snprintf(o, (size_t)cap, "ran:%s", c); return 0; }
int as_h_readline(const char *p, char *b, int cap) { fputs(p, stdout); if (!fgets(b, cap, stdin)) return 0; b[strcspn(b, "\n")] = 0; return 1; }
int as_h_module(const char *n, char *b, int cap) { char p[400]; snprintf(p, sizeof p, "%s/%s", dir, n); return as_h_file_read(p, b, cap); }
int as_h_package(const char *n, char *b, int cap) { (void)n; (void)b; (void)cap; return -1; }
int as_h_check_command(const char *l, char *m, int c) { (void)l; (void)m; (void)c; return 0; }
unsigned as_h_ms(void) { return 0; }

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    static char src[65536];
    FILE *f = fopen(argv[1], "rb"); if (!f) { perror(argv[1]); return 2; }
    unsigned n = (unsigned)fread(src, 1, sizeof src - 1, f); fclose(f); src[n] = 0;
    strncpy(dir, argv[1], sizeof dir - 1); char *sl = strrchr(dir, '/'); if (sl) *sl = 0; else strcpy(dir, ".");
    if (argc > 2 && !strcmp(argv[2], "--check")) return as2_check(src, n);
    return as2_run(src, n, 0, 0);
}
