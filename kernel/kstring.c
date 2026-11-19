/* Minimal freestanding string/memory helpers. */
#include "astra.h"

size_t kstrlen(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

int kstrcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (u8)*a - (u8)*b;
}

int kstrncmp(const char *a, const char *b, size_t n)
{
    while (n && *a && *a == *b) { a++; b++; n--; }
    return n ? (u8)*a - (u8)*b : 0;
}

char *kstrchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c) return (char *)s;
        if (!*s) return NULL;
    }
}

char *kstrrchr(const char *s, int c)
{
    const char *last = NULL;
    for (;; s++) {
        if (*s == (char)c) last = s;
        if (!*s) return (char *)last;
    }
}

char *kstrstr(const char *h, const char *n)
{
    size_t nl = kstrlen(n);
    if (!nl) return (char *)h;
    for (; *h; h++)
        if (!kstrncmp(h, n, nl)) return (char *)h;
    return NULL;
}

size_t kstrcspn(const char *s, const char *reject)
{
    size_t n = 0;
    while (s[n] && !kstrchr(reject, s[n])) n++;
    return n;
}

char *kstrncpy(char *d, const char *s, size_t n)
{
    size_t i = 0;
    for (; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
    return d;
}

char *kstrcpy(char *d, const char *s)
{
    size_t i = 0;
    do d[i] = s[i]; while (s[i++]);
    return d;
}

void *kmemcpy(void *dst, const void *src, size_t n)
{
    u8 *d = dst; const u8 *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

void *kmemset(void *dst, int c, size_t n)
{
    u8 *d = dst;
    while (n--) *d++ = (u8)c;
    return dst;
}

void *kmemmove(void *dst, const void *src, size_t n)
{
    u8 *d = dst; const u8 *s = src;
    if (d < s) { while (n--) *d++ = *s++; }
    else       { d += n; s += n; while (n--) *--d = *--s; }
    return dst;
}

int kmemcmp(const void *a, const void *b, size_t n)
{
    const u8 *x = a, *y = b;
    while (n--) { if (*x != *y) return *x - *y; x++; y++; }
    return 0;
}

/* ---- calendar helpers (proleptic Gregorian, 32-bit only) ---- */
#define DAYS_1970_TO_2000 10957

static int days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    int era = y / 400;
    int yoe = y - era * 400;
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

u32 ktime_pack(const hal_time *t)
{
    if (t->year < 2000) return 0;
    int days = days_from_civil((int)t->year, (int)t->month, (int)t->day) - DAYS_1970_TO_2000;
    if (days < 0) return 0;
    return (u32)days * 86400u + t->hour * 3600u + t->min * 60u + t->sec;
}

void ktime_unpack(u32 secs, hal_time *t)
{
    u32 days = secs / 86400u, rem = secs % 86400u;
    int z = (int)days + DAYS_1970_TO_2000 + 719468;
    int era = z / 146097;
    int doe = z - era * 146097;
    int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int y = yoe + era * 400;
    int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int mp = (5 * doy + 2) / 153;
    int d = doy - (153 * mp + 2) / 5 + 1;
    int m = mp < 10 ? mp + 3 : mp - 9;
    t->year = (u32)(y + (m <= 2));
    t->month = (u32)m; t->day = (u32)d;
    t->hour = rem / 3600u; t->min = (rem / 60u) % 60u; t->sec = rem % 60u;
}

#ifndef HOST_TEST
/* The compiler may emit calls to these even with -ffreestanding. */
void *memcpy(void *d, const void *s, size_t n) { return kmemcpy(d, s, n); }
void *memset(void *d, int c, size_t n)         { return kmemset(d, c, n); }
int   memcmp(const void *a, const void *b, size_t n) { return kmemcmp(a, b, n); }

void *memmove(void *d, const void *s, size_t n) { return kmemmove(d, s, n); }
#endif
