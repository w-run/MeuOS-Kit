/* stdio/fmt_out.c -- printf family: vformat + sink primitives + wrappers.
 *
 * Architecture:
 *   sink_*  -- write single chars/repeated chars/numbers to a sink
 *   vformat -- parse %-conversions and dispatch to sink_*
 *   vfprintf/snprintf -- bind sinks to FILE* and char[] respectively
 *
 * The sink abstraction lets vfprintf and vsnprintf share the exact same
 * formatter; only the put() callback differs. */

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "internal.h"

int
__meuos_sink_put(struct __meuos_print_sink *sink, int character)
{
	if (sink->put(sink->context, character) == EOF)
		return -1;
	++sink->total;
	return 0;
}

static int
file_put(void *context, int character)
{
	return fputc(character, context);
}

int
__meuos_sink_repeat(struct __meuos_print_sink *sink, int character, int count)
{
	while (count-- > 0)
		if (__meuos_sink_put(sink, character) < 0)
			return -1;
	return 0;
}

int
__meuos_sink_number(struct __meuos_print_sink *sink, unsigned long long value, unsigned base,
	int width, int zero, int negative, const char *prefix, int left, int plus,
	int min_digits, int upper)
{
	char digits[sizeof(value) * 3];
	char *cursor = digits + sizeof(digits);
	int length = 0;
	int prefix_length = 0;
	int sign = 0;
	int ndigits;
	int pad;
#if defined(__i386__)
	/* Copy the Kl parameter to a local to avoid the i386 kloffset
	 * bug when taking the address of a Kl parameter. */
	unsigned long long v = value;
	extern unsigned meuos_u64_divmod(unsigned long long *, unsigned);
	do {
		unsigned digit = meuos_u64_divmod(&v, base);
		*--cursor = (char)(digit < 10 ? '0' + digit : (upper ? 'A' : 'a') + digit - 10);
		++length;
	} while (v);
#else
	do {
		unsigned digit = (unsigned)(value % base);
		*--cursor = (char)(digit < 10 ? '0' + digit : (upper ? 'A' : 'a') + digit - 10);
		value /= base;
		++length;
	} while (value);
#endif
	/* %.0d with value 0 prints no digits at all. */
	if (min_digits == 0 && length == 1 && cursor[0] == '0')
		length = 0;
	while (prefix[prefix_length])
		++prefix_length;
	if (negative)
		sign = '-';
	else if (plus)
		sign = '+';
	ndigits = length < min_digits ? min_digits : length;
	{
		int content = ndigits + prefix_length + (sign != 0);
		pad = width > content ? width - content : 0;
	}
	if (!left && !zero && __meuos_sink_repeat(sink, ' ', pad) < 0)
		return -1;
	if (sign && __meuos_sink_put(sink, sign) < 0)
		return -1;
	while (*prefix)
		if (__meuos_sink_put(sink, *prefix++) < 0)
			return -1;
	if (zero && !left && __meuos_sink_repeat(sink, '0', pad) < 0)
		return -1;
	if (ndigits > length && __meuos_sink_repeat(sink, '0', ndigits - length) < 0)
		return -1;
	while (length-- > 0)
		if (__meuos_sink_put(sink, *cursor++) < 0)
			return -1;
	if (left && __meuos_sink_repeat(sink, ' ', pad) < 0)
		return -1;
	return 0;
}

int
__meuos_vformat(struct __meuos_print_sink *sink, const char *format, va_list arguments)
{
	while (*format) {
		int flags = 0;    /* bit 0='-', 1='+', 2=' ', 3='#', 4='0' */
		int width = 0;
		int length = 0;
		int precision = -1;
		char conversion;
		unsigned long long value;

		if (*format != '%') {
			if (__meuos_sink_put(sink, *format++) < 0)
				return -1;
			continue;
		}
		++format;
		while (*format == '-' || *format == '+' || *format == ' ' || *format == '#'
		 || *format == '0') {
			switch (*format) {
			case '-': flags |= 1; break;
			case '+': flags |= 2; break;
			case ' ': flags |= 4; break;
			case '#': flags |= 8; break;
			case '0': flags |= 16; break;
			}
			++format;
		}
		if (*format == '*') {
			/* 星号宽度：从可变参数读取（负值视为左对齐 + 取绝对值）。 */
			width = va_arg(arguments, int);
			if (width < 0) {
				flags |= 1;
				width = -width;
			}
			++format;
		} else {
			while (*format >= '0' && *format <= '9')
				width = width * 10 + *format++ - '0';
		}
		if (*format == '.') {
			++format;
			if (*format == '*') {
				/* 星号精度：从可变参数读取（负值视为省略精度）。 */
				precision = va_arg(arguments, int);
				++format;
			} else {
				precision = 0;
				while (*format >= '0' && *format <= '9')
					precision = precision * 10 + *format++ - '0';
			}
		}
		if (*format == 'l') {
			length = 1;
			++format;
			if (*format == 'l') {
				length = 2;
				++format;
			}
		} else if (*format == 'h') {
			length = 5;
			++format;
			if (*format == 'h') {
				length = 6;
				++format;
			}
		} else if (*format == 'z') {
			length = 3;
			++format;
		} else if (*format == 'L') {
			/* long double — read as double (minimal libc limitation). */
			length = 4;
			++format;
		}
		conversion = *format ? *format++ : '%';
		switch (conversion) {
		case '%':
			if (__meuos_sink_put(sink, '%') < 0) return -1;
			break;
		case 'c': {
			int ch = va_arg(arguments, int);
			int pad = width - 1;
			if (pad < 0) pad = 0;
			if (!(flags & 1) && __meuos_sink_repeat(sink, ' ', pad) < 0) return -1;
			if (__meuos_sink_put(sink, ch) < 0) return -1;
			if ((flags & 1) && __meuos_sink_repeat(sink, ' ', pad) < 0) return -1;
			}
			break;
		case 's': {
			const char *text = va_arg(arguments, const char *);
			int text_length;
			int pad;
			if (!text) text = "(null)";
			text_length = (int)strlen(text);
			if (precision >= 0 && text_length > precision)
				text_length = precision;
			pad = width - text_length;
			if (pad < 0) pad = 0;
			if (!(flags & 1) && __meuos_sink_repeat(sink, ' ', pad) < 0) return -1;
			while (text_length--)
				if (__meuos_sink_put(sink, *text++) < 0) return -1;
			if ((flags & 1) && __meuos_sink_repeat(sink, ' ', pad) < 0) return -1;
			}
			break;
		case 'd':
		case 'i': {
			int zero = (flags & 16) != 0;
			long long signed_value;
			if (precision >= 0) zero = 0;	/* 精度与 0 标志互斥 */
			if (length == 1) signed_value = va_arg(arguments, long);
			else if (length == 2) signed_value = va_arg(arguments, long long);
			else if (length == 3) signed_value = (long long)va_arg(arguments, ptrdiff_t);
			else signed_value = va_arg(arguments, int);
			value = signed_value < 0 ? (unsigned long long)(-(signed_value + 1)) + 1 : (unsigned long long)signed_value;
			if (__meuos_sink_number(sink, value, 10, width, zero, signed_value < 0,
				"", flags & 1, flags & 2, precision, 0) < 0) return -1;
			break;
		}
		case 'u':
		case 'x':
		case 'X':
		case 'o': {
			int zero = (flags & 16) != 0;
			const char *prefix = "";
			if (precision >= 0) zero = 0;
			if (length == 1) value = va_arg(arguments, unsigned long);
			else if (length == 2) value = va_arg(arguments, unsigned long long);
			else if (length == 3) value = va_arg(arguments, size_t);
			else value = va_arg(arguments, unsigned int);
			/* '#'：hex 加 0x/0X 前缀（值非零时），octal 补前导 0。 */
			if ((flags & 8) && value != 0) {
				if (conversion == 'x') prefix = "0x";
				else if (conversion == 'X') prefix = "0X";
				else if (conversion == 'o') prefix = "0";
			}
			if (__meuos_sink_number(sink, value, conversion == 'o' ? 8 : conversion == 'u' ? 10 : 16,
				width, zero, 0, prefix, flags & 1, 0, precision,
				conversion == 'X') < 0) return -1;
			break;
		}
		case 'p': {
			int zero = (flags & 16) != 0;
			value = (unsigned long long)(unsigned long)va_arg(arguments, void *);
			if (__meuos_sink_number(sink, value, 16, width, zero, 0, "0x",
				flags & 1, 0, -1, 0) < 0) return -1;
			break;
		}
		case 'n': {
			if (length == 1) { long *ptr = va_arg(arguments, long *); if (ptr) *ptr = (long)sink->total; }
			else if (length == 2) { long long *ptr = va_arg(arguments, long long *); if (ptr) *ptr = (long long)sink->total; }
			else if (length == 3) { size_t *ptr = va_arg(arguments, size_t *); if (ptr) *ptr = (size_t)sink->total; }
			else if (length == 5) { short *ptr = va_arg(arguments, short *); if (ptr) *ptr = (short)sink->total; }
			else if (length == 6) { signed char *ptr = va_arg(arguments, signed char *); if (ptr) *ptr = (signed char)sink->total; }
			else { int *ptr = va_arg(arguments, int *); if (ptr) *ptr = sink->total; }
			break;
		}
		case 'f':
		case 'F':
		case 'e':
		case 'E':
		case 'g':
		case 'G': {
			double dv = va_arg(arguments, double);
			if (__meuos_fmt_fp(sink, dv, conversion, width, precision, flags) < 0)
				return -1;
			break;
		}
		case 'a':
		case 'A': {
			double dv = va_arg(arguments, double);
			if (__meuos_fmt_hexfp(sink, dv, conversion, width, precision, flags) < 0)
				return -1;
			break;
		}
		default:
			if (__meuos_sink_put(sink, '%') < 0 || __meuos_sink_put(sink, conversion) < 0) return -1;
			break;
		}
	}
	return sink->total;
}

int
vfprintf(FILE *stream, const char *format, va_list arguments)
{
	struct __meuos_print_sink sink = { file_put, stream, 0 };
	return __meuos_vformat(&sink, format, arguments);
}

int
fprintf(FILE *stream, const char *format, ...)
{
	va_list arguments;
	int result;
	va_start(arguments, format);
	result = vfprintf(stream, format, arguments);
	va_end(arguments);
	return result;
}

struct __meuos_buffer_sink { char *buffer; size_t size; size_t pos; };

static int
buffer_put(void *context, int character)
{
	struct __meuos_buffer_sink *sink = context;
	if (sink->size && sink->pos + 1 < sink->size)
		sink->buffer[sink->pos] = (char)character;
	++sink->pos;
	return character;
}

int
vsnprintf(char *buffer, size_t size, const char *format, va_list arguments)
{
	struct __meuos_buffer_sink buffer_sink = { buffer, size, 0 };
	struct __meuos_print_sink sink = { buffer_put, &buffer_sink, 0 };
	int result = __meuos_vformat(&sink, format, arguments);
	if (size)
		buffer[buffer_sink.pos < size ? buffer_sink.pos : size - 1] = 0;
	return result;
}

int
snprintf(char *buffer, size_t size, const char *format, ...)
{
	va_list arguments;
	int result;
	va_start(arguments, format);
	result = vsnprintf(buffer, size, format, arguments);
	va_end(arguments);
	return result;
}

int
sprintf(char *buffer, const char *format, ...)
{
	va_list arguments;
	int result;
	va_start(arguments, format);
	result = vsnprintf(buffer, (size_t)-1, format, arguments);
	va_end(arguments);
	return result;
}

int
vsprintf(char *buffer, const char *format, va_list arguments)
{
	return vsnprintf(buffer, (size_t)-1, format, arguments);
}

int
vprintf(const char *format, va_list arguments)
{
	return vfprintf(stdout, format, arguments);
}

int
printf(const char *format, ...)
{
	va_list arguments;
	int result;
	va_start(arguments, format);
	result = vfprintf(stdout, format, arguments);
	va_end(arguments);
	return result;
}
