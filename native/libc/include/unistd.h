#ifndef _AS_UNISTD_H
#define _AS_UNISTD_H
#include <stddef.h>
#include <sys/types.h>
int close(int fd); ssize_t read(int fd, void *b, size_t n); ssize_t write(int fd, const void *b, size_t n);
off_t lseek(int fd, off_t o, int w); int rmdir(const char *p); int dup(int fd); int unlink(const char *p); int access(const char *p, int m); char *getcwd(char *b, size_t n);
ssize_t pread(int fd, void *b, size_t n, off_t o); ssize_t pwrite(int fd, const void *b, size_t n, off_t o);
int unlinkat(int dirfd, const char *p, int flags);
unsigned sleep(unsigned s); int usleep(unsigned us); long sysconf(int n); int isatty(int fd);
#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#include <getopt.h>
#endif
static inline int getpid(void) { return 1; }
