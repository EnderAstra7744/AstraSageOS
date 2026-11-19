#ifndef _AS_REGEX_H
#define _AS_REGEX_H
#include <sys/types.h>
/* a backtracking POSIX-style engine (BRE/ERE, groups, classes, {m,n}, alternation, anchors); leftmost, first-match (Perl order) */
typedef struct { void *prog; int flags; size_t re_nsub; } regex_t;
typedef int regoff_t;
typedef struct { regoff_t rm_so, rm_eo; } regmatch_t;
#define REG_EXTENDED 1
#define REG_ICASE 2
#define REG_NOSUB 4
#define REG_NEWLINE 8
#define REG_NOMATCH 1
#define REG_BADPAT 2
#define REG_ESPACE 3
#define REG_NOTBOL 1
#define REG_NOTEOL 2
int regcomp(regex_t *r, const char *pat, int flags);
int regexec(const regex_t *r, const char *s, size_t n, regmatch_t *m, int flags);
size_t regerror(int code, const regex_t *r, char *buf, size_t n);
void regfree(regex_t *r);
#endif
