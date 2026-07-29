#ifndef MEUOS_STDINT_H
#define MEUOS_STDINT_H

#include <stddef.h>

typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
#if defined(__i386__)
typedef long long int64_t;
typedef unsigned long long uint64_t;
typedef int intptr_t;
typedef unsigned int uintptr_t;
#else
typedef long int64_t;
typedef unsigned long uint64_t;
typedef long intptr_t;
typedef unsigned long uintptr_t;
#endif
typedef int8_t int_least8_t;
typedef uint8_t uint_least8_t;
typedef int16_t int_least16_t;
typedef uint16_t uint_least16_t;
typedef int32_t int_least32_t;
typedef uint32_t uint_least32_t;
typedef int64_t int_least64_t;
typedef uint64_t uint_least64_t;
typedef long int_fast8_t;
typedef unsigned long uint_fast8_t;
typedef long int_fast16_t;
typedef unsigned long uint_fast16_t;
typedef long int_fast32_t;
typedef unsigned long uint_fast32_t;
typedef long int_fast64_t;
typedef unsigned long uint_fast64_t;

#define SIZE_MAX ((size_t)-1)

#define INT8_MIN (-128)
#define INT8_MAX 127
#define UINT8_MAX 255
#define INT16_MIN (-32768)
#define INT16_MAX 32767
#define UINT16_MAX 65535
#define INT32_MIN (-2147483647 - 1)
#define INT32_MAX 2147483647
#define UINT32_MAX 4294967295U
#define INT64_MIN (-9223372036854775807LL - 1LL)
#define INT64_MAX 9223372036854775807LL
#define UINT64_MAX 18446744073709551615ULL
#if defined(__i386__)
#define INTPTR_MIN INT32_MIN
#define INTPTR_MAX INT32_MAX
#define UINTPTR_MAX UINT32_MAX
#else
#define INTPTR_MIN INT64_MIN
#define INTPTR_MAX INT64_MAX
#define UINTPTR_MAX UINT64_MAX
#endif

/* Maximum-width integer types (C99 §7.18.1.5) */
#if defined(__i386__)
typedef long long intmax_t;
typedef unsigned long long uintmax_t;
#define INTMAX_MIN (-9223372036854775807LL - 1LL)
#define INTMAX_MAX (9223372036854775807LL)
#define UINTMAX_MAX (18446744073709551615ULL)
#define INTMAX_C(x) (x ## LL)
#define UINTMAX_C(x) (x ## ULL)
#else
typedef long intmax_t;
typedef unsigned long uintmax_t;
#define INTMAX_MIN (-9223372036854775807L - 1)
#define INTMAX_MAX (9223372036854775807L)
#define UINTMAX_MAX (18446744073709551615UL)
#define INTMAX_C(x) (x ## L)
#define UINTMAX_C(x) (x ## UL)
#endif

/* intptr_t / uintptr_t already defined in the arch-conditional block above. */

#endif
