#ifndef MEUOS_X86INTRIN_H
#define MEUOS_X86INTRIN_H

/* Minimal stub for x86intrin.h.  mcc does not support SIMD intrinsics;
 * this header provides type definitions only so that source files that
 * unconditionally include it can compile.  Software needing actual SIMD
 * support should use a fallback path or be compiled with gcc. */

typedef long long __m128i __attribute__((vector_size(16)));
typedef float __m128 __attribute__((vector_size(16)));
typedef double __m128d __attribute__((vector_size(16)));

/* SHA intrinsics types (no-op stubs) */
typedef int __attribute__((vector_size(16))) __v4si;

#endif
