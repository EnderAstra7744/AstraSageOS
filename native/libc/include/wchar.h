#ifndef _AS_WCHAR_H
#define _AS_WCHAR_H
#include <stddef.h>
typedef int wint_t;
typedef struct { int dummy; } mbstate_t;
#define WEOF (-1)
size_t wcslen(const wchar_t *s);
#endif
