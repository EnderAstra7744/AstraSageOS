/* AstraScript engine 2 - memory.c: the arena and the string helpers. */
#include <stdarg.h>
#include "as_types.h"

static unsigned char arena[AS_ARENA_SIZE] __attribute__((aligned(16)));
static unsigned used;
static void *abort_buf[32];
static const char *abort_msg;

void as_intern_reset(void);
void as_mem_reset(void) { used = 0; abort_msg = 0; as_intern_reset(); }
unsigned as_mem_used(void) { return used; }
void *as_abort_target(void) { return abort_buf; }
const char *as_abort_message(void) { return abort_msg; }

void as_abort(const char *msg)
{
    abort_msg = msg;
    __builtin_longjmp(abort_buf, 1);
}

void *as_alloc(unsigned n)
{
    n = (n + 7u) & ~7u;
    if (used + n > AS_ARENA_SIZE) as_abort("AstraScript memory limit reached (4 MiB): the program creates too many values");
    void *p = arena + used;
    used += n;
    as_memset(p, 0, n);
    return p;
}

unsigned as_strlen(const char *s) { unsigned n = 0; while (s[n]) n++; return n; }
int as_strcmp(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (unsigned char)*a - (unsigned char)*b; }
int as_strncmp(const char *a, const char *b, unsigned n) { while (n && *a && *a == *b) { a++; b++; n--; } return n ? (unsigned char)*a - (unsigned char)*b : 0; }
void *as_memcpy(void *d, const void *s, unsigned n) { unsigned char *x = d; const unsigned char *y = s; while (n--) *x++ = *y++; return d; }
void *as_memmove(void *d, const void *s, unsigned n)
{
    unsigned char *x = d; const unsigned char *y = s;
    if (x < y) while (n--) *x++ = *y++;
    else { x += n; y += n; while (n--) *--x = *--y; }
    return d;
}
void *as_memset(void *d, int c, unsigned n) { unsigned char *x = d; while (n--) *x++ = (unsigned char)c; return d; }
int as_memcmp(const void *a, const void *b, unsigned n)
{
    const unsigned char *x = a, *y = b;
    for (; n; n--, x++, y++) if (*x != *y) return *x - *y;
    return 0;
}

int as_fmt(char *buf, int cap, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    int n = 0;
    for (; *fmt && n < cap - 1; fmt++) {
        if (*fmt != '%') { buf[n++] = *fmt; continue; }
        fmt++;
        if (*fmt == 's') { const char *s = va_arg(ap, const char *); if (!s) s = "(null)"; while (*s && n < cap - 1) buf[n++] = *s++; }
        else if (*fmt == 'd') {
            int v = va_arg(ap, int); char t[16]; int k = 0; unsigned u = v < 0 ? 0u - (unsigned)v : (unsigned)v;
            do { t[k++] = (char)('0' + u % 10); u /= 10; } while (u);
            if (v < 0 && n < cap - 1) buf[n++] = '-';
            while (k && n < cap - 1) buf[n++] = t[--k];
        } else if (*fmt == 'c') buf[n++] = (char)va_arg(ap, int);
        else if (*fmt == '%') buf[n++] = '%';
    }
    buf[n] = 0;
    va_end(ap);
    return n;
}

/* names are interned: two equal names are the same pointer */
#define IT 2048
static Str *itab[IT];
void as_intern_reset(void) { as_memset(itab, 0, sizeof itab); }

Str *as_intern(const char *s, int len)
{
    unsigned h = 5381;
    for (int i = 0; i < len; i++) h = h * 33 + (unsigned char)s[i];
    for (unsigned k = 0; k < IT; k++) {
        Str **slot = &itab[(h + k) % IT];
        if (!*slot) {
            Str *r = as_alloc(sizeof(Str) + (unsigned)len);
            r->len = len; as_memcpy(r->s, s, (unsigned)len); r->s[len] = 0;
            *slot = r;
            return r;
        }
        if ((*slot)->len == len && !as_memcmp((*slot)->s, s, (unsigned)len)) return *slot;
    }
    as_abort("too many different names in the program");
}
