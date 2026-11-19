/* AstraScript engine 2 - public interface. */
#ifndef ASTRASCRIPT_H
#define ASTRASCRIPT_H

/* Runs a program (strict layout: start; ... end;). Returns 0 when it ran to the end, 1 on an error. */
int as2_run(const char *src, unsigned len, const char *ctx, unsigned ctxlen);
/* strict = 0: files without 'start;' (a line break ends a statement, main() does not start by itself) */
int as2_run_ex(const char *src, unsigned len, const char *ctx, unsigned ctxlen, int strict);
/* 1 when the program parses (silently); otherwise 0 and the first problem in *line / *msg */
int as2_probe(const char *src, unsigned len, int strict, int *line, const char **msg);
/* 1 when a file without 'start;' uses the new language (True/False/None, bool/list/dict types, classes, methods, [ ], ...) */
int as2_uses_new(const char *src, unsigned len);
/* Parses the program only (the editor's live check); reports the problem through as_h_error. Returns the number of errors. */
int as2_check(const char *src, unsigned len);
int as2_check_ctx(const char *src, unsigned len, const char *ctx, unsigned ctxlen);   /* + names defined in other files of the same package */
/* 1 when the first line of code is "start;" (comments and #import / #from lines are skipped) */
int as2_is_strict(const char *src, unsigned len);

#endif
