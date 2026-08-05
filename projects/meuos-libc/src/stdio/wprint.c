/* stdio/wprint.c -- wide printf family (C11 7.29.2.3).
 *
 * Architecture mirrors the narrow fmt_out.c: a single wide format engine
 * `__meuos_wvformat` parses %-conversions and dispatches to the shared number
 * / repeat / floating-point helper primitives (see internal.h), which emit
 * through a sink.  For wide output the sink's put callback receives an `int`
 * that is already a wide-char code (ASCII digits/padding emitted by the
 * shared helpers carry byte==code identity in the C locale; `%ls`/`%lc` pass
 * wide codes directly).  That lets fwprintf/wprintf/swprintf reuse the exact
 * same number/padding/fp primitives as the narrow family without duplicating
 * the converter logic; only the parse loop differs (wide format) plus the
 * `%c/%s/%lc/%ls/%C/%S` cases and the `%n` counting unit (wide char count).
 *
 * The narrow printf's pre-existing `%lc/%ls`-ignores-`l` bias is NOT touched
 * here (kept out of scope per team-lead); this file implements wide printf
 * correctly.
 */

#include <stdarg.h>
#include <stddef.h>
#include <wchar.h>
#include <stdio.h>
#include <string.h>
#include "internal.h"

/* --- wide sinks --------------------------------------------------------- */

/* FILE-backed: writes (wide char code)c to `stream`. */
static int
wfile_put(void *context, int c)
{
	return fputwc((wchar_t)c, context) == WEOF ? EOF : c;
}

/* wide-buffer sink for swprintf: writes (wide char code)c into buffer. */
struct __meuos_wbuf_sink {
	wchar_t *buffer;
	size_t size;    /* in wchar_t units incl. room for NUL */
	size_t pos;
};

static int
wbuf_put(void *context, int c)
{
	struct __meuos_wbuf_sink *sink = context;
	if (sink->size && sink->pos + 1 < sink->size)
		sink->buffer[sink->pos] = (wchar_t)c;
	++sink->pos;
	return c;
}

/* --- wide format engine ------------------------------------------------ */

/* Copies narrow `text` as wide chars; returns (wide char) length, respecting
 * precision (chars) and (null) handling done by caller. */
static void
wputs_narrow(struct __meuos_print_sink *sink, const char *text, int len)
{
	while (len-- > 0)
		if (__meuos_sink_put(sink, (unsigned char)*text++) < 0)
			break;
}

static void
wputs_wide(struct __meuos_print_sink *sink, const wchar_t *text, int len)
{
	while (len-- > 0)
		if (__meuos_sink_put(sink, *text++) < 0)
			break;
}

int
__meuos_wvformat(struct __meuos_print_sink *sink, const wchar_t *format,
	va_list arguments)
{
	while (*format) {
		int flags = 0;    /* bit 0='-', 1='+', 2=' ', 3='#', 4='0' */
		int width = 0;
		int length = 0;
		int precision = -1;
		wchar_t conversion;

		if (*format != L'%') {
			if (__meuos_sink_put(sink, *format++) < 0)
				return -1;
			continue;
		}
		++format;
		while (*format == L'-' || *format == L'+' || *format == L' '
		 || *format == L'#' || *format == L'0') {
			switch (*format) {
			case L'-': flags |= 1; break;
			case L'+': flags |= 2; break;
			case L' ': flags |= 4; break;
			case L'#': flags |= 8; break;
			case L'0': flags |= 16; break;
			}
			++format;
		}
		if (*format == L'*') {
			width = va_arg(arguments, int);
			if (width < 0) { flags |= 1; width = -width; }
			++format;
		} else {
			while (*format >= L'0' && *format <= L'9')
				width = width * 10 + *format++ - L'0';
		}
		if (*format == L'.') {
			++format;
			if (*format == L'*') {
				precision = va_arg(arguments, int);
				++format;
			} else {
				precision = 0;
				while (*format >= L'0' && *format <= L'9')
					precision = precision * 10 + *format++ - L'0';
			}
		}
		if (*format == L'l') {
			length = 1;
			++format;
			if (*format == L'l') { length = 2; ++format; }
		} else if (*format == L'h') {
			length = 5;
			++format;
			if (*format == L'h') { length = 6; ++format; }
		} else if (*format == L'z') {
			length = 3;
			++format;
		} else if (*format == L'L') {
			length = 4;
			++format;
		}
		conversion = *format ? *format++ : L'%';
		switch (conversion) {
		case L'%':
			if (__meuos_sink_put(sink, L'%') < 0) return -1;
			break;
		case L'c': {
			/* wide printf emits a single wide char; %c reads int, %lc reads
			 * wint_t (both int-backed here); %C is the XSI alias for %lc. */
			int ch;
			if (length == 1)
				ch = (int)va_arg(arguments, wint_t);
			else
				ch = va_arg(arguments, int);
			int pad = width - 1;
			if (pad < 0) pad = 0;
			if (!(flags & 1) && __meuos_sink_repeat(sink, L' ', pad) < 0) return -1;
			if (__meuos_sink_put(sink, ch) < 0) return -1;
			if ((flags & 1) && __meuos_sink_repeat(sink, L' ', pad) < 0) return -1;
			break;
		}
		case L'C': {  /* XSI alias for %lc */
			int ch = (int)va_arg(arguments, wint_t);
			int pad = width - 1;
			if (pad < 0) pad = 0;
			if (!(flags & 1) && __meuos_sink_repeat(sink, L' ', pad) < 0) return -1;
			if (__meuos_sink_put(sink, ch) < 0) return -1;
			if ((flags & 1) && __meuos_sink_repeat(sink, L' ', pad) < 0) return -1;
			break;
		}
		case L's': {
			int text_length;
			int pad;
			if (length == 1) {
				/* %ls: source is a wide string */
				const wchar_t *text = va_arg(arguments, const wchar_t *);
				if (!text) text = L"(null)";
				text_length = (int)wcslen(text);
				if (precision >= 0 && text_length > precision)
					text_length = precision;
				pad = width - text_length;
				if (pad < 0) pad = 0;
				if (!(flags & 1) && __meuos_sink_repeat(sink, L' ', pad) < 0) return -1;
				wputs_wide(sink, text, text_length);
				if ((flags & 1) && __meuos_sink_repeat(sink, L' ', pad) < 0) return -1;
			} else {
				/* %s: source is a multibyte string (byte-as-wchar in C locale) */
				const char *text = va_arg(arguments, const char *);
				if (!text) text = "(null)";
				text_length = (int)strlen(text);
				if (precision >= 0 && text_length > precision)
					text_length = precision;
				pad = width - text_length;
				if (pad < 0) pad = 0;
				if (!(flags & 1) && __meuos_sink_repeat(sink, L' ', pad) < 0) return -1;
				wputs_narrow(sink, text, text_length);
				if ((flags & 1) && __meuos_sink_repeat(sink, L' ', pad) < 0) return -1;
			}
			break;
		}
		case L'S': {  /* XSI alias for %ls */
			const wchar_t *text = va_arg(arguments, const wchar_t *);
			int text_length;
			int pad;
			if (!text) text = L"(null)";
			text_length = (int)wcslen(text);
			if (precision >= 0 && text_length > precision)
				text_length = precision;
			pad = width - text_length;
			if (pad < 0) pad = 0;
			if (!(flags & 1) && __meuos_sink_repeat(sink, L' ', pad) < 0) return -1;
			wputs_wide(sink, text, text_length);
			if ((flags & 1) && __meuos_sink_repeat(sink, L' ', pad) < 0) return -1;
			break;
		}
		case L'd':
		case L'i': {
			int zero = (flags & 16) != 0;
			long long signed_value;
			unsigned long long value;
			if (precision >= 0) zero = 0;
			if (length == 1) signed_value = va_arg(arguments, long);
			else if (length == 2) signed_value = va_arg(arguments, long long);
			else if (length == 3) signed_value = (long long)va_arg(arguments, ptrdiff_t);
			else signed_value = va_arg(arguments, int);
			value = signed_value < 0
				? (unsigned long long)(-(signed_value + 1)) + 1
				: (unsigned long long)signed_value;
			if (__meuos_sink_number(sink, value, 10, width, zero,
				signed_value < 0, "", flags & 1, flags & 2, precision, 0) < 0)
				return -1;
			break;
		}
		case L'u':
		case L'x':
		case L'X':
		case L'o': {
			int zero = (flags & 16) != 0;
			const char *prefix = "";
			unsigned long long value;
			if (precision >= 0) zero = 0;
			if (length == 1) value = va_arg(arguments, unsigned long);
			else if (length == 2) value = va_arg(arguments, unsigned long long);
			else if (length == 3) value = va_arg(arguments, size_t);
			else value = va_arg(arguments, unsigned int);
			if ((flags & 8) && value != 0) {
				if (conversion == L'x') prefix = "0x";
				else if (conversion == L'X') prefix = "0X";
				else if (conversion == L'o') prefix = "0";
			}
			if (__meuos_sink_number(sink, value,
				conversion == L'o' ? 8 : conversion == L'u' ? 10 : 16,
				width, zero, 0, prefix, flags & 1, 0, precision,
				conversion == L'X') < 0) return -1;
			break;
		}
		case L'p': {
			int zero = (flags & 16) != 0;
			unsigned long long value;
			value = (unsigned long long)(unsigned long)va_arg(arguments, void *);
			if (__meuos_sink_number(sink, value, 16, width, zero, 0, "0x",
				flags & 1, 0, -1, 0) < 0) return -1;
			break;
		}
		case L'n': {
			if (length == 1) { long *ptr = va_arg(arguments, long *); if (ptr) *ptr = (long)sink->total; }
			else if (length == 2) { long long *ptr = va_arg(arguments, long long *); if (ptr) *ptr = (long long)sink->total; }
			else if (length == 3) { size_t *ptr = va_arg(arguments, size_t *); if (ptr) *ptr = (size_t)sink->total; }
			else if (length == 5) { short *ptr = va_arg(arguments, short *); if (ptr) *ptr = (short)sink->total; }
			else if (length == 6) { signed char *ptr = va_arg(arguments, signed char *); if (ptr) *ptr = (signed char)sink->total; }
			else { int *ptr = va_arg(arguments, int *); if (ptr) *ptr = sink->total; }
			break;
		}
		case L'f':
		case L'F':
		case L'e':
		case L'E':
		case L'g':
		case L'G': {
			double dv = va_arg(arguments, double);
			if (__meuos_fmt_fp(sink, dv, (char)conversion, width, precision, flags) < 0)
				return -1;
			break;
		}
		case L'a':
		case L'A': {
			double dv = va_arg(arguments, double);
			if (__meuos_fmt_hexfp(sink, dv, (char)conversion, width, precision, flags) < 0)
				return -1;
			break;
		}
		default:
			if (__meuos_sink_put(sink, L'%') < 0 ||
			    __meuos_sink_put(sink, (int)conversion) < 0) return -1;
			break;
		}
	}
	return sink->total;
}

/* --- public wide printf family ------------------------------------------ */

int
vfwprintf(FILE *stream, const wchar_t *format, va_list arguments)
{
	struct __meuos_print_sink sink = { wfile_put, stream, 0 };
	return __meuos_wvformat(&sink, format, arguments);
}

int
fwprintf(FILE *stream, const wchar_t *format, ...)
{
	va_list arguments;
	int result;
	va_start(arguments, format);
	result = vfwprintf(stream, format, arguments);
	va_end(arguments);
	return result;
}

int
vwprintf(const wchar_t *format, va_list arguments)
{
	return vfwprintf(stdout, format, arguments);
}

int
wprintf(const wchar_t *format, ...)
{
	va_list arguments;
	int result;
	va_start(arguments, format);
	result = vwprintf(format, arguments);
	va_end(arguments);
	return result;
}

int
vswprintf(wchar_t *buffer, size_t size, const wchar_t *format, va_list arguments)
{
	struct __meuos_wbuf_sink wbuf = { buffer, size, 0 };
	struct __meuos_print_sink sink = { wbuf_put, &wbuf, 0 };
	int result = __meuos_wvformat(&sink, format, arguments);
	if (size)
		buffer[wbuf.pos < size ? wbuf.pos : size - 1] = L'\0';
	return result;
}

int
swprintf(wchar_t *buffer, size_t size, const wchar_t *format, ...)
{
	va_list arguments;
	int result;
	va_start(arguments, format);
	result = vswprintf(buffer, size, format, arguments);
	va_end(arguments);
	return result;
}
