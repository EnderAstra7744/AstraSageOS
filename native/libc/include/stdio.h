#ifndef _AS_STDIO_H
#define _AS_STDIO_H
#include <stddef.h>
#include <stdarg.h>
typedef struct _AS_FILE FILE;
typedef long fpos_t;
extern FILE *stdin, *stdout, *stderr;
#define EOF (-1)
#define BUFSIZ 1024
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define FILENAME_MAX 256
#define PATH_MAX 256
#define L_tmpnam 32
#define _IONBF 2
#define _IOLBF 1
#define _IOFBF 0
FILE *fopen(const char *p, const char *m); int fclose(FILE *f); int fflush(FILE *f);
size_t fread(void *b, size_t s, size_t n, FILE *f); size_t fwrite(const void *b, size_t s, size_t n, FILE *f);
int fseek(FILE *f, long o, int w); long ftell(FILE *f); void rewind(FILE *f); int feof(FILE *f); int ferror(FILE *f); void clearerr(FILE *f);
int fgetc(FILE *f); int getc(FILE *f); int getchar(void); int ungetc(int c, FILE *f);
int fputc(int c, FILE *f); int putc(int c, FILE *f); int putchar(int c); int fputs(const char *s, FILE *f); int puts(const char *s);
char *fgets(char *b, int n, FILE *f);
int printf(const char *f, ...); int fprintf(FILE *o, const char *f, ...); int sprintf(char *b, const char *f, ...);
int snprintf(char *b, size_t n, const char *f, ...);
int vprintf(const char *f, va_list a); int vfprintf(FILE *o, const char *f, va_list a); int vsprintf(char *b, const char *f, va_list a);
int vsnprintf(char *b, size_t n, const char *f, va_list a);
int asprintf(char **o, const char *f, ...); int vasprintf(char **o, const char *f, va_list a);
int sscanf(const char *s, const char *f, ...);
void setbuf(FILE *f, char *b); int setvbuf(FILE *f, char *b, int m, size_t n);
int remove(const char *p); int rename(const char *a, const char *b);
void perror(const char *s);
FILE *tmpfile(void); char *tmpnam(char *s);
int fileno(FILE *f);
FILE *fdopen(int fd, const char *m);
#endif
