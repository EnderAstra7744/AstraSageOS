#ifndef _AS_SETJMP_H
#define _AS_SETJMP_H
#if defined(__i386__)
typedef unsigned int jmp_buf[8];
#else
typedef unsigned long long jmp_buf[24];
#endif
typedef jmp_buf sigjmp_buf;
int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val) __attribute__((noreturn));
#define sigsetjmp(e, m) setjmp(e)
#define siglongjmp(e, v) longjmp(e, v)
#endif
