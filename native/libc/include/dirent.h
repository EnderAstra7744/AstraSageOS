#ifndef _AS_DIRENT_H
#define _AS_DIRENT_H
#include <sys/types.h>
struct dirent { unsigned d_ino; unsigned char d_type; char d_name[256]; };
typedef struct _AS_DIR DIR;
DIR *opendir(const char *p); struct dirent *readdir(DIR *d); int closedir(DIR *d); int dirfd(DIR *d);
int scandir(const char *dir, struct dirent ***list, int (*filter)(const struct dirent *), int (*cmp)(const struct dirent **, const struct dirent **));
int alphasort(const struct dirent **a, const struct dirent **b);
#define DT_DIR 4
#define DT_REG 8
#endif
