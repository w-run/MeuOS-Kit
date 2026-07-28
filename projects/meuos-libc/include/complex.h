#ifndef _COMPLEX_H
#define _COMPLEX_H

/* C99/C11 complex arithmetic support.
 * mcc has _Complex as a keyword (T_COMPLEX).
 * This header provides convenience macros and declarations. */

#define complex _Complex

/* Define _Complex_I and I.
 * On GCC/Clang, _Complex_I is a compiler built-in.
 * On mcc, _Complex_I is not predefined; use __extension__ approach. */
#if defined(__GNUC__) && !defined(__MCC__)
#  define _Complex_I (__extension__ 1.0iF)
#else
   /* mcc: _Complex_I not available as a built-in.
    * Define I using compound literal workaround. */
#  define _Complex_I ((float _Complex)(0.0f + 1.0f * (float _Complex)1.0f))
#endif
#define I _Complex_I

/* Basic complex operations.
 * When compiled by GCC, these use __builtin_ variants.
 * When compiled by mcc, function implementations are in libc. */

#ifdef __GNUC__
#define creal(x)   __builtin_creal(x)
#define cimag(x)   __builtin_cimag(x)
#define conj(x)    __builtin_conj(x)
#else
double       creal(double complex);
double       cimag(double complex);
double complex conj(double complex);
float        crealf(float complex);
float        cimagf(float complex);
float complex conjf(float complex);
#endif

#endif
