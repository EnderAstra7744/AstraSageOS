/*
 * A small freestanding C library for AstraSageOS native programs: string, ctype, stdlib (malloc, strtol, qsort ...),
 * stdio on top of the file calls of the API, x87 maths, time, setjmp. Not a full POSIX libc: no threads, no real
 * file descriptors, no locale (the "C" locale only), files are read/written whole.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include <limits.h>
#include <float.h>
#include "../as_api.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#include <setjmp.h>
#include <assert.h>
#include <locale.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/select.h>
#include <unistd.h>
#include <wchar.h>

#if defined(__i386__)
typedef long double ldbl;
#else
#define strtold strtold_d
typedef double ldbl;      /* ARM: long double would be a 128-bit soft type on ARM64 (no compiler-rt here) */
#endif
const as_api *__api;
int errno;

/* ------------------------------------------------------------------ string */
void *memcpy(void *d, const void *s, size_t n)
{
    void *r = d;
#if defined(__i386__)
    __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
#else
    unsigned char *a = d; const unsigned char *b = s;
    while (n--) *a++ = *b++;
#endif
    return r;
}
void *mempcpy(void *d, const void *s, size_t n) { memcpy(d, s, n); return (char *)d + n; }
void *memmove(void *d, const void *s, size_t n)
{
    unsigned char *a = d; const unsigned char *b = s;
    if (a == b || !n) return d;
    if (a < b || a >= b + n) return memcpy(d, s, n);
    while (n--) a[n] = b[n];
    return d;
}
void *memset(void *d, int c, size_t n)
{
    void *r = d;
#if defined(__i386__)
    __asm__ volatile("rep stosb" : "+D"(d), "+c"(n) : "a"(c) : "memory");
#else
    unsigned char *a = d;
    while (n--) *a++ = (unsigned char)c;
#endif
    return r;
}
int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    for (; n; n--, x++, y++) if (*x != *y) return *x - *y;
    return 0;
}
void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = s;
    for (; n; n--, p++) if (*p == (unsigned char)c) return (void *)p;
    return 0;
}
void *memrchr(const void *s, int c, size_t n)
{
    const unsigned char *p = s;
    while (n--) if (p[n] == (unsigned char)c) return (void *)(p + n);
    return 0;
}
size_t strlen(const char *s) { const char *p = s; while (*p) p++; return (size_t)(p - s); }
size_t strnlen(const char *s, size_t n) { size_t i = 0; while (i < n && s[i]) i++; return i; }
char *strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++)) {} return r; }
char *strncpy(char *d, const char *s, size_t n) { size_t i = 0; for (; i < n && s[i]; i++) d[i] = s[i]; for (; i < n; i++) d[i] = 0; return d; }
char *strcat(char *d, const char *s) { strcpy(d + strlen(d), s); return d; }
char *strncat(char *d, const char *s, size_t n) { char *e = d + strlen(d); size_t i = 0; for (; i < n && s[i]; i++) e[i] = s[i]; e[i] = 0; return d; }
int strcmp(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (unsigned char)*a - (unsigned char)*b; }
int strncmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) { if (*a != *b) return (unsigned char)*a - (unsigned char)*b; if (!*a) return 0; }
    return 0;
}
int strcoll(const char *a, const char *b) { return strcmp(a, b); }
int strcasecmp(const char *a, const char *b)
{
    for (;; a++, b++) { int x = tolower((unsigned char)*a), y = tolower((unsigned char)*b); if (x != y) return x - y; if (!x) return 0; }
}
int strncasecmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) { int x = tolower((unsigned char)*a), y = tolower((unsigned char)*b); if (x != y) return x - y; if (!x) return 0; }
    return 0;
}
char *strchr(const char *s, int c) { for (;; s++) { if (*s == (char)c) return (char *)s; if (!*s) return 0; } }
char *strrchr(const char *s, int c) { const char *r = 0; for (;; s++) { if (*s == (char)c) r = s; if (!*s) return (char *)r; } }
char *strstr(const char *h, const char *n)
{
    size_t l = strlen(n);
    if (!l) return (char *)h;
    for (; *h; h++) if (*h == *n && !strncmp(h, n, l)) return (char *)h;
    return 0;
}
__attribute__((weak)) char *strcasestr(const char *h, const char *n)
{
    size_t l = strlen(n);
    if (!l) return (char *)h;
    for (; *h; h++) if (!strncasecmp(h, n, l)) return (char *)h;
    return 0;
}
size_t strspn(const char *s, const char *a) { size_t i = 0; while (s[i] && strchr(a, s[i])) i++; return i; }
size_t strcspn(const char *s, const char *r) { size_t i = 0; while (s[i] && !strchr(r, s[i])) i++; return i; }
char *strpbrk(const char *s, const char *a) { for (; *s; s++) if (strchr(a, *s)) return (char *)s; return 0; }
char *strtok_r(char *s, const char *d, char **sp)
{
    if (!s) s = *sp;
    if (!s) return 0;
    s += strspn(s, d);
    if (!*s) { *sp = 0; return 0; }
    char *e = s + strcspn(s, d);
    if (*e) { *e = 0; *sp = e + 1; } else *sp = 0;
    return s;
}
static char *tok_save;
char *strtok(char *s, const char *d) { return strtok_r(s, d, &tok_save); }
char *strsep(char **sp, const char *d)
{
    char *s = *sp;
    if (!s) return 0;
    char *e = s + strcspn(s, d);
    if (*e) { *e = 0; *sp = e + 1; } else *sp = 0;
    return s;
}
size_t strlcpy(char *d, const char *s, size_t n) { size_t l = strlen(s); if (n) { size_t c = l < n - 1 ? l : n - 1; memcpy(d, s, c); d[c] = 0; } return l; }
size_t strlcat(char *d, const char *s, size_t n) { size_t dl = strnlen(d, n); if (dl == n) return n + strlen(s); return dl + strlcpy(d + dl, s, n - dl); }
char *strdup(const char *s) { size_t l = strlen(s) + 1; char *p = malloc(l); if (p) memcpy(p, s, l); return p; }
char *strndup(const char *s, size_t n) { size_t l = strnlen(s, n); char *p = malloc(l + 1); if (p) { memcpy(p, s, l); p[l] = 0; } return p; }
char *strerror(int e)
{
    switch (e) {
    case 0: return "Success"; case ENOENT: return "No such file or directory"; case ENOMEM: return "Out of memory";
    case EACCES: return "Permission denied"; case EEXIST: return "File exists"; case EINVAL: return "Invalid argument";
    case EIO: return "Input/output error"; case ERANGE: return "Result too large"; case EBADF: return "Bad file descriptor";
    case ETIMEDOUT: return "Timed out"; case EISDIR: return "Is a directory"; case ENOSYS: return "Not implemented";
    default: return "Unknown error";
    }
}

/* ------------------------------------------------------------------ ctype ("C" locale) */
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int isalpha(int c) { return isupper(c) || islower(c); }
int isdigit(int c) { return c >= '0' && c <= '9'; }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int isspace(int c) { return c == ' ' || (c >= 9 && c <= 13); }
int isblank(int c) { return c == ' ' || c == '\t'; }
int iscntrl(int c) { return (c >= 0 && c < 32) || c == 127; }
int isprint(int c) { return c >= 32 && c < 127; }
int isgraph(int c) { return c > 32 && c < 127; }
int ispunct(int c) { return isgraph(c) && !isalnum(c); }
int isascii(int c) { return c >= 0 && c < 128; }
int tolower(int c) { return isupper(c) ? c + 32 : c; }
int toupper(int c) { return islower(c) ? c - 32 : c; }

/* ------------------------------------------------------------------ malloc: first fit with coalescing */
typedef struct blk { size_t size; struct blk *next; int free; int pad; } blk;     /* 16 bytes, keeps 16-byte alignment */
static blk *heap_head;
static char *heap_end;

static void heap_init(void)
{
    char *b = (char *)(((uintptr_t)__api->heap + 15) & ~(uintptr_t)15);
    size_t n = (size_t)((char *)__api->heap + __api->heap_size - b);
    heap_head = (blk *)b;
    heap_head->size = n - sizeof(blk);
    heap_head->next = 0;
    heap_head->free = 1;
    heap_end = b + n;
}

void *malloc(size_t n)
{
    if (!heap_head) heap_init();
    if (!n) n = 1;
    n = (n + 15) & ~(size_t)15;
    for (blk *b = heap_head; b; b = b->next) {
        if (!b->free || b->size < n) continue;
        if (b->size >= n + sizeof(blk) + 16) {
            blk *r = (blk *)((char *)(b + 1) + n);
            r->size = b->size - n - sizeof(blk); r->next = b->next; r->free = 1;
            b->size = n; b->next = r;
        }
        b->free = 0;
        return b + 1;
    }
    errno = ENOMEM;
    return 0;
}
void free(void *p)
{
    if (!p) return;
    blk *b = (blk *)p - 1;
    b->free = 1;
    for (blk *x = heap_head; x; x = x->next)               /* coalesce (list is small enough to walk; simple and safe) */
        while (x->free && x->next && x->next->free) { x->size += sizeof(blk) + x->next->size; x->next = x->next->next; }
}
void *calloc(size_t a, size_t b)
{
    if (b && a > (size_t)-1 / b) { errno = ENOMEM; return 0; }
    void *p = malloc(a * b);
    if (p) memset(p, 0, a * b);
    return p;
}
void *realloc(void *p, size_t n)
{
    if (!p) return malloc(n);
    if (!n) { free(p); return 0; }
    blk *b = (blk *)p - 1;
    size_t need = (n + 15) & ~(size_t)15;
    if (b->size >= need) return p;
    if (b->next && b->next->free && b->size + sizeof(blk) + b->next->size >= need) {      /* grow in place */
        b->size += sizeof(blk) + b->next->size; b->next = b->next->next;
        if (b->size >= need + sizeof(blk) + 16) {
            blk *r = (blk *)((char *)(b + 1) + need);
            r->size = b->size - need - sizeof(blk); r->next = b->next; r->free = 1;
            b->size = need; b->next = r;
        }
        return p;
    }
    void *q = malloc(n);
    if (!q) return 0;
    memcpy(q, p, b->size);
    free(p);
    return q;
}
void *aligned_alloc(size_t al, size_t n) { (void)al; return malloc(n); }       /* every block is 16-byte aligned */
int posix_memalign(void **p, size_t al, size_t n) { (void)al; *p = malloc(n); return *p ? 0 : ENOMEM; }

/* ------------------------------------------------------------------ stdlib */
static jmp_buf exit_env;
static int exit_code, exit_ready;
static void (*atexits[32])(void);
static int natexit;
int atexit(void (*f)(void)) { if (natexit >= 32) return -1; atexits[natexit++] = f; return 0; }
void exit(int c)
{
    while (natexit) atexits[--natexit]();
    fflush(0);
    exit_code = c;
    if (exit_ready) longjmp(exit_env, 1);
    for (;;) {}
}
void _Exit(int c) { exit_code = c; if (exit_ready) longjmp(exit_env, 1); for (;;) {} }
void abort(void) { __api->puts("abort() called\n"); exit(134); }
void __assert_fail(const char *e, const char *f, int l)
{
    char b[24]; snprintf(b, sizeof b, ":%d: ", l);
    __api->puts("assertion failed: "); __api->puts(f); __api->puts(b); __api->puts(e); __api->puts("\n");
    abort();
}
int abs(int v) { return v < 0 ? -v : v; }
long labs(long v) { return v < 0 ? -v : v; }
long long llabs(long long v) { return v < 0 ? -v : v; }
div_t div(int a, int b) { div_t r = { a / b, a % b }; return r; }
ldiv_t ldiv(long a, long b) { ldiv_t r = { a / b, a % b }; return r; }
static unsigned long long rseed = 1;
void srand(unsigned s) { rseed = s; }
int rand(void) { rseed = rseed * 6364136223846793005ULL + 1442695040888963407ULL; return (int)((rseed >> 33) & 0x7fffffff); }
char *getenv(const char *n) { (void)n; return 0; }
int setenv(const char *n, const char *v, int o) { (void)n; (void)v; (void)o; return 0; }
int system(const char *c) { (void)c; return -1; }
char *realpath(const char *p, char *out) { if (!out) out = malloc(256); if (out) strlcpy(out, p, 256); return out; }

static void swap_bytes(char *a, char *b, size_t n) { while (n--) { char t = *a; *a++ = *b; *b++ = t; } }
static void qs(char *b, size_t n, size_t sz, int (*cmp)(const void *, const void *))
{
    while (n > 1) {
        if (n < 8) {                                       /* insertion sort */
            for (size_t i = 1; i < n; i++)
                for (size_t j = i; j > 0 && cmp(b + (j - 1) * sz, b + j * sz) > 0; j--) swap_bytes(b + (j - 1) * sz, b + j * sz, sz);
            return;
        }
        swap_bytes(b, b + (n / 2) * sz, sz);
        size_t m = 0;
        for (size_t i = 1; i < n; i++) if (cmp(b + i * sz, b) < 0) { m++; swap_bytes(b + m * sz, b + i * sz, sz); }
        swap_bytes(b, b + m * sz, sz);
        if (m < n - m - 1) { qs(b, m, sz, cmp); b += (m + 1) * sz; n -= m + 1; }
        else { qs(b + (m + 1) * sz, n - m - 1, sz, cmp); n = m; }
    }
}
void qsort(void *b, size_t n, size_t sz, int (*cmp)(const void *, const void *)) { qs(b, n, sz, cmp); }
void *bsearch(const void *k, const void *b, size_t n, size_t sz, int (*cmp)(const void *, const void *))
{
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        int c = cmp(k, (const char *)b + mid * sz);
        if (!c) return (void *)((const char *)b + mid * sz);
        if (c < 0) hi = mid; else lo = mid + 1;
    }
    return 0;
}

static unsigned long long conv_int(const char *s, char **e, int base, int *neg, int *ovf)
{
    const char *p = s;
    unsigned long long v = 0;
    *neg = 0; *ovf = 0;
    while (isspace((unsigned char)*p)) p++;
    if (*p == '-') { *neg = 1; p++; } else if (*p == '+') p++;
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X') && isxdigit((unsigned char)p[2])) { p += 2; base = 16; }
    else if (base == 0 && p[0] == '0') base = 8;
    else if (base == 0) base = 10;
    const char *start = p;
    for (;; p++) {
        int d = isdigit((unsigned char)*p) ? *p - '0' : isalpha((unsigned char)*p) ? tolower((unsigned char)*p) - 'a' + 10 : 99;
        if (d >= base) break;
        if (v > (ULLONG_MAX - (unsigned)d) / (unsigned)base) *ovf = 1;
        v = v * (unsigned)base + (unsigned)d;
    }
    if (e) *e = (char *)(p == start ? s : p);
    return v;
}
long long strtoll(const char *s, char **e, int base)
{
    int neg, ovf; unsigned long long v = conv_int(s, e, base, &neg, &ovf);
    if (ovf || (!neg && v > LLONG_MAX) || (neg && v > (unsigned long long)LLONG_MAX + 1)) { errno = ERANGE; return neg ? LLONG_MIN : LLONG_MAX; }
    return neg ? -(long long)v : (long long)v;
}
unsigned long long strtoull(const char *s, char **e, int base)
{
    int neg, ovf; unsigned long long v = conv_int(s, e, base, &neg, &ovf);
    if (ovf) { errno = ERANGE; return ULLONG_MAX; }
    return neg ? -v : v;
}
long strtol(const char *s, char **e, int base)
{
    long long v = strtoll(s, e, base);
    if (v > LONG_MAX) { errno = ERANGE; return LONG_MAX; }
    if (v < LONG_MIN) { errno = ERANGE; return LONG_MIN; }
    return (long)v;
}
unsigned long strtoul(const char *s, char **e, int base)
{
    unsigned long long v = strtoull(s, e, base);
    if (v > ULONG_MAX) { errno = ERANGE; return ULONG_MAX; }
    return (unsigned long)v;
}
int atoi(const char *s) { return (int)strtol(s, 0, 10); }
long atol(const char *s) { return strtol(s, 0, 10); }
long long atoll(const char *s) { return strtoll(s, 0, 10); }

static ldbl p10(int e)
{
    ldbl r = ((ldbl)1.0), b = ((ldbl)10.0);
    int n = e < 0 ? -e : e;
    while (n) { if (n & 1) r *= b; b *= b; n >>= 1; }
    return e < 0 ? ((ldbl)1.0) / r : r;
}
ldbl strtold(const char *s, char **end)
{
    const char *p = s;
    while (isspace((unsigned char)*p)) p++;
    int neg = 0;
    if (*p == '-') { neg = 1; p++; } else if (*p == '+') p++;
    if (!strncasecmp(p, "inf", 3)) { if (end) *end = (char *)p + (strncasecmp(p, "infinity", 8) ? 3 : 8); return neg ? -(ldbl)HUGE_VAL : (ldbl)HUGE_VAL; }
    if (!strncasecmp(p, "nan", 3)) { if (end) *end = (char *)p + 3; return (ldbl)NAN; }
    ldbl v = 0; int any = 0, ex = 0;
    while (isdigit((unsigned char)*p)) { v = v * 10 + (*p - '0'); p++; any = 1; }
    if (*p == '.') { p++; while (isdigit((unsigned char)*p)) { v = v * 10 + (*p - '0'); ex--; p++; any = 1; } }
    if (!any) { if (end) *end = (char *)s; return 0; }
    if (*p == 'e' || *p == 'E') {
        const char *q = p + 1; int en = 0, eneg = 0;
        if (*q == '-') { eneg = 1; q++; } else if (*q == '+') q++;
        if (isdigit((unsigned char)*q)) { while (isdigit((unsigned char)*q)) { if (en < 5000) en = en * 10 + (*q - '0'); q++; } ex += eneg ? -en : en; p = q; }
    }
    if (ex) v *= p10(ex);
    if (end) *end = (char *)p;
    return neg ? -v : v;
}
double strtod(const char *s, char **e) { return (double)strtold(s, e); }
float strtof(const char *s, char **e) { return (float)strtold(s, e); }
double atof(const char *s) { return strtod(s, 0); }

/* ------------------------------------------------------------------ setjmp */
#if defined(__i386__)
__asm__(
    ".text\n.globl setjmp\nsetjmp:\n"
    "  movl 4(%esp), %eax\n"
    "  movl %ebx, 0(%eax)\n  movl %esi, 4(%eax)\n  movl %edi, 8(%eax)\n  movl %ebp, 12(%eax)\n"
    "  leal 4(%esp), %ecx\n  movl %ecx, 16(%eax)\n"
    "  movl (%esp), %ecx\n  movl %ecx, 20(%eax)\n"
    "  xorl %eax, %eax\n  ret\n"
    ".globl longjmp\nlongjmp:\n"
    "  movl 4(%esp), %edx\n  movl 8(%esp), %eax\n  testl %eax, %eax\n  jnz 1f\n  incl %eax\n1:\n"
    "  movl 0(%edx), %ebx\n  movl 4(%edx), %esi\n  movl 8(%edx), %edi\n  movl 12(%edx), %ebp\n"
    "  movl 16(%edx), %esp\n  jmp *20(%edx)\n"
);
#elif defined(__aarch64__)
__asm__(
    ".text\n.globl setjmp\nsetjmp:\n"
    "  stp x19, x20, [x0, #0]\n  stp x21, x22, [x0, #16]\n  stp x23, x24, [x0, #32]\n  stp x25, x26, [x0, #48]\n"
    "  stp x27, x28, [x0, #64]\n  stp x29, x30, [x0, #80]\n  mov x2, sp\n  str x2, [x0, #96]\n"
    "  stp d8, d9, [x0, #104]\n  stp d10, d11, [x0, #120]\n  stp d12, d13, [x0, #136]\n  stp d14, d15, [x0, #152]\n"
    "  mov w0, #0\n  ret\n"
    ".globl longjmp\nlongjmp:\n"
    "  ldp x19, x20, [x0, #0]\n  ldp x21, x22, [x0, #16]\n  ldp x23, x24, [x0, #32]\n  ldp x25, x26, [x0, #48]\n"
    "  ldp x27, x28, [x0, #64]\n  ldp x29, x30, [x0, #80]\n  ldr x2, [x0, #96]\n  mov sp, x2\n"
    "  ldp d8, d9, [x0, #104]\n  ldp d10, d11, [x0, #120]\n  ldp d12, d13, [x0, #136]\n  ldp d14, d15, [x0, #152]\n"
    "  cmp w1, #0\n  csinc w0, w1, wzr, ne\n  ret\n"
);
#else
__asm__(
    ".text\n.arm\n.globl setjmp\nsetjmp:\n"
    "  stmia r0!, {r4-r11, sp, lr}\n  vstmia r0, {d8-d15}\n  mov r0, #0\n  bx lr\n"
    ".globl longjmp\nlongjmp:\n"
    "  ldmia r0!, {r4-r11, r12, lr}\n  mov sp, r12\n  vldmia r0, {d8-d15}\n  movs r0, r1\n  moveq r0, #1\n  bx lr\n"
);
#endif

#if defined(__i386__)
/* ------------------------------------------------------------------ math (x87) */
double sqrt(double x) { double r; __asm__("fsqrt" : "=t"(r) : "0"(x)); return r; }
float sqrtf(float x) { return (float)sqrt(x); }
double fabs(double x) { return x < 0 ? -x : x; }
float fabsf(float x) { return x < 0 ? -x : x; }
double copysign(double x, double y) { x = fabs(x); return (y < 0 || (y == 0 && 1 / y < 0)) ? -x : x; }
double trunc(double x)
{
    if (x != x || x > 4503599627370496.0 || x < -4503599627370496.0) return x;
    return (double)(long long)x;
}
double floor(double x) { double t = trunc(x); return (x < t) ? t - 1 : t; }
double ceil(double x) { double t = trunc(x); return (x > t) ? t + 1 : t; }
double round(double x) { return x < 0 ? -floor(-x + 0.5) : floor(x + 0.5); }
double rint(double x) { double r = floor(x + 0.5); if (r - x == 0.5 && ((long long)r & 1)) r -= 1; return r; }
long lround(double x) { return (long)round(x); }
long lroundf(float x) { return (long)roundf(x); }
long lrint(double x) { return (long)rint(x); }
float floorf(float x) { return (float)floor(x); }
float ceilf(float x) { return (float)ceil(x); }
float roundf(float x) { return (float)round(x); }
float truncf(float x) { return (float)trunc(x); }
double fmod(double x, double y) { double r; __asm__("1: fprem\n fnstsw %%ax\n sahf\n jp 1b" : "=t"(r) : "0"(x), "u"(y) : "ax", "cc"); return r; }
float fmodf(float x, float y) { return (float)fmod(x, y); }
double fmin(double a, double b) { return a < b ? a : b; }
double fmax(double a, double b) { return a > b ? a : b; }
double sin(double x) { double r; __asm__("fsin" : "=t"(r) : "0"(fmod(x, 6.283185307179586))); return r; }
double cos(double x) { double r; __asm__("fcos" : "=t"(r) : "0"(fmod(x, 6.283185307179586))); return r; }
double tan(double x) { return sin(x) / cos(x); }
float sinf(float x) { return (float)sin(x); }
float cosf(float x) { return (float)cos(x); }
double atan2(double y, double x) { double r; __asm__("fpatan" : "=t"(r) : "0"(x), "u"(y) : "st(1)"); return r; }
double atan(double x) { return atan2(x, 1.0); }
double asin(double x) { return atan2(x, sqrt(1 - x * x)); }
double acos(double x) { return atan2(sqrt(1 - x * x), x); }
double log2(double x) { double r; __asm__("fld1\n fxch\n fyl2x" : "=t"(r) : "0"(x) : "st(1)"); return r; }
double log(double x) { return log2(x) * 0.6931471805599453; }
double log10(double x) { return log2(x) * 0.30102999566398120; }
float logf(float x) { return (float)log(x); }
double exp2(double x)
{
    double ip = floor(x), fp = x - ip, r;
    __asm__("f2xm1" : "=t"(r) : "0"(fp));
    r += 1.0;
    return ldexp(r, (int)ip);
}
double exp(double x) { return exp2(x * 1.4426950408889634); }
float expf(float x) { return (float)exp(x); }
double pow(double x, double y)
{
    if (y == 0) return 1;
    if (x == 0) return y > 0 ? 0 : HUGE_VAL;
    if (y == floor(y) && fabs(y) < 1024) {                  /* exact integer powers */
        double r = 1, b = x; long long n = (long long)fabs(y);
        while (n) { if (n & 1) r *= b; b *= b; n >>= 1; }
        return y < 0 ? 1 / r : r;
    }
    if (x < 0) return NAN;
    return exp2(y * log2(x));
}
float powf(float x, float y) { return (float)pow(x, y); }
double hypot(double a, double b) { return sqrt(a * a + b * b); }
double cbrt(double x) { return x < 0 ? -pow(-x, 1.0 / 3) : pow(x, 1.0 / 3); }
double ldexp(double x, int e) { double r; __asm__("fscale" : "=t"(r) : "0"(x), "u"((double)e)); return r; }
#else
/* ARM: sqrt is a hardware instruction, the rest is plain C (accuracy ~1e-12, enough for a coding assistant) */
double sqrt(double x) { return __builtin_sqrt(x); }
float sqrtf(float x) { return __builtin_sqrtf(x); }
double fabs(double x) { return __builtin_fabs(x); }
float fabsf(float x) { return __builtin_fabsf(x); }
double copysign(double x, double y) { x = fabs(x); return (y < 0 || (y == 0 && 1 / y < 0)) ? -x : x; }
double trunc(double x)
{
    if (x != x || x > 4503599627370496.0 || x < -4503599627370496.0) return x;
    return (double)(long long)x;
}
double floor(double x) { double t = trunc(x); return (x < t) ? t - 1 : t; }
double ceil(double x) { double t = trunc(x); return (x > t) ? t + 1 : t; }
double round(double x) { return x < 0 ? -floor(-x + 0.5) : floor(x + 0.5); }
double rint(double x) { double r = floor(x + 0.5); if (r - x == 0.5 && ((long long)r & 1)) r -= 1; return r; }
long lround(double x) { return (long)round(x); }
long lroundf(float x) { return (long)roundf(x); }
long lrint(double x) { return (long)rint(x); }
float floorf(float x) { return (float)floor(x); }
float ceilf(float x) { return (float)ceil(x); }
float roundf(float x) { return (float)round(x); }
float truncf(float x) { return (float)trunc(x); }
double fmod(double x, double y)
{
    if (y == 0 || x != x || y != y) return NAN;
    double q = trunc(x / y), r = x - q * y;
    return r;
}
float fmodf(float x, float y) { return (float)fmod(x, y); }
double fmin(double a, double b) { return a < b ? a : b; }
double fmax(double a, double b) { return a > b ? a : b; }
double ldexp(double x, int e)
{
    while (e > 0) { int k = e > 1000 ? 1000 : e; unsigned long long b = (unsigned long long)(1023 + k) << 52; double f; __builtin_memcpy(&f, &b, 8); x *= f; e -= k; }
    while (e < 0) { int k = -e > 1000 ? 1000 : -e; unsigned long long b = (unsigned long long)(1023 - k) << 52; double f; __builtin_memcpy(&f, &b, 8); x *= f; e += k; }
    return x;
}
static double sin_k(double x)     /* |x| <= pi/4 */
{
    double x2 = x * x;
    return x * (1 + x2 * (-1.0 / 6 + x2 * (1.0 / 120 + x2 * (-1.0 / 5040 + x2 * (1.0 / 362880 + x2 * (-1.0 / 39916800 + x2 * (1.0 / 6227020800.0)))))));
}
static double cos_k(double x)
{
    double x2 = x * x;
    return 1 + x2 * (-0.5 + x2 * (1.0 / 24 + x2 * (-1.0 / 720 + x2 * (1.0 / 40320 + x2 * (-1.0 / 3628800 + x2 * (1.0 / 479001600.0))))));
}
static double trig(double x, int c)
{
    if (x != x) return x;
    const double h = 1.5707963267948966;
    double q = floor(x / h + 0.5), r = x - q * h;      /* r in [-pi/4, pi/4] */
    long long n = (long long)fmod(q, 4.0); if (n < 0) n += 4;
    n = (n + (c ? 1 : 0)) & 3;
    switch (n) { case 0: return sin_k(r); case 1: return cos_k(r); case 2: return -sin_k(r); default: return -cos_k(r); }
}
double sin(double x) { return trig(x, 0); }
double cos(double x) { return trig(x, 1); }
double tan(double x) { return sin(x) / cos(x); }
float sinf(float x) { return (float)sin(x); }
float cosf(float x) { return (float)cos(x); }
double atan(double x)
{
    int neg = x < 0, inv = 0; if (neg) x = -x;
    if (x > 1) { x = 1 / x; inv = 1; }
    double t = x;                               /* atan(x) = 2 atan(x / (1 + sqrt(1 + x^2))) twice */
    for (int i = 0; i < 2; i++) { t = t / (1 + sqrt(1 + t * t)); }
    double t2 = t * t, r = t * (1 - t2 * (1.0 / 3 - t2 * (1.0 / 5 - t2 * (1.0 / 7 - t2 * (1.0 / 9 - t2 * (1.0 / 11))))));
    r *= 4;
    if (inv) r = 1.5707963267948966 - r;
    return neg ? -r : r;
}
double atan2(double y, double x)
{
    const double pi = 3.141592653589793;
    if (x > 0) return atan(y / x);
    if (x < 0) return y >= 0 ? atan(y / x) + pi : atan(y / x) - pi;
    return y > 0 ? pi / 2 : y < 0 ? -pi / 2 : 0;
}
double asin(double x) { return atan2(x, sqrt(1 - x * x)); }
double acos(double x) { return atan2(sqrt(1 - x * x), x); }
double frexp(double x, int *e);
double log(double x)
{
    if (x <= 0) return x == 0 ? -HUGE_VAL : NAN;
    int e; double m = frexp(x, &e);                      /* x = m * 2^e, m in [0.5, 1) */
    if (m < 0.7071067811865476) { m *= 2; e--; }
    double f = (m - 1) / (m + 1), f2 = f * f;
    double s = 2 * f * (1 + f2 * (1.0 / 3 + f2 * (1.0 / 5 + f2 * (1.0 / 7 + f2 * (1.0 / 9 + f2 * (1.0 / 11 + f2 * (1.0 / 13)))))));
    return s + e * 0.6931471805599453;
}
double log2(double x) { return log(x) * 1.4426950408889634; }
double log10(double x) { return log(x) * 0.4342944819032518; }
float logf(float x) { return (float)log(x); }
double exp(double x)
{
    if (x > 709) return HUGE_VAL;
    if (x < -745) return 0;
    double k = floor(x * 1.4426950408889634 + 0.5), r = x - k * 0.6931471805599453;
    double t = 1, term = 1;
    for (int i = 1; i < 18; i++) { term *= r / i; t += term; }
    return ldexp(t, (int)k);
}
double exp2(double x) { return exp(x * 0.6931471805599453); }
float expf(float x) { return (float)exp(x); }
double pow(double x, double y)
{
    if (y == 0) return 1;
    if (x == 0) return y > 0 ? 0 : HUGE_VAL;
    if (y == floor(y) && fabs(y) < 1024) {
        double r = 1, b = x; long long n = (long long)fabs(y);
        while (n) { if (n & 1) r *= b; b *= b; n >>= 1; }
        return y < 0 ? 1 / r : r;
    }
    if (x < 0) return NAN;
    return exp(y * log(x));
}
float powf(float x, float y) { return (float)pow(x, y); }
double hypot(double a, double b) { return sqrt(a * a + b * b); }
double cbrt(double x) { return x < 0 ? -pow(-x, 1.0 / 3) : pow(x, 1.0 / 3); }
#endif
double frexp(double x, int *e)
{
    if (x == 0 || x != x) { *e = 0; return x; }
    int n = 0; double a = fabs(x);
    while (a >= 1.0) { a /= 2; n++; }
    while (a < 0.5) { a *= 2; n--; }
    *e = n;
    return x < 0 ? -a : a;
}
double modf(double x, double *ip) { double t = trunc(x); *ip = t; return x - t; }

/* ------------------------------------------------------------------ printf engine */
typedef struct { char *buf; size_t cap, len; FILE *f; } sink;
static void put(sink *s, char c)
{
    if (s->buf) { if (s->len + 1 < s->cap) s->buf[s->len] = c; }
    s->len++;
}
static void puts_n(sink *s, const char *p, size_t n) { while (n--) put(s, *p++); }

static void fmt_uint(sink *s, unsigned long long v, int base, int upper, int neg, int width, int prec, int left, int zero, int plus, int space, const char *prefix)
{
    char tmp[72]; int n = 0;
    const char *dg = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (!(v == 0 && prec == 0)) do { tmp[n++] = dg[v % (unsigned)base]; v /= (unsigned)base; } while (v);
    int zeros = prec > n ? prec - n : 0;
    char sign = neg ? '-' : plus ? '+' : space ? ' ' : 0;
    int pl = (int)strlen(prefix);
    int total = n + zeros + (sign ? 1 : 0) + pl;
    int pad = width > total ? width - total : 0;
    if (zero && !left && prec < 0) { zeros += pad; pad = 0; }
    if (!left) while (pad--) put(s, ' ');
    if (sign) put(s, sign);
    puts_n(s, prefix, (size_t)pl);
    while (zeros-- > 0) put(s, '0');
    while (n) put(s, tmp[--n]);
    if (left) while (pad-- > 0) put(s, ' ');
}

#if !defined(__i386__)
static ldbl floorl_(ldbl x) { return floor(x); }
#else
static ldbl floorl_(ldbl x)
{
    unsigned short cw, cw2;
    __asm__ volatile("fnstcw %0" : "=m"(cw));
    cw2 = (unsigned short)((cw & ~0xC00) | 0x400);
    __asm__ volatile("fldcw %0" : : "m"(cw2));
    __asm__("frndint" : "+t"(x));
    __asm__ volatile("fldcw %0" : : "m"(cw));
    return x;
}
#endif

static void fmt_double(sink *s, double v, char conv, int width, int prec, int left, int zero, int plus, int space, int alt)
{
    int upper = conv == 'E' || conv == 'G' || conv == 'F';
    char lc = (char)(conv | 0x20);
    char out[400]; int n = 0;
    int neg = v < 0 || (v == 0 && 1 / v < 0);
    if (neg) v = -v;
    if (v != v) { memcpy(out, upper ? "NAN" : "nan", 3); n = 3; neg = 0; zero = 0; }
    else if (v > 1.7e308) { memcpy(out, upper ? "INF" : "inf", 3); n = 3; zero = 0; }
    else {
        if (prec < 0) prec = 6;
        if (lc == 'g' && prec == 0) prec = 1;
        int e10 = 0;
        ldbl a = v;
        if (a != 0) { while (a >= ((ldbl)10.0)) { a /= 10; e10++; } while (a < ((ldbl)1.0)) { a *= 10; e10--; } }
        int style = lc;                                        /* 'f', 'e' */
        int strip = 0;
        if (lc == 'g') {
            /* decide by the exponent after rounding to prec significant digits */
            ldbl r = v; int x = e10;
            ldbl scale = p10(prec - 1 - x);
            ldbl t = r * scale + ((ldbl)0.5);
            if (t >= p10(prec)) x++;
            if (x < -4 || x >= prec) { style = 'e'; prec -= 1; } else { style = 'f'; prec = prec - 1 - x; if (prec < 0) prec = 0; }
            strip = !alt;
        }
        char digits[360]; int nd = 0, dexp;
        if (style == 'f') {
            /* integer part and fraction digits */
            ldbl ip = floorl_(v);
            ldbl fr = (ldbl)v - ip;
            char ib[330]; int il = 0;
            if (ip == 0) ib[il++] = '0';
            else { while (ip >= ((ldbl)1.0) && il < 320) { ldbl q = floorl_(ip / ((ldbl)10.0)); int d = (int)(ip - q * ((ldbl)10.0)); ib[il++] = (char)('0' + (d < 0 ? 0 : d > 9 ? 9 : d)); ip = q; } }
            ldbl rnd = ((ldbl)0.5) / p10(prec);
            fr += rnd;
            if (fr >= ((ldbl)1.0)) {                                   /* carry into the integer part */
                int i = 0;
                for (; i < il; i++) { if (ib[i] == '9') ib[i] = '0'; else { ib[i]++; break; } }
                if (i == il) ib[il++] = '1';
                fr -= ((ldbl)1.0);
            }
            for (int i = il - 1; i >= 0; i--) digits[nd++] = ib[i];
            dexp = nd;
            for (int i = 0; i < prec && nd < 350; i++) { fr *= 10; int d = (int)fr; if (d > 9) d = 9; digits[nd++] = (char)('0' + d); fr -= d; }
            (void)dexp;
            int ilen = (int)(nd - prec);
            if (strip) { while (prec > 0 && digits[nd - 1] == '0') { nd--; prec--; } }
            for (int i = 0; i < ilen; i++) out[n++] = digits[i];
            if (prec > 0 || alt) out[n++] = '.';
            for (int i = 0; i < prec; i++) out[n++] = digits[ilen + i];
        } else {
            ldbl m = a;
            ldbl rnd = ((ldbl)0.5) / p10(prec);
            m += rnd;
            if (m >= ((ldbl)10.0)) { m /= 10; e10++; }
            digits[0] = (char)('0' + (int)m); m -= (int)m;
            nd = 1;
            for (int i = 0; i < prec; i++) { m *= 10; int d = (int)m; if (d > 9) d = 9; digits[nd++] = (char)('0' + d); m -= d; }
            int fl = prec;
            if (strip) while (fl > 0 && digits[nd - 1] == '0') { nd--; fl--; }
            out[n++] = digits[0];
            if (fl > 0 || alt) out[n++] = '.';
            for (int i = 1; i <= fl; i++) out[n++] = digits[i];
            out[n++] = upper ? 'E' : 'e';
            out[n++] = e10 < 0 ? '-' : '+';
            int ae = e10 < 0 ? -e10 : e10;
            if (ae >= 100) out[n++] = (char)('0' + ae / 100);
            out[n++] = (char)('0' + (ae / 10) % 10);
            out[n++] = (char)('0' + ae % 10);
        }
    }
    char sign = neg ? '-' : plus ? '+' : space ? ' ' : 0;
    int total = n + (sign ? 1 : 0), pad = width > total ? width - total : 0;
    if (!left && !zero) while (pad-- > 0) put(s, ' ');
    if (sign) put(s, sign);
    if (!left && zero) while (pad-- > 0) put(s, '0');
    puts_n(s, out, (size_t)n);
    if (left) while (pad-- > 0) put(s, ' ');
}

static int do_printf(sink *s, const char *f, va_list ap)
{
    for (; *f; f++) {
        if (*f != '%') { put(s, *f); continue; }
        f++;
        int left = 0, plus = 0, space = 0, alt = 0, zero = 0, width = 0, prec = -1, lng = 0;
        for (;; f++) {
            if (*f == '-') left = 1; else if (*f == '+') plus = 1; else if (*f == ' ') space = 1;
            else if (*f == '#') alt = 1; else if (*f == '0') zero = 1; else break;
        }
        if (*f == '*') { width = va_arg(ap, int); if (width < 0) { left = 1; width = -width; } f++; }
        else while (isdigit((unsigned char)*f)) width = width * 10 + (*f++ - '0');
        if (*f == '.') {
            f++; prec = 0;
            if (*f == '*') { prec = va_arg(ap, int); f++; }
            else while (isdigit((unsigned char)*f)) prec = prec * 10 + (*f++ - '0');
        }
        for (;; f++) {
            if (*f == 'l') lng++; else if (*f == 'h') lng--; else if (*f == 'z' || *f == 't' || *f == 'j') lng = 1;
            else if (*f == 'L') lng = 2; else break;
        }
        char c = *f;
        if (!c) break;
        switch (c) {
        case '%': put(s, '%'); break;
        case 'c': { char ch = (char)va_arg(ap, int); int pad = width > 1 ? width - 1 : 0; if (!left) while (pad--) put(s, ' '); put(s, ch); if (left) while (pad-- > 0) put(s, ' '); break; }
        case 's': {
            const char *p = va_arg(ap, const char *);
            if (!p) p = "(null)";
            size_t l = prec >= 0 ? strnlen(p, (size_t)prec) : strlen(p);
            int pad = width > (int)l ? width - (int)l : 0;
            if (!left) while (pad--) put(s, ' ');
            puts_n(s, p, l);
            if (left) while (pad-- > 0) put(s, ' ');
            break; }
        case 'd': case 'i': {
            long long v = lng >= 2 ? va_arg(ap, long long) : lng == 1 ? va_arg(ap, long) : va_arg(ap, int);
            if (lng == -1) v = (short)v; else if (lng == -2) v = (signed char)v;
            int neg = v < 0;
            fmt_uint(s, neg ? -(unsigned long long)v : (unsigned long long)v, 10, 0, neg, width, prec, left, zero, plus, space, "");
            break; }
        case 'u': case 'x': case 'X': case 'o': {
            unsigned long long v = lng >= 2 ? va_arg(ap, unsigned long long) : lng == 1 ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
            if (lng == -1) v = (unsigned short)v; else if (lng == -2) v = (unsigned char)v;
            int base = c == 'u' ? 10 : c == 'o' ? 8 : 16;
            fmt_uint(s, v, base, c == 'X', 0, width, prec, left, zero, 0, 0, alt && v ? (c == 'x' ? "0x" : c == 'X' ? "0X" : c == 'o' ? "0" : "") : "");
            break; }
        case 'p': { unsigned long long v = (uintptr_t)va_arg(ap, void *); fmt_uint(s, v, 16, 0, 0, width, prec, left, zero, 0, 0, "0x"); break; }
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': {
            double v = lng >= 2 ? (double)va_arg(ap, ldbl) : va_arg(ap, double);
            fmt_double(s, v, c, width, prec, left, zero, plus, space, alt);
            break; }
        case 'n': { int *p = va_arg(ap, int *); *p = (int)s->len; break; }
        default: put(s, '%'); put(s, c); break;
        }
    }
    return (int)s->len;
}

/* ------------------------------------------------------------------ embedded files (a program may carry read-only resources) */
struct as_res { const char *name; const unsigned char *data; unsigned len; };
extern const struct as_res __as_res[] __attribute__((weak));
static const struct as_res *res_find(const char *p)
{
    if (!__as_res) return 0;
    for (const struct as_res *r = __as_res; r->name; r++) if (!strcmp(r->name, p)) return r;
    return 0;
}
static int res_isdir(const char *p)
{
    size_t n = strlen(p);
    while (n > 1 && p[n - 1] == '/') n--;
    if (!__as_res) return 0;
    for (const struct as_res *r = __as_res; r->name; r++) if (!strncmp(r->name, p, n) && r->name[n] == '/') return 1;
    return 0;
}

/* ------------------------------------------------------------------ stdio */
struct _AS_FILE {
    int kind;                 /* 0 free, 1 console out, 2 memory file read, 3 memory file write, 4 console in */
    char path[128];
    char *data; size_t len, cap, pos;
    int eof, err, append, dirty;
    int unget;
    int is_static;            /* data points into the program image: never freed */
};
static FILE f_in = { 4 }, f_out = { 1 }, f_err = { 1 };
FILE *stdin = &f_in, *stdout = &f_out, *stderr = &f_err;
static FILE f_pool[16];

static void console_write(const char *p, size_t n)
{
    char b[129];
    while (n) { size_t c = n > 128 ? 128 : n; memcpy(b, p, c); b[c] = 0; __api->puts(b); p += c; n -= c; }
}

int fflush(FILE *f)
{
    if (!f) { for (int i = 0; i < 16; i++) if (f_pool[i].kind == 3) fflush(&f_pool[i]); return 0; }
    if (f->kind == 3 && f->dirty) {
        char *t = malloc(f->len + 1);
        if (!t) return EOF;
        memcpy(t, f->data, f->len); t[f->len] = 0;
        int r = __api->file_put(f->path, t, 0);
        free(t);
        if (r < 0) { f->err = 1; return EOF; }
        f->dirty = 0;
    }
    return 0;
}

FILE *fopen(const char *path, const char *mode)
{
    FILE *f = 0;
    for (int i = 0; i < 16; i++) if (!f_pool[i].kind) { f = &f_pool[i]; break; }
    if (!f) { errno = EMFILE; return 0; }
    memset(f, 0, sizeof *f);
    strlcpy(f->path, path, sizeof f->path);
    int w = mode[0] == 'w' || mode[0] == 'a' || strchr(mode, '+');
    const struct as_res *rs = mode[0] == 'r' && !w ? res_find(path) : 0;
    if (rs) { f->data = (char *)rs->data; f->len = rs->len; f->cap = rs->len; f->is_static = 1; f->kind = 2; return f; }
    if (mode[0] == 'r' || mode[0] == 'a') {
        size_t cap = 4200;
        char *d = malloc(cap);
        if (!d) { errno = ENOMEM; return 0; }
        int n = __api->file_read(path, d, (int)cap - 1);
        if (n < 0) {
            free(d);
            if (mode[0] == 'r') { errno = ENOENT; return 0; }
            n = 0; d = malloc(64); if (!d) return 0; cap = 64;
        }
        f->data = d; f->len = (size_t)n; f->cap = cap;
        if (mode[0] == 'a') f->pos = f->len;
    } else { f->data = malloc(64); f->cap = 64; if (!f->data) return 0; }
    f->kind = w ? 3 : 2;
    f->append = mode[0] == 'a';
    return f;
}
int fclose(FILE *f)
{
    if (!f || f->kind < 2 || f->kind == 4) return 0;
    int r = fflush(f);
    if (!f->is_static) free(f->data);
    f->kind = 0;
    return r;
}
static int mem_put(FILE *f, const void *b, size_t n)
{
    if (f->append) f->pos = f->len;
    if (f->pos + n + 1 > f->cap) {
        size_t nc = (f->pos + n + 1) * 2;
        char *d = realloc(f->data, nc);
        if (!d) { f->err = 1; return -1; }
        f->data = d; f->cap = nc;
    }
    if (f->pos > f->len) memset(f->data + f->len, 0, f->pos - f->len);
    memcpy(f->data + f->pos, b, n);
    f->pos += n;
    if (f->pos > f->len) f->len = f->pos;
    f->dirty = 1;
    return 0;
}
size_t fwrite(const void *b, size_t s, size_t n, FILE *f)
{
    size_t t = s * n;
    if (!t) return 0;
    if (f->kind == 1) { console_write(b, t); return n; }
    if (f->kind != 3) { f->err = 1; return 0; }
    return mem_put(f, b, t) < 0 ? 0 : n;
}
size_t fread(void *b, size_t s, size_t n, FILE *f)
{
    if (f->kind != 2 && f->kind != 3) return 0;
    size_t t = s * n;
    if (!t) return 0;
    size_t avail = f->pos < f->len ? f->len - f->pos : 0;
    if (t > avail) { t = avail; f->eof = 1; }
    memcpy(b, f->data + f->pos, t);
    f->pos += t;
    return s ? t / s : 0;
}
int fgetc(FILE *f)
{
    if (f->unget) { int c = f->unget - 1; f->unget = 0; return c; }
    unsigned char c;
    if (fread(&c, 1, 1, f) != 1) { f->eof = 1; return EOF; }
    return c;
}
int getc(FILE *f) { return fgetc(f); }
int getchar(void) { return EOF; }
int ungetc(int c, FILE *f) { if (c == EOF) return EOF; f->unget = c + 1; f->eof = 0; return c; }
int fputc(int c, FILE *f) { unsigned char ch = (unsigned char)c; return fwrite(&ch, 1, 1, f) == 1 ? c : EOF; }
int putc(int c, FILE *f) { return fputc(c, f); }
int putchar(int c) { return fputc(c, stdout); }
int fputs(const char *s, FILE *f) { size_t l = strlen(s); return l == 0 || fwrite(s, 1, l, f) == l ? 0 : EOF; }
int puts(const char *s) { fputs(s, stdout); fputc('\n', stdout); return 0; }
char *fgets(char *b, int n, FILE *f)
{
    int i = 0;
    while (i < n - 1) { int c = fgetc(f); if (c == EOF) break; b[i++] = (char)c; if (c == '\n') break; }
    if (!i) return 0;
    b[i] = 0;
    return b;
}
int fseek(FILE *f, long o, int w)
{
    if (f->kind != 2 && f->kind != 3) return -1;
    long base = w == SEEK_SET ? 0 : w == SEEK_CUR ? (long)f->pos : (long)f->len;
    if (base + o < 0) return -1;
    f->pos = (size_t)(base + o); f->eof = 0; f->unget = 0;
    return 0;
}
long ftell(FILE *f) { return (long)f->pos; }
void rewind(FILE *f) { fseek(f, 0, SEEK_SET); f->err = 0; }
int feof(FILE *f) { return f->eof; }
int ferror(FILE *f) { return f->err; }
void clearerr(FILE *f) { f->eof = f->err = 0; }
int setvbuf(FILE *f, char *b, int m, size_t n) { (void)f; (void)b; (void)m; (void)n; return 0; }
int fileno(FILE *f) { return f == &f_in ? 0 : f == &f_out ? 1 : f == &f_err ? 2 : 3; }
FILE *fdopen(int fd, const char *m) { (void)m; return fd == 0 ? stdin : fd == 1 ? stdout : fd == 2 ? stderr : 0; }
int remove(const char *p) { (void)p; errno = ENOSYS; return -1; }
int rename(const char *a, const char *b) { (void)a; (void)b; errno = ENOSYS; return -1; }
void perror(const char *s) { fprintf(stderr, "%s: %s\n", s, strerror(errno)); }
FILE *tmpfile(void) { return fopen("/tmp_native.tmp", "w+"); }
char *tmpnam(char *s) { static char b[32]; if (!s) s = b; strcpy(s, "/tmp_native.tmp"); return s; }

int vsnprintf(char *b, size_t n, const char *f, va_list ap)
{
    sink s = { b, n, 0, 0 };
    int r = do_printf(&s, f, ap);
    if (b && n) b[s.len < n ? s.len : n - 1] = 0;
    return r;
}
int snprintf(char *b, size_t n, const char *f, ...) { va_list ap; va_start(ap, f); int r = vsnprintf(b, n, f, ap); va_end(ap); return r; }
int vsprintf(char *b, const char *f, va_list ap) { return vsnprintf(b, (size_t)INT_MAX, f, ap); }
int sprintf(char *b, const char *f, ...) { va_list ap; va_start(ap, f); int r = vsnprintf(b, (size_t)INT_MAX, f, ap); va_end(ap); return r; }
int vfprintf(FILE *o, const char *f, va_list ap)
{
    va_list c; va_copy(c, ap);
    int n = vsnprintf(0, 0, f, c);
    va_end(c);
    if (n < 0) return n;
    char *t = malloc((size_t)n + 1);
    if (!t) return -1;
    vsnprintf(t, (size_t)n + 1, f, ap);
    fwrite(t, 1, (size_t)n, o);
    free(t);
    return n;
}
int vprintf(const char *f, va_list ap) { return vfprintf(stdout, f, ap); }
int fprintf(FILE *o, const char *f, ...) { va_list ap; va_start(ap, f); int r = vfprintf(o, f, ap); va_end(ap); return r; }
int printf(const char *f, ...) { va_list ap; va_start(ap, f); int r = vfprintf(stdout, f, ap); va_end(ap); return r; }
int vasprintf(char **o, const char *f, va_list ap)
{
    va_list c; va_copy(c, ap);
    int n = vsnprintf(0, 0, f, c);
    va_end(c);
    *o = malloc((size_t)n + 1);
    if (!*o) return -1;
    vsnprintf(*o, (size_t)n + 1, f, ap);
    return n;
}
int asprintf(char **o, const char *f, ...) { va_list ap; va_start(ap, f); int r = vasprintf(o, f, ap); va_end(ap); return r; }

/* sscanf: %d %i %u %x %s %c %f %lf %[...] %n and literal text (enough for the usual config parsing) */
int sscanf(const char *s, const char *f, ...)
{
    va_list ap; va_start(ap, f);
    int cnt = 0; const char *s0 = s;
    for (; *f; f++) {
        if (isspace((unsigned char)*f)) { while (isspace((unsigned char)*s)) s++; continue; }
        if (*f != '%') { if (*s != *f) break; s++; continue; }
        f++;
        int skip = 0, lng = 0, width = 0;
        if (*f == '*') { skip = 1; f++; }
        while (isdigit((unsigned char)*f)) width = width * 10 + (*f++ - '0');
        while (*f == 'l' || *f == 'h' || *f == 'z') { if (*f == 'l') lng++; f++; }
        char c = *f;
        if (c == '%') { if (*s != '%') break; s++; continue; }
        if (c == 'n') { if (!skip) *va_arg(ap, int *) = (int)(s - s0); continue; }
        if (c != 'c' && c != '[') while (isspace((unsigned char)*s)) s++;
        if (!*s) { if (!cnt) cnt = EOF; break; }
        if (c == 'd' || c == 'i' || c == 'u' || c == 'x' || c == 'X' || c == 'o') {
            char *e; long long v = c == 'u' ? (long long)strtoull(s, &e, 10) : strtoll(s, &e, c == 'x' || c == 'X' ? 16 : c == 'o' ? 8 : c == 'i' ? 0 : 10);
            if (e == s) break;
            s = e;
            if (!skip) { if (lng >= 2) *va_arg(ap, long long *) = v; else if (lng == 1) *va_arg(ap, long *) = (long)v; else *va_arg(ap, int *) = (int)v; cnt++; }
        } else if (c == 'f' || c == 'g' || c == 'e') {
            char *e; double v = strtod(s, &e);
            if (e == s) break;
            s = e;
            if (!skip) { if (lng) *va_arg(ap, double *) = v; else *va_arg(ap, float *) = (float)v; cnt++; }
        } else if (c == 's') {
            char *d = skip ? 0 : va_arg(ap, char *); int n = 0;
            while (*s && !isspace((unsigned char)*s) && (!width || n < width)) { if (d) d[n] = *s; n++; s++; }
            if (d) d[n] = 0;
            if (!skip) cnt++;
        } else if (c == 'c') {
            char *d = skip ? 0 : va_arg(ap, char *);
            if (d) *d = *s;
            s++;
            if (!skip) cnt++;
        } else if (c == '[') {
            f++; int neg = 0; if (*f == '^') { neg = 1; f++; }
            const char *set = f; if (*f == ']') f++;
            while (*f && *f != ']') f++;
            size_t sl = (size_t)(f - set);
            char *d = skip ? 0 : va_arg(ap, char *); int n = 0;
            while (*s && (!width || n < width)) {
                int in = 0;
                for (size_t i = 0; i < sl; i++) {
                    if (i + 2 < sl && set[i + 1] == '-') { if (*s >= set[i] && *s <= set[i + 2]) in = 1; i += 2; }
                    else if (set[i] == *s) in = 1;
                }
                if (in == neg) break;
                if (d) d[n] = *s;
                n++; s++;
            }
            if (!n) break;
            if (d) d[n] = 0;
            if (!skip) cnt++;
        } else break;
    }
    va_end(ap);
    return cnt;
}

/* ------------------------------------------------------------------ time */
static const int mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
static int leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
time_t time(time_t *t) { time_t v = (time_t)__api->epoch(); if (t) *t = v; return v; }
clock_t clock(void) { return (clock_t)__api->ms(); }
double difftime(time_t a, time_t b) { return (double)(a - b); }
int gettimeofday(struct timeval *tv, void *tz)
{
    (void)tz;
    /* monotonic: the RTC seconds are read once, everything after that follows the millisecond counter
     * (mixing live RTC seconds with ms % 1000 made the time jump backwards by up to a second) */
    static unsigned ms0; static time_t ep0; static int have;
    unsigned ms = __api->ms();
    if (!have) { have = 1; ms0 = ms; ep0 = (time_t)__api->epoch(); }
    unsigned d = ms - ms0;
    tv->tv_sec = ep0 + (time_t)(d / 1000); tv->tv_usec = (long)(d % 1000) * 1000;
    return 0;
}
struct tm *gmtime_r(const time_t *tp, struct tm *o)
{
    long t = *tp; long days = t / 86400, rem = t % 86400;
    if (rem < 0) { rem += 86400; days--; }
    o->tm_hour = (int)(rem / 3600); o->tm_min = (int)(rem % 3600 / 60); o->tm_sec = (int)(rem % 60);
    o->tm_wday = (int)((days + 4) % 7); if (o->tm_wday < 0) o->tm_wday += 7;
    int y = 1970;
    while (days < 0) { y--; days += leap(y) ? 366 : 365; }
    while (days >= (leap(y) ? 366 : 365)) { days -= leap(y) ? 366 : 365; y++; }
    o->tm_year = y - 1900; o->tm_yday = (int)days;
    int m = 0;
    while (m < 11 && days >= mdays[m] + (m == 1 && leap(y))) { days -= mdays[m] + (m == 1 && leap(y)); m++; }
    o->tm_mon = m; o->tm_mday = (int)days + 1; o->tm_isdst = 0; o->tm_gmtoff = 0; o->tm_zone = "UTC";
    return o;
}
static struct tm tm_buf;
struct tm *gmtime(const time_t *t) { return gmtime_r(t, &tm_buf); }
struct tm *localtime_r(const time_t *t, struct tm *o) { return gmtime_r(t, o); }
struct tm *localtime(const time_t *t) { return gmtime_r(t, &tm_buf); }
time_t timegm(struct tm *t)
{
    long y = t->tm_year + 1900L, days = 0;
    for (long i = 1970; i < y; i++) days += leap((int)i) ? 366 : 365;
    for (long i = y; i < 1970; i++) days -= leap((int)i) ? 366 : 365;
    int m = t->tm_mon, yy = (int)y + m / 12; m %= 12; if (m < 0) { m += 12; yy--; }
    for (int i = 0; i < m; i++) days += mdays[i] + (i == 1 && leap(yy));
    days += t->tm_mday - 1;
    return days * 86400 + t->tm_hour * 3600L + t->tm_min * 60L + t->tm_sec;
}
time_t mktime(struct tm *t) { time_t r = timegm(t); gmtime_r(&r, t); return r; }
size_t strftime(char *s, size_t n, const char *f, const struct tm *t)
{
    static const char *wd[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
    static const char *mn[] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
    size_t o = 0;
    for (; *f && o + 1 < n; f++) {
        if (*f != '%') { s[o++] = *f; continue; }
        char tmp[40]; f++;
        switch (*f) {
        case 'Y': snprintf(tmp, sizeof tmp, "%d", t->tm_year + 1900); break;
        case 'y': snprintf(tmp, sizeof tmp, "%02d", t->tm_year % 100); break;
        case 'm': snprintf(tmp, sizeof tmp, "%02d", t->tm_mon + 1); break;
        case 'd': snprintf(tmp, sizeof tmp, "%02d", t->tm_mday); break;
        case 'e': snprintf(tmp, sizeof tmp, "%2d", t->tm_mday); break;
        case 'H': snprintf(tmp, sizeof tmp, "%02d", t->tm_hour); break;
        case 'M': snprintf(tmp, sizeof tmp, "%02d", t->tm_min); break;
        case 'S': snprintf(tmp, sizeof tmp, "%02d", t->tm_sec); break;
        case 'a': snprintf(tmp, sizeof tmp, "%.3s", wd[t->tm_wday % 7]); break;
        case 'A': snprintf(tmp, sizeof tmp, "%s", wd[t->tm_wday % 7]); break;
        case 'b': case 'h': snprintf(tmp, sizeof tmp, "%.3s", mn[t->tm_mon % 12]); break;
        case 'B': snprintf(tmp, sizeof tmp, "%s", mn[t->tm_mon % 12]); break;
        case 'j': snprintf(tmp, sizeof tmp, "%03d", t->tm_yday + 1); break;
        case 'Z': snprintf(tmp, sizeof tmp, "GMT"); break;
        case 'z': snprintf(tmp, sizeof tmp, "+0000"); break;
        case '%': snprintf(tmp, sizeof tmp, "%%"); break;
        default: snprintf(tmp, sizeof tmp, "%%%c", *f); break;
        }
        size_t l = strlen(tmp);
        if (o + l >= n) break;
        memcpy(s + o, tmp, l); o += l;
    }
    s[o] = 0;
    return o;
}
char *strptime(const char *s, const char *f, struct tm *t) { (void)s; (void)f; (void)t; return 0; }

/* ------------------------------------------------------------------ small leftovers */
char *setlocale(int c, const char *l) { (void)c; (void)l; return "C"; }
static struct lconv lc = { ".", "", "" };
struct lconv *localeconv(void) { return &lc; }
sighandler_t signal(int s, sighandler_t h) { (void)s; (void)h; return SIG_DFL; }
int raise(int s) { (void)s; return 0; }
ssize_t write(int fd, const void *b, size_t n) { if (fd == 1 || fd == 2) { console_write(b, n); return (ssize_t)n; } return -1; }
int unlink(const char *p) { (void)p; return -1; }
char *getcwd(char *b, size_t n) { if (n > 1) { b[0] = '/'; b[1] = 0; } return b; }
unsigned sleep(unsigned s) { unsigned t = __api->ms() + s * 1000; while ((int)(__api->ms() - t) < 0) { __api->win_wait(10); } return 0; }
int usleep(unsigned us) { unsigned t = __api->ms() + us / 1000; while ((int)(__api->ms() - t) < 0) { __api->win_wait(5); } return 0; }
long sysconf(int n) { (void)n; return 4096; }
int isatty(int fd) { return fd <= 2; }
int mkdir(const char *p, mode_t m) { (void)p; (void)m; return -1; }
size_t wcslen(const wchar_t *s) { size_t n = 0; while (s[n]) n++; return n; }

/* ------------------------------------------------------------------ program start: as_main -> main */
extern int main(int argc, char **argv);

__attribute__((section(".text.as_main")))
int as_main(const as_api *api, const char *args)
{
    __api = api;
    {   extern void (*__init_array_start[])(void), (*__init_array_end[])(void);
        for (void (**f)(void) = __init_array_start; f < __init_array_end; f++) (*f)();
    }
    static char line[512];
    static char *argv[40];
    int argc = 0;
    argv[argc++] = "program";
    strlcpy(line, args ? args : "", sizeof line);
    for (char *p = line; *p && argc < 39; ) {
        while (*p == ' ') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = 0;
    }
    argv[argc] = 0;
    if (setjmp(exit_env) == 0) { exit_ready = 1; exit_code = main(argc, argv); }
    fflush(0);
    return exit_code;
}

/* ------------------------------------------------------------------ 64-bit division helpers (no libgcc) */
unsigned long long __udivmoddi4(unsigned long long n, unsigned long long d, unsigned long long *rem)
{
    unsigned long long q = 0, r = 0;
    if (d == 0) { if (rem) *rem = 0; return 0; }
    if ((n >> 32) == 0 && (d >> 32) == 0) { if (rem) *rem = (unsigned)n % (unsigned)d; return (unsigned)n / (unsigned)d; }
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1);
        if (r >= d) { r -= d; q |= 1ULL << i; }
    }
    if (rem) *rem = r;
    return q;
}
unsigned long long __udivdi3(unsigned long long n, unsigned long long d) { return __udivmoddi4(n, d, 0); }
unsigned long long __umoddi3(unsigned long long n, unsigned long long d) { unsigned long long r; __udivmoddi4(n, d, &r); return r; }
long long __divdi3(long long n, long long d)
{
    int neg = (n < 0) ^ (d < 0);
    unsigned long long q = __udivmoddi4(n < 0 ? -(unsigned long long)n : (unsigned long long)n, d < 0 ? -(unsigned long long)d : (unsigned long long)d, 0);
    return neg ? -(long long)q : (long long)q;
}
long long __moddi3(long long n, long long d)
{
    unsigned long long r;
    __udivmoddi4(n < 0 ? -(unsigned long long)n : (unsigned long long)n, d < 0 ? -(unsigned long long)d : (unsigned long long)d, &r);
    return n < 0 ? -(long long)r : (long long)r;
}

/* ------------------------------------------------------------------ more leftovers */
void setbuf(FILE *f, char *b) { (void)f; (void)b; }
int rmdir(const char *p) { (void)p; errno = ENOSYS; return -1; }
int dup(int fd) { (void)fd; errno = ENOSYS; return -1; }
char *ctime(const time_t *t)
{
    static char b[32];
    struct tm *tm = gmtime(t);
    strftime(b, sizeof b, "%a %b %e %H:%M:%S %Y\n", tm);
    return b;
}
char *asctime(const struct tm *tm) { static char b[32]; strftime(b, sizeof b, "%a %b %e %H:%M:%S %Y\n", tm); return b; }
int select(int n, fd_set *r, fd_set *w, fd_set *e, struct timeval *tv)
{
    (void)n; (void)r; (void)w; (void)e;
    if (tv) usleep((unsigned)(tv->tv_sec * 1000000 + tv->tv_usec));
    return 0;
}
void __stack_chk_fail(void) { abort(); }

/* ------------------------------------------------------------------ file descriptors (memory backed, read only) */
#define NFD 24
static struct { int used; const unsigned char *data; size_t len, pos; unsigned char *own; } fdt[NFD];
int open(const char *p, int f, ...)
{
    (void)f;
    const struct as_res *r = res_find(p);
    for (int i = 0; i < NFD; i++) {
        if (fdt[i].used) continue;
        if (r) { fdt[i].data = r->data; fdt[i].len = r->len; fdt[i].own = 0; }
        else {
            unsigned char *b = malloc(4200);
            if (!b) { errno = ENOMEM; return -1; }
            int n = __api->file_read(p, (char *)b, 4199);
            if (n < 0) { free(b); errno = ENOENT; return -1; }
            fdt[i].data = b; fdt[i].len = (size_t)n; fdt[i].own = b;
        }
        fdt[i].used = 1; fdt[i].pos = 0;
        return i + 3;
    }
    errno = EMFILE;
    return -1;
}
int close(int fd)
{
    if (fd < 3 || fd >= NFD + 3 || !fdt[fd - 3].used) return 0;
    free(fdt[fd - 3].own);
    fdt[fd - 3].used = 0;
    return 0;
}
ssize_t read(int fd, void *b, size_t n)
{
    if (fd < 3 || fd >= NFD + 3 || !fdt[fd - 3].used) { errno = EBADF; return -1; }
    size_t left = fdt[fd - 3].len - fdt[fd - 3].pos;
    if (n > left) n = left;
    memcpy(b, fdt[fd - 3].data + fdt[fd - 3].pos, n);
    fdt[fd - 3].pos += n;
    return (ssize_t)n;
}
off_t lseek(int fd, off_t o, int w)
{
    if (fd < 3 || fd >= NFD + 3 || !fdt[fd - 3].used) { errno = EBADF; return -1; }
    long base = w == SEEK_SET ? 0 : w == SEEK_CUR ? (long)fdt[fd - 3].pos : (long)fdt[fd - 3].len;
    if (base + o < 0) return -1;
    fdt[fd - 3].pos = (size_t)(base + o);
    return (off_t)fdt[fd - 3].pos;
}
int fstat(int fd, struct stat *s)
{
    memset(s, 0, sizeof *s);
    if (fd < 3 || fd >= NFD + 3 || !fdt[fd - 3].used) { errno = EBADF; return -1; }
    s->st_mode = S_IFREG | 0444; s->st_size = (off_t)fdt[fd - 3].len;
    return 0;
}
/* read-only mapping of an open file: the descriptor table already holds the whole file in memory, so the "mapping" is a pointer into it */
void *mmap(void *a, size_t l, int p, int f, int fd, off_t o)
{
    (void)a; (void)p; (void)f;
    if (fd < 3 || fd >= NFD + 3 || !fdt[fd - 3].used || o < 0 || (size_t)o > fdt[fd - 3].len || l > fdt[fd - 3].len - (size_t)o) { errno = EBADF; return (void *)-1; }
    return (void *)(fdt[fd - 3].data + o);
}
int munmap(void *a, size_t l) { (void)a; (void)l; return 0; }
int stat(const char *p, struct stat *s)
{
    char b[8];
    memset(s, 0, sizeof *s);
    const struct as_res *r = res_find(p);
    if (r) { s->st_mode = S_IFREG | 0444; s->st_size = (off_t)r->len; return 0; }
    if (res_isdir(p)) { s->st_mode = S_IFDIR | 0555; return 0; }
    int n = __api->file_read(p, b, 1);
    if (n < 0) { errno = ENOENT; return -1; }
    s->st_mode = S_IFREG | 0644;
    char *big = malloc(4200);
    if (big) { n = __api->file_read(p, big, 4199); if (n >= 0) s->st_size = n; free(big); }
    return 0;
}
int access(const char *p, int m)
{
    struct stat st;
    (void)m;
    return stat(p, &st);
}
