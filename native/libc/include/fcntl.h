#ifndef _AS_FCNTL_H
#define _AS_FCNTL_H
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 0x40
#define O_APPEND 0x400
#define O_TRUNC 0x200
#define AT_FDCWD (-100)
#define AT_SYMLINK_NOFOLLOW 0x100
#define AT_REMOVEDIR 0x200
int open(const char *p, int f, ...);
#endif
