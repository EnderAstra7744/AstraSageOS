#ifndef _AS_STDLIB_H
#define _AS_STDLIB_H
#include <stddef.h>
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 2147483647
typedef struct { int quot, rem; } div_t;
typedef struct { long quot, rem; } ldiv_t;
void *malloc(size_t n); void *calloc(size_t a, size_t b); void *realloc(void *p, size_t n); void free(void *p);
void *aligned_alloc(size_t al, size_t n);
int posix_memalign(void **p, size_t al, size_t n);
void abort(void) __attribute__((noreturn));
void exit(int c) __attribute__((noreturn));
void _Exit(int c) __attribute__((noreturn));
int atexit(void (*f)(void));
int abs(int v); long labs(long v); long long llabs(long long v);
div_t div(int a, int b); ldiv_t ldiv(long a, long b);
int atoi(const char *s); long atol(const char *s); long long atoll(const char *s); double atof(const char *s);
long strtol(const char *s, char **e, int base); unsigned long strtoul(const char *s, char **e, int base);
long long strtoll(const char *s, char **e, int base); unsigned long long strtoull(const char *s, char **e, int base);
double strtod(const char *s, char **e); float strtof(const char *s, char **e); long double strtold(const char *s, char **e);
int rand(void); void srand(unsigned s);
void qsort(void *b, size_t n, size_t sz, int (*cmp)(const void *, const void *));
void *bsearch(const void *k, const void *b, size_t n, size_t sz, int (*cmp)(const void *, const void *));
char *getenv(const char *n); int setenv(const char *n, const char *v, int o); int system(const char *c);
char *realpath(const char *p, char *out);
#endif
