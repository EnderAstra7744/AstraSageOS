/*
 * Host-side HAL for the unit tests.
 *   - keyboard: keys come from a script file; <UP> <DOWN> <LEFT> <RIGHT> <HOME> <END>
 *     <PGUP> <PGDN> <DEL> <BS> <TAB> <ESC> <C-x> (Ctrl+letter) are special keys and
 *     <SNAP> asks the harness to take a screenshot of the virtual terminal
 *   - console: the raw byte stream exactly as the serial console would send it
 *     (ESC sequences kept) - tests/vt.py interprets it
 *   - disk: a file given in $AS_DISK acts as the ATA disk (absent = no disk)
 *   - time: fixed, so output is reproducible
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "astra.h"

static char *keys;
static size_t klen, kpos;
static FILE *disk;
static u32 disk_secs;

void con_init(void) {}

void con_clear(void) { fputs("\033[0m\033[2J\033[H", stdout); }
void con_set_font(const u8 *g, u32 len) { (void)g; (void)len; }

void con_putc(char c)
{
    if (c == '\n') fputs("\r\n", stdout);
    else if (c == '\b') fputs("\b \b", stdout);
    else if (c == '\t') fputs("    ", stdout);
    else putchar(c);
}

static void eof_exit(void)
{
    fflush(stdout);
    if (disk) fclose(disk);
    exit(0);
}

int kbd_getkey(void)
{
    for (;;) {
        if (kpos >= klen) eof_exit();
        char c = keys[kpos];
        if (c == '<') {
            static const struct { const char *n; int k; } sp[] = {
                {"<UP>", KEY_UP}, {"<DOWN>", KEY_DOWN}, {"<LEFT>", KEY_LEFT}, {"<RIGHT>", KEY_RIGHT},
                {"<HOME>", KEY_HOME}, {"<END>", KEY_END}, {"<PGUP>", KEY_PGUP}, {"<PGDN>", KEY_PGDN},
                {"<DEL>", KEY_DEL}, {"<BS>", '\b'}, {"<TAB>", '\t'}, {"<ESC>", 27}, {"<ENTER>", '\n'},
            };
            for (unsigned i = 0; i < sizeof sp / sizeof sp[0]; i++) {
                size_t l = strlen(sp[i].n);
                if (!strncmp(keys + kpos, sp[i].n, l)) { kpos += l; return sp[i].k; }
            }
            if (!strncmp(keys + kpos, "<SNAP>", 6)) {
                kpos += 6;
                fwrite("\0SNAP\0", 1, 6, stdout);   /* NUL framing: the harness splits here */
                continue;
            }
            if (!strncmp(keys + kpos, "<C-", 3) && keys[kpos + 4] == '>') {
                int k = keys[kpos + 3] - 'a' + 1;
                kpos += 5;
                return k;
            }
        }
        kpos++;
        if (c == '\r') continue;
        return (unsigned char)c;
    }
}

int kbd_getc(void)
{
    for (;;) {
        int k = kbd_getkey();
        if (k == '\n' || k == '\b' || (k >= 32 && k < 127)) return k;
    }
}

int serial_getb_nb(void) { return -1; }
int serial_pending(void) { return 0; }

/* `as reboot`: leave a marker in the byte stream and end the run (the disk file stays, like a real restart) */
void hal_reboot(void)
{
    fwrite("\0REBOOT\0", 1, 8, stdout);
    eof_exit();
}
void hal_shutdown(void)
{
    fwrite("\0SHUTDOWN\0", 1, 10, stdout);
    eof_exit();
}
int hal_term_size(u32 *cols, u32 *rows)
{
    const char *c = getenv("AS_COLS"), *r = getenv("AS_ROWS");
    *cols = c ? (u32)atoi(c) : 80;
    *rows = r ? (u32)atoi(r) : 25;
    return 1;
}

void hal_datetime(hal_time *t)
{
    t->year = 2026; t->month = 10; t->day = 11; t->hour = 21; t->min = 14; t->sec = 7;
}

const char *hal_cpu_name(void)      { return "Test CPU @ 3.00GHz"; }
u32         hal_cpu_cores(void)     { return 2; }
int         hal_cpu_long_mode(void) { return 1; }
const char *hal_gpu_name(void)      { return "QEMU Standard VGA"; }
const char *hal_model(void)         { return "QEMU (TCG)"; }
u32         hal_mem_total_mib(void) { return 128; }
u32         hal_mem_used_mib(void)  { return 3; }
u32         hal_uptime_sec(void)    { return 3725; }
u32         hal_ticks(void)         { static u32 t = 12345; return t += 7919; }

/* ---- ATA disk backed by a file ---- */
int ata_init(void)
{
    const char *path = getenv("AS_DISK");
    if (!path) return 0;
    disk = fopen(path, "r+b");
    if (!disk) return 0;
    fseek(disk, 0, SEEK_END);
    disk_secs = (u32)(ftell(disk) / 512);
    return disk_secs != 0;
}
u32 ata_sectors(void) { return disk_secs; }
int ata_read(u32 lba, void *b)
{
    if (!disk || lba >= disk_secs) return -1;
    fseek(disk, (long)lba * 512, SEEK_SET);
    return fread(b, 512, 1, disk) == 1 ? 0 : -1;
}
int ata_write(u32 lba, const void *b)
{
    if (!disk || lba >= disk_secs) return -1;
    fseek(disk, (long)lba * 512, SEEK_SET);
    if (fwrite(b, 512, 1, disk) != 1) return -1;
    fflush(disk);
    return 0;
}

/* big files: --bigtest runs on $AS_DISK (a blank disk of at least 64 MiB) */
static int bigtest(void)
{
    int bad = 0;
#define CK(c, m) do { if (c) printf("PASS: %s\n", m); else { printf("FAIL: %s\n", m); bad = 1; } } while (0)
    static unsigned char buf[70000], rb[70000];
    for (u32 i = 0; i < sizeof buf; i++) buf[i] = (unsigned char)(i * 31 + (i >> 8));
    fs_init();
    persist_init();
    int d = fs_create("bigdir", 0, 1);
    d = fs_resolve("/bigdir", 0);
    int f = fs_create("huge.bin", d, 0), small = fs_create("small.txt", d, 0);
    CK(d > 0 && f > 0 && small > 0 && persist_active(), "big: disk mounted, nodes created");
    fs_write(small, "hello", 5, 0);
    u32 total = 3u * 1024 * 1024 + 1234;                   /* 3 MiB: far beyond FS_STORE_MAX */
    int ok = 1;
    for (u32 o = 0; ok && o < total; o += sizeof buf) {
        u32 k = total - o < sizeof buf ? total - o : (u32)sizeof buf;
        for (u32 i = 0; i < k; i++) buf[i] = (unsigned char)((o + i) * 7 + ((o + i) >> 9));
        ok = fs_pwrite(f, o, buf, k) == FS_OK;
    }
    CK(ok && fs_is_big(f) && fs_size_real(f) == total && fs_size(f) == 0, "big: 3 MiB written in pieces becomes a big file");
    int good = 1;
    for (u32 o = 0; good && o < total; o += 4099) {
        u32 k = total - o < 4099 ? total - o : 4099;
        int r = fs_pread(f, o, rb, k);
        if (r != (int)k) { good = 0; break; }
        for (u32 i = 0; i < k; i++) if (rb[i] != (unsigned char)((o + i) * 7 + ((o + i) >> 9))) { good = 0; break; }
    }
    CK(good, "big: contents read back exactly (odd-sized reads)");
    CK(fs_pread(f, total, rb, 10) == 0 && fs_pread(f, total - 4, rb, 10) == 4, "big: reads stop at the end");
    CK(fs_pwrite(f, total + 100, "tail", 4) == FS_OK && fs_size_real(f) == total + 104 && fs_pread(f, total + 50, rb, 1) == 1 && rb[0] == 0, "big: growing with a gap (zero filled)");
    CK(persist_save() == 0, "big: saved");
    fs_clear_all();
    persist_init();
    d = fs_resolve("/bigdir", 0);
    int f2 = fs_resolve("/bigdir/huge.bin", 0);
    CK(f2 > 0 && fs_is_big(f2) && fs_size_real(f2) == total + 104, "big: still there, still big after a reboot");
    good = f2 > 0;
    for (u32 o = 0; good && o < total; o += 65536) {
        u32 k = total - o < 65536 ? total - o : 65536;
        if (fs_pread(f2, o, rb, k) != (int)k) { good = 0; break; }
        for (u32 i = 0; i < k; i += 97) if (rb[i] != (unsigned char)((o + i) * 7 + ((o + i) >> 9))) { good = 0; break; }
    }
    CK(good, "big: contents survive the reboot");
    int s2 = fs_resolve("/bigdir/small.txt", 0);
    CK(s2 > 0 && fs_size(s2) == 5 && !kmemcmp(fs_data(s2), "hello", 5), "big: small file next to it is fine");
    int cp = fs_create("copy.bin", d, 0);
    CK(cp > 0 && fs_copy_data(cp, f2) == FS_OK && fs_size_real(cp) == total + 104, "big: copy of a big file");
    fs_pread(cp, 1000000, rb, 100);
    unsigned char want[100]; for (u32 i = 0; i < 100; i++) want[i] = (unsigned char)((1000000 + i) * 7 + ((1000000 + i) >> 9));
    CK(!kmemcmp(rb, want, 100), "big: the copy has the same bytes");
    u32 nu, bu, bc; (void)nu; (void)bu; (void)bc;
    CK(fs_delete(f2) == FS_OK && fs_delete(cp) == FS_OK, "big: deleting frees the extents");
    int g = fs_create("again.bin", d, 0);
    CK(fs_pwrite(g, 0, buf, 60000) == FS_OK && fs_pwrite(g, 60000, buf, 60000) == FS_OK && fs_size_real(g) == 120000 && !fs_is_big(g), "big: below the limit stays an ordinary file");
    CK(fs_write(g, "short", 5, 0) == FS_OK && fs_size(g) == 5, "big: rewrite shrinks");
    CK(persist_save() == 0, "big: saved again");
    return bad;
}

int main(int argc, char **argv)
{
    FILE *f;
    if (argc >= 2 && !strcmp(argv[1], "--bigtest")) return bigtest();
    if (argc < 2 || !(f = fopen(argv[1], "rb"))) { fprintf(stderr, "usage: host_test keys-file\n"); return 2; }
    fseek(f, 0, SEEK_END);
    klen = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    keys = malloc(klen + 1);
    if (fread(keys, 1, klen, f) != klen) return 2;
    fclose(f);
    ast_main();
    return 0;
}

/* ---- drive-indexed API: 0 = $AS_DISK, 1 = $AS_REPO (aspi repo disk) ---- */
static FILE *rdisk; static u32 rsecs;
int ata_init_drive(int d)
{
    if (d == 0) return ata_init();
    const char *path = getenv("AS_REPO");
    if (!path) return 0;
    rdisk = fopen(path, "rb");
    if (!rdisk) return 0;
    fseek(rdisk, 0, SEEK_END);
    rsecs = (u32)(ftell(rdisk) / 512);
    return rsecs != 0;
}
u32 ata_sectors_drive(int d) { return d == 0 ? disk_secs : rsecs; }
int ata_read_drive(int d, u32 lba, void *b)
{
    if (d == 0) return ata_read(lba, b);
    if (!rdisk || lba >= rsecs) return -1;
    fseek(rdisk, (long)lba * 512, SEEK_SET);
    return fread(b, 512, 1, rdisk) == 1 ? 0 : -1;
}
int ata_write_drive(int d, u32 lba, const void *b) { return d == 0 ? ata_write(lba, b) : -1; }

/* ---- graphical desktop: not running in the text-mode unit tests ---- */
int  ui_active(void) { return 0; }
void ui_resolution(int *w, int *h) { *w = 0; *h = 0; }
int  ui_take_note(char *out, size_t cap) { (void)out; (void)cap; return 0; }

/* network commands: the host test has no network card */
void net_command(const char *args) { (void)args; kprintf("net: not available in the test host\n"); }
void weather_command(const char *args) { (void)args; kprintf("WX|ERR|no network in the test host\n"); }
void wifi_command(const char *args) { (void)args; kprintf("wifi: not available in the test host\n"); }
void bluetooth_command(const char *args) { (void)args; kprintf("bluetooth: not available in the test host\n"); }
void net_status(net_info *o) { kmemset(o, 0, sizeof *o); kstrcpy(o->name, "test card"); o->card = 1; o->link = 1; o->ip = 0x0A00020F; o->mask = 0xFFFFFF00; o->gw = 0x0A000202; o->dns = 0x0A000203; }
void net_fmt_ip(char *o, u32 cap, u32 v) { ksnprintf(o, cap, "%u.%u.%u.%u", v >> 24, (v >> 16) & 255, (v >> 8) & 255, v & 255); }
void ping_command(const char *args) { (void)args; kprintf("ping: not available in the test host\n"); }
/* A fake GitHub for the host test: AS_FAKE_GH=<folder> serves the tree listing of that folder and its files under /HEAD/<path> */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
static char fake_body[65536];
int net_http_get(const char *u, const char **b, u32 *l, int *s, int *t, int v)
{
    (void)v;
    const char *dir = getenv("AS_FAKE_GH");
    if (!dir || !strstr(u, "/git/trees/")) return 0;
    int n = snprintf(fake_body, sizeof fake_body, "{\"sha\":\"x\",\"tree\":[");
    DIR *d = opendir(dir);
    struct dirent *e;
    int first = 1;
    while (d && (e = readdir(d))) {
        char p[600]; struct stat st;
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        if (e->d_name[0] == '.' || stat(p, &st) || !S_ISREG(st.st_mode)) continue;
        n += snprintf(fake_body + n, sizeof fake_body - (size_t)n, "%s{\"path\":\"packages/%s\",\"mode\":\"100644\",\"type\":\"blob\",\"sha\":\"0\",\"size\":%ld}", first ? "" : ",", e->d_name, (long)st.st_size);
        first = 0;
    }
    if (d) closedir(d);
    n += snprintf(fake_body + n, sizeof fake_body - (size_t)n, "],\"truncated\":false}");
    *b = fake_body; *l = (u32)n; *s = 200; *t = 0;
    return 1;
}
const char *net_http_error(void) { return "not available in the test host"; }

/* AstraScript GUI: the host test has no desktop */
int  sgui_open(const char *t, int w, int h) { (void)t; (void)w; (void)h; return 0; }
void sgui_close(void) {}
int  sgui_set_icon(const u8 *b, u32 n) { (void)b; (void)n; return 1; }
int  sgui_is_open(void) { return 0; }
void sgui_clear(u32 c) { (void)c; }
void sgui_rect(int x, int y, int w, int h, u32 c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
void sgui_pixel(int x, int y, u32 c) { (void)x; (void)y; (void)c; }
void sgui_text(int x, int y, const char *s, u32 c, int k) { (void)x; (void)y; (void)s; (void)c; (void)k; }
void sgui_pal(int i, u32 c) { (void)i; (void)c; }
void sgui_sprite_row(int x, int y, int k, const char *r) { (void)x; (void)y; (void)k; (void)r; }
void sgui_show(void) {}
int  sgui_wait(int ms) { (void)ms; return -1; }
void sgui_mouse(int *x, int *y) { *x = *y = 0; }
int net_http_download(const char *u, u8 *b, u32 c, u32 *l, int *st)
{
    const char *dir = getenv("AS_FAKE_GH");
    const char *h = strstr(u, "/HEAD/");
    if (!dir || !h) return 0;
    char name[300]; int k = 0;
    for (h += 6; *h && k < 290; h++) {
        if (h[0] == '%' && h[1] && h[2]) { char x[3] = { h[1], h[2], 0 }; name[k++] = (char)strtol(x, 0, 16); h += 2; }
        else name[k++] = *h;
    }
    name[k] = 0;
    char p[700]; snprintf(p, sizeof p, "%s/%s", dir, strrchr(name, '/') ? strrchr(name, '/') + 1 : name);
    FILE *f = fopen(p, "rb");
    if (!f) { *st = 404; *l = 0; return 1; }
    size_t r = fread(b, 1, c, f); fclose(f);
    *l = (u32)r; *st = 200;
    return 1;
}
int  kbd_poll_nb(void) { return -1; }
void kbd_wait_hint(void) {}
void net_poll(void) {}
void net_my_ip(char *o, u32 c) { if (c) o[0] = 0; }

/* native programs cannot run inside the host test */
int native_run(const u8 *img, u32 len, const char *args, const char **why) { (void)img; (void)len; (void)args; if (why) *why = "native programs need the real kernel"; return -1; }
int native_run_node(int node, const char *args, const char **why) { (void)node; (void)args; if (why) *why = "native programs need the real kernel"; return -1; }
void tls_command(const char *a) { (void)a; }

/* error screens: a marker in the byte stream and the end of the run */
void err_kernel_panic(const char *reason)
{
    err_log("kernel panic", reason);
    fwrite("\0PANIC\0", 1, 7, stdout); fputs(reason ? reason : "", stdout); fputc(0, stdout);
    eof_exit();
    for (;;) { }
}
void err_overload(const char *reason)
{
    err_log("heavy load", reason);
    fwrite("\0OVERLOAD\0", 1, 10, stdout); fputs(reason ? reason : "", stdout); fputc(0, stdout);
    eof_exit();
}
u32 ui_ms(void) { static u32 t; t += 1000; return t; }
int ast_tab_op(int op, const char *name, char *msg, int cap) { (void)op; (void)name; ksnprintf(msg, (size_t)cap, "Tabs need the graphical terminal window."); return -1; }
int con_gui_active(void) { return 0; }
unsigned con_scroll_total(void) { return 0; }
void con_gui_cursor(int *x, int *y) { *x = 0; *y = 0; }
void con_image_place(int r, int c, int w, int h, int s, const unsigned char *d, unsigned n) { (void)r; (void)c; (void)w; (void)h; (void)s; (void)d; (void)n; }
void con_gui_size(int *c, int *r) { *c = 80; *r = 25; }

void ui_launch_net(void) { }
