/* parse/expr_literal.c -- literal-token helpers used by primaryexpr.
 *
 * Implements:
 *   inttype()      -> classify an integer literal (LLU vs U vs ...)
 *   octval/hexval  -> single-digit decoders for oct/hex literals
 *   decodechar()   -> parse an escaped character/utf8 literal
 *   encodechar*()  -> encode a uint_least32_t to 8/16/32-bit storage
 *   stringconcat() -> fold adjacent narrow/wide string literals into one
 *
 * These are pure (no scope / no tok consumption) helpers invoked from
 * primaryexpr() in expr_primary.c and from string-literal handling in
 * stmt.c/sema/init.c. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"
#include "mcc.h"
#include "utf.h"
#include "expr_internal.h"

struct type *
inttype(unsigned long long val, bool decimal, char *suffix)
{
	enum {
		U = 1,
		L = 2,
		LL = 4,
		WB = 8,
	};
	static struct type *const types[] = {
		&typeint,
		&typeuint,   /* u */
		&typelong,   /* l */
		&typeulong,  /* ul */
		&typellong,  /* ll */
		&typeullong, /* ull */
	};
	int flags, i, step;
	bool sign;
	char *s;
	struct type *t;

	/* parse suffix */
	flags = 0;
	for (s = suffix; *s; ++s) {
		int new;

		switch (*s) {
		case 'u':
		case 'U': new = U; break;
		case 'l':
		case 'L': new = s[1] == s[0] ? (++s, LL) : L; break;
		case 'w': if (s[1] != 'b') goto invalid; ++s, new = WB; break;
		case 'W': if (s[1] != 'B') goto invalid; ++s, new = WB; break;
		default:
		invalid: error_code(E_SYNTAX, &tok.loc, "invalid integer constant suffix '%s'", suffix);
		}
		if (flags & new || flags & ~U && new & ~U)
			goto invalid;
		flags |= new;
	}

	/* find integer type */
	sign = !(flags & U);
	if (flags & WB) {
		for (i = 1;; ++i) {
			if (i > 64 - sign)
				goto notype;
			if (val <= 0xffffffffffffffff >> 64 - i)
				break;
		}
		return mkbitinttype(i + sign, sign);
	}
	assert(flags < countof(types));
	step = !sign || decimal ? 2 : 1;
	for (i = flags; i < countof(types); i += step) {
		t = types[i];
		if (typehasint(t, val, false))
			return t;
	}
	/* C 6.4.4.1: decimal constant beyond signed max has no type in its
	 * signed-only list (the behaviour is undefined).  Hex/oct/bin
	 * constants have unsigned types in their list so they reach
	 * &typeullong naturally via the step=1 loop above.  For the decimal
	 * case chibicc wraps the value into signed long long (two's
	 * complement), e.g. 18446744073709551615 >> 63 == -1; match that so
	 * the chibicc literal suite agrees. */
	if (decimal && !(flags & U)) {
		if (typehasint(&typellong, val, false))
			return &typellong;
		return &typellong;
	}
notype:
	error_code(E_CTYPE, &tok.loc, "no suitable type for constant '%s'", tok.lit);
}
static int
octval(int c)
{
	if ('0' <= c && c <= '7')
		return c - '0';
	return -1;
}
static int
hexval(int c)
{
	if ('0' <= c && c <= '9')
		return c - '0';
	if ('a' <= c && c <= 'f')
		return c - 'a' + 10;
	if ('A' <= c && c <= 'F')
		return c - 'A' + 10;
	return -1;
}
size_t
decodechar(const char *src, uint_least32_t *chr, bool *hexoct, const char *desc, struct location *loc)
{
	uint_least32_t c;
	size_t n;
	int i, v;
	const unsigned char *s = (const unsigned char *)src;

	*hexoct = false;
	if (*s == '\\') {
		++s;
		switch (*s) {
		case '\'':
		case '"':
		case '?':
		case '\\': c = *s;   ++s; break;
		case 'a':  c = '\a'; ++s; break;
		case 'b':  c = '\b'; ++s; break;
		case 'f':  c = '\f'; ++s; break;
		case 'n':  c = '\n'; ++s; break;
		case 'r':  c = '\r'; ++s; break;
		case 't':  c = '\t'; ++s; break;
		case 'v':  c = '\v'; ++s; break;
		case 'x':
			++s;
			c = 0;
			v = hexval(*s);
			assert(v >= 0);
			do {
				if (c > 0xffffffff / 16)
					error_code(E_SYNTAX, &tok.loc, "character constant escape is out of range");
				c = c * 16 + v;
				v = hexval(*++s);
			} while (v >= 0);
			*hexoct = true;
			break;
		default:
			c = 0;
			v = octval(*s);
			assert(v >= 0);
			i = 0;
			do {
				if (c > 0xffffffff / 8)
					error_code(E_SYNTAX, &tok.loc, "character constant escape is out of range");
				c = c * 8 + v;
				v = octval(*++s);
			} while (v >= 0 && ++i < 3);
			*hexoct = true;
		}
	} else {
		n = utf8dec(&c, s, 4);
		if (n == (size_t)-1)
			error(loc, "%s contains invalid UTF-8", desc);
		s += n;
	}
	*chr = c;
	return s - (const unsigned char *)src;
}
static size_t
encodechar8(void *dst, uint_least32_t chr, bool hexoct)
{
	if (!hexoct)
		return utf8enc(dst, chr);
	*(unsigned char *)dst = chr;
	return 1;
}
static size_t
encodechar16(void *dst, uint_least32_t chr, bool hexoct)
{
	if (!hexoct)
		return utf16enc(dst, chr) * sizeof(uint_least16_t);
	*(uint_least16_t *)dst = chr;
	return sizeof(uint_least16_t);
}
static size_t
encodechar32(void *dst, uint_least32_t chr, bool hexoct)
{
	*(uint_least32_t *)dst = chr;
	return sizeof(uint_least32_t);
}
struct type *
stringconcat(struct stringlit *str, bool forceutf8)
{
	static struct array parts;
	struct {
		struct location loc;
		char *str;
	} *p;
	int kind, newkind;
	struct type *t;
	size_t (*encodechar)(void *, uint_least32_t, bool);
	char *src;
	unsigned char *buf, *dst;
	uint_least32_t chr;
	bool hexoct;
	size_t len, width;

	assert(tok.kind == TSTRINGLIT);
	parts.len = 0;
	len = 0;
	kind = 0;
	do {
		src = tok.lit;
		switch (*src) {
		case 'u': if (src[1] == '8') ++src; /* fallthrough */
		case 'L':
		case 'U': newkind = *src, ++src; break;
		case '"': newkind = 0; break;
		default: assert(0);
		}
		if (kind != newkind && kind && newkind)
			error_code(E_SYNTAX, &tok.loc, "adjacent string literals have differing prefixes");
		if (newkind)
			kind = newkind;
		p = arrayadd(&parts, sizeof(*p));
		p->loc = tok.loc;
		p->str = src + 1;
		len += strlen(src) - 2;
		next();
	} while (tok.kind == TSTRINGLIT);
	if (forceutf8)
		kind = '8';
	++len;  /* null byte */
	switch (kind) {
	case 0: t = &typechar; break;
	case '8': t = &typechar; break;
	case 'u': t = &typeushort; break;
	case 'U': t = &typeuint; break;
	case 'L': t = targ->typewchar; break;
	default: assert(0);
	}
	switch (t->size) {
	case 1:
		width = 1;
		encodechar = encodechar8;
		break;
	case 2:
		width = sizeof(uint_least16_t);
		encodechar = encodechar16;
		break;
	case 4:
		width = sizeof(uint_least32_t);
		encodechar = encodechar32;
		break;
	default:
		assert(0);
	}
	buf = xreallocarray(NULL, len, width);
	str->data = buf;
	dst = buf;
	arrayforeach(&parts, p) {
		src = p->str;
		while (*src != '"') {
			src += decodechar(src, &chr, &hexoct, "string literal", &p->loc);
			dst += encodechar(dst, chr, hexoct);
		}
	}
	dst += encodechar(dst, 0, false);
	str->size = (dst - buf) / width;
	return t;
}
