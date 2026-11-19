#ifndef _AS_MATH_H
#define _AS_MATH_H
#define M_PI 3.14159265358979323846
#define M_PI_2 1.57079632679489661923
#define M_PI_4 0.78539816339744830962
#define M_1_PI 0.31830988618379067154
#define M_2_PI 0.63661977236758134308
#define M_LN10 2.30258509299404568402
#define M_E 2.7182818284590452354
#define M_SQRT2 1.41421356237309504880
#define M_LN2 0.69314718055994530942
#define HUGE_VAL (__builtin_huge_val())
#define INFINITY (__builtin_inff())
#define NAN (__builtin_nanf(""))
#define isnan(x) __builtin_isnan(x)
#define isinf(x) __builtin_isinf(x)
#define isfinite(x) __builtin_isfinite(x)
double sqrt(double); double floor(double); double ceil(double); double fabs(double); double fmod(double, double);
double pow(double, double); double exp(double); double log(double); double log10(double); double log2(double);
double sin(double); double cos(double); double tan(double); double atan(double); double atan2(double, double);
double asin(double); double acos(double); double round(double); double trunc(double); double rint(double);
double fmin(double, double); double fmax(double, double); double hypot(double, double); double ldexp(double, int); double frexp(double, int *);
double modf(double, double *); double cbrt(double); double copysign(double, double);
float sqrtf(float); float floorf(float); float ceilf(float); float fabsf(float); float powf(float, float); float fmodf(float, float);
long lroundf(float); float sinf(float); float cosf(float); float roundf(float); float truncf(float); float logf(float); float expf(float);
long lround(double); long lrint(double);
#endif
