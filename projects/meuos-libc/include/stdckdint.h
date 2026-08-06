/* stdckdint.h — ISO C23 7.21: checked integer arithmetic.
 *
 * Defines ckd_add/ckd_sub/ckd_mul macros that dispatch via _Generic on
 * the *value* (not its address) of the result lvalue, because mcc rejects
 * _Generic(&(lvalue), ...) with pointer-type incompatibility.
 *
 * Each helper takes a pointer to the result lvalue as its last argument.
 */

#ifndef MEUOS_STDCKDINT_H
#define MEUOS_STDCKDINT_H

#include <stdbool.h>
#include <limits.h>

#define ckd_add(result, a, b) \
	((void)sizeof((result) = (a) + (b)), \
	 _Generic((result), \
		signed char:		__ckd_add_sc(a, b, &(result)), \
		short:			__ckd_add_s(a, b, &(result)), \
		int:			__ckd_add_i(a, b, &(result)), \
		long:			__ckd_add_l(a, b, &(result)), \
		long long:		__ckd_add_ll(a, b, &(result)), \
		unsigned char:		__ckd_add_uc(a, b, &(result)), \
		unsigned short:		__ckd_add_us(a, b, &(result)), \
		unsigned int:		__ckd_add_ui(a, b, &(result)), \
		unsigned long:		__ckd_add_ul(a, b, &(result)), \
		unsigned long long:	__ckd_add_ull(a, b, &(result))))

#define ckd_sub(result, a, b) \
	((void)sizeof((result) = (a) - (b)), \
	 _Generic((result), \
		signed char:		__ckd_sub_sc(a, b, &(result)), \
		short:			__ckd_sub_s(a, b, &(result)), \
		int:			__ckd_sub_i(a, b, &(result)), \
		long:			__ckd_sub_l(a, b, &(result)), \
		long long:		__ckd_sub_ll(a, b, &(result)), \
		unsigned char:		__ckd_sub_uc(a, b, &(result)), \
		unsigned short:		__ckd_sub_us(a, b, &(result)), \
		unsigned int:		__ckd_sub_ui(a, b, &(result)), \
		unsigned long:		__ckd_sub_ul(a, b, &(result)), \
		unsigned long long:	__ckd_sub_ull(a, b, &(result))))

#define ckd_mul(result, a, b) \
	((void)sizeof((result) = (a) * (b)), \
	 _Generic((result), \
		signed char:		__ckd_mul_sc(a, b, &(result)), \
		short:			__ckd_mul_s(a, b, &(result)), \
		int:			__ckd_mul_i(a, b, &(result)), \
		long:			__ckd_mul_l(a, b, &(result)), \
		long long:		__ckd_mul_ll(a, b, &(result)), \
		unsigned char:		__ckd_mul_uc(a, b, &(result)), \
		unsigned short:		__ckd_mul_us(a, b, &(result)), \
		unsigned int:		__ckd_mul_ui(a, b, &(result)), \
		unsigned long:		__ckd_mul_ul(a, b, &(result)), \
		unsigned long long:	__ckd_mul_ull(a, b, &(result))))

/* ---- Implementation inline helpers ---- */

static inline bool __ckd_add_sc(signed char a, signed char b, signed char *r) {
	*r = (signed char)((int)a + (int)b);
	return (a > 0 && b > 0 && *r <= 0) || (a < 0 && b < 0 && *r >= 0);
}
static inline bool __ckd_add_s(short a, short b, short *r) {
	*r = (short)((int)a + (int)b);
	return (a > 0 && b > 0 && *r <= 0) || (a < 0 && b < 0 && *r >= 0);
}
static inline bool __ckd_add_i(int a, int b, int *r) {
	*r = a + b;
	return (a > 0 && b > 0 && *r <= 0) || (a < 0 && b < 0 && *r >= 0);
}
static inline bool __ckd_add_l(long a, long b, long *r) {
	*r = a + b;
	return (a > 0 && b > 0 && *r <= 0) || (a < 0 && b < 0 && *r >= 0);
}
static inline bool __ckd_add_ll(long long a, long long b, long long *r) {
	*r = a + b;
	return (a > 0 && b > 0 && *r <= 0) || (a < 0 && b < 0 && *r >= 0);
}
static inline bool __ckd_add_uc(unsigned char a, unsigned char b, unsigned char *r) {
	*r = (unsigned char)((unsigned)a + (unsigned)b);
	return (unsigned)((unsigned)a + (unsigned)b) > 0xFFu;
}
static inline bool __ckd_add_us(unsigned short a, unsigned short b, unsigned short *r) {
	*r = (unsigned short)((unsigned)a + (unsigned)b);
	return (unsigned)((unsigned)a + (unsigned)b) > 0xFFFFu;
}
static inline bool __ckd_add_ui(unsigned int a, unsigned int b, unsigned int *r) {
	*r = a + b;
	return *r < a;
}
static inline bool __ckd_add_ul(unsigned long a, unsigned long b, unsigned long *r) {
	*r = a + b;
	return *r < a;
}
static inline bool __ckd_add_ull(unsigned long long a, unsigned long long b,
                                  unsigned long long *r) {
	*r = a + b;
	return *r < a;
}
/* ... sub helpers ... (truncated for space, but included in actual file) */
static inline bool __ckd_sub_sc(signed char a, signed char b, signed char *r) {
	*r = (signed char)((int)a - (int)b);
	return (b > 0 && a < SCHAR_MIN + b) || (b < 0 && a > SCHAR_MAX + b);
}
static inline bool __ckd_sub_s(short a, short b, short *r) {
	*r = (short)((int)a - (int)b);
	return (b > 0 && a < SHRT_MIN + b) || (b < 0 && a > SHRT_MAX + b);
}
static inline bool __ckd_sub_i(int a, int b, int *r) {
	*r = a - b;
	return (b > 0 && a < INT_MIN + b) || (b < 0 && a > INT_MAX + b);
}
static inline bool __ckd_sub_l(long a, long b, long *r) {
	*r = a - b;
	return (b > 0 && a < LONG_MIN + b) || (b < 0 && a > LONG_MAX + b);
}
static inline bool __ckd_sub_ll(long long a, long long b, long long *r) {
	*r = a - b;
	return (b > 0 && a < LLONG_MIN + b) || (b < 0 && a > LLONG_MAX + b);
}
static inline bool __ckd_sub_uc(unsigned char a, unsigned char b, unsigned char *r) {
	*r = (unsigned char)((unsigned)a - (unsigned)b);
	return (unsigned)((unsigned)a - (unsigned)b) > 0xFFu;
}
static inline bool __ckd_sub_us(unsigned short a, unsigned short b, unsigned short *r) {
	*r = (unsigned short)((unsigned)a - (unsigned)b);
	return (unsigned)((unsigned)a - (unsigned)b) > 0xFFFFu;
}
static inline bool __ckd_sub_ui(unsigned int a, unsigned int b, unsigned int *r) {
	*r = a - b;
	return *r > a;
}
static inline bool __ckd_sub_ul(unsigned long a, unsigned long b, unsigned long *r) {
	*r = a - b;
	return *r > a;
}
static inline bool __ckd_sub_ull(unsigned long long a, unsigned long long b,
                                  unsigned long long *r) {
	*r = a - b;
	return *r > a;
}
/* mul helpers remain unchanged from the previous version */
static inline bool __ckd_mul_sc(signed char a, signed char b, signed char *r) {
	int p = (int)a * (int)b;
	*r = (signed char)p;
	return p < SCHAR_MIN || p > SCHAR_MAX;
}
static inline bool __ckd_mul_s(short a, short b, short *r) {
	int p = (int)a * (int)b;
	*r = (short)p;
	return p < SHRT_MIN || p > SHRT_MAX;
}
static inline bool __ckd_mul_i(int a, int b, int *r) {
	long long p = (long long)a * (long long)b;
	*r = (int)p;
	return p < INT_MIN || p > INT_MAX;
}
static inline bool __ckd_mul_l(long a, long b, long *r) {
	long long p = (long long)a * (long long)b;
	*r = (long)p;
	return p < LONG_MIN || p > LONG_MAX;
}
static inline bool __ckd_mul_ll(long long a, long long b, long long *r) {
	*r = a * b;
	if (a == 0 || b == 0) return false;
	if (a == -1 && b == LLONG_MIN) return true;
	if (b == -1 && a == LLONG_MIN) return true;
	return *r / b != a;
}
static inline bool __ckd_mul_uc(unsigned char a, unsigned char b, unsigned char *r) {
	unsigned p = (unsigned)a * (unsigned)b;
	*r = (unsigned char)p;
	return p > 0xFFu;
}
static inline bool __ckd_mul_us(unsigned short a, unsigned short b, unsigned short *r) {
	unsigned p = (unsigned)a * (unsigned)b;
	*r = (unsigned short)p;
	return p > 0xFFFFu;
}
static inline bool __ckd_mul_ui(unsigned int a, unsigned int b, unsigned int *r) {
	*r = a * b;
	return b != 0 && *r / b != a;
}
static inline bool __ckd_mul_ul(unsigned long a, unsigned long b, unsigned long *r) {
	*r = a * b;
	return b != 0 && *r / b != a;
}
static inline bool __ckd_mul_ull(unsigned long long a, unsigned long long b,
                                  unsigned long long *r) {
	*r = a * b;
	return b != 0 && *r / b != a;
}

#endif /* MEUOS_STDCKDINT_H */