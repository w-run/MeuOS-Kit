#ifndef MEUOS_MATH_H
#define MEUOS_MATH_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Minimal math.h: declarations for common C math functions.
 * Implementations are in src/stdlib/math.c (soft-float, simple versions). */

#define HUGE_VAL (__builtin_huge_val())
#define HUGE_VALF (__builtin_huge_valf())
#define INFINITY (__builtin_inff())
#define NAN (__builtin_nanf(""))

#define M_PI 3.14159265358979323846
#define M_E 2.71828182845904523536

#define FP_NAN 0
#define FP_INFINITE 1
#define FP_ZERO 2
#define FP_SUBNORMAL 3
#define FP_NORMAL 4

double floor(double);
double ceil(double);
double fabs(double);
double sqrt(double);
double pow(double, double);
double log(double);
double log2(double);
double log10(double);
double exp(double);
double sin(double);
double cos(double);
double tan(double);
double asin(double);
double acos(double);
double atan(double);
double atan2(double, double);
double fmod(double, double);
double round(double);
double trunc(double);
long lround(double);

float floorf(float);
float ceilf(float);
float fabsf(float);
float sqrtf(float);

int isnan(double);
int isinf(double);
int isfinite(double);
int fpclassify(double);

#ifdef __cplusplus
}
#endif

#endif
double ldexp(double, int);
double frexp(double, int *);
double modf(double, double *);
float ldexpf(float, int);
