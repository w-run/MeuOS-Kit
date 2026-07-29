/* stdio/fmt_in.c -- scanf family: vsscanf + sscanf + scanf.
 *
 * Minimal implementation supporting %c / %s / %d / %x with optional
 * whitespace skipping. scanf() pulls a whole buffer from stdin with
 * one read(); this is enough for the test cases we run but is not a
 * full streaming scanf. */

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
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
		if (*format == 'c') {
			char *out = va_arg(arguments, char *);
			if (!*input) break;
			*out = *input++;
			++assigned;
			++format;
			continue;
		}
		while (isspace_int(*input)) ++input;
		if (*format == 's') {
			char *out = va_arg(arguments, char *);
			if (!*input) break;
			while (*input && !isspace_int(*input))
				*out++ = *input++;
			*out = 0;
			++assigned;
			++format;
			continue;
		}
		if (*format == 'd' || *format == 'x') {
			int base = *format == 'x' ? 16 : 10, negative = 0, digits = 0;
			unsigned value = 0;
			int *out = va_arg(arguments, int *);
			if (*input == '-') { negative = 1; ++input; }
			while (*input) {
				int digit = *input >= '0' && *input <= '9' ? *input - '0'
					: (*input >= 'a' && *input <= 'f' ? *input - 'a' + 10
					: (*input >= 'A' && *input <= 'F' ? *input - 'A' + 10 : base));
				if (digit >= base) break;
				value = value * (unsigned)base + (unsigned)digit;
				++digits;
				++input;
			}
			if (!digits) break;
			if (negative) {
				if (value > (unsigned)INT_MAX + 1u)
					*out = INT_MIN;
				else
					*out = -(int)value;
			} else {
				if (value > (unsigned)INT_MAX)
					*out = INT_MAX;
				else
					*out = (int)value;
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
