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
	int width, int zero, int negative, const char *prefix)
{
	char digits[sizeof(value) * 3];
	char *cursor = digits + sizeof(digits);
	int length = 0;
	int prefix_length = 0;
#if defined(__i386__)
	/* Copy the Kl parameter to a local to avoid the i386 kloffset
	 * bug when taking the address of a Kl parameter. */
	unsigned long long v = value;
	extern unsigned meuos_u64_divmod(unsigned long long *, unsigned);
	do {
		unsigned digit = meuos_u64_divmod(&v, base);
		*--cursor = (char)(digit < 10 ? '0' + digit : 'a' + digit - 10);
		++length;
	} while (v);
#else
	do {
		unsigned digit = (unsigned)(value % base);
		*--cursor = (char)(digit < 10 ? '0' + digit : 'a' + digit - 10);
		value /= base;
		++length;
	} while (value);
#endif
	while (prefix[prefix_length])
		++prefix_length;
	if (negative)
		++prefix_length;
	if (!zero && __meuos_sink_repeat(sink, ' ', width - length - prefix_length) < 0)
		return -1;
	if (negative && __meuos_sink_put(sink, '-') < 0)
		return -1;
	while (*prefix)
		if (__meuos_sink_put(sink, *prefix++) < 0)
			return -1;
	if (zero && __meuos_sink_repeat(sink, '0', width - length - prefix_length) < 0)
		return -1;
	while (length-- > 0)
		if (__meuos_sink_put(sink, *cursor++) < 0)
			return -1;
	return 0;
}

int
__meuos_vformat(struct __meuos_print_sink *sink, const char *format, va_list arguments)
{
	while (*format) {
		int zero = 0;
		int width = 0;
		int length = 0;
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
			if (*format == '0')
				zero = 1;
			++format;
		}
		while (*format >= '0' && *format <= '9')
			width = width * 10 + *format++ - '0';
		if (*format == '.') {
			++format;
			while (*format >= '0' && *format <= '9')
				++format;
		}
		if (*format == 'l') {
			length = 1;
			++format;
			if (*format == 'l') {
				length = 2;
				++format;
			}
		} else if (*format == 'z') {
			length = 3;
			++format;
		}
		conversion = *format ? *format++ : '%';
		switch (conversion) {
		case '%':
			if (__meuos_sink_put(sink, '%') < 0) return -1;
			break;
		case 'c':
			if (__meuos_sink_put(sink, va_arg(arguments, int)) < 0) return -1;
			break;
		case 's': {
			const char *text = va_arg(arguments, const char *);
			int text_length;
			if (!text) text = "(null)";
			text_length = (int)strlen(text);
			if (__meuos_sink_repeat(sink, ' ', width - text_length) < 0) return -1;
			while (*text)
				if (__meuos_sink_put(sink, *text++) < 0) return -1;
			}
			break;
		case 'd':
		case 'i': {
			long long signed_value;
			if (length == 1) signed_value = va_arg(arguments, long);
			else if (length == 2) signed_value = va_arg(arguments, long long);
			else if (length == 3) signed_value = (long long)va_arg(arguments, ptrdiff_t);
			else signed_value = va_arg(arguments, int);
			value = signed_value < 0 ? (unsigned long long)(-(signed_value + 1)) + 1 : (unsigned long long)signed_value;
			if (__meuos_sink_number(sink, value, 10, width, zero, signed_value < 0, "") < 0) return -1;
			break;
		}
		case 'u':
		case 'x':
		case 'X':
		case 'o':
			if (length == 1) value = va_arg(arguments, unsigned long);
			else if (length == 2) value = va_arg(arguments, unsigned long long);
			else if (length == 3) value = va_arg(arguments, size_t);
			else value = va_arg(arguments, unsigned int);
			if (__meuos_sink_number(sink, value, conversion == 'o' ? 8 : conversion == 'u' ? 10 : 16,
				width, zero, 0, "") < 0) return -1;
			break;
		case 'p':
			value = (unsigned long long)(unsigned long)va_arg(arguments, void *);
			if (__meuos_sink_number(sink, value, 16, width, zero, 0, "0x") < 0) return -1;
			break;
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
