/* forced into the AstraScript sources on ARM32 (-include): clang has no __builtin_setjmp there */
#ifndef AS_ARM32_JMP_H
#define AS_ARM32_JMP_H
int  as_setjmp(void *buf);
void as_longjmp(void *buf, int v) __attribute__((noreturn));
#define __builtin_setjmp(b)       as_setjmp((void *)(b))
#define __builtin_longjmp(b, v)   as_longjmp((void *)(b), (v))
#endif
