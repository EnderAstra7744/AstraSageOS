#ifndef _AS_SYS_MMAN_H
#define _AS_SYS_MMAN_H
#include <sys/types.h>
#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_PRIVATE 2
#define MAP_SHARED 1
#define MAP_ANONYMOUS 0x20
#define MAP_FAILED ((void *)-1)
void *mmap(void *a, size_t l, int p, int f, int fd, off_t o);
int munmap(void *a, size_t l);
#endif
