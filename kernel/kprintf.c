/* Tiny printf: %s %c %d %u %x %% with optional '-', '0', width and (for %s) .precision. */
#include "astra.h"

static int utoa_rev(char *t, u32 u, u32 base, int neg)
{
    int p = 0;
    do { t[p++] = "0123456789abcdef"[u % base]; u /= base; } while (u);
    if (neg) t[p++] = '-';
    return p;
}

int kvsnprintf(char *buf, size_t n, const char *fmt, va_list ap)
{
    size_t o = 0;
#define PUT(c) do { if (o + 1 < n) buf[o] = (c); o++; } while (0)

    for (; *fmt; fmt++) {
        if (*fmt != '%') { PUT(*fmt); continue; }
        fmt++;
        int left = 0, zero = 0, width = 0;
        if (*fmt == '-') { left = 1; fmt++; }
        if (*fmt == '0') { zero = 1; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        int prec = -1;
        if (*fmt == '.') { fmt++; prec = 0; while (*fmt >= '0' && *fmt <= '9') prec = prec * 10 + (*fmt++ - '0'); }

        char tmp[16], rev[16];
        const char *s = tmp;
        int len = 0, numeric = 0;

        switch (*fmt) {
        case 's':
            s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            len = 0;
            while (s[len] && (prec < 0 || len < prec)) len++;
            break;
        case 'c':
            tmp[0] = (char)va_arg(ap, int); len = 1;
            break;
        case 'd': {
            int v = va_arg(ap, int);
            int neg = v < 0;
            int p = utoa_rev(rev, neg ? 0u - (u32)v : (u32)v, 10, neg);
            for (int i = 0; i < p; i++) tmp[i] = rev[p - 1 - i];
            len = p; numeric = 1;
            break;
        }
        case 'u': case 'x': {
            u32 v = va_arg(ap, u32);
            int p = utoa_rev(rev, v, *fmt == 'x' ? 16 : 10, 0);
            for (int i = 0; i < p; i++) tmp[i] = rev[p - 1 - i];
            len = p; numeric = 1;
            break;
        }
        case '%':
            tmp[0] = '%'; len = 1;
            break;
        case 0:
            goto done;
        default:
            tmp[0] = '%'; tmp[1] = *fmt; len = 2;
            break;
        }

        int pad = width > len ? width - len : 0;
        if (!left) while (pad--) PUT(zero && numeric ? '0' : ' ');
        for (int i = 0; i < len; i++) PUT(s[i]);
        if (left) while (pad--) PUT(' ');
    }
done:
    if (n) buf[o < n ? o : n - 1] = 0;
    return (int)o;
#undef PUT
}

int ksnprintf(char *buf, size_t n, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    int r = kvsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return r;
}

/* Output capture: while active, kputs/kprintf text goes into a buffer (ANSI escapes dropped) instead of the screen. */
static char *cap_buf;
static u32 cap_cap, cap_len;
static int cap_esc;

static char *sv_buf; static u32 sv_cap, sv_len; static int sv_on;      /* an outer capture (a background job) that is paused while an inner one runs */

void con_capture_begin(char *buf, u32 cap)
{
    if (cap_buf && !sv_on) { sv_buf = cap_buf; sv_cap = cap_cap; sv_len = cap_len; sv_on = 1; }
    cap_buf = buf; cap_cap = cap; cap_len = 0; cap_esc = 0;
    if (cap) buf[0] = 0;
}

u32 con_capture_end(void)
{
    u32 n = cap_len;
    cap_buf = 0;
    if (sv_on) { cap_buf = sv_buf; cap_cap = sv_cap; cap_len = sv_len; sv_on = 0; }
    return n;
}

void con_capture_resume(char *buf, u32 cap, u32 len)      /* continues a capture that was paused: the text so far stays */
{
    cap_buf = buf; cap_cap = cap; cap_len = len; cap_esc = 0;
}

static void capture_char(char c)
{
    if (cap_esc == 1) { cap_esc = c == '[' ? 2 : 0; return; }
    if (cap_esc == 2) { if (c >= '@' && c <= '~') cap_esc = 0; return; }
    if (c == 27) { cap_esc = 1; return; }
    if (c == '\t') c = ' ';
    if (c == '\b') { if (cap_len) cap_buf[--cap_len] = 0; return; }
    if (c != '\n' && ((u8)c < 32 || (u8)c >= 127)) return;
    if (cap_len + 1 < cap_cap) { cap_buf[cap_len++] = c; cap_buf[cap_len] = 0; }
}

void kputs(const char *s)
{
    if (cap_buf) { while (*s) capture_char(*s++); return; }
    while (*s) con_putc(*s++);
}

void kprintf(const char *fmt, ...)
{
    char buf[512];
    va_list ap; va_start(ap, fmt);
    kvsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    kputs(buf);
}
