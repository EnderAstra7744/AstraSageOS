/* The TLS client built exactly like the kernel (32-bit, freestanding) and run as a Linux process with raw system calls:
 * tls32 HOST PORT ROOTS.BIN VERIFY. Proves the 32-bit code generation (64-bit arithmetic, no libc) of sys/crypto.c,
 * sys/x509.c and sys/tls.c. */
#include "../tls.h"
static long sys3(long n, long a, long b, long c) { long r; __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c) : "memory"); return r; }
void *memcpy(void *d, const void *s, size_t n) { char *a = d; const char *b = s; while (n--) *a++ = *b++; return d; }
void *memset(void *d, int c, size_t n) { char *a = d; while (n--) *a++ = (char)c; return d; }
void *memmove(void *d, const void *s, size_t n) { char *a = d; const char *b = s; if (a < b) while (n--) *a++ = *b++; else { a += n; b += n; while (n--) *--a = *--b; } return d; }
static void out(const char *s) { size_t n = 0; while (s[n]) n++; sys3(4, 1, (long)s, (long)n); }
static void outnum(unsigned v) { char b[12]; int i = 11; b[i] = 0; do { b[--i] = (char)('0' + v % 10); v /= 10; } while (v); out(b + i); }
static int fd;
static int io_send(void *c, const cu8 *d, size_t n) { (void)c; while (n) { long w = sys3(4, fd, (long)d, (long)n); if (w <= 0) return -1; d += w; n -= (size_t)w; } return 0; }
struct pfd { int fd; short ev, rev; };
static int io_recv(void *c, cu8 *b, size_t m, cu32 to) { (void)c; struct pfd p = { fd, 1, 0 }; if (sys3(168, (long)&p, 1, (long)to) <= 0) return 0; long r = sys3(3, fd, (long)b, (long)m); return r > 0 ? (int)r : -1; }
void tls_random(cu8 *o, size_t n) { long f = sys3(5, (long)"/dev/urandom", 0, 0); sys3(3, f, (long)o, (long)n); sys3(6, f, 0, 0); }
static tls_t T;
static cu8 roots[200000], buf[70000];
int cmain(int argc, char **argv)
{
    if (argc < 5) return 2;
    long f = sys3(5, (long)argv[3], 0, 0);
    long rl = sys3(3, f, (long)roots, sizeof roots);
    int port = 0; for (const char *p = argv[2]; *p; p++) port = port * 10 + (*p - '0');
    long sa[3] = { 0, 0, 0 };
    sa[0] = 1; (void)sa;
    long args[3] = { 2, 1, 0 };
    fd = (int)sys3(102, 1, (long)args, 0);
    unsigned char addr[16] = { 2, 0, (unsigned char)(port >> 8), (unsigned char)port, 127, 0, 0, 1 };
    long cargs[3] = { fd, (long)addr, 16 };
    if (sys3(102, 3, (long)cargs, 0)) { out("ERR connect\n"); return 1; }
    tls_io io = { io_send, io_recv, 0 };
    cu32 now = (cu32)sys3(13, 0, 0, 0);
    if (tls_connect(&T, &io, argv[1], argv[4][0] == '1', now, roots, (size_t)rl)) { out("ERR "); out(tls_error()); out("\n"); return 1; }
    const char *req = "GET / HTTP/1.0\r\n\r\n";
    tls_write(&T, (const cu8 *)req, 18);
    size_t got = 0;
    for (;;) { int r = tls_read(&T, buf + got, sizeof buf - got - 1, 3000); if (r <= 0) break; got += (size_t)r; }
    out("OK "); out(tls_suite_name(&T)); out(" verified="); outnum((unsigned)T.verified); out(" bytes="); outnum((unsigned)got); out("\n");
    return 0;
}
__asm__(".globl _start\n_start:\n xorl %ebp,%ebp\n movl (%esp),%eax\n leal 4(%esp),%ecx\n andl $-16,%esp\n subl $8,%esp\n pushl %ecx\n pushl %eax\n call cmain\n movl %eax,%ebx\n movl $1,%eax\n int $0x80\n");
