#ifndef _AS_ICONV_H
#define _AS_ICONV_H
#include <stddef.h>
typedef void *iconv_t;
iconv_t iconv_open(const char *to, const char *from);
size_t iconv(iconv_t cd, char **in, size_t *inleft, char **out, size_t *outleft);
int iconv_close(iconv_t cd);
#endif
