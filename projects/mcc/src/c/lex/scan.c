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
ident(struct scanner *s)
{
	int tok;

	s->usebuf = true;
	for (;;) {
		if (isalnum(s->chr) || s->chr == '_' || s->chr == '$'
		    || (unsigned char)s->chr >= 0x80) {
			nextchar(s);
		} else if (s->chr == '\\') {
			/* UCN: \uXXXX or \UXXXXXXXX in identifiers.
			 * We read the UCN directly from file, add UTF-8 to
			 * the buffer, and leave s->chr pointing to the next
			 * character (so the loop can continue). */
			int ucn_val = 0, ucn_n = 0, hexdig;
			int c1 = getc(s->file);
			if (c1 == 'u') ucn_n = 4;
			else if (c1 == 'U') ucn_n = 8;
			else { ungetc(c1, s->file); break; }
			for (hexdig = 0; hexdig < ucn_n; hexdig++) {
				int hc = getc(s->file);
				if (hc >= '0' && hc <= '9') ucn_val = (ucn_val << 4) | (hc - '0');
				else if (hc >= 'a' && hc <= 'f') ucn_val = (ucn_val << 4) | (hc - 'a' + 10);
				else if (hc >= 'A' && hc <= 'F') ucn_val = (ucn_val << 4) | (hc - 'A' + 10);
				else { ungetc(hc, s->file); break; }
			}
			if (hexdig == ucn_n) {
				/* Encode as UTF-8 and fill buffer */
				unsigned char utf8[4]; int ulen = 1;
				if (ucn_val < 0x80) utf8[0] = ucn_val;
				else if (ucn_val < 0x800) { utf8[0] = 0xC0|(ucn_val>>6); utf8[1]=0x80|(ucn_val&0x3F); ulen=2; }
				else if (ucn_val < 0x10000) { utf8[0]=0xE0|(ucn_val>>12); utf8[1]=0x80|((ucn_val>>6)&0x3F); utf8[2]=0x80|(ucn_val&0x3F); ulen=3; }
				else { utf8[0]=0xF0|(ucn_val>>18); utf8[1]=0x80|((ucn_val>>12)&0x3F); utf8[2]=0x80|((ucn_val>>6)&0x3F); utf8[3]=0x80|(ucn_val&0x3F); ulen=4; }
				for (int k = 0; k < ulen; k++) bufadd(&s->buf, utf8[k]);
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
	return TNUMBER;
}

static void
escape(struct scanner *s)
{
	nextchar(s);
	if (s->chr == 'x') {
		nextchar(s);
		if (!isxdigit(s->chr))
			error(&s->loc, "invalid hexadecimal escape sequence");
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
		error(&s->loc, "invalid escape sequence");
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
			error(&s->loc, "newline in character constant");
		case EOF:
			error(&s->loc, "EOF in character constant");
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
			error(&s->loc, "newline in string literal");
		case EOF:
			error(&s->loc, "EOF in string literal");
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
				error(&s->loc, "EOF in comment");
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
		/* UCN \u / \U at start of identifier */
		if (s->chr == '\\') {
			int peek = getc(s->file);
			ungetc(peek, s->file);
			if (peek == 'u' || peek == 'U')
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
