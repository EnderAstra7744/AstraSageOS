/* small POSIX leftovers for native programs: uname, dirent stubs, iconv (UTF-8 / Latin-1 / ASCII / CP1252), inet */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <dirent.h>
#include <iconv.h>
#include <sys/utsname.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <getopt.h>
#include <unistd.h>

int uname(struct utsname *b)
{
    memset(b, 0, sizeof *b);
    strcpy(b->sysname, "AstraSageOS"); strcpy(b->nodename, "astrasage"); strcpy(b->release, "0.9"); strcpy(b->version, "native"); strcpy(b->machine, "i386");
    return 0;
}
DIR *opendir(const char *p) { (void)p; errno = ENOENT; return 0; }
struct dirent *readdir(DIR *d) { (void)d; return 0; }
int closedir(DIR *d) { (void)d; return 0; }
int socket(int d, int t, int p) { (void)d; (void)t; (void)p; errno = ENOSYS; return -1; }
int connect(int s, const struct sockaddr *a, socklen_t l) { (void)s; (void)a; (void)l; errno = ENOSYS; return -1; }

int inet_aton(const char *cp, struct in_addr *ia)
{
    unsigned v[4]; int n = 0; const char *p = cp;
    while (n < 4) {
        char *e; unsigned long x = strtoul(p, &e, 10);
        if (e == p || x > 255) return 0;
        v[n++] = (unsigned)x; p = e;
        if (*p == '.') p++; else break;
    }
    if (n != 4 || *p) return 0;
    ia->s_addr = v[0] | v[1] << 8 | v[2] << 16 | v[3] << 24;
    return 1;
}
int inet_pton(int af, const char *s, void *dst) { if (af != AF_INET) { errno = EAFNOSUPPORT; return -1; } return inet_aton(s, dst); }
const char *inet_ntop(int af, const void *src, char *dst, socklen_t size)
{
    if (af != AF_INET) { errno = EAFNOSUPPORT; return 0; }
    const unsigned char *b = src;
    if (snprintf(dst, size, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]) >= (int)size) return 0;
    return dst;
}

/* ---- iconv: codes 1 = UTF-8, 2 = ISO-8859-1, 3 = ASCII, 4 = windows-1252 ---- */
static int cs(const char *n)
{
    char b[32]; size_t i;
    for (i = 0; n[i] && i < 31; i++) b[i] = (char)((n[i] >= 'A' && n[i] <= 'Z') ? n[i] + 32 : n[i]);
    b[i] = 0;
    char *sl = strstr(b, "//"); if (sl) *sl = 0;
    if (!strcmp(b, "utf-8") || !strcmp(b, "utf8")) return 1;
    if (!strcmp(b, "iso-8859-1") || !strcmp(b, "iso8859-1") || !strcmp(b, "latin1") || !strcmp(b, "l1")) return 2;
    if (!strcmp(b, "us-ascii") || !strcmp(b, "ascii") || !strcmp(b, "ansi_x3.4-1968")) return 3;
    if (!strcmp(b, "windows-1252") || !strcmp(b, "cp1252")) return 4;
    return 0;
}
iconv_t iconv_open(const char *to, const char *from)
{
    int t = cs(to), f = cs(from);
    if (!t || !f) { errno = EINVAL; return (iconv_t)-1; }
    return (iconv_t)(uintptr_t)(t << 4 | f);
}
int iconv_close(iconv_t cd) { (void)cd; return 0; }
static const unsigned short cp1252[32] = { 0x20AC,0x81,0x201A,0x192,0x201E,0x2026,0x2020,0x2021,0x2C6,0x2030,0x160,0x2039,0x152,0x8D,0x17D,0x8F,
    0x90,0x2018,0x2019,0x201C,0x201D,0x2022,0x2013,0x2014,0x2DC,0x2122,0x161,0x203A,0x153,0x9D,0x17E,0x178 };
size_t iconv(iconv_t cd, char **in, size_t *il, char **out, size_t *ol)
{
    int t = (int)((uintptr_t)cd >> 4), f = (int)((uintptr_t)cd & 15);
    if (!in || !*in) return 0;
    size_t bad = 0;
    while (*il) {
        const unsigned char *p = (const unsigned char *)*in;
        uint32_t cp; size_t used = 1;
        if (f == 1) {
            if (p[0] < 0x80) cp = p[0];
            else if ((p[0] & 0xE0) == 0xC0) { if (*il < 2) { errno = EINVAL; return (size_t)-1; } if ((p[1] & 0xC0) != 0x80) { errno = EILSEQ; return (size_t)-1; } cp = (uint32_t)(p[0] & 0x1F) << 6 | (p[1] & 0x3F); used = 2; }
            else if ((p[0] & 0xF0) == 0xE0) { if (*il < 3) { errno = EINVAL; return (size_t)-1; } if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80) { errno = EILSEQ; return (size_t)-1; } cp = (uint32_t)(p[0] & 0x0F) << 12 | (uint32_t)(p[1] & 0x3F) << 6 | (p[2] & 0x3F); used = 3; }
            else if ((p[0] & 0xF8) == 0xF0) { if (*il < 4) { errno = EINVAL; return (size_t)-1; } cp = (uint32_t)(p[0] & 7) << 18 | (uint32_t)(p[1] & 0x3F) << 12 | (uint32_t)(p[2] & 0x3F) << 6 | (p[3] & 0x3F); used = 4; }
            else { errno = EILSEQ; return (size_t)-1; }
        } else if (f == 4 && p[0] >= 0x80 && p[0] < 0xA0) cp = cp1252[p[0] - 0x80];
        else if (f == 3 && p[0] >= 0x80) { errno = EILSEQ; return (size_t)-1; }
        else cp = p[0];
        if (t == 1) {
            unsigned char b[4]; size_t n;
            if (cp < 0x80) { b[0] = (unsigned char)cp; n = 1; }
            else if (cp < 0x800) { b[0] = (unsigned char)(0xC0 | cp >> 6); b[1] = (unsigned char)(0x80 | (cp & 0x3F)); n = 2; }
            else if (cp < 0x10000) { b[0] = (unsigned char)(0xE0 | cp >> 12); b[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F)); b[2] = (unsigned char)(0x80 | (cp & 0x3F)); n = 3; }
            else { b[0] = (unsigned char)(0xF0 | cp >> 18); b[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F)); b[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F)); b[3] = (unsigned char)(0x80 | (cp & 0x3F)); n = 4; }
            if (*ol < n) { errno = 7; return (size_t)-1; }
            memcpy(*out, b, n); *out += n; *ol -= n;
        } else {
            unsigned char c;
            if (t == 4 && cp >= 0x80 && cp < 0xA0) { c = '?'; bad++; }
            else if (t == 4 && cp > 0xFF) { int k, found = 0; c = '?'; for (k = 0; k < 32; k++) if (cp1252[k] == cp) { c = (unsigned char)(0x80 + k); found = 1; } if (!found) bad++; }
            else if (cp > (t == 3 ? 0x7Fu : 0xFFu)) { c = '?'; bad++; }
            else c = (unsigned char)cp;
            if (!*ol) { errno = 7; return (size_t)-1; }
            *(*out)++ = (char)c; (*ol)--;
        }
        *in += used; *il -= used;
    }
    return bad;
}

char *optarg; int optind = 1, opterr = 1, optopt;
int getopt(int argc, char *const argv[], const char *opts)
{
    static int pos = 1;
    if (optind >= argc || !argv[optind] || argv[optind][0] != '-' || !argv[optind][1]) return -1;
    if (!strcmp(argv[optind], "--")) { optind++; return -1; }
    char c = argv[optind][pos];
    const char *o = strchr(opts, c);
    if (!o || c == ':') { optopt = c; if (!argv[optind][++pos]) { optind++; pos = 1; } return '?'; }
    if (o[1] == ':') {
        if (argv[optind][pos + 1]) optarg = &argv[optind][pos + 1];
        else if (optind + 1 < argc) optarg = argv[++optind];
        else { optopt = c; optind++; pos = 1; return '?'; }
        optind++; pos = 1;
    } else if (!argv[optind][++pos]) { optind++; pos = 1; }
    return c;
}
int lstat(const char *p, struct stat *s) { return stat(p, s); }
int chmod(const char *p, mode_t m) { (void)p; (void)m; return 0; }
int getopt_long(int argc, char *const argv[], const char *opts, const struct option *lo, int *idx) { (void)lo; (void)idx; return getopt(argc, argv, opts); }

ssize_t pread(int fd, void *b, size_t n, off_t o)
{
    off_t cur = lseek(fd, 0, 1);
    if (cur < 0 || lseek(fd, o, 0) < 0) return -1;
    ssize_t r = read(fd, b, n);
    lseek(fd, cur, 0);
    return r;
}
ssize_t pwrite(int fd, const void *b, size_t n, off_t o)
{
    off_t cur = lseek(fd, 0, 1);
    if (cur < 0 || lseek(fd, o, 0) < 0) return -1;
    ssize_t r = write(fd, b, n);
    lseek(fd, cur, 0);
    return r;
}

int scandir(const char *dir, struct dirent ***list, int (*filter)(const struct dirent *), int (*cmp)(const struct dirent **, const struct dirent **))
{ (void)dir; (void)list; (void)filter; (void)cmp; errno = ENOENT; return -1; }
int alphasort(const struct dirent **a, const struct dirent **b) { return strcmp((*a)->d_name, (*b)->d_name); }
int dirfd(DIR *d) { (void)d; return -1; }
int fstatat(int dirfd, const char *p, struct stat *s, int flags) { (void)dirfd; (void)flags; return stat(p, s); }
int unlinkat(int dirfd, const char *p, int flags) { (void)dirfd; (void)flags; return unlink(p); }
