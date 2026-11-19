/* AstraScript engine - memory: one arena per run (everything is released when the run ends), plus the tiny
 * string helpers the engine needs so that it depends on no C library. */
#ifndef AS_MEMORY_H
#define AS_MEMORY_H

#define AS_ARENA_SIZE (4u << 20)          /* 4 MiB for a whole run: tokens, syntax tree, values */

void    *as_alloc(unsigned n);            /* zeroed, 8-byte aligned; never returns on exhaustion (as_abort) */
void     as_mem_reset(void);
unsigned as_mem_used(void);
void     as_abort(const char *msg) __attribute__((noreturn));      /* fatal engine error: unwinds to as_run() */
void    *as_abort_target(void);

unsigned as_strlen(const char *s);
int      as_strcmp(const char *a, const char *b);
int      as_strncmp(const char *a, const char *b, unsigned n);
void    *as_memcpy(void *d, const void *s, unsigned n);
void    *as_memmove(void *d, const void *s, unsigned n);
void    *as_memset(void *d, int c, unsigned n);
int      as_memcmp(const void *a, const void *b, unsigned n);
int      as_fmt(char *buf, int cap, const char *fmt, ...);        /* %s %d %c %% only */

#endif
