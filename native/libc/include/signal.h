#ifndef _AS_SIGNAL_H
#define _AS_SIGNAL_H
typedef int sig_atomic_t;
typedef void (*sighandler_t)(int);
#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIGINT 2
#define SIGPIPE 13
#define SIGTERM 15
sighandler_t signal(int sig, sighandler_t h);
int raise(int sig);
#endif
