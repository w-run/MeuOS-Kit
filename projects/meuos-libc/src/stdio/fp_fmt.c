/* stdio/fp_fmt.c -- floating-point conversion for the printf family.
 *
 * Implements %f, %F, %e, %E, %g, %G.  %a/%A degrade to %g/%G (hex
 * float not yet implemented).
 *
 * Algorithm:
 *   1. Extract sign via IEEE-754 bit pattern; handle NaN/Inf.
 *   2. Use frexp + a 10^(2^k) table to scale |v| into [1, 10).
 *   3. Pull digits by repeated (int)cast + subtraction; the (int)cast
 *      is always in [0,9] so it never needs 64-bit int<->double helpers
 *      that i386 lacks.
 *   4. Round half-up at the requested precision; propagate carries
 *      (including carry-out that bumps the decimal exponent).
 *   5. Lay out sign / integer / '.' / fraction / exponent and apply
 *      width / zero / left / '#' flag rules.
 *   6. For %g without '#': strip trailing zeros from the fractional
 *      part (and the decimal point if no fraction remains).
 *
 * Only <math.h> dependency is frexp.  Works on i386 (x87) and x86_64 (SSE). */

#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include "internal.h"

#define FP_MAX_PREC 200   /* cap to bound output size */

/* 10^(2^k) for k = 0..8: 1e1, 1e2, ..., 1e256.
 * Combined via bit decomposition to scale by 10^n for |n| <= 511.
 * The compiler rounds these to the nearest representable double. */
static const double pow10_pos[9] = {
	1e1, 1e2, 1e4, 1e8, 1e16, 1e32, 1e64, 1e128, 1e256
};
static const double pow10_neg[9] = {
	1e-1, 1e-2, 1e-4, 1e-8, 1e-16, 1e-32, 1e-64, 1e-128, 1e-256
};

static double
scale10(double v, int n)
{
	int i;
	if (n >= 0) {
		for (i = 0; i < 9 && n; i++, n >>= 1)
			if (n & 1) v *= pow10_pos[i];
	} else {
		n = -n;
		for (i = 0; i < 9 && n; i++, n >>= 1)
			if (n & 1) v *= pow10_neg[i];
	}
	return v;
}

/* Sign bit via IEEE-754 bit pattern.  Little-endian: the high 32 bits
 * of the double (sign | exponent | mantissa_hi) live at u[1].  All
 * currently supported targets (i386, x86_64, aarch64) are LE. */
/* Sign bit via IEEE-754 high bit (endian-agnostic when using uint64_t).
 * Replaces a previous union-based hack that assumed little-endian u[1]. */
static int
sign_bit(double v)
{
	union { double d; unsigned long long u; } u;
	u.d = v;
	return (int)(u.u >> 63) != 0;
}

static int
is_nan(double v)
{
	return v != v;
}

static int
is_inf(double v)
{
	return v != 0 && v == v + v;
}

/* 128-bit unsigned integer, used by the exact digit extractor. */
typedef struct {
	unsigned long long lo;
	unsigned long long hi;
} u128;

static void
u128_mul10(u128 *a)
{
	unsigned long long lo = a->lo;
	unsigned long long hi = a->hi;
	/* a*2 */
	unsigned long long lo2 = lo << 1;
	unsigned long long hi2 = (hi << 1) | (lo >> 63);
	/* a*8 */
	unsigned long long lo8 = lo << 3;
	unsigned long long hi8 = (hi << 3) | (lo >> 61);
	/* a*10 = a*8 + a*2 */
	unsigned long long lo10 = lo8 + lo2;
	unsigned long long hi10 = hi8 + hi2 + (lo10 < lo8);
	a->lo = lo10;
	a->hi = hi10;
}

/* a >> n for n in [0, 127]. */
static unsigned long long
u128_shr(const u128 *a, int n)
{
	if (n == 0)
		return a->lo;
	if (n < 64)
		return (a->lo >> n) | (a->hi << (64 - n));
	if (n < 128)
		return a->hi >> (n - 64);
	return 0;
}

/* Keep the low n bits of a. */
static void
u128_mask(u128 *a, int n)
{
	if (n >= 128)
		return;
	if (n >= 64)
		a->hi &= (n == 64) ? 0 : ((1ULL << (n - 64)) - 1);
	else {
		a->hi = 0;
		a->lo &= (1ULL << n) - 1;
	}
}

/* Round half-up at position `count`; adjust decpt on carry-out. */
static void
round_digits(char *digits, int count, int *decpt)
{
	if (digits[count] >= '5') {
		int i = count - 1;
		while (i >= 0 && digits[i] == '9') {
			digits[i] = '0';
			i--;
		}
		if (i >= 0) {
			digits[i]++;
		} else {
			/* All nines: carry out, e.g. 9.99 -> 10.0. */
			digits[0] = '1';
			for (i = 1; i < count; i++) digits[i] = '0';
			(*decpt)++;
		}
	}
	/* count==0: digits[0] holds the rounded first digit (or the raw
	 * first digit when no carry) -- do not NUL it out, the caller needs
	 * it for a %.0f of a value in (0,1). */
	if (count > 0)
		digits[count] = '\0';
}

/* Exact decimal digit extraction via the integer mantissa.
 *
 * Every finite double is exactly v = M * 2^E with M a 53-bit integer.
 * When E < 0 write v = M / 2^N; the decimal digits are then obtained
 * exactly by long division (multiply the remainder by 10, take the
 * quotient digit, keep the remainder).  This avoids the precision loss
 * of the frexp/scale10 path, which rounds e.g. 0.1*10 to exactly 1.0
 * and then emits a tail of zeros.
 *
 * Feasible when N <= 120 so all intermediates fit in 128 bits; values
 * outside that range (|v| >= 2^53 or < ~1e-36) fall back to the double
 * path, where it is either exact (large integers) or produces leading
 * zeros that are correct at the requested precision.
 *
 * On success generates digits[0..count] and rounds, returning 1.
 * Returns 0 to ask the caller to use the double fallback. */
static int
dto_digits_exact(double v, int count, char *digits, int *decpt)
{
	union { double d; unsigned long long u; } un;
	unsigned long long M;
	unsigned long long idig[20];
	u128 num;
	long long N;
	int eb, binexp, e10, i, nid, di, skip;
	double scaled;

	un.d = v;
	eb = (int)((un.u >> 52) & 0x7ff);
	M = un.u & 0xfffffffffffffULL;
	if (eb == 0)
		N = 1074;		/* subnormal: v = M / 2^1074 */
	else {
		M |= 1ULL << 52;
		N = (long long)1075 - eb;	/* v = M / 2^N */
	}
	if (N < 1 || N > 120)
		return 0;

	/* Decimal exponent via the same approximation the double path uses. */
	(void)frexp(v, &binexp);
	e10 = (int)((binexp - 1) * 0.301029995663981);
	scaled = scale10(v, -e10);
	while (scaled >= 10.0) { scaled *= 0.1; e10++; }
	while (scaled < 1.0)   { scaled *= 10.0; e10--; }
	*decpt = e10 + 1;

	/* Integer part digits (v >= 1 implies N < 53, so M >> N is exact). */
	nid = 0;
	if (N < 64) {
		unsigned long long ip = M >> N;
		unsigned long long mask = (1ULL << N) - 1;
		while (ip) {
			idig[nid++] = ip % 10;
			ip /= 10;
		}
		/* Trust the integer digit count over the log approximation. */
		if (nid > 0)
			*decpt = nid;
		num.lo = M & mask;
	} else {
		num.lo = M;
	}
	di = 0;
	for (i = nid - 1; i >= 0 && di <= count; i--)
		digits[di++] = (char)('0' + (int)idig[i]);

	/* Fractional digits by exact long division of (M mod 2^N) / 2^N.
	 * The division yields digits at positions -1, -2, ... (tenths,
	 * hundredths, ...).  The first significant digit sits at position
	 * decpt-1, so skip the -decpt leading zero positions for v < 1. */
	skip = (nid == 0) ? -(*decpt) : 0;
	num.hi = 0;
	while (di <= count) {
		unsigned long long d;
		if (skip > 0) {
			u128_mul10(&num);
			u128_mask(&num, (int)N);
			skip--;
			continue;
		}
		u128_mul10(&num);
		d = u128_shr(&num, (int)N);
		if (d > 9) d = 9;
		digits[di++] = (char)('0' + (int)d);
		u128_mask(&num, (int)N);
	}

	round_digits(digits, count, decpt);
	return 1;
}

/* Generate `count` significant decimal digits of |v| into `digits`
 * (NUL-terminated).  v must be finite, nonzero, non-negative.
 *
 * After return, the value is reconstructed as:
 *   digits[0] . digits[1] digits[2] ... * 10^(decpt-1)
 * i.e. digits[0] is the coefficient of 10^(decpt-1).
 *
 * Generates count+1 digits internally, then rounds half-up at position
 * `count` and adjusts decpt on carry-out (e.g. 9.99..9 -> 10.00..0). */
static void
dto_digits(double v, int count, char *digits, int *decpt)
{
	int binexp, e10, i;
	double scaled;

	/* Exact integer-mantissa extraction when it fits in 128 bits. */
	if (dto_digits_exact(v, count, digits, decpt))
		return;

	/* frexp: v = m * 2^binexp, m in [0.5, 1).
	 * So 2^(binexp-1) <= v < 2^binexp, giving
	 * log10(v) in [(binexp-1)*0.30103, binexp*0.30103). */
	(void)frexp(v, &binexp);
	e10 = (int)((binexp - 1) * 0.301029995663981);

	/* Scale v into [1, 10). */
	scaled = scale10(v, -e10);
	while (scaled >= 10.0) { scaled *= 0.1; e10++; }
	while (scaled < 1.0)   { scaled *= 10.0; e10--; }

	/* digits[0] carries 10^e10, so the decimal point sits at e10+1. */
	*decpt = e10 + 1;

	/* Extract count+1 digits; the extra drives half-up rounding. */
	for (i = 0; i < count + 1; i++) {
		int d = (int)scaled;
		if (d < 0) d = 0;
		if (d > 9) d = 9;
		digits[i] = (char)('0' + d);
		scaled = (scaled - (double)d) * 10.0;
	}

	round_digits(digits, count, decpt);
}

/* Compute just the decimal point position (decpt = e10 + 1, as in
 * dto_digits) without extracting digits; used to size the digit buffer
 * for %f before the full extraction pass. */
static int
dto_decpt(double v)
{
	int binexp, e10;
	double scaled;

	if (v == 0.0)
		return 1;
	(void)frexp(v, &binexp);
	e10 = (int)((binexp - 1) * 0.301029995663981);
	scaled = scale10(v, -e10);
	while (scaled >= 10.0) { scaled *= 0.1; e10++; }
	while (scaled < 1.0)   { scaled *= 10.0; e10--; }
	return e10 + 1;
}

#define EMIT(c) do { if (__meuos_sink_put(sink, (c)) < 0) return -1; } while (0)

/* flags: bit 0='-', 1='+', 2=' ', 3='#', 4='0'
 * width:  minimum field width
 * precision: -1 if unspecified (resolved to 6), otherwise >= 0
 * conv: 'f','F','e','E','g','G' (caller maps a/A to g/G) */
int
__meuos_fmt_fp(struct __meuos_print_sink *sink, double value, int conv,
	int width, int precision, int flags)
{
	char digits[FP_MAX_PREC + 340];      /* sig digits + rounding slack */
	int decpt;
	int ndig;         /* number of significant digits in `digits` */
	char sign_char = 0;
	int prec;
	int left  = flags & 1;
	int plus  = flags & 2;
	int space = flags & 4;
	int alt   = flags & 8;
	int zero  = flags & 16;
	int was_g = (conv == 'g' || conv == 'G');
	int upper = (conv == 'F' || conv == 'E' || conv == 'G');
	int i;

	/* ---- Sign (treat -0.0 as negative so it prints "-0") ---- */
	if (sign_bit(value)) {
		value = -value;
		sign_char = '-';
	} else if (plus) {
		sign_char = '+';
	} else if (space) {
		sign_char = ' ';
	}

	/* ---- NaN / Inf ---- */
	if (is_nan(value) || is_inf(value)) {
		const char *body;
		int pad, content_len;
		if (is_nan(value))
			body = upper ? "NAN" : "nan";
		else
			body = upper ? "INF" : "inf";
		content_len = (sign_char != 0) + 3;
		pad = width > content_len ? width - content_len : 0;
		if (!left && !zero)
			if (__meuos_sink_repeat(sink, ' ', pad) < 0) return -1;
		if (sign_char) EMIT(sign_char);
		if (zero && !left)
			if (__meuos_sink_repeat(sink, '0', pad) < 0) return -1;
		for (i = 0; i < 3; i++) EMIT(body[i]);
		if (left)
			if (__meuos_sink_repeat(sink, ' ', pad) < 0) return -1;
		return 0;
	}

	/* ---- Resolve precision ---- */
	if (precision < 0) precision = 6;
	if (precision > FP_MAX_PREC) precision = FP_MAX_PREC;

	/* ---- Generate significant digits ---- */
	{
		int sig;
		if (was_g) {
			/* %g: precision is the number of significant digits. */
			sig = precision;
			if (sig < 1) sig = 1;
		} else if (conv == 'f' || conv == 'F') {
			/* %f: need the integer part plus `precision` fractional
			 * digits.  dto_digits generates sig+1 digits and rounds at
			 * position sig, so sig must equal int_digits + precision:
			 * the rounding trigger is then exactly the (precision+1)-th
			 * fractional digit, and the result has precision fraction
			 * digits.  (An extra +1 here, or a minimum of 17, moves the
			 * rounding point past the digits %f actually prints and
			 * silently drops the carry -- e.g. pi %.6f printed
			 * 3.141592 instead of 3.141593.) */
			int int_digits = 1;
			if (value != 0.0) {
				int_digits = dto_decpt(value);
				if (int_digits < 0) int_digits = 0;
			}
			sig = int_digits + precision;
		} else {
			/* %e: one digit before the point + precision fraction. */
			sig = precision + 1;
		}
		ndig = sig;
		if (ndig < 1) ndig = 1;
		if (value == 0.0) {
			if (sig < 1) sig = 1;   /* %.0f of 0.0 must still emit digits[0]='0' */
			for (i = 0; i < sig; i++) digits[i] = '0';
			digits[sig] = '\0';
			decpt = 1;
		} else {
			dto_digits(value, sig, digits, &decpt);
		}
	}

	/* ---- %g: decide %e vs %f, compute fractional precision ---- */
	if (was_g) {
		int E = decpt - 1;
		int p = precision;
		if (p == 0) p = 1;
		if (E < -4 || E >= p) {
			conv = upper ? 'E' : 'e';
			prec = p - 1;
		} else {
			conv = upper ? 'F' : 'f';
			prec = p - 1 - E;
		}
	} else if (conv == 'e' || conv == 'E') {
		prec = precision;
	} else {
		prec = precision;  /* %f */
	}
	if (prec > FP_MAX_PREC) prec = FP_MAX_PREC;
	if (prec < 0) prec = 0;

	/* ---- %g without '#': strip trailing zeros from fraction ---- */
	if (was_g && !alt) {
		int strip = 0;
		if (conv == 'e' || conv == 'E') {
			/* Fractional digits are digits[1..prec]. */
			for (i = prec - 1; i >= 0; i--) {
				int idx = i + 1;
				char c = (idx < ndig) ? digits[idx] : '0';
				if (c == '0') strip++;
				else break;
			}
		} else {
			/* %f: fractional digits at virtual positions decpt..decpt+prec-1. */
			for (i = prec - 1; i >= 0; i--) {
				int idx = decpt + i;
				char c = (idx >= 0 && idx < ndig) ? digits[idx] : '0';
				if (c == '0') strip++;
				else break;
			}
		}
		prec -= strip;
	}

	int has_dot = (prec > 0) || alt;

	/* ---- Compute body length (sign handled separately) ---- */
	int body_len;
	if (conv == 'e' || conv == 'E') {
		int exp = decpt - 1;
		int ea = exp < 0 ? -exp : exp;
		body_len = 1 + (has_dot ? 1 + prec : 0) + 2 /* e± */;
		body_len += ea >= 100 ? 3 : 2;
	} else {
		int int_digits = decpt <= 0 ? 1 : decpt;
		body_len = int_digits + (has_dot ? 1 + prec : 0);
	}

	int content_len = (sign_char != 0) + body_len;
	int pad = width > content_len ? width - content_len : 0;

	/* ---- Left padding (spaces, unless zero-pad) ---- */
	if (!left && !zero)
		if (__meuos_sink_repeat(sink, ' ', pad) < 0) return -1;
	/* Sign. */
	if (sign_char) EMIT(sign_char);
	/* Zero padding (between sign and body). */
	if (zero && !left)
		if (__meuos_sink_repeat(sink, '0', pad) < 0) return -1;

	/* ---- Body ---- */
	if (conv == 'e' || conv == 'E') {
		EMIT(digits[0]);
		if (has_dot) {
			EMIT('.');
			for (i = 0; i < prec; i++) {
				int idx = i + 1;
				EMIT(idx < ndig ? digits[idx] : '0');
			}
		}
		EMIT(conv);  /* 'e' or 'E' */
		{
			int exp = decpt - 1;
			int ea = exp < 0 ? -exp : exp;
			EMIT(exp < 0 ? '-' : '+');
			if (ea >= 100) {
				EMIT('0' + ea / 100);
				EMIT('0' + (ea / 10) % 10);
			} else {
				EMIT('0' + ea / 10);
			}
			EMIT('0' + ea % 10);
		}
	} else {
		/* %f */
		if (decpt <= 0) {
			EMIT('0');
		} else {
			for (i = 0; i < decpt; i++)
				EMIT(i < ndig ? digits[i] : '0');
		}
		if (has_dot) {
			EMIT('.');
			for (i = 0; i < prec; i++) {
				int idx = decpt + i;
				EMIT(idx >= 0 && idx < ndig ? digits[idx] : '0');
			}
		}
	}

	/* ---- Right padding (left-justified) ---- */
	if (left)
		if (__meuos_sink_repeat(sink, ' ', pad) < 0) return -1;

	return 0;
}

/* printf %a/%A: C99 hexadecimal floating-point conversion (7.19.6.1).
 *
 * Outputs "[-]0x1.<hexdigits>p<±exp>": a hex fraction mantissa followed
 * by a binary (power-of-two) exponent.  Normal doubles print the
 * implicit '1' before the point; zero and subnormals print '0'.  The
 * precision counts hex fractional digits (default 13, enough to
 * represent any double exactly; subnormals pad with leading zeroes).
 * NaN/Inf and the width/flags layout mirror __meuos_fmt_fp. */
static int
hexnib(unsigned long long v)
{
	return (int)(v & 0xF);
}

static int
hexval(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return c - 'A' + 10;
}

int
__meuos_fmt_hexfp(struct __meuos_print_sink *sink, double value, int conv,
	int width, int precision, int flags)
{
	union { double d; unsigned long long u; } un;
	static const char hexdig[] = "0123456789abcdef";
	const char *hexch = (conv == 'A') ? "0123456789ABCDEF" : hexdig;
	char digits[16];
	char sign_char = 0;
	int ndig = 0, e2 = 0, i, lead;
	int left  = flags & 1;
	int plus  = flags & 2;
	int space = flags & 4;
	int alt   = flags & 8;
	int zero  = flags & 16;
	int upper = (conv == 'A');
	int body_len, content_len, pad, ea;
	int was_default_precision = (precision < 0);

	/* ---- Sign ---- */
	if (sign_bit(value)) {
		value = -value;
		sign_char = '-';
	} else if (plus) {
		sign_char = '+';
	} else if (space) {
		sign_char = ' ';
	}

	/* ---- NaN / Inf ---- */
	if (is_nan(value) || is_inf(value)) {
		const char *body = is_nan(value) ? (upper ? "NAN" : "nan")
		                                  : (upper ? "INF" : "inf");
		content_len = (sign_char != 0) + 3;
		pad = width > content_len ? width - content_len : 0;
		if (!left && !zero)
			if (__meuos_sink_repeat(sink, ' ', pad) < 0) return -1;
		if (sign_char) EMIT(sign_char);
		if (zero && !left)
			if (__meuos_sink_repeat(sink, '0', pad) < 0) return -1;
		for (i = 0; i < 3; i++) EMIT(body[i]);
		if (left)
			if (__meuos_sink_repeat(sink, ' ', pad) < 0) return -1;
		return 0;
	}

	/* ---- Extract mantissa digits and binary exponent ---- */
	if (precision < 0) {
		/* default: emit exactly as many hex digits as needed */
		precision = 13;
	} else if (precision > 13) {
		precision = 13;
	}

	if (value == 0.0) {
		e2 = 0;
		ndig = 0;
		lead = '0';
	} else {
		un.d = value;
		int eb = (int)((un.u >> 52) & 0x7ff);
		unsigned long long mant = un.u & 0xfffffffffffffULL;
		if (eb == 0) {
			e2 = -1022;		/* subnormal: 0.mant * 2^-1022 */
			lead = '0';
		} else {
			mant |= 1ULL << 52;
			e2 = eb - 1023;
			lead = '1';
		}
		/* 52 mantissa bits -> 13 hex digits, high nibble first */
		for (i = 12; i >= 0; i--)
			digits[12 - i] = hexch[hexnib(mant >> (i * 4))];
		ndig = 13;
		/* round half-up at the requested precision (extra digit >= 8) */
		if (precision < ndig && digits[precision] >= '8') {
			int j = precision - 1;
			while (j >= 0 && digits[j] == 'f') { digits[j] = '0'; j--; }
			if (j >= 0) {
				digits[j] = hexch[hexval(digits[j]) + 1];
			} else if (lead == '1') {
				/* carry out: 0x1.fff -> 0x1.000 p+1 */
				lead = '1';
				e2++;
				for (i = 0; i < precision; i++) digits[i] = '0';
			} else {
				/* subnormal rounding across the boundary */
				lead = '1';
				e2 = -1021;
				for (i = 0; i < precision; i++) digits[i] = '0';
			}
		}
		/* strip trailing zeroes for the default precision */
		if (was_default_precision && precision > 0) {
			while (precision > 0 && digits[precision - 1] == '0')
				precision--;
		}
	}
	if (value == 0.0 && was_default_precision)
		precision = 0;

	/* ---- Layout: [pad][sign][0x][lead][.digits][p][±][exp][pad] ---- */
	ea = e2 < 0 ? -e2 : e2;
	{
		int has_dot = (precision > 0) || alt;
		body_len = 2 /*0x*/ + 1 /*lead*/ + (has_dot ? 1 + precision : 0)
			+ 1 /*p*/ + 1 /*sign*/ + (ea >= 100 ? 3 : ea >= 10 ? 2 : 1);
		content_len = (sign_char != 0) + body_len;
		pad = width > content_len ? width - content_len : 0;
		if (!left && !zero)
			if (__meuos_sink_repeat(sink, ' ', pad) < 0) return -1;
		if (sign_char) EMIT(sign_char);
		if (zero && !left)
			if (__meuos_sink_repeat(sink, '0', pad) < 0) return -1;
		EMIT('0');
		EMIT(upper ? 'X' : 'x');
		EMIT(lead);
		if (has_dot) {
			EMIT('.');
			for (i = 0; i < precision; i++)
				EMIT(i < ndig ? digits[i] : '0');
		}
		EMIT(upper ? 'P' : 'p');
		EMIT(e2 < 0 ? '-' : '+');
		{
			char ebuf[8];
			int ei = 0, tmp = ea;
			if (tmp == 0) ebuf[ei++] = '0';
			while (tmp) { ebuf[ei++] = (char)('0' + tmp % 10); tmp /= 10; }
			while (ei-- > 0) EMIT(ebuf[ei]);
		}
		if (left)
			if (__meuos_sink_repeat(sink, ' ', pad) < 0) return -1;
	}
	return 0;
}
