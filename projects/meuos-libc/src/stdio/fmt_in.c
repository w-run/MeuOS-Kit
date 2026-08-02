/* stdio/fmt_in.c -- scanf family: vsscanf + sscanf + scanf.
 *
 * Minimal implementation supporting %c / %s / %d / %x with optional
 * whitespace skipping. scanf() pulls a whole buffer from stdin with
 * one read(); this is enough for the test cases we run but is not a
 * full streaming scanf. */

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
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
		if (*format == 'd' || *format == 'x') {
			int base = *format == 'x' ? 16 : 10, negative = 0, digits = 0;
			int overflow = 0;
			unsigned value = 0;
			unsigned limit;
			int *out = va_arg(arguments, int *);
			if (*input == '-') { negative = 1; ++input; }
			else if (*input == '+') ++input;
			/* Clamp target: the most negative int is INT_MAX+1. */
			limit = negative ? (unsigned)INT_MAX + 1u : (unsigned)INT_MAX;
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
			if (overflow)
				*out = negative ? INT_MIN : INT_MAX;
			else if (negative)
				*out = value > (unsigned)INT_MAX ? INT_MIN : -(int)value;
			else
				*out = value > (unsigned)INT_MAX ? INT_MAX : (int)value;
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
