#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "util.h"
#include "mcc.h"
#include "ir.h"

struct token tok;
static struct map tokmap;  /* maps string to token */
static struct array tokstr;  /* maps token to string */

/* Min-color support for diagnostics (p9-ui: colored error/warn like clang).
 * No external dependency — gated on isatty(stderr). */
static int diag_color_on(void)
{
	static int cached = -1;
	if (cached < 0)
		cached = isatty(fileno(stderr)) ? 1 : 0;
	return cached;
}
#define DC_RED    "\033[31m"
#define DC_YELLOW "\033[33m"
#define DC_CYAN   "\033[36m"
#define DC_BOLD   "\033[1m"
#define DC_RESET  "\033[0m"
#define dcol(c) (diag_color_on() ? (c) : "")

void
tokeninit(void)
{
	static const unsigned char strings[] =
#define TOKEN(t, s) s "\0"
#include "tokens.h"
#undef TOKEN
		;
	const unsigned char *s, *e;
	struct mapkey k;
	size_t l, i;

	mapinit(&tokmap, 1024);
	for (s = strings, e = strings + sizeof strings - 1; s != e; s += l + 1) {
		l = strlen((char *)s);
		if (l) {
			mapkey(&k, s, l);
			mapput(&tokmap, &k, &i);
			tokmap.vals[i].i = tokstr.len / sizeof(void *);
			arrayaddptr(&tokstr, (void *)s);
		} else {
			arrayaddptr(&tokstr, NULL);
		}
	}
}

int
tokenget(const void *str, size_t len)
{
	static char *pos, *end;
	struct mapkey k;
	size_t i;
	char *buf;

	mapkey(&k, str, len);
	if (mapput(&tokmap, &k, &i)) {
		if (len > 8192)
			fatal("token is too long");
		if (INT_MAX < tokstr.len / sizeof(void *))
			fatal("too many tokens");
		if (!pos || end - pos < len + 1) {
			buf = xmalloc(8192);
			pos = buf;
			end = buf + 8192;
		}
		memcpy(pos, str, len);
		pos[len] = '\0';
		tokmap.keys[i].str = pos;
		tokmap.vals[i].i = tokstr.len / sizeof(void *);
		arrayaddptr(&tokstr, pos);
		pos += len + 1;
	}
	return tokmap.vals[i].i;
}

char *
tokenstr(enum tokenkind kind)
{
	assert(kind < tokstr.len / sizeof(void *));
	return ((void **)tokstr.val)[kind];
}

void
tokenprint(const struct token *t, FILE *f)
{
	const char *str;

	if (t->space)
		fputc(' ', f);
	switch (t->kind) {
	case TNUMBER:
	case TCHARCONST:
	case TSTRINGLIT:
		str = t->lit;
		break;
	case TNEWLINE:
		str = "\n";
		break;
	case TEOF:
		return;
	default:
		str = tokenstr(t->kind);
	}
	if (!str)
		fatal("cannot print token %d", t->kind);
	fputs(str, f);
}

static void
tokendesc(char *buf, size_t len, enum tokenkind kind, const char *lit)
{
	const char *class;
	bool quote = true;

	assert(kind < tokstr.len / sizeof(void *));
	switch (kind) {
	case TEOF:       class = "EOF";                       break;
	case TNUMBER:    class = "number";     quote = true;  break;
	case TCHARCONST: class = "character";  quote = false; break;
	case TSTRINGLIT: class = "string";     quote = false; break;
	case TNEWLINE:   class = "newline";                   break;
	case TOTHER:     class = NULL;                        break;
	default:
		if (kind >= TIDENT)
			class = "identifier", quote = true;
		else
			class = NULL;
		lit = ((void **)tokstr.val)[kind];
	}
	if (class && lit)
		snprintf(buf, len, quote ? "%s '%s'" : "%s %s", class, lit);
	else if (class)
		snprintf(buf, len, "%s", class);
	else if (kind == TOTHER && !isprint(*(unsigned char *)lit))
		snprintf(buf, len, "<U+%04x>", *(unsigned char *)lit);
	else if (lit)
		snprintf(buf, len, "'%s'", lit);
	else
		snprintf(buf, len, "<unknown>");
}

char *
tokencheck(const struct token *t, enum tokenkind kind, const char *msg)
{
	char want[64], got[64];

	if (kind == TPPIDENT || kind == TIDENT) {
		if (t->kind < kind) {
			strcpy(want, "identifier");
			goto err;
		}
		return tokenstr(t->kind);
	}
	if (t->kind != kind && (kind != TIDENT || t->kind < TIDENT)) {
		tokendesc(want, sizeof(want), kind, NULL);
	err:
		tokendesc(got, sizeof(got), t->kind, t->lit);
		error(&t->loc, "expected %s %s, saw %s", want, msg, got);
	}
	return t->lit;
}

void
diagloc(const struct location *loc)
{
	fprintf(stderr, "%s%s:%zu:%zu:%s ", dcol(DC_CYAN), loc->file,
	        loc->line + 1, loc->col, dcol(DC_RESET));
}

void
error(const struct location *loc, const char *fmt, ...)
{
	va_list ap;

	/* --error-json: emit one structured JSON object per diagnostic so
	 * tooling (editors, CI) can parse diagnostics without scraping text.
	 * Format: {"level":"error","file":...,"line":N,"col":M,"message":"..."} */
	if (g_error_json) {
		char msg[1024];
		va_start(ap, fmt);
		vsnprintf(msg, sizeof(msg), fmt, ap);
		va_end(ap);
		if (loc && loc->file)
			fprintf(stderr, "{\"level\":\"error\",\"file\":\"%s\",\"line\":%zu,"
			        "\"col\":%zu,\"message\":\"%s\"}\n",
			        loc->file, loc->line + 1, loc->col, msg);
		else
			fprintf(stderr, "{\"level\":\"error\",\"message\":\"%s\"}\n", msg);
		exit(1);
	}

	diagloc(loc);
	fprintf(stderr, "%serror: %s", dcol(DC_BOLD DC_RED), dcol(DC_RESET));
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	putc('\n', stderr);

	/* Caret diagnostic: show the source line with ^ marker.
	 * loc->line is 0-based — the first line is line 0. */
	if (loc && loc->file) {
		FILE *src = fopen(loc->file, "r");
		if (src) {
			char linebuf[4096];
			size_t target = loc->line; /* 0-based */
			size_t l = 0;
			while (l <= target && fgets(linebuf, sizeof(linebuf), src))
				l++;
			if (l > target) {
				size_t len = strlen(linebuf);
				while (len > 0 && (linebuf[len-1] == '\n' || linebuf[len-1] == '\r'))
					linebuf[--len] = '\0';
				fprintf(stderr, "    %s\n", linebuf);
				fprintf(stderr, "    %s", dcol(DC_RED));
				size_t ci;
				for (ci = 1; ci < loc->col; ci++)
					fputc(linebuf[ci-1] == '\t' ? '\t' : ' ', stderr);
				fprintf(stderr, "^%s\n", dcol(DC_RESET));
			}
			fclose(src);
		}
	}

	exit(1);
}

int warn_level = WARN_ALL;
bool warn_as_error;

void
cc_warn(const struct location *loc, int kind, const char *fmt, ...)
{
	va_list ap;

	if (!(warn_level & kind))
		return;

	if (loc)
		fprintf(stderr, "%s%s:%zu:%zu:%s ", dcol(DC_CYAN), loc->file,
		        loc->line + 1, loc->col, dcol(DC_RESET));
	else
		fprintf(stderr, "%s%s: %s", dcol(DC_CYAN), argv0, dcol(DC_RESET));
	fprintf(stderr, "%swarning: %s", dcol(DC_BOLD DC_YELLOW), dcol(DC_RESET));
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	putc('\n', stderr);

	if (warn_as_error)
		error(loc, "warning treated as error");
}
