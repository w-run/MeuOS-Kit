#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"
#include "mcc.h"

struct buffer {
	unsigned char *str;
	size_t len, cap;
};

struct scanner {
	int chr;
	bool usebuf;
	bool sawspace;
	FILE *file;
	struct location loc;
	struct buffer buf;
	struct scanner *next;
};

static struct scanner *scanner;

static void
bufadd(struct buffer *b, int c)
{
	if (b->len >= b->cap) {
		b->cap = b->cap ? b->cap * 2 : 1<<8;
		b->str = xreallocarray(b->str, b->cap, 1);
	}
	b->str[b->len++] = c;
}

static char *
bufget(struct buffer *b)
{
	char *s;

	bufadd(b, '\0');
	s = xmalloc(b->len);
	memcpy(s, b->str, b->len);
	b->len = 0;

	return s;
}

static void
nextchar(struct scanner *s)
{
	int c;

	if (s->usebuf)
		bufadd(&s->buf, s->chr);
	for (;;) {
		s->chr = getc(s->file);
		if (s->chr == '\n') {
			++s->loc.line, s->loc.col = 0;
			break;
		}
		++s->loc.col;
		if (s->chr != '\\')
			break;
		c = getc(s->file);
		if (c != '\n') {
			ungetc(c, s->file);
			break;
		}
		++s->loc.line, s->loc.col = 0;
	}
}

static int
op2(struct scanner *s, int t1, int t2)
{
	nextchar(s);
	if (s->chr != '=')
		return t1;
	nextchar(s);
	return t2;
}

static int
op3(struct scanner *s, int t1, int t2, int t3)
{
	int c;

	c = s->chr;
	nextchar(s);
	if (s->chr == '=') {
		nextchar(s);
		return t2;
	}
	if (s->chr != c)
		return t1;
	nextchar(s);
	return t3;
}

static int
op4(struct scanner *s, int t1, int t2, int t3, int t4)
{
	int c;

	c = s->chr;
	nextchar(s);
	if (s->chr == '=') {
		nextchar(s);
		return t2;
	}
	if (s->chr != c)
		return t1;
	nextchar(s);
	if (s->chr != '=')
		return t3;
	nextchar(s);
	return t4;
}

static int
hexdigval(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/* Append the UTF-8 encoding of the code point val to b. */
static void
bufaddutf8(struct buffer *b, unsigned long val)
{
	if (val < 0x80) {
		bufadd(b, val);
	} else if (val < 0x800) {
		bufadd(b, 0xC0 | val >> 6);
		bufadd(b, 0x80 | (val & 0x3F));
	} else if (val < 0x10000) {
		bufadd(b, 0xE0 | val >> 12);
		bufadd(b, 0x80 | (val >> 6 & 0x3F));
		bufadd(b, 0x80 | (val & 0x3F));
	} else {
		bufadd(b, 0xF0 | val >> 18);
		bufadd(b, 0x80 | (val >> 12 & 0x3F));
		bufadd(b, 0x80 | (val >> 6 & 0x3F));
		bufadd(b, 0x80 | (val & 0x3F));
	}
}

/* Resolve a \N{...} character name (without braces) to its code point.
 * Shipping the whole Unicode name table is not practical, so only the
 * Latin letter names are recognised (C++23 P2361, shared by the string
 * literal and identifier paths). */
static unsigned long
namedval(const char *name, size_t n)
{
	if (n == sizeof("LATIN CAPITAL LETTER A") - 1
	    && memcmp(name, "LATIN CAPITAL LETTER ", 21) == 0
	    && name[21] >= 'A' && name[21] <= 'Z')
		return name[21];
	if (n == sizeof("LATIN SMALL LETTER A") - 1
	    && memcmp(name, "LATIN SMALL LETTER ", 19) == 0
	    && name[19] >= 'A' && name[19] <= 'Z')
		return name[19] - 'A' + 'a';
	error_code(E_SYNTAX, NULL, "unsupported named universal character '%.*s'", (int)n, name);
}

static int
ident(struct scanner *s)
{
	int tok;

	s->usebuf = true;
	for (;;) {
		if (isalnum(s->chr) || s->chr == '_' || s->chr == '$'
		    || (unsigned char)s->chr >= 0x80) {
			nextchar(s);
		} else if (s->chr == '\\') {
			/* UCN in identifiers: \uXXXX, \UXXXXXXXX (fixed width),
			 * or the C++23 P2290 delimited forms \u{...} / \U{...},
			 * plus the C++23 P2361 named form \N{NAME}.  We read the
			 * UCN directly from the file, add UTF-8 to the buffer and
			 * leave s->chr on the character after the UCN. */
			int ucn_val = 0, ucn_n = 0, hexdig, d;
			int c1 = getc(s->file);
			if (c1 == 'u') ucn_n = 4;
			else if (c1 == 'U') ucn_n = 8;
			else if (c1 == 'N') {
				/* \N{NAME}: read the name then resolve it. */
				int c2 = getc(s->file);
				char name[64];
				size_t nlen = 0;
				if (c2 != '{') {
					if (c2 != EOF)
						ungetc(c2, s->file);
					break;  /* not a UCN escape */
				}
				for (;;) {
					int nc = getc(s->file);
					if (nc == '}') {
						s->chr = getc(s->file);
						break;
					}
					if (nc == EOF || nc == '\n') {
						error_code(E_SYNTAX, &s->loc, "unterminated named universal character");
					}
					if (nlen == sizeof(name) - 1)
						error_code(E_SYNTAX, &s->loc, "named universal character is too long");
					name[nlen++] = nc;
				}
				ucn_val = namedval(name, nlen);
				bufaddutf8(&s->buf, ucn_val);
				continue;
			} else {
				ungetc(c1, s->file);
				break;
			}
			{
				int c2 = getc(s->file);
				if (c2 == '{') {
					/* C++23 P2290 delimited form \u{...} / \U{...}. */
					if ((d = hexdigval(s->chr = getc(s->file))) < 0)
						error_code(E_SYNTAX, &s->loc, "empty delimited universal character name");
					while (d >= 0) {
						if (ucn_val > 0x0fffffff)
							error_code(E_SYNTAX, &s->loc, "universal character name out of range");
						ucn_val = ucn_val * 16 + d;
						d = hexdigval(s->chr = getc(s->file));
					}
					if (s->chr != '}')
						error_code(E_SYNTAX, &s->loc, "unterminated delimited universal character name");
					s->chr = getc(s->file);
					if (ucn_val > 0x10ffff)
						error_code(E_SYNTAX, &s->loc, "universal character name out of range");
					bufaddutf8(&s->buf, ucn_val);
					continue;
				}
				ungetc(c2, s->file);
			}
			for (hexdig = 0; hexdig < ucn_n; hexdig++) {
				int hc = getc(s->file);
				if (hc >= '0' && hc <= '9') ucn_val = (ucn_val << 4) | (hc - '0');
				else if (hc >= 'a' && hc <= 'f') ucn_val = (ucn_val << 4) | (hc - 'a' + 10);
				else if (hc >= 'A' && hc <= 'F') ucn_val = (ucn_val << 4) | (hc - 'A' + 10);
				else { ungetc(hc, s->file); break; }
			}
			if (hexdig == ucn_n) {
				bufaddutf8(&s->buf, ucn_val);
				/* Read the next character (don't buffer it yet) */
				s->chr = getc(s->file);
				/* s->chr now holds the char after UCN; loop will check it */
			} else {
				break;
			}
		} else {
			break;
		}
	}
	tok = tokenget(s->buf.str, s->buf.len);
	s->usebuf = false;
	s->buf.len = 0;
	return tok;
}

static int
isodigit(int c)
{
	return (unsigned)c - '0' < 8;
}

/* C++23 P1467 (and C23 _FloatN): accept the extended floating-point
 * suffixes f16/f32/f64/f128/bf16.  mcc has no distinct _FloatN types, so
 * the suffix is rewritten in place to the closest supported one: the
 * 16/32 bit forms become 'f' (float), the 64/128 bit forms become an
 * empty suffix (double).  Everything downstream then sees a plain
 * floating constant. */
static void
extfloatsuffix(struct buffer *b)
{
	static const struct {
		const char *name;
		char repl;
	} suffix[] = {
		{"bf16", 'f'},  /* before f16: "1.0bf16" also ends in "f16" */
		{"f128", '\0'},
		{"f16", 'f'},
		{"f32", 'f'},
		{"f64", '\0'},
	};
	size_t i, j, n;
	int c;

	if (b->len < 2)
		return;
	/* A hexadecimal constant without an exponent is an integer, so
	 * "0xf32" and friends must keep their digits. */
	if (b->str[0] == '0' && (b->str[1] == 'x' || b->str[1] == 'X')) {
		for (i = 2; i < b->len; ++i) {
			if (b->str[i] == 'p' || b->str[i] == 'P')
				break;
		}
		if (i == b->len)
			return;
	}
	for (i = 0; i < countof(suffix); ++i) {
		n = strlen(suffix[i].name);
		if (b->len <= n)
			continue;
		for (j = 0; j < n; ++j) {
			c = b->str[b->len - n + j];
			if (tolower(c) != suffix[i].name[j])
				break;
		}
		if (j < n)
			continue;
		/* only a real suffix, i.e. one directly after the value */
		c = b->str[b->len - n - 1];
		if (!isdigit(c) && c != '.')
			continue;
		b->len -= n;
		if (suffix[i].repl)
			bufadd(b, suffix[i].repl);
		return;
	}
}

static enum tokenkind
number(struct scanner *s)
{
	bool allowsign = false;

	s->usebuf = true;
	for (;;) {
		nextchar(s);
		switch (s->chr) {
		case 'e':
		case 'E':
		case 'p':
		case 'P':
			allowsign = true;
			break;
		case '+':
		case '-':
			if (!allowsign)
				goto done;
			break;
		case '.':
			allowsign = false;
			break;
		case '\'':
			nextchar(s);
			--s->buf.len;  /* ignore separator */
			/* fallthrough */
		default:
			if (!isalnum(s->chr) && s->chr != '_')
				goto done;
			allowsign = false;
		}
	}
done:
	extfloatsuffix(&s->buf);
	return TNUMBER;
}

/* Is the literal currently being scanned a narrow (single-byte) one?
 * The buffer begins with the literal prefix, or the opening quote for an
 * unprefixed narrow literal: "..." / u8"..." are narrow, while L"...",
 * U"..." and u"..." are wide. */
static bool
litnarrow(struct scanner *s)
{
	if (s->buf.len == 0)
		return true;
	switch (s->buf.str[0]) {
	case 'L':
	case 'U':
		return false;
	case 'u':
		/* u"..." (char16_t) is wide; u8"..." is a UTF-8 narrow string. */
		return !(s->buf.len >= 2 && s->buf.str[1] != '8');
	default:
		return true;
	}
}

/* Replace the escape sequence that starts at offset start of the token
 * buffer with a canonical form the literal decoder already understands.
 *
 * A code point below 0x80 is written as a three-digit octal escape: that
 * keeps quotes, backslashes and NUL escaped in the token text, and the
 * fixed width means a following digit can never be absorbed into it.
 * Larger values become UTF-8 bytes, except for \x{...}, whose value has
 * no character-set meaning and is emitted as a plain \x escape.
 *
 * A wide \x{...} above 0x1FF is also emitted as a plain \x escape, which
 * the downstream decoder reads greedily; a wide literal of the form
 * \x{BIG} followed by a hexadecimal digit therefore merges the two into
 * one value (a known limitation shared with the non-delimited \x form).
 * For a narrow literal only the low byte survives, so we write it as a
 * three-digit octal escape regardless of magnitude -- that keeps the
 * narrowing correct and a trailing digit unambiguous. */
static void
rewriteescape(struct scanner *s, size_t start, unsigned long val, bool hexoct, bool narrow)
{
	int shift;

	s->buf.len = start;
	if (val < (hexoct ? 0x200UL : 0x80UL)) {
		bufadd(&s->buf, '\\');
		bufadd(&s->buf, '0' + (val >> 6 & 7));
		bufadd(&s->buf, '0' + (val >> 3 & 7));
		bufadd(&s->buf, '0' + (val & 7));
	} else if (hexoct && narrow) {
		unsigned long b = val & 0xFF;
		bufadd(&s->buf, '\\');
		bufadd(&s->buf, '0' + (b >> 6 & 7));
		bufadd(&s->buf, '0' + (b >> 3 & 7));
		bufadd(&s->buf, '0' + (b & 7));
	} else if (hexoct) {
		bufadd(&s->buf, '\\');
		bufadd(&s->buf, 'x');
		for (shift = 28; shift > 0 && !(val >> shift); shift -= 4)
			;
		for (; shift >= 0; shift -= 4)
			bufadd(&s->buf, "0123456789abcdef"[val >> shift & 15]);
	} else {
		bufaddutf8(&s->buf, val);
	}
}

/* C++23 P2290 delimited escape sequence: read "{h...h}" with s->chr on
 * the opening brace. */
static unsigned long
delimited(struct scanner *s, const char *what)
{
	unsigned long val = 0;
	int d;

	nextchar(s);
	if (hexdigval(s->chr) < 0)
		error_code(E_SYNTAX, &s->loc, "empty delimited %s", what);
	while ((d = hexdigval(s->chr)) >= 0) {
		if (val > 0x0fffffff)
			error_code(E_SYNTAX, &s->loc, "%s out of range", what);
		val = val * 16 + d;
		nextchar(s);
	}
	if (s->chr != '}')
		error_code(E_SYNTAX, &s->loc, "unterminated delimited %s", what);
	nextchar(s);
	return val;
}

/* C++23 P2361 named universal character: read "{NAME}" with s->chr on
 * the opening brace, then resolve it via namedval(). */
static unsigned long
namedchar(struct scanner *s)
{
	char name[64];
	size_t n = 0;

	if (s->chr != '{')
		error_code(E_SYNTAX, &s->loc, "expected '{' after \\N");
	nextchar(s);
	while (s->chr != '}') {
		if (s->chr == '\n' || s->chr == EOF)
			error_code(E_SYNTAX, &s->loc, "unterminated named universal character");
		if (n == sizeof(name) - 1)
			error_code(E_SYNTAX, &s->loc, "named universal character is too long");
		name[n++] = s->chr;
		nextchar(s);
	}
	nextchar(s);
	return namedval(name, n);
}

static void
escape(struct scanner *s)
{
	size_t start;
	unsigned long val;
	bool narrow;
	int i, n, d;

	start = s->buf.len;  /* where nextchar() is about to put the '\\' */
	narrow = litnarrow(s);
	nextchar(s);
	if (s->chr == 'u' || s->chr == 'U') {
		n = s->chr == 'u' ? 4 : 8;
		nextchar(s);
		if (s->chr == '{') {
			val = delimited(s, "universal character name");
		} else {
			for (val = 0, i = 0; i < n; ++i) {
				d = hexdigval(s->chr);
				if (d < 0)
					error_code(E_SYNTAX, &s->loc, "invalid universal character name");
				val = val * 16 + d;
				nextchar(s);
			}
		}
		if (val > 0x10ffff)
			error_code(E_SYNTAX, &s->loc, "universal character name out of range");
		rewriteescape(s, start, val, false, narrow);
	} else if (s->chr == 'N') {
		nextchar(s);
		rewriteescape(s, start, namedchar(s), false, narrow);
	} else if (s->chr == 'x') {
		nextchar(s);
		if (s->chr == '{') {
			rewriteescape(s, start, delimited(s, "hexadecimal escape sequence"), true, narrow);
			return;
		}
		if (!isxdigit(s->chr))
			error_code(E_SYNTAX, &s->loc, "invalid hexadecimal escape sequence");
		do nextchar(s);
		while (isxdigit(s->chr));
	} else if (isodigit(s->chr)) {
		nextchar(s);
		if (isodigit(s->chr)) {
			nextchar(s);
			if (isodigit(s->chr))
				nextchar(s);
		}
	} else if (strchr("'\"?\\abfnrtv", s->chr)) {
		nextchar(s);
	} else {
		error_code(E_SYNTAX, &s->loc, "invalid escape sequence");
	}
}

static enum tokenkind
charconst(struct scanner *s)
{
	s->usebuf = true;
	nextchar(s);
	for (;;) {
		switch (s->chr) {
		case '\\':
			escape(s);
			break;
		case '\'':
			nextchar(s);
			return TCHARCONST;
		case '\n':
			error_code(E_SYNTAX, &s->loc, "newline in character constant");
		case EOF:
			error_code(E_SYNTAX, &s->loc, "EOF in character constant");
		default:
			nextchar(s);
			break;
		}
	}
}

static int
stringlit(struct scanner *s)
{
	s->usebuf = true;
	nextchar(s);
	for (;;) {
		switch (s->chr) {
		case '\\':
			escape(s);
			break;
		case '"':
			nextchar(s);
			return TSTRINGLIT;
		case '\n':
			error_code(E_SYNTAX, &s->loc, "newline in string literal");
		case EOF:
			error_code(E_SYNTAX, &s->loc, "EOF in string literal");
		default:
			nextchar(s);
			break;
		}
	}
}

static bool
comment(struct scanner *s)
{
	int last;

	switch (s->chr) {
	case '/':  /* C++-style comment */
		do nextchar(s);
		while (s->chr != '\n' && s->chr != EOF);
		break;
	case '*':  /* C-style comment */
		nextchar(s);
		do {
			last = s->chr;
			nextchar(s);
			if (s->chr == EOF)
				error_code(E_SYNTAX, &s->loc, "EOF in comment");
		} while (last != '*' || s->chr != '/');
		nextchar(s);
		break;
	default:
		return false;
	}
	s->sawspace = true;
	return true;
}

static int
scankind(struct scanner *s, struct location *loc)
{
	enum tokenkind tok;
	struct location oldloc;

again:
	*loc = s->loc;
	switch (s->chr) {
	case ' ':
	case '\t':
	case '\f':
	case '\v':
		s->sawspace = true;
		nextchar(s);
		goto again;
	case '!':
		return op2(s, TLNOT, TNEQ);
	case '"':
		return stringlit(s);
	case '#':
		nextchar(s);
		if (s->chr != '#')
			return THASH;
		nextchar(s);
		return THASHHASH;
	case '%':
		return op2(s, TMOD, TMODASSIGN);
	case '&':
		return op3(s, TBAND, TBANDASSIGN, TLAND);
	case '\'':
		return charconst(s);
	case '*':
		return op2(s, TMUL, TMULASSIGN);
	case '+':
		return op3(s, TADD, TADDASSIGN, TINC);
	case '-':
		tok = op3(s, TSUB, TSUBASSIGN, TDEC);
		if (tok != TSUB || s->chr != '>')
			return tok;
		nextchar(s);
		return TARROW;
	case '/':
		tok = op2(s, TDIV, TDIVASSIGN);
		if (tok == TDIV && comment(s))
			goto again;
		return tok;
	case '<':
		/* C++20 three-way comparison `<=>`: `<` `=` `>` */
		nextchar(s); /* consume the first '<'; s->chr is the next char */
		if (s->chr == '<') {
			nextchar(s);
			if (s->chr == '=') {
				nextchar(s);
				return TSHLASSIGN;
			}
			return TSHL;
		}
		if (s->chr == '=') {
			nextchar(s);
			if (s->chr == '>') {
				nextchar(s);
				return TSPACESHIP;
			}
			return TLEQ;
		}
		return TLESS;
	case '=':
		return op2(s, TASSIGN, TEQL);
	case '>':
		return op4(s, TGREATER, TGEQ, TSHR, TSHRASSIGN);
	case '^':
		return op2(s, TXOR, TXORASSIGN);
	case '|':
		return op3(s, TBOR, TBORASSIGN, TLOR);
	case '\n':
		nextchar(s);
		return TNEWLINE;
	case '[':
		nextchar(s);
		return TLBRACK;
	case ']':
		nextchar(s);
		return TRBRACK;
	case '(':
		nextchar(s);
		return TLPAREN;
	case ')':
		nextchar(s);
		return TRPAREN;
	case '{':
		nextchar(s);
		return TLBRACE;
	case '}':
		nextchar(s);
		return TRBRACE;
	case '.':
		nextchar(s);
		if (isdigit(s->chr)) {
			bufadd(&s->buf, '.');
			return number(s);
		}
		if (s->chr != '.')
			return TPERIOD;
		oldloc = s->loc;
		nextchar(s);
		if (s->chr != '.') {
			ungetc(s->chr, s->file);
			s->loc = oldloc;
			s->chr = '.';
			return TPERIOD;
		}
		nextchar(s);
		return TELLIPSIS;
	case '~':
		nextchar(s);
		return TBNOT;
	case '?':
		nextchar(s);
		return TQUESTION;
	case ':':
		nextchar(s);
		if (s->chr != ':')
			return TCOLON;
		nextchar(s);
		return TCOLONCOLON;
	case ';':
		nextchar(s);
		return TSEMICOLON;
	case ',':
		nextchar(s);
		return TCOMMA;
	case 'L':
	case 'U':
	case 'u':
		s->usebuf = true;
		nextchar(s);
		if (s->buf.str[0] == 'u' && s->chr == '8')
			nextchar(s);
		switch (s->chr) {
		case '\'': return charconst(s);
		case '"': return stringlit(s);
		}
		return ident(s);
	case EOF:
		return TEOF;
	default:
		if (isdigit(s->chr))
			return number(s);
		if (isalpha(s->chr) || s->chr == '_' || s->chr == '$'
		    || (unsigned char)s->chr >= 0x80)
			return ident(s);
		/* UCN \u / \U, and C++23 \N{...}, at start of identifier */
		if (s->chr == '\\') {
			int peek = getc(s->file);
			ungetc(peek, s->file);
			if (peek == 'u' || peek == 'U' || peek == 'N')
				return ident(s);
		}
		s->usebuf = true;
		nextchar(s);
		return TOTHER;
	}
}

void
scanfrom(const char *name, FILE *file)
{
	struct scanner *s;

	s = xmalloc(sizeof(*s));
	s->file = file;
	s->buf.str = NULL;
	s->buf.len = 0;
	s->buf.cap = 0;
	s->usebuf = false;
	s->loc.file = name;
	s->loc.line = 0;
	s->loc.col = 0;
	s->next = scanner;
	if (file)
		nextchar(s);
	scanner = s;
}

void
scanopen(void)
{
	if (!scanner->file) {
		scanner->file = fopen(scanner->loc.file, "r");
		if (!scanner->file)
			fatal("open %s:", scanner->loc.file);
		nextchar(scanner);
	}
}

void
scansetloc(struct location loc)
{
	scanner->loc = loc;
}

static void
scanclose(void)
{
	fclose(scanner->file);
	free(scanner->buf.str);
	free(scanner);
}

void
scan(struct token *t)
{
	scanner->sawspace = false;
	for (;;) {
		struct scanner *parent;

		t->kind = scankind(scanner, &t->loc);
		if (t->kind != TEOF || !scanner->next)
			break;
		/* scanclose frees scanner, so preserve its parent before popping. */
		parent = scanner->next;
		scanclose();
		scanner = parent;
		scanopen();
	}
	if (scanner->usebuf) {
		t->lit = bufget(&scanner->buf);
		scanner->usebuf = false;
	} else {
		t->lit = NULL;
	}
	t->space = scanner->sawspace;
	t->hide = false;
}

/* Return the pathname of the file currently being scanned, or NULL. */
const char *
scanfile(void)
{
	return scanner ? scanner->loc.file : NULL;
}

/* Push an isolated scanner that reads from the open FILE *f (e.g. a
 * fmemopen buffer) without linking it to the global input stack.
 * This lets callers tokenize a synthetic fragment (such as a
 * command-line -D definition) independently of the main translation
 * unit. Returns an opaque cookie to pass to scanpopisolated().
 *
 * The pushed scanner has next == NULL, so a TEOF from it does NOT
 * auto-pop into the main input - the caller must drain it to EOF and
 * call scanpopisolated(). */
void *
scanpushisolated(const char *name, FILE *f)
{
	struct scanner *prev = scanner;

	scanner = NULL;  /* detach so scanfrom links next == NULL */
	scanfrom(name, f);
	return prev;
}

/* Pop an isolated scanner previously pushed by scanpushisolated() and
 * restore the saved global input stack. The caller is expected to
 * have consumed the isolated input up to (or past) EOF. */
void
scanpopisolated(void *cookie)
{
	if (scanner)
		scanclose();
	scanner = cookie;
}
