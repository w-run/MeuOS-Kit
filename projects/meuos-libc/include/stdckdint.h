/* stdckdint.h — ISO C23 7.21: checked integer arithmetic.
 *
 * Defines ckd_add/ckd_sub/ckd_mul macros that dispatch via _Generic on
 * the *value* type of the result lvalue.  Each _Generic alternative names
 * an inline helper function that takes the operands by value and returns
 * a struct { bool ov; T val; } — the comma-expression wrapper assigns
 * result from the return value, then evaluates to the overflow flag.
 *
 * mcc supports _Generic and inline + struct-by-value return, but does NOT
 * support GNU statement-expressions (({...})), so the per-type code lives
 * in named static inline functions rather than inlined in the _Generic
 * branch.
 */

#ifndef MEUOS_STDCKDINT_H
#define MEUOS_STDCKDINT_H

#include <stdbool.h>
#include <limits.h>

/* ---- return-by-value helper types ---- */

struct __ckd_sc { bool ov; signed char val; };
struct __ckd_s  { bool ov; short       val; };
struct __ckd_i  { bool ov; int         val; };
struct __ckd_l  { bool ov; long        val; };
struct __ckd_ll { bool ov; long long   val; };
struct __ckd_uc { bool ov; unsigned char  val; };
struct __ckd_us { bool ov; unsigned short val; };
struct __ckd_ui { bool ov; unsigned int   val; };
struct __ckd_ul { bool ov; unsigned long  val; };
struct __ckd_ull{ bool ov; unsigned long long val; };

/* ---- add helpers ---- */
static inline struct __ckd_sc __ckd_add_sc(signed char a, signed char b) {
    struct __ckd_sc r;
    r.val = (signed char)((int)a + (int)b);
    r.ov  = (a > 0 && b > 0 && r.val <= 0) || (a < 0 && b < 0 && r.val >= 0);
    return r;
}
static inline struct __ckd_s __ckd_add_s(short a, short b) {
    struct __ckd_s r;
    r.val = (short)((int)a + (int)b);
    r.ov  = (a > 0 && b > 0 && r.val <= 0) || (a < 0 && b < 0 && r.val >= 0);
    return r;
}
static inline struct __ckd_i __ckd_add_i(int a, int b) {
    struct __ckd_i r;
    r.val = a + b;
    r.ov  = (a > 0 && b > 0 && r.val <= 0) || (a < 0 && b < 0 && r.val >= 0);
    return r;
}
static inline struct __ckd_l __ckd_add_l(long a, long b) {
    struct __ckd_l r;
    r.val = a + b;
    r.ov  = (a > 0 && b > 0 && r.val <= 0) || (a < 0 && b < 0 && r.val >= 0);
    return r;
}
static inline struct __ckd_ll __ckd_add_ll(long long a, long long b) {
    struct __ckd_ll r;
    r.val = a + b;
    r.ov  = (a > 0 && b > 0 && r.val <= 0) || (a < 0 && b < 0 && r.val >= 0);
    return r;
}
static inline struct __ckd_uc __ckd_add_uc(unsigned char a, unsigned char b) {
    struct __ckd_uc r;
    unsigned t = (unsigned)a + (unsigned)b;
    r.val = (unsigned char)t;
    r.ov  = t > 0xFFu;
    return r;
}
static inline struct __ckd_us __ckd_add_us(unsigned short a, unsigned short b) {
    struct __ckd_us r;
    unsigned t = (unsigned)a + (unsigned)b;
    r.val = (unsigned short)t;
    r.ov  = t > 0xFFFFu;
    return r;
}
static inline struct __ckd_ui __ckd_add_ui(unsigned int a, unsigned int b) {
    struct __ckd_ui r;
    r.val = a + b;
    r.ov  = r.val < a;
    return r;
}
static inline struct __ckd_ul __ckd_add_ul(unsigned long a, unsigned long b) {
    struct __ckd_ul r;
    r.val = a + b;
    r.ov  = r.val < a;
    return r;
}
static inline struct __ckd_ull __ckd_add_ull(unsigned long long a, unsigned long long b) {
    struct __ckd_ull r;
    r.val = a + b;
    r.ov  = r.val < a;
    return r;
}

/* ---- sub helpers ---- */
static inline struct __ckd_sc __ckd_sub_sc(signed char a, signed char b) {
    struct __ckd_sc r;
    r.val = (signed char)((int)a - (int)b);
    r.ov  = (b > 0 && a < SCHAR_MIN + b) || (b < 0 && a > SCHAR_MAX + b);
    return r;
}
static inline struct __ckd_s __ckd_sub_s(short a, short b) {
    struct __ckd_s r;
    r.val = (short)((int)a - (int)b);
    r.ov  = (b > 0 && a < SHRT_MIN + b) || (b < 0 && a > SHRT_MAX + b);
    return r;
}
static inline struct __ckd_i __ckd_sub_i(int a, int b) {
    struct __ckd_i r;
    r.val = a - b;
    r.ov  = (b > 0 && a < INT_MIN + b) || (b < 0 && a > INT_MAX + b);
    return r;
}
static inline struct __ckd_l __ckd_sub_l(long a, long b) {
    struct __ckd_l r;
    r.val = a - b;
    r.ov  = (b > 0 && a < LONG_MIN + b) || (b < 0 && a > LONG_MAX + b);
    return r;
}
static inline struct __ckd_ll __ckd_sub_ll(long long a, long long b) {
    struct __ckd_ll r;
    r.val = a - b;
    r.ov  = (b > 0 && a < LLONG_MIN + b) || (b < 0 && a > LLONG_MAX + b);
    return r;
}
static inline struct __ckd_uc __ckd_sub_uc(unsigned char a, unsigned char b) {
    struct __ckd_uc r;
    unsigned t = (unsigned)a - (unsigned)b;
    r.val = (unsigned char)t;
    r.ov  = t > 0xFFu;
    return r;
}
static inline struct __ckd_us __ckd_sub_us(unsigned short a, unsigned short b) {
    struct __ckd_us r;
    unsigned t = (unsigned)a - (unsigned)b;
    r.val = (unsigned short)t;
    r.ov  = t > 0xFFFFu;
    return r;
}
static inline struct __ckd_ui __ckd_sub_ui(unsigned int a, unsigned int b) {
    struct __ckd_ui r;
    r.val = a - b;
    r.ov  = r.val > a;
    return r;
}
static inline struct __ckd_ul __ckd_sub_ul(unsigned long a, unsigned long b) {
    struct __ckd_ul r;
    r.val = a - b;
    r.ov  = r.val > a;
    return r;
}
static inline struct __ckd_ull __ckd_sub_ull(unsigned long long a, unsigned long long b) {
    struct __ckd_ull r;
    r.val = a - b;
    r.ov  = r.val > a;
    return r;
}

/* ---- mul helpers ---- */
static inline struct __ckd_sc __ckd_mul_sc(signed char a, signed char b) {
    struct __ckd_sc r;
    int p = (int)a * (int)b;
    r.val = (signed char)p;
    r.ov  = p < SCHAR_MIN || p > SCHAR_MAX;
    return r;
}
static inline struct __ckd_s __ckd_mul_s(short a, short b) {
    struct __ckd_s r;
    int p = (int)a * (int)b;
    r.val = (short)p;
    r.ov  = p < SHRT_MIN || p > SHRT_MAX;
    return r;
}
static inline struct __ckd_i __ckd_mul_i(int a, int b) {
    struct __ckd_i r;
    long long p = (long long)a * (long long)b;
    r.val = (int)p;
    r.ov  = p < (long long)INT_MIN || p > (long long)INT_MAX;
    return r;
}
static inline struct __ckd_l __ckd_mul_l(long a, long b) {
    struct __ckd_l r;
    long long p = (long long)a * (long long)b;
    r.val = (long)p;
    r.ov  = p < (long long)LONG_MIN || p > (long long)LONG_MAX;
    return r;
}
static inline struct __ckd_ll __ckd_mul_ll(long long a, long long b) {
    struct __ckd_ll r;
    r.val = a * b;
    if (a == 0 || b == 0) { r.ov = false; }
    else if (a == -1 && b == LLONG_MIN) { r.ov = true; }
    else if (b == -1 && a == LLONG_MIN) { r.ov = true; }
    else { r.ov = (r.val / b != a); }
    return r;
}
static inline struct __ckd_uc __ckd_mul_uc(unsigned char a, unsigned char b) {
    struct __ckd_uc r;
    unsigned p = (unsigned)a * (unsigned)b;
    r.val = (unsigned char)p;
    r.ov  = p > 0xFFu;
    return r;
}
static inline struct __ckd_us __ckd_mul_us(unsigned short a, unsigned short b) {
    struct __ckd_us r;
    unsigned p = (unsigned)a * (unsigned)b;
    r.val = (unsigned short)p;
    r.ov  = p > 0xFFFFu;
    return r;
}
static inline struct __ckd_ui __ckd_mul_ui(unsigned int a, unsigned int b) {
    struct __ckd_ui r;
    r.val = a * b;
    r.ov  = (b != 0 && r.val / b != a);
    return r;
}
static inline struct __ckd_ul __ckd_mul_ul(unsigned long a, unsigned long b) {
    struct __ckd_ul r;
    r.val = a * b;
    r.ov  = (b != 0 && r.val / b != a);
    return r;
}
static inline struct __ckd_ull __ckd_mul_ull(unsigned long long a, unsigned long long b) {
    struct __ckd_ull r;
    r.val = a * b;
    r.ov  = (b != 0 && r.val / b != a);
    return r;
}

/* ---- macros: dispatch via _Generic on value type, comma-expression assignment ---- */

#define ckd_add(result, a, b) \
    _Generic((result), \
        signed char:        ((result) = __ckd_add_sc(a, b).val), __ckd_add_sc(a, b).ov, \
        short:              ((result) = __ckd_add_s(a, b).val), __ckd_add_s(a, b).ov, \
        int:                ((result) = __ckd_add_i(a, b).val), __ckd_add_i(a, b).ov, \
        long:               ((result) = __ckd_add_l(a, b).val), __ckd_add_l(a, b).ov, \
        long long:          ((result) = __ckd_add_ll(a, b).val), __ckd_add_ll(a, b).ov, \
        unsigned char:      ((result) = __ckd_add_uc(a, b).val), __ckd_add_uc(a, b).ov, \
        unsigned short:     ((result) = __ckd_add_us(a, b).val), __ckd_add_us(a, b).ov, \
        unsigned int:       ((result) = __ckd_add_ui(a, b).val), __ckd_add_ui(a, b).ov, \
        unsigned long:      ((result) = __ckd_add_ul(a, b).val), __ckd_add_ul(a, b).ov, \
        unsigned long long: ((result) = __ckd_add_ull(a, b).val), __ckd_add_ull(a, b).ov))

#define ckd_sub(result, a, b) \
    _Generic((result), \
        signed char:        ((result) = __ckd_sub_sc(a, b).val), __ckd_sub_sc(a, b).ov, \
        short:              ((result) = __ckd_sub_s(a, b).val), __ckd_sub_s(a, b).ov, \
        int:                ((result) = __ckd_sub_i(a, b).val), __ckd_sub_i(a, b).ov, \
        long:               ((result) = __ckd_sub_l(a, b).val), __ckd_sub_l(a, b).ov, \
        long long:          ((result) = __ckd_sub_ll(a, b).val), __ckd_sub_ll(a, b).ov, \
        unsigned char:      ((result) = __ckd_sub_uc(a, b).val), __ckd_sub_uc(a, b).ov, \
        unsigned short:     ((result) = __ckd_sub_us(a, b).val), __ckd_sub_us(a, b).ov, \
        unsigned int:       ((result) = __ckd_sub_ui(a, b).val), __ckd_sub_ui(a, b).ov, \
        unsigned long:      ((result) = __ckd_sub_ul(a, b).val), __ckd_sub_ul(a, b).ov, \
        unsigned long long: ((result) = __ckd_sub_ull(a, b).val), __ckd_sub_ull(a, b).ov))

#define ckd_mul(result, a, b) \
    _Generic((result), \
        signed char:        ((result) = __ckd_mul_sc(a, b).val), __ckd_mul_sc(a, b).ov, \
        short:              ((result) = __ckd_mul_s(a, b).val), __ckd_mul_s(a, b).ov, \
        int:                ((result) = __ckd_mul_i(a, b).val), __ckd_mul_i(a, b).ov, \
        long:               ((result) = __ckd_mul_l(a, b).val), __ckd_mul_l(a, b).ov, \
        long long:          ((result) = __ckd_mul_ll(a, b).val), __ckd_mul_ll(a, b).ov, \
        unsigned char:      ((result) = __ckd_mul_uc(a, b).val), __ckd_mul_uc(a, b).ov, \
        unsigned short:     ((result) = __ckd_mul_us(a, b).val), __ckd_mul_us(a, b).ov, \
        unsigned int:       ((result) = __ckd_mul_ui(a, b).val), __ckd_mul_ui(a, b).ov, \
        unsigned long:      ((result) = __ckd_mul_ul(a, b).val), __ckd_mul_ul(a, b).ov, \
        unsigned long long: ((result) = __ckd_mul_ull(a, b).val), __ckd_mul_ull(a, b).ov))

#endif /* MEUOS_STDCKDINT_H */