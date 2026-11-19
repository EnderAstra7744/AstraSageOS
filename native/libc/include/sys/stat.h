#ifndef _AS_SYS_STAT_H
#define _AS_SYS_STAT_H
#include <sys/types.h>
#include <time.h>
struct stat { mode_t st_mode; off_t st_size; time_t st_mtime; time_t st_atime; time_t st_ctime; unsigned st_ino; unsigned st_dev; int st_nlink; };
#define S_IFMT 0170000
#define S_IFDIR 0040000
#define S_IFREG 0100000
#define S_IRWXU 0700
#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IXUSR 0100
#define S_IRWXG 070
#define S_IRWXO 07
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define AT_SYMLINK_NOFOLLOW 0x100
#define AT_REMOVEDIR 0x200
int fstatat(int dirfd, const char *p, struct stat *s, int flags);
int lstat(const char *p, struct stat *s); int fstat(int fd, struct stat *s); int chmod(const char *p, mode_t m); int stat(const char *p, struct stat *s); int mkdir(const char *p, mode_t m);
#endif
