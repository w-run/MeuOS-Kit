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
			unsigned value = 0;
			unsigned limit;
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
			/* Clamp target: the most negative int is INT_MAX+1. */
			limit = (is_signed && negative) ? (unsigned)INT_MAX + 1u : (unsigned)INT_MAX;
			while (*input && (width == 0 || digits < width)) {
				int digit = *input >= '0' && *input <= '9' ? *input - '0'
					: (*input >= 'a' && *input <= 'f' ? *input - 'a' + 10
					: (*input >= 'A' && *input <= 'F' ? *input - 'A' + 10 : base));
				if (digit >= base) break;
				/* Guard against unsigned wraparound on overflow; keep
				 * consuming the remaining digits either way. */
				if (value > (limit - (unsigned)digit) / (unsigned)base)
					overflow = 1;
				else if (!overflow)
					value = value * (unsigned)base + (unsigned)digit;
				++digits;
				++input;
			}
			if (!digits) break;
			if (is_signed) {
				int *out = va_arg(arguments, int *);
				if (overflow)
					*out = negative ? INT_MIN : INT_MAX;
				else if (negative)
					*out = value > (unsigned)INT_MAX ? INT_MIN : -(int)value;
				else
					*out = value > (unsigned)INT_MAX ? INT_MAX : (int)value;
			} else {
				unsigned *out = va_arg(arguments, unsigned *);
				*out = value;
			}
			++assigned;
			++format;
			continue;
		}
		if (*format == 'f' || *format == 'e' || *format == 'g'
		 || *format == 'F' || *format == 'E' || *format == 'G'
		 || *format == 'l') {
			/* %f/%e/%g -> float*, %lf/%le/%lg -> double*; the token is a
			 * decimal floating constant ([sign] digits [. digits] [eE
			 * [sign] digits]) which we collect and hand to strtod. */
			int is_long = 0;
			char tmp[64];
			int ti = 0, used = 0;
			if (*format == 'l') {
				if (format[1] == 'f' || format[1] == 'e' || format[1] == 'g'
				 || format[1] == 'F' || format[1] == 'E' || format[1] == 'G')
					is_long = 1;
				else
					break;
			}
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
			if (is_long) {
				double *out = va_arg(arguments, double *);
				*out = strtod(tmp, NULL);
			} else {
				float *out = va_arg(arguments, float *);
				*out = (float)strtod(tmp, NULL);
			}
			++assigned;
			format += is_long ? 2 : 1;
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
