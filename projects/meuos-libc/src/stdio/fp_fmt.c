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
static int
sign_bit(double v)
{
	union { double d; unsigned u[2]; } u;
	u.d = v;
	return (int)u.u[1] < 0;
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

	/* Round half-up at position `count`. */
	if (digits[count] >= '5') {
		i = count - 1;
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
	digits[count] = '\0';
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
	char digits[24];      /* up to 17 sig digits + rounding slack */
	int decpt;
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
	if (value == 0.0) {
		for (i = 0; i < 18; i++) digits[i] = '0';
		digits[18] = '\0';
		decpt = 1;
	} else {
		int sig = 17;
		if (was_g) {
			sig = precision;
			if (sig < 1) sig = 1;
			if (sig > 17) sig = 17;
		}
		dto_digits(value, sig, digits, &decpt);
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
				char c = (idx < 18) ? digits[idx] : '0';
				if (c == '0') strip++;
				else break;
			}
		} else {
			/* %f: fractional digits at virtual positions decpt..decpt+prec-1. */
			for (i = prec - 1; i >= 0; i--) {
				int idx = decpt + i;
				char c = (idx >= 0 && idx < 18) ? digits[idx] : '0';
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
				EMIT(idx < 18 ? digits[idx] : '0');
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
		} else if (decpt <= 18) {
			for (i = 0; i < decpt; i++) EMIT(digits[i]);
		} else {
			for (i = 0; i < 18; i++) EMIT(digits[i]);
			for (i = 18; i < decpt; i++) EMIT('0');
		}
		if (has_dot) {
			EMIT('.');
			for (i = 0; i < prec; i++) {
				int idx = decpt + i;
				EMIT(idx >= 0 && idx < 18 ? digits[idx] : '0');
			}
		}
	}

	/* ---- Right padding (left-justified) ---- */
	if (left)
		if (__meuos_sink_repeat(sink, ' ', pad) < 0) return -1;

	return 0;
}
