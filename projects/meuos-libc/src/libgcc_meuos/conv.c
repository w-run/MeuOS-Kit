/* conv.c — integer<->floating-point conversions (libgcc ABI).
 *
 * __floatsisf/__floatsidf/__floatunsisf/__floatunsidf  (si -> float/double)
 * __floatdisf/__floatdidf/__floatundisf/__floatundidf  (di -> float/double)
 * __fixsfsi/__fixdfsi/__fixsfdi/__fixdfdi               (float/double -> signed)
 * __fixunssfsi/__fixunsdfsi/__fixunssfdi/__fixunsdfdi   (float/double -> unsigned)
 * __extendsfdf2/__truncdfsf2                           (float<->double)
 *
 * On the targets libgcc-meuos targets, mcc and the host C compiler inline
 * these conversions natively (see mcc backend), so plain C casts here do NOT
 * re-emit a call to the very helper being defined (verified: mcc produces no
 * undefined references for these).  Keeping them as small casts keeps this
 * archive toolchain-agnostic across x86_64/aarch64/riscv64/loongarch64.
 */

#include "libgcc_int.h"

/* ---- si (32-bit signed) -> float/double ---- */
float __floatsisf(int i) { return (float)i; }
double __floatsidf(int i) { return (double)i; }
/* ---- usi (32-bit unsigned) -> float/double ---- */
float __floatunsisf(unsigned i) { return (float)i; }
double __floatunsidf(unsigned i) { return (double)i; }

/* ---- di (64-bit signed) -> float/double ---- */
float __floatdisf(long long i) { return (float)i; }
double __floatdidf(long long i) { return (double)i; }
/* ---- udi (64-bit unsigned) -> float/double ---- */
float __floatundisf(unsigned long long i) { return (float)i; }
double __floatundidf(unsigned long long i) { return (double)i; }

/* ---- float -> signed int / long long ---- */
int __fixsfsi(float x) { return (int)x; }
int __fixdfsi(double x) { return (int)x; }
long long __fixsfdi(float x) { return (long long)x; }
long long __fixdfdi(double x) { return (long long)x; }

/* ---- float -> unsigned int / long long ---- */
unsigned __fixunssfsi(float x) { return (unsigned)x; }
unsigned __fixunsdfsi(double x) { return (unsigned)x; }
unsigned long long __fixunssfdi(float x) { return (unsigned long long)x; }
unsigned long long __fixunsdfdi(double x) { return (unsigned long long)x; }

/* ---- float <-> double widening/narrowing ---- */
double __extendsfdf2(float x) { return (double)x; }
float __truncdfsf2(double x) { return (float)x; }
