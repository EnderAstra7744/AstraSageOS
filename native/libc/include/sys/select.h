#ifndef _AS_SYS_SELECT_H
#define _AS_SYS_SELECT_H
#include <sys/time.h>
typedef struct { unsigned long bits[32]; } fd_set;
#define FD_SETSIZE 1024
#define FD_ZERO(s) do { int _i; for (_i = 0; _i < 32; _i++) (s)->bits[_i] = 0; } while (0)
#define FD_SET(fd, s) ((s)->bits[(fd) / 32] |= 1UL << ((fd) % 32))
#define FD_CLR(fd, s) ((s)->bits[(fd) / 32] &= ~(1UL << ((fd) % 32)))
#define FD_ISSET(fd, s) (((s)->bits[(fd) / 32] >> ((fd) % 32)) & 1)
int select(int n, fd_set *r, fd_set *w, fd_set *e, struct timeval *tv);
#endif
