/* Self test of the libc inside the OS: prints PASS/FAIL lines. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <setjmp.h>

static int fails, total;
static void chk(const char *name, int ok) { total++; if (!ok) { fails++; printf("FAIL: %s\n", name); } }
static int cmpi(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

int main(int argc, char **argv)
{
    char b[128];
    snprintf(b, sizeof b, "%d|%5d|%-5d|%05d|%x|%X|%#x|%o|%u|%c|%s|%.3s", 42, 42, 42, 42, 255, 255, 255, 8, 4000000000u, 'z', "str", "abcdef");
    chk("printf ints", !strcmp(b, "42|   42|42   |00042|ff|FF|0xff|10|4000000000|z|str|abc"));
    snprintf(b, sizeof b, "%lld %ld %hd %zu", 1234567890123LL, -5L, (short)7, (size_t)9);
    chk("printf lengths", !strcmp(b, "1234567890123 -5 7 9"));
    snprintf(b, sizeof b, "%.2f %.0f %f %5.1f", 3.14159, 2.5, 1.0, -0.25);
    chk("printf f", !strcmp(b, "3.14 3 1.000000  -0.2") || !strcmp(b, "3.14 2 1.000000  -0.3") || !strcmp(b, "3.14 3 1.000000  -0.3"));
    snprintf(b, sizeof b, "%g %g %g %e", 100000.0, 1e-5, 0.5, 12345.678);
    chk("printf g/e", !strcmp(b, "100000 1e-05 0.5 1.234568e+04"));
    chk("strtol", strtol("  -123abc", 0, 10) == -123 && strtol("0x1f", 0, 0) == 31);
    chk("strtod", fabs(strtod("2.5e3", 0) - 2500.0) < 1e-9 && fabs(atof("0.125") - 0.125) < 1e-12);
    int a[] = { 5, 3, 9, 1, 7, 2, 8, 6, 4, 0, 11, 15, 13, 12, 14, 10 };
    qsort(a, 16, sizeof a[0], cmpi);
    int ok = 1; for (int i = 0; i < 16; i++) if (a[i] != i + (i > 9 ? 0 : 0)) ok = 0;
    chk("qsort", ok);
    void *p[50]; for (int i = 0; i < 50; i++) { p[i] = malloc(100 + i * 37); memset(p[i], i, 100); }
    for (int i = 0; i < 50; i += 2) free(p[i]);
    char *q = malloc(5000); memset(q, 1, 5000);
    for (int i = 1; i < 50; i += 2) free(p[i]);
    free(q);
    char *big = malloc(8 << 20); chk("malloc 8M", big != 0); if (big) { big[0] = 1; big[(8 << 20) - 1] = 2; free(big); }
    q = realloc(0, 10); strcpy(q, "hello"); q = realloc(q, 1000); chk("realloc", !strcmp(q, "hello")); free(q);
    chk("math", fabs(sqrt(2.0) - 1.41421356) < 1e-6 && fabs(sin(M_PI / 2) - 1) < 1e-9 && fabs(pow(2, 10) - 1024) < 1e-9 &&
                fabs(exp(1.0) - M_E) < 1e-9 && fabs(log(M_E) - 1) < 1e-9 && floor(-1.5) == -2 && ceil(1.2) == 2 && fabs(pow(2, 0.5) - 1.4142135) < 1e-6);
    chk("fmod/atan2", fabs(fmod(7.5, 2) - 1.5) < 1e-12 && fabs(atan2(1, 1) - M_PI / 4) < 1e-9);
    chk("string", !strcmp(strstr("hello world", "o w"), "o world") && strcasecmp("AbC", "aBc") == 0 && strchr("abc", 'c') != 0 && strspn("aabbc", "ab") == 4);
    char *d = strdup("copy"); chk("strdup", !strcmp(d, "copy")); free(d);
    jmp_buf jb; volatile int hit = 0;
    if (setjmp(jb) == 0) { hit = 1; longjmp(jb, 5); } else hit += 10;
    chk("setjmp", hit == 11);
    time_t t = 86400 * 365; struct tm *tm = gmtime(&t);
    chk("gmtime", tm->tm_year == 71 && tm->tm_mon == 0 && tm->tm_mday == 1);
    struct tm x = { 0 }; x.tm_year = 100; x.tm_mon = 1; x.tm_mday = 29; chk("timegm", timegm(&x) == 951782400);
    strftime(b, sizeof b, "%Y-%m-%d %H:%M:%S", tm); chk("strftime", !strcmp(b, "1971-01-01 00:00:00"));
    int n1; char w[16]; sscanf("12 abc 3.5", "%d %15s %lf", &n1, w, (double[]){0}); chk("sscanf", n1 == 12 && !strcmp(w, "abc"));
    FILE *f = fopen("libc_test.txt", "w"); if (f) { fprintf(f, "line %d\n", 7); fclose(f); }
    f = fopen("libc_test.txt", "r"); b[0] = 0; if (f) { fgets(b, sizeof b, f); fclose(f); }
    chk("file roundtrip", !strcmp(b, "line 7\n"));
    chk("fopen missing", fopen("no_such_file_x", "r") == 0 && errno == ENOENT);
    printf("libc self test: %d/%d passed (%d args)\n", total - fails, total, argc - 1);
    if (argc > 1) printf("first arg: %s\n", argv[1]);
    return fails;
}
