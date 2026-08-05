/* stdlib/strfmon.c — POSIX monetary value formatting (strfmon).
 *
 * Format string: %[flags][field-width][.precision][format]
 *   flags: '='<c>  fill character (default space)
 *          '^'     suppress grouping
 *          '+'     always print sign; '(' print negative in parentheses
 *          '!'     suppress the currency symbol
 *          '-'     left-justify within field width
 *   format: 'i'    international currency symbol (int_curr_symbol)
 *           'n'    national currency symbol (currency_symbol)
 *           '%'    print a literal '%'
 *   .precision: fraction digits; default = locale frac_digits (or 2).
 *   value: a double, or long double with 'l'/'L' before the format char.
 *
 * Zero GNU dependency; reuses localeconv()'s struct lconv (C locale has
 * empty currency, so the default run formats the bare signed number).
 * glibc keeps strfmon in libc itself, so the symbol belongs to core libc.
 */

#include <money.h>
#include <stdio.h>
#include <stdarg.h>
#include <locale.h>
#include <string.h>
#include <stdlib.h>

/* The arithmetic below wants the widest available floating type.  mcc only
 * supports long double on x86_64; on the other five targets it rejects the
 * type outright ("long double is not yet supported"), which made this file
 * -- and therefore the whole library -- unbuildable there.  Fall back to
 * double where the wider type is unavailable: %Ln then formats at double
 * precision instead of the build failing altogether.  Revisit once mcc
 * grows long double on the remaining targets. */
#if defined(__x86_64__)
typedef long double money_float;
#define MONEY_C(x) x##L
#else
typedef double money_float;
#define MONEY_C(x) x
#endif

/* Append str to a growable-ish fixed buffer; returns bytes or -1 on overflow. */
static size_t
addstr(char **pp, size_t *left, const char *str)
{
	size_t n = strlen(str);
	if (n > *left)
		return (size_t)-1;
	memcpy(*pp, str, n);
	*pp += n;
	*left -= n;
	return n;
}

/* Append a single character. */
static size_t
addc(char **pp, size_t *left, int c)
{
	if (*left < 1)
		return (size_t)-1;
	**pp = (char)c;
	(*pp)++;
	(*left)--;
	return 1;
}

ssize_t
strfmon(char *s, size_t maxsize, const char *format, ...)
{
	struct lconv *lc = localeconv();
	va_list ap;
	va_start(ap, format);

	char *out = s;
	size_t outleft = maxsize;
	const char *f = format;

	while (*f) {
		if (*f != '%') {
			if (addc(&out, &outleft, *f) == (size_t)-1)
				goto overflow;
			f++;
			continue;
		}
		f++;
		if (*f == '%') {
			if (addstr(&out, &outleft, "%") == (size_t)-1)
				goto overflow;
			f++;
			continue;
		}

		/* flags */
		int no_group = 0, force_sign = 0, paren_neg = 0, no_symbol = 0;
		int left_align = 0;
		char fill = ' ';
		for (;;) {
			if (*f == '^') { no_group = 1; f++; }
			else if (*f == '+') { force_sign = 1; f++; }
			else if (*f == '(') { paren_neg = 1; force_sign = 1; f++; }
			else if (*f == '!') { no_symbol = 1; f++; }
			else if (*f == '-') { left_align = 1; f++; }
			else if (*f == '=') { f++; fill = *f ? *f : ' '; if (*f) f++; }
			else break;
		}

		/* field width */
		int width = -1;
		if (*f >= '0' && *f <= '9') {
			width = 0;
			while (*f >= '0' && *f <= '9')
				width = width * 10 + (*f++ - '0');
		}

		/* precision */
		int precision = -1;
		if (*f == '.') {
			f++;
			precision = 0;
			while (*f >= '0' && *f <= '9')
				precision = precision * 10 + (*f++ - '0');
		}

		/* optional length modifier */
		int is_ldouble = 0;
		if (*f == 'l' || *f == 'L') {
			is_ldouble = 1;
			f++;
		}

		char fmt = *f;
		if (fmt == 'i' || fmt == 'n') {
			int use_intl = (fmt == 'i');
			money_float val = is_ldouble
			        ? (money_float)va_arg(ap, money_float)
			        : (money_float)va_arg(ap, double);
			/* default precision = locale frac_digits or 2 */
			if (precision < 0) {
				int fd = use_intl ? lc->int_frac_digits : lc->frac_digits;
				precision = (fd >= 0) ? fd : 2;
			}
			if (precision > 8)
				precision = 8;

			/* --- build the monetary number token: sign + symbol + int + frac --- */
			int neg = (val < 0);
			if (neg)
				val = -val;

			/* scale to fraction, rounding the last digit */
			money_float scaled = val;
			money_float unit = MONEY_C(1.0);
			for (int k = 0; k < precision; k++) {
				scaled *= MONEY_C(10.0);
				unit *= MONEY_C(10.0);
			}
			scaled += MONEY_C(0.5);
			unsigned long long whole = (unsigned long long)(scaled / unit);
			long long frac = (long long)(scaled - whole * unit);

			/* integer digits, optionally grouped */
			char idig[64];
			char fracdig[16];
			char ints[64];
			snprintf(idig, sizeof idig, "%llu", whole);
			/* left-pad frac to precision digits */
			{
				char tmp[16];
				snprintf(tmp, sizeof tmp, "%lld", frac);
				size_t tl = strlen(tmp);
				if (tl < (size_t)precision) {
					memset(fracdig, '0', precision - tl);
					memcpy(fracdig + (precision - tl), tmp, tl);
					fracdig[precision] = 0;
				} else {
					memcpy(fracdig, tmp, precision);
					fracdig[precision] = 0;
				}
			}
			if (no_group || !lc->mon_thousands_sep || !lc->mon_thousands_sep[0] ||
			    !lc->mon_grouping || !lc->mon_grouping[0]) {
				strcpy(ints, idig);
			} else {
				/* simple grouping by mon_grouping[0] (or 3) */
				size_t il = strlen(idig);
				int gs = (lc->mon_grouping[0] > 0) ? lc->mon_grouping[0] : 3;
				int first = (int)(il % gs);
				if (first == 0)
					first = gs;
				char *q = ints;
				int i = 0;
				while (i < (int)il) {
					int take = (i == 0) ? first : gs;
					if (i > 0)
						*q++ = lc->mon_thousands_sep[0] ? lc->mon_thousands_sep[0] : ',';
					while (take-- > 0 && i < (int)il)
						*q++ = idig[i++];
				}
				*q = 0;
			}

			/* assemble token pieces */
			char dec[2] = { '.', 0 };
			if (lc->mon_decimal_point && lc->mon_decimal_point[0])
				dec[0] = lc->mon_decimal_point[0];
			const char *sym = use_intl ? lc->int_curr_symbol : lc->currency_symbol;
			if (no_symbol)
				sym = "";
			const char *sign = neg ? (lc->negative_sign ? lc->negative_sign : "-")
			                       : (lc->positive_sign ? lc->positive_sign : "");

			char num[128];
			char *np = num;
			size_t nl = sizeof num;
			if (paren_neg && neg)
				addstr(&np, &nl, "(");
			else if (force_sign || neg)
				addstr(&np, &nl, sign);
			addstr(&np, &nl, sym);
			if (use_intl && lc->int_curr_symbol && !no_symbol &&
			    lc->int_curr_symbol[0] && lc->int_p_sep_by_space != 0)
				addstr(&np, &nl, " ");
			addstr(&np, &nl, ints);
			if (precision > 0) {
				addstr(&np, &nl, dec);
				addstr(&np, &nl, fracdig);
			}
			/* NOTE: the '(' flag opens the paren for negatives; the closing
			 * ')' is provided by the caller's literal in the format string
			 * (POSIX/glibc convention), so we do not emit one here. */
			*np = 0;

			/* field width / alignment */
			size_t vlen = strlen(num);
			size_t pad = (width > (int)vlen) ? (size_t)(width - (int)vlen) : 0;
			if (pad && !left_align) {
				while (pad-- > 0) {
					if (addc(&out, &outleft, fill) == (size_t)-1)
						goto overflow;
				}
			}
			if (addstr(&out, &outleft, num) == (size_t)-1)
				goto overflow;
			if (pad && left_align) {
				while (pad-- > 0) {
					if (addc(&out, &outleft, ' ') == (size_t)-1)
						goto overflow;
				}
			}
			f++;
		} else {
			/* unknown format char: skip */
			f++;
		}
	}

	va_end(ap);
	if (out == s + maxsize) {
		/* no room even for NUL */
		s[0] = 0;
		va_end(ap);
		return -1;
	}
	*out = 0;
	return (ssize_t)(out - s);

overflow:
	va_end(ap);
	if (maxsize > 0)
		s[0] = 0;
	return -1;
}
