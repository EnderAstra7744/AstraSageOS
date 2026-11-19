/* compiler runtime helpers (the freestanding build has no libgcc / compiler-rt): 64-bit division, 64-bit <-> double, memcpy/memset entry points */
extern void *memcpy(void *, const void *, unsigned long); extern void *memmove(void *, const void *, unsigned long); extern void *memset(void *, int, unsigned long);
typedef unsigned long long u64_;
typedef long long s64_;

/* shift-and-subtract: no division instruction on 64-bit operands */
u64_ __udivmod64(u64_ n, u64_ d, u64_ *rem)
{
    u64_ q = 0, r = 0;
    if (!d) { if (rem) *rem = n; return ~0ULL; }
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1);
        if (r >= d) { r -= d; q |= 1ULL << i; }
    }
    if (rem) *rem = r;
    return q;
}
__asm__(
    ".text\n.arm\n.globl __aeabi_uldivmod\n"
    "__aeabi_uldivmod:\n"
    "    push {r4, lr}\n"
    "    sub sp, sp, #16\n"
    "    add r4, sp, #8\n"
    "    str r4, [sp]\n"                     /* third argument: &remainder */
    "    bl __udivmod64\n"
    "    ldr r2, [sp, #8]\n"
    "    ldr r3, [sp, #12]\n"
    "    add sp, sp, #16\n"
    "    pop {r4, pc}\n"
);
__asm__(
    ".text\n.arm\n.globl __aeabi_ldivmod\n"
    "__aeabi_ldivmod:\n"                       /* signed: divide the magnitudes, fix the signs */
    "    push {r4-r6, lr}\n"
    "    sub sp, sp, #16\n"
    "    asr r4, r1, #31\n"                    /* r4 = sign of n */
    "    asr r5, r3, #31\n"                    /* r5 = sign of d */
    "    eor r5, r5, r4\n"                     /* r5 = sign of the quotient */
    "    cmp r4, #0\n    beq 1f\n    rsbs r0, r0, #0\n    rsc r1, r1, #0\n"
    "1:  cmp r3, #0\n    bge 2f\n    rsbs r2, r2, #0\n    rsc r3, r3, #0\n"
    "2:  add r6, sp, #8\n    str r6, [sp]\n"
    "    bl __udivmod64\n"
    "    ldr r2, [sp, #8]\n    ldr r3, [sp, #12]\n"
    "    cmp r5, #0\n    beq 3f\n    rsbs r0, r0, #0\n    rsc r1, r1, #0\n"
    "3:  cmp r4, #0\n    beq 4f\n    rsbs r2, r2, #0\n    rsc r3, r3, #0\n"
    "4:  add sp, sp, #16\n"
    "    pop {r4-r6, pc}\n"
);

double __aeabi_l2d(s64_ v)  { return (double)(int)(v >> 32) * 4294967296.0 + (double)(unsigned)v; }
double __aeabi_ul2d(u64_ v) { return (double)(unsigned)(v >> 32) * 4294967296.0 + (double)(unsigned)v; }
s64_ __aeabi_d2lz(double d)
{
    int neg = d < 0;
    if (neg) d = -d;
    unsigned hi = (unsigned)(d / 4294967296.0);
    unsigned lo = (unsigned)(d - (double)hi * 4294967296.0);
    s64_ v = (s64_)(((u64_)hi << 32) | lo);
    return neg ? -v : v;
}
u64_ __aeabi_d2ulz(double d)
{
    if (d < 0) return 0;
    unsigned hi = (unsigned)(d / 4294967296.0);
    unsigned lo = (unsigned)(d - (double)hi * 4294967296.0);
    return ((u64_)hi << 32) | lo;
}

void __aeabi_memcpy(void *d, const void *s, unsigned n)  { memcpy(d, s, n); }
void __aeabi_memcpy4(void *d, const void *s, unsigned n) { memcpy(d, s, n); }
void __aeabi_memcpy8(void *d, const void *s, unsigned n) { memcpy(d, s, n); }
void __aeabi_memmove(void *d, const void *s, unsigned n)  { memmove(d, s, n); }
void __aeabi_memmove4(void *d, const void *s, unsigned n) { memmove(d, s, n); }
void __aeabi_memmove8(void *d, const void *s, unsigned n) { memmove(d, s, n); }
void __aeabi_memset(void *d, unsigned n, int c)  { memset(d, c, n); }
void __aeabi_memset4(void *d, unsigned n, int c) { memset(d, c, n); }
void __aeabi_memset8(void *d, unsigned n, int c) { memset(d, c, n); }
void __aeabi_memclr(void *d, unsigned n)  { memset(d, 0, n); }
void __aeabi_memclr4(void *d, unsigned n) { memset(d, 0, n); }
void __aeabi_memclr8(void *d, unsigned n) { memset(d, 0, n); }

float __aeabi_l2f(s64_ v)  { return (float)__aeabi_l2d(v); }
float __aeabi_ul2f(u64_ v) { return (float)__aeabi_ul2d(v); }
s64_ __aeabi_f2lz(float f)  { return __aeabi_d2lz((double)f); }
u64_ __aeabi_f2ulz(float f) { return __aeabi_d2ulz((double)f); }
