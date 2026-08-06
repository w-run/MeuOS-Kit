/* stdckdint.h — ISO C23 7.21: checked integer arithmetic.
 *
 * Defines ckd_add/ckd_sub/ckd_mul macros for integer overflow detection.
 *
 * Design: wraps the whole macro body in a GNU statement-expression ({ })
 * so the address-of-result (&(lval)) can be taken *once* before entering
 * _Generic, then reused inside each type branch via a pointer dereference.
 * This avoids mcc's pointer-compatibility complaint (E0003) that arises
 * when &(lvalue) appears directly inside a _Generic selected expression.
 * mcc supports ({ }) and _Generic separately, and the combination works
 * as tested.
 */

#ifndef MEUOS_STDCKDINT_H
#define MEUOS_STDCKDINT_H

#include <stdbool.h>
#include <limits.h>

/* ---- ckd_add ---- */

#define ckd_add(result, a, b) \
    ({ \
        __typeof__(&(result)) __ckd_p = &(result); \
        (void)sizeof((result) = (a) + (b)); \
        _Generic((result), \
            signed char: ({ \
                signed char __v = (signed char)((int)(a) + (int)(b)); \
                *__ckd_p = __v; \
                ((a) > 0 && (b) > 0 && __v <= 0) \
                || ((a) < 0 && (b) < 0 && __v >= 0); }), \
            short: ({ \
                short __v = (short)((int)(a) + (int)(b)); \
                *__ckd_p = __v; \
                ((a) > 0 && (b) > 0 && __v <= 0) \
                || ((a) < 0 && (b) < 0 && __v >= 0); }), \
            int: ({ \
                int __v = (a) + (b); \
                *__ckd_p = __v; \
                ((a) > 0 && (b) > 0 && __v <= 0) \
                || ((a) < 0 && (b) < 0 && __v >= 0); }), \
            long: ({ \
                long __v = (a) + (b); \
                *__ckd_p = __v; \
                ((a) > 0 && (b) > 0 && __v <= 0) \
                || ((a) < 0 && (b) < 0 && __v >= 0); }), \
            long long: ({ \
                long long __v = (a) + (b); \
                *__ckd_p = __v; \
                ((a) > 0 && (b) > 0 && __v <= 0) \
                || ((a) < 0 && (b) < 0 && __v >= 0); }), \
            unsigned char: ({ \
                unsigned __tmp = (unsigned)(a) + (unsigned)(b); \
                *__ckd_p = (unsigned char)__tmp; \
                __tmp > 0xFFu; }), \
            unsigned short: ({ \
                unsigned __tmp = (unsigned)(a) + (unsigned)(b); \
                *__ckd_p = (unsigned short)__tmp; \
                __tmp > 0xFFFFu; }), \
            unsigned int: ({ \
                unsigned int __v = (a) + (b); \
                *__ckd_p = __v; \
                __v < (a); }), \
            unsigned long: ({ \
                unsigned long __v = (a) + (b); \
                *__ckd_p = __v; \
                __v < (a); }), \
            unsigned long long: ({ \
                unsigned long long __v = (a) + (b); \
                *__ckd_p = __v; \
                __v < (a); })) \
    })

/* ---- ckd_sub ---- */

#define ckd_sub(result, a, b) \
    ({ \
        __typeof__(&(result)) __ckd_p = &(result); \
        (void)sizeof((result) = (a) - (b)); \
        _Generic((result), \
            signed char: ({ \
                signed char __v = (signed char)((int)(a) - (int)(b)); \
                *__ckd_p = __v; \
                ((b) > 0 && (a) < SCHAR_MIN + (b)) \
                || ((b) < 0 && (a) > SCHAR_MAX + (b)); }), \
            short: ({ \
                short __v = (short)((int)(a) - (int)(b)); \
                *__ckd_p = __v; \
                ((b) > 0 && (a) < SHRT_MIN + (b)) \
                || ((b) < 0 && (a) > SHRT_MAX + (b)); }), \
            int: ({ \
                int __v = (a) - (b); \
                *__ckd_p = __v; \
                ((b) > 0 && (a) < INT_MIN + (b)) \
                || ((b) < 0 && (a) > INT_MAX + (b)); }), \
            long: ({ \
                long __v = (a) - (b); \
                *__ckd_p = __v; \
                ((b) > 0 && (a) < LONG_MIN + (b)) \
                || ((b) < 0 && (a) > LONG_MAX + (b)); }), \
            long long: ({ \
                long long __v = (a) - (b); \
                *__ckd_p = __v; \
                ((b) > 0 && (a) < LLONG_MIN + (b)) \
                || ((b) < 0 && (a) > LLONG_MAX + (b)); }), \
            unsigned char: ({ \
                unsigned __tmp = (unsigned)(a) - (unsigned)(b); \
                *__ckd_p = (unsigned char)__tmp; \
                __tmp > 0xFFu; }), \
            unsigned short: ({ \
                unsigned __tmp = (unsigned)(a) - (unsigned)(b); \
                *__ckd_p = (unsigned short)__tmp; \
                __tmp > 0xFFFFu; }), \
            unsigned int: ({ \
                unsigned int __v = (a) - (b); \
                *__ckd_p = __v; \
                __v > (a); }), \
            unsigned long: ({ \
                unsigned long __v = (a) - (b); \
                *__ckd_p = __v; \
                __v > (a); }), \
            unsigned long long: ({ \
                unsigned long long __v = (a) - (b); \
                *__ckd_p = __v; \
                __v > (a); })) \
    })

/* ---- ckd_mul ---- */

#define ckd_mul(result, a, b) \
    ({ \
        __typeof__(&(result)) __ckd_p = &(result); \
        (void)sizeof((result) = (a) * (b)); \
        _Generic((result), \
            signed char: ({ \
                int __p = (int)(a) * (int)(b); \
                *__ckd_p = (signed char)__p; \
                __p < SCHAR_MIN || __p > SCHAR_MAX; }), \
            short: ({ \
                int __p = (int)(a) * (int)(b); \
                *__ckd_p = (short)__p; \
                __p < SHRT_MIN || __p > SHRT_MAX; }), \
            int: ({ \
                long long __p = (long long)(a) * (long long)(b); \
                *__ckd_p = (int)__p; \
                __p < (long long)INT_MIN || __p > (long long)INT_MAX; }), \
            long: ({ \
                long long __p = (long long)(a) * (long long)(b); \
                *__ckd_p = (long)__p; \
                __p < (long long)LONG_MIN || __p > (long long)LONG_MAX; }), \
            long long: ({ \
                long long __v = (a) * (b); \
                *__ckd_p = __v; \
                (a) == 0 || (b) == 0 ? false : \
                (a) == -1 && (b) == LLONG_MIN ? true : \
                (b) == -1 && (a) == LLONG_MIN ? true : \
                __v / (b) != (a); }), \
            unsigned char: ({ \
                unsigned __p = (unsigned)(a) * (unsigned)(b); \
                *__ckd_p = (unsigned char)__p; \
                __p > 0xFFu; }), \
            unsigned short: ({ \
                unsigned __p = (unsigned)(a) * (unsigned)(b); \
                *__ckd_p = (unsigned short)__p; \
                __p > 0xFFFFu; }), \
            unsigned int: ({ \
                unsigned int __v = (a) * (b); \
                *__ckd_p = __v; \
                (b) != 0 && __v / (b) != (a); }), \
            unsigned long: ({ \
                unsigned long __v = (a) * (b); \
                *__ckd_p = __v; \
                (b) != 0 && __v / (b) != (a); }), \
            unsigned long long: ({ \
                unsigned long long __v = (a) * (b); \
                *__ckd_p = __v; \
                (b) != 0 && __v / (b) != (a); })) \
    })

#endif /* MEUOS_STDCKDINT_H */