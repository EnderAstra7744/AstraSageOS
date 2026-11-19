#ifndef _AS_TIME_H
#define _AS_TIME_H
#include <stddef.h>
typedef long time_t;
typedef long clock_t;
#define CLOCKS_PER_SEC 1000
struct tm { int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst; long tm_gmtoff; const char *tm_zone; };
struct timespec { time_t tv_sec; long tv_nsec; };
time_t time(time_t *t); clock_t clock(void);
struct tm *gmtime(const time_t *t); struct tm *localtime(const time_t *t);
struct tm *gmtime_r(const time_t *t, struct tm *o); struct tm *localtime_r(const time_t *t, struct tm *o);
time_t mktime(struct tm *t); time_t timegm(struct tm *t);
size_t strftime(char *s, size_t n, const char *f, const struct tm *t);
char *strptime(const char *s, const char *f, struct tm *t);
double difftime(time_t a, time_t b);
char *ctime(const time_t *t); char *asctime(const struct tm *t);
#endif
