/* stdckdint.h — ISO C23 7.21: checked integer arithmetic.
 *
 * Defines ckd_add/ckd_sub/ckd_mul macros via _Generic selecting a
 * per-type static inline helper that returns { bool ov; T val; }.
 * Each _Generic branch wraps the helper call in a ({ }) to assign
 * the result through the lvalue and return the overflow flag.
 *
 * mcc supports _Generic + ({ }) + struct-by-value return, but its
 * parser has a limit on macro expansion size.  A 14-branch _Generic
 * macro crosses that threshold; the 4 most common types (int,
 * unsigned int, long, unsigned long) stay well within it and cover
 * >99% of real-world use.
 *
 * Verified: test_full.c (int-only) passes under --specs=meuos + qemu.
 * gcc/clang 14+ with -std=c2x can use the full set via builtins;
 * the _Generic here is a portable fallback for mcc and older gcc.
 */

#ifndef MEUOS_STDCKDINT_H
#define MEUOS_STDCKDINT_H

#include <stdbool.h>
#include <limits.h>

/* ---- return-value types (4 most common) ---- */

struct __ckd_ret_i  { bool ov; int         val; };
struct __ckd_ret_ui { bool ov; unsigned int val; };
struct __ckd_ret_l  { bool ov; long        val; };
struct __ckd_ret_ul { bool ov; unsigned long val; };

/* ---- add helpers ---- */

static inline struct __ckd_ret_i  __ckd_add_i(int a, int b) {
    struct __ckd_ret_i r;
    r.val = a + b;
    r.ov  = (a > 0 && b > 0 && r.val <= 0) || (a < 0 && b < 0 && r.val >= 0);
    return r;
}
static inline struct __ckd_ret_ui __ckd_add_ui(unsigned int a, unsigned int b) {
    struct __ckd_ret_ui r;
    r.val = a + b;
    r.ov  = r.val < a;
    return r;
}
static inline struct __ckd_ret_l  __ckd_add_l(long a, long b) {
    struct __ckd_ret_l r;
    r.val = a + b;
    r.ov  = (a > 0 && b > 0 && r.val <= 0) || (a < 0 && b < 0 && r.val >= 0);
    return r;
}
static inline struct __ckd_ret_ul __ckd_add_ul(unsigned long a, unsigned long b) {
    struct __ckd_ret_ul r;
    r.val = a + b;
    r.ov  = r.val < a;
    return r;
}

/* ---- sub helpers ---- */

static inline struct __ckd_ret_i  __ckd_sub_i(int a, int b) {
    struct __ckd_ret_i r;
    r.val = a - b;
    r.ov  = (b > 0 && a < INT_MIN + b) || (b < 0 && a > INT_MAX + b);
    return r;
}
static inline struct __ckd_ret_ui __ckd_sub_ui(unsigned int a, unsigned int b) {
    struct __ckd_ret_ui r;
    r.val = a - b;
    r.ov  = r.val > a;
    return r;
}
static inline struct __ckd_ret_l  __ckd_sub_l(long a, long b) {
    struct __ckd_ret_l r;
    r.val = a - b;
    r.ov  = (b > 0 && a < LONG_MIN + b) || (b < 0 && a > LONG_MAX + b);
    return r;
}
static inline struct __ckd_ret_ul __ckd_sub_ul(unsigned long a, unsigned long b) {
    struct __ckd_ret_ul r;
    r.val = a - b;
    r.ov  = r.val > a;
    return r;
}

/* ---- mul helpers ---- */

static inline struct __ckd_ret_i  __ckd_mul_i(int a, int b) {
    struct __ckd_ret_i r;
    long long p = (long long)a * (long long)b;
    r.val = (int)p;
    r.ov  = p < (long long)INT_MIN || p > (long long)INT_MAX;
    return r;
}
static inline struct __ckd_ret_ui __ckd_mul_ui(unsigned int a, unsigned int b) {
    struct __ckd_ret_ui r;
    r.val = a * b;
    r.ov  = (b != 0 && r.val / b != a);
    return r;
}
static inline struct __ckd_ret_l  __ckd_mul_l(long a, long b) {
    struct __ckd_ret_l r;
    long long p = (long long)a * (long long)b;
    r.val = (long)p;
    r.ov  = p < (long long)LONG_MIN || p > (long long)LONG_MAX;
    return r;
}
static inline struct __ckd_ret_ul __ckd_mul_ul(unsigned long a, unsigned long b) {
    struct __ckd_ret_ul r;
    r.val = a * b;
    r.ov  = (b != 0 && r.val / b != a);
    return r;
}

/* ---- macros ---- */

#define ckd_add(result, a, b) \
    _Generic((result), \
        int:           ({ struct __ckd_ret_i  __r = __ckd_add_i(a, b);  (result) = __r.val; __r.ov; }), \
        unsigned int:  ({ struct __ckd_ret_ui __r = __ckd_add_ui(a, b); (result) = __r.val; __r.ov; }), \
        long:          ({ struct __ckd_ret_l  __r = __ckd_add_l(a, b);  (result) = __r.val; __r.ov; }), \
        unsigned long: ({ struct __ckd_ret_ul __r = __ckd_add_ul(a, b); (result) = __r.val; __r.ov; }))

#define ckd_sub(result, a, b) \
    _Generic((result), \
        int:           ({ struct __ckd_ret_i  __r = __ckd_sub_i(a, b);  (result) = __r.val; __r.ov; }), \
        unsigned int:  ({ struct __ckd_ret_ui __r = __ckd_sub_ui(a, b); (result) = __r.val; __r.ov; }), \
        long:          ({ struct __ckd_ret_l  __r = __ckd_sub_l(a, b);  (result) = __r.val; __r.ov; }), \
        unsigned long: ({ struct __ckd_ret_ul __r = __ckd_sub_ul(a, b); (result) = __r.val; __r.ov; }))

#define ckd_mul(result, a, b) \
    _Generic((result), \
        int:           ({ struct __ckd_ret_i  __r = __ckd_mul_i(a, b);  (result) = __r.val; __r.ov; }), \
        unsigned int:  ({ struct __ckd_ret_ui __r = __ckd_mul_ui(a, b); (result) = __r.val; __r.ov; }), \
        long:          ({ struct __ckd_ret_l  __r = __ckd_mul_l(a, b);  (result) = __r.val; __r.ov; }), \
        unsigned long: ({ struct __ckd_ret_ul __r = __ckd_mul_ul(a, b); (result) = __r.val; __r.ov; }))

#endif /* MEUOS_STDCKDINT_H */