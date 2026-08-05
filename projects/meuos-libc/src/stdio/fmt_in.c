/* stdio/fmt_in.c -- scanf family: vsscanf + sscanf + scanf.
 *
 * Minimal implementation supporting %c / %s / %d / %x with optional
 * whitespace skipping. scanf() pulls a whole buffer from stdin with
 * one read(); this is enough for the test cases we run but is not a
 * full streaming scanf. */

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include "internal.h"

static int
isspace_int(int character)
{
	return character == ' ' || character == '\t' || character == '\n' || character == '\r';
}

int
vsscanf(const char *input, const char *format, va_list arguments)
{
	int assigned = 0;
	while (*format) {
		int width = 0;
		int length = 0;

		if (isspace_int(*format)) {
			while (isspace_int(*format)) ++format;
			while (isspace_int(*input)) ++input;
			continue;
		}
		if (*format != '%') {
			if (*input != *format)
				break;
			++input;
			++format;
			continue;
		}
		++format;
		/* Optional field width: limits how many input chars a
		 * conversion consumes (0/absent means unlimited). */
		while (*format >= '0' && *format <= '9')
			width = width * 10 + *format++ - '0';
		/* Length modifier: 0=none, 1=l, 2=ll, 5=h, 6=hh. */
		if (*format == 'l') {
			length = 1;
			++format;
			if (*format == 'l') { length = 2; ++format; }
		} else if (*format == 'h') {
			length = 5;
			++format;
			if (*format == 'h') { length = 6; ++format; }
		}
		if (*format == 'c') {
			char *out = va_arg(arguments, char *);
			int i;
			if (width == 0) width = 1;
			for (i = 0; i < width && *input; i++)
				out[i] = *input++;
			if (i == 0) break;
			assigned += i;
			++format;
			continue;
		}
		while (isspace_int(*input)) ++input;
		if (*format == 's') {
			char *out = va_arg(arguments, char *);
			int i = 0;
			if (!*input) break;
			while (*input && !isspace_int(*input) && (width == 0 || i < width))
				out[i++] = *input++;
			out[i] = 0;
			++assigned;
			++format;
			continue;
		}
		if (*format == 'd' || *format == 'i' || *format == 'o'
		 || *format == 'u' || *format == 'x' || *format == 'X') {
			int base;
			int is_signed = (*format == 'd' || *format == 'i');
			int negative = 0, digits = 0;
			int overflow = 0;
			unsigned long long value = 0;
			unsigned long long limit, tmax;
			if (*format == 'i')
				base = 0;		/* auto-detect */
			else if (*format == 'o')
				base = 8;
			else if (*format == 'x' || *format == 'X')
				base = 16;
			else
				base = 10;
			if (*input == '-') { negative = 1; ++input; }
			else if (*input == '+') ++input;
			if (base == 0) {
				/* %i: 0x/0X -> 16, leading 0 -> 8, else 10 */
				base = 10;
				if (*input == '0') {
					base = 8;
					if (input[1] == 'x' || input[1] == 'X') {
						base = 16;
						input += 2;
						digits += 2;
					}
				}
			} else if (base == 16 && input[0] == '0'
			 && (input[1] == 'x' || input[1] == 'X')) {
				input += 2;
				digits += 2;
			}
			/* Target-type bounds (length: 0=int, 1=long, 2=long long,
			 * 5=short, 6=signed/unsigned char). */
			switch (length) {
			case 2: tmax = is_signed ? 0x7fffffffffffffffULL : 0xffffffffffffffffULL; break;
			case 1: tmax = is_signed ? (unsigned long long)LONG_MAX : (unsigned long long)ULONG_MAX; break;
			case 5: tmax = is_signed ? (unsigned long long)SHRT_MAX : (unsigned long long)USHRT_MAX; break;
			case 6: tmax = is_signed ? (unsigned long long)SCHAR_MAX : (unsigned long long)UCHAR_MAX; break;
			default: tmax = is_signed ? (unsigned long long)INT_MAX : (unsigned long long)UINT_MAX; break;
			}
			limit = (is_signed && negative) ? tmax + 1 : tmax;
			while (*input && (width == 0 || digits < width)) {
				int digit = *input >= '0' && *input <= '9' ? *input - '0'
					: (*input >= 'a' && *input <= 'f' ? *input - 'a' + 10
					: (*input >= 'A' && *input <= 'F' ? *input - 'A' + 10 : base));
				if (digit >= base) break;
				/* Guard against wraparound on overflow; keep consuming
				 * the remaining digits either way. */
				if (value > (limit - (unsigned long long)digit) / (unsigned long long)base)
					overflow = 1;
				else if (!overflow)
					value = value * (unsigned long long)base + (unsigned long long)digit;
				++digits;
				++input;
			}
			if (!digits) break;
			if (is_signed) {
				long long signed_val;
				if (overflow)
					signed_val = negative ? -(long long)limit : (long long)tmax;
				else if (negative)
					signed_val = -(long long)value;
				else
					signed_val = (long long)value;
				switch (length) {
				case 2: { long long *o = va_arg(arguments, long long *); *o = signed_val; break; }
				case 1: { long *o = va_arg(arguments, long *); *o = (long)signed_val; break; }
				case 5: { short *o = va_arg(arguments, short *); *o = (short)signed_val; break; }
				case 6: { signed char *o = va_arg(arguments, signed char *); *o = (signed char)signed_val; break; }
				default: { int *o = va_arg(arguments, int *); *o = (int)signed_val; break; }
				}
			} else {
				unsigned long long uval = overflow ? limit : value;
				switch (length) {
				case 2: { unsigned long long *o = va_arg(arguments, unsigned long long *); *o = uval; break; }
				case 1: { unsigned long *o = va_arg(arguments, unsigned long *); *o = (unsigned long)uval; break; }
				case 5: { unsigned short *o = va_arg(arguments, unsigned short *); *o = (unsigned short)uval; break; }
				case 6: { unsigned char *o = va_arg(arguments, unsigned char *); *o = (unsigned char)uval; break; }
				default: { unsigned *o = va_arg(arguments, unsigned *); *o = (unsigned)uval; break; }
				}
			}
			++assigned;
			++format;
			continue;
		}
		if (*format == 'f' || *format == 'e' || *format == 'g'
		 || *format == 'F' || *format == 'E' || *format == 'G') {
			/* %f/%e/%g -> float*, %lf/%le/%lg -> double* (l length modifier);
			 * the token is a decimal floating constant ([sign] digits
			 * [. digits] [eE [sign] digits]) fed to strtod. */
			char tmp[64];
			int ti = 0, used = 0;
#define FTOK_COND (width == 0 || used < width)
			if ((*input == '+' || *input == '-') && FTOK_COND) {
				tmp[ti++] = *input++;
				used++;
			}
			while (*input >= '0' && *input <= '9' && FTOK_COND && ti < (int)sizeof(tmp) - 1) {
				tmp[ti++] = *input++;
				used++;
			}
			if (*input == '.' && FTOK_COND) {
				tmp[ti++] = *input++;
				used++;
				while (*input >= '0' && *input <= '9' && FTOK_COND && ti < (int)sizeof(tmp) - 1) {
					tmp[ti++] = *input++;
					used++;
				}
			}
			if ((*input == 'e' || *input == 'E') && FTOK_COND) {
				tmp[ti++] = *input++;
				used++;
				if ((*input == '+' || *input == '-') && FTOK_COND) {
					tmp[ti++] = *input++;
					used++;
				}
				while (*input >= '0' && *input <= '9' && FTOK_COND && ti < (int)sizeof(tmp) - 1) {
					tmp[ti++] = *input++;
					used++;
				}
			}
#undef FTOK_COND
			if (ti == 0)
				break;
			tmp[ti] = 0;
			if (length == 1) {
				double *out = va_arg(arguments, double *);
				*out = strtod(tmp, NULL);
			} else {
				float *out = va_arg(arguments, float *);
				*out = (float)strtod(tmp, NULL);
			}
			++assigned;
			++format;
			continue;
		}
		break;
	}
	return assigned;
}

int
sscanf(const char *input, const char *format, ...)
{
	va_list arguments;
	int result;
	va_start(arguments, format);
	result = vsscanf(input, format, arguments);
	va_end(arguments);
	return result;
}

int
scanf(const char *format, ...)
{
	char input[1024];
	va_list arguments;
	ssize_t count;
	int result;

	count = read(0, input, sizeof(input) - 1);
	if (count <= 0)
		return EOF;
	input[count] = 0;
	va_start(arguments, format);
	result = vsscanf(input, format, arguments);
	va_end(arguments);
	return result;
}

/* vfscanf/fscanf/vscanf: read the whole stream into a buffer and run the
 * string scanner on it (matches the existing scanf() approach; adequate
 * for the console/pipe streams the libc supports). */
static int
stream_to_buffer(FILE *stream, char *input, size_t size)
{
	size_t n = 0;
	int c;
	while (n + 1 < size && (c = fgetc(stream)) != EOF)
		input[n++] = (char)c;
	input[n] = 0;
	return (int)n;
}

int
vfscanf(FILE *stream, const char *format, va_list arguments)
{
	char input[1024];
	if (stream_to_buffer(stream, input, sizeof input) == 0)
		return EOF;
	return vsscanf(input, format, arguments);
}

int
fscanf(FILE *stream, const char *format, ...)
{
	va_list arguments;
	int result;
	va_start(arguments, format);
	result = vfscanf(stream, format, arguments);
	va_end(arguments);
	return result;
}

int
vscanf(const char *format, va_list arguments)
{
	return vfscanf(stdin, format, arguments);
}
