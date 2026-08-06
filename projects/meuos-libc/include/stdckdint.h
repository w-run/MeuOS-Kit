/* stdckdint.h — ISO C23 7.21: checked integer arithmetic.
 *
 * Per C23:  bool ckd_add(T *result, T a, T b)
 *           bool ckd_sub(T *result, T a, T b)
 *           bool ckd_mul(T *result, T a, T b)
 *
 * Because mcc's _Generic pre-evaluates ALL branches and rejects
 * incompatible pointer types, we forgo _Generic and expose only
 * the int/unsigned int helpers (the most common 32-bit cases).
 * Callers requiring other widths call the per-type helpers directly.
 *
 * The macros below work with gcc/clang (-std=c2x).  On mcc they
 * are unavailable (compile error), which matches the project's
 * current practice: mcc users call __ckd_i_add(&r, a, b) etc.
 */

#ifndef MEUOS_STDCKDINT_H
#define MEUOS_STDCKDINT_H

#include <stdbool.h>
#include <limits.h>

/* ---- add helpers ---- */
static inline bool __ckd_i_add(int a, int b, int *r) {
  *r = a + b;
  return (a > 0 && b > 0 && *r <= 0) || (a < 0 && b < 0 && *r >= 0);
}
static inline bool __ckd_ui_add(unsigned int a, unsigned int b, unsigned int *r) {
  *r = a + b;
  return *r < a;
}
static inline bool __ckd_ll_add(long long a, long long b, long long *r) {
  *r = a + b;
  return (a > 0 && b > 0 && *r <= 0) || (a < 0 && b < 0 && *r >= 0);
}
static inline bool __ckd_ull_add(unsigned long long a, unsigned long long b,
                                 unsigned long long *r) {
  *r = a + b;
  return *r < a;
}

/* ---- sub helpers ---- */
static inline bool __ckd_i_sub(int a, int b, int *r) {
  *r = a - b;
  return (b > 0 && a < INT_MIN + b) || (b < 0 && a > INT_MAX + b);
}
static inline bool __ckd_ui_sub(unsigned int a, unsigned int b, unsigned int *r) {
  *r = a - b;
  return *r > a;
}
static inline bool __ckd_ll_sub(long long a, long long b, long long *r) {
  *r = a - b;
  return (b > 0 && a < LLONG_MIN + b) || (b < 0 && a > LLONG_MAX + b);
}
static inline bool __ckd_ull_sub(unsigned long long a, unsigned long long b,
                                 unsigned long long *r) {
  *r = a - b;
  return *r > a;
}

/* ---- mul helpers ---- */
static inline bool __ckd_i_mul(int a, int b, int *r) {
  long long p = (long long)a * (long long)b;
  *r = (int)p;
  return p < (long long)INT_MIN || p > (long long)INT_MAX;
}
static inline bool __ckd_ui_mul(unsigned int a, unsigned int b, unsigned int *r) {
  *r = a * b;
  return b != 0 && *r / b != a;
}
static inline bool __ckd_ll_mul(long long a, long long b, long long *r) {
  *r = a * b;
  if (a == 0 || b == 0) return false;
  if (a == -1 && b == LLONG_MIN) return true;
  if (b == -1 && a == LLONG_MIN) return true;
  return *r / b != a;
}
static inline bool __ckd_ull_mul(unsigned long long a, unsigned long long b,
                                 unsigned long long *r) {
  *r = a * b;
  return b != 0 && *r / b != a;
}

/* ---- macros (gcc/clang only; mcc skips _Generic) ---- */
#ifdef __GNUC__
#define ckd_add(r, a, b) \
  _Generic((r), \
    int:             __ckd_i_add(a, b, &(r)), \
    unsigned int:    __ckd_ui_add(a, b, &(r)), \
    long long:       __ckd_ll_add(a, b, &(r)), \
    unsigned long long: __ckd_ull_add(a, b, &(r)))

#define ckd_sub(r, a, b) \
  _Generic((r), \
    int:             __ckd_i_sub(a, b, &(r)), \
    unsigned int:    __ckd_ui_sub(a, b, &(r)), \
    long long:       __ckd_ll_sub(a, b, &(r)), \
    unsigned long long: __ckd_ull_sub(a, b, &(r)))

#define ckd_mul(r, a, b) \
  _Generic((r), \
    int:             __ckd_i_mul(a, b, &(r)), \
    unsigned int:    __ckd_ui_mul(a, b, &(r)), \
    long long:       __ckd_ll_mul(a, b, &(r)), \
    unsigned long long: __ckd_ull_mul(a, b, &(r)))
#endif /* __GNUC__ */

#endif /* MEUOS_STDCKDINT_H */