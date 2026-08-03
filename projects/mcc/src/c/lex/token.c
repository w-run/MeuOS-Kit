#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "util.h"
#include "mcc.h"
#include "ir.h"
#include "i18n.h"

struct token tok;
static struct map tokmap;  /* maps string to token */
static struct array tokstr;  /* maps token to string */

/* Trial-parse support (C++ requires-expressions and other SFINAE-style
 * well-formedness checks): while g_cpp_trial_depth > 0, error() longjmps
 * to the innermost cpp_trial_begin() instead of exiting the compiler.
 * g_cpp_trial_env holds the jump buffer of the innermost trial (saved and
 * restored around nested trials by cpp_trial_begin/end), so an error in a
 * nested trial rethrows to its enclosing trial. */
static jmp_buf g_cpp_trial_env;
static int g_cpp_trial_depth;

int
cpp_trial_depth(void)
{
	return g_cpp_trial_depth;
}

void
cpp_trial_begin(jmp_buf env)
{
	jmp_buf old;
	memcpy(old, g_cpp_trial_env, sizeof old);
	memcpy(g_cpp_trial_env, env, sizeof g_cpp_trial_env);
	memcpy(env, old, sizeof old);
	++g_cpp_trial_depth;
}

void
cpp_trial_end(jmp_buf env)
{
	jmp_buf old;
	memcpy(old, g_cpp_trial_env, sizeof old);
	memcpy(g_cpp_trial_env, env, sizeof g_cpp_trial_env);
	memcpy(env, old, sizeof old);
	--g_cpp_trial_depth;
}

/* Called by the trial wrappers when the parse under trial triggered an
 * error: restore the innermost trial's env so the setjmp returns. */
void
cpp_trial_rethrow(void)
{
	if (g_cpp_trial_depth > 0)
		longjmp(g_cpp_trial_env, 1);
}

/* Min-color support for diagnostics (p9-ui: colored error/warn like clang).
 * No external dependency — gated on isatty(stderr) unless the driver's
 * --color=always/never overrides it (-1 = auto, 0 = never, 1 = always). */
int g_diag_color = -1;
static int diag_color_on(void)
{
	if (g_diag_color >= 0)
		return g_diag_color;
	static int cached = -1;
	if (cached < 0)
		cached = isatty(fileno(stderr)) ? 1 : 0;
	return cached;
}
#define DC_RED    "\033[31m"
#define DC_GREEN  "\033[32m"
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
		if (kind == TSEMICOLON || (msg && strstr(msg, "';'")))
			/* the most common fix: a missing ';' at the end of a
			 * declaration or statement */
			error_fixit(E_SYNTAX, t, "add ';' here",
			    "expected %s %s, saw %s", want, msg, got);
		else
			error_tok_code(E_SYNTAX, t,
			    "expected %s %s, saw %s", want, msg, got);
	}
	return t->lit;
}

void
diagloc(const struct location *loc)
{
	fprintf(stderr, "%s%s:%zu:%zu:%s ", dcol(DC_CYAN), loc->file,
	        loc->line + 1, loc->col, dcol(DC_RESET));
}

/* --- diagnostic error codes --------------------------------------- */

static const char *const errcode_names[] = {
	"E0000", /* E_GENERIC */
	"E0001", /* E_SYNTAX */
	"E0002", /* E_UNDECLARED */
	"E0003", /* E_TYPE */
	"E0004", /* E_REDEF */
	"E0005", /* E_DECL */
	"E0006", /* E_STMT */
	"E0007", /* E_CTYPE */
	"E0008", /* E_INCOMPLETE */
	"E0009", /* E_QUAL */
	"E0010", /* E_ACCESS */
	"E0011", /* E_TEMPLATE */
	"E0012", /* E_OVERLOAD */
};

static const char *
errcode_str(enum errcode code)
{
	if ((unsigned)code >= countof(errcode_names))
		return "E0000";
	return errcode_names[code];
}

/* JSON multi-error collection: count collected errors and stop after
 * g_error_limit (10 by default); with a recovery point armed, longjmp
 * there so the top-level loop can resume collecting instead of dying on
 * the first error. */
int g_error_count;
int g_error_limit = 10;
jmp_buf g_err_recovery;
int g_err_recovery_set;

/* Skip to a safe resynchronization point after a collected error: the
 * next top-level ';' (consumed) or top-level '}' (consumed), tracking
 * nested braces/parens/brackets so a mid-body error resumes at the end of
 * that declaration rather than drifting to EOF. */
void
err_sync(void)
{
	int depth = 0;

	while (tok.kind != TEOF) {
		switch (tok.kind) {
		case TLBRACE: case TLPAREN: case TLBRACK:
			depth++;
			break;
		case TRBRACE: case TRPAREN: case TRBRACK:
			if (depth > 0) {
				depth--;
			} else if (tok.kind == TRBRACE) {
				next(); /* consume the closing '}' */
				return;
			}
			break;
		case TSEMICOLON:
			if (depth == 0) {
				next(); /* consume the ';' */
				return;
			}
			break;
		default:
			break;
		}
		next();
	}
}

/* The number of source columns the token spans, for the full-width caret. */
static size_t
token_width(const struct token *t)
{
	const char *s = t->lit ? t->lit : tokenstr(t->kind);
	size_t n = s ? strlen(s) : 1;
	return n > 0 ? n : 1;
}

/* Core diagnostic.  All paths are non-returning: either the trial
 * rethrows, the JSON mode longjmps to the top-level recovery point (or
 * exits once the error limit is hit), or the process exits. */
static void
error_common(enum errcode code, const struct location *loc, size_t width,
    const char *fix, const char *fmt, va_list ap)
{
	char msg[1024];

	/* i18n: 按当前语言翻译格式串与 fix-it 提示（未收录条目降级为 en）。 */
	fmt = msg_tr(fmt);
	fix = msg_tr_fix(fix);

	vsnprintf(msg, sizeof(msg), fmt, ap);

	/* Trial parse (SFINAE-style well-formedness check, e.g. C++20
	 * requires-expressions): a parse/type error is not fatal — unwind to
	 * the enclosing trial and report failure to the caller. */
	if (g_cpp_trial_depth > 0) {
		(void)loc;
		(void)code;
		(void)width;
		(void)fix;
		(void)msg;
		cpp_trial_rethrow();
	}

	/* --error-json: emit one structured JSON object per diagnostic so
	 * tooling (editors, CI) can parse diagnostics without scraping text.
	 * Format:
	 * {"level":"error","code":"E0001","file":...,"line":N,"col":M,
	 *  "end_col":P,"message":"..."}  Multiple errors are collected (up to
	 * g_error_limit) before the process exits non-zero. */
	if (g_error_json) {
		fprintf(stderr, "{\"level\":\"error\",\"code\":\"%s\","
		    "\"file\":\"%s\",\"line\":%zu,\"col\":%zu,\"end_col\":%zu,"
		    "\"message\":\"%s\"}\n",
		    errcode_str(code),
		    loc && loc->file ? loc->file : "",
		    loc ? loc->line + 1 : 0,
		    loc ? loc->col : 0,
		    loc ? loc->col + (width ? width - 1 : 0) : 0,
		    msg);
		if (++g_error_count >= g_error_limit)
			exit(1);
		if (g_err_recovery_set)
			longjmp(g_err_recovery, 1);
		exit(1); /* no recovery point: cannot safely continue */
	}

	if (loc && loc->file)
		diagloc(loc);
	else
		fprintf(stderr, "%s%s: %s", dcol(DC_CYAN), argv0, dcol(DC_RESET));
	fprintf(stderr, "%s%s %s%s%s ",
	    dcol(DC_BOLD DC_RED), msg_word_error(),
	    dcol(DC_BOLD), errcode_str(code), dcol(DC_RESET));
	fputs(msg, stderr);
	putc('\n', stderr);

	/* Caret diagnostic: show the source line with a ^ marker covering the
	 * token span (width columns).  loc->line is 0-based — the first line
	 * is line 0; loc->col is 1-based. */
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
				size_t n, ci;
				while (len > 0 && (linebuf[len-1] == '\n' || linebuf[len-1] == '\r'))
					linebuf[--len] = '\0';
				fprintf(stderr, "    %s\n", linebuf);
				fprintf(stderr, "    %s", dcol(DC_RED));
				for (ci = 1; ci < loc->col; ci++)
					fputc(linebuf[ci-1] == '\t' ? '\t' : ' ', stderr);
				/* full-span caret: width ^ (capped at the line end) */
				n = width ? width : 1;
				for (ci = 0; ci < n && loc->col - 1 + ci < len; ci++)
					fputc('^', stderr);
				fprintf(stderr, "%s\n", dcol(DC_RESET));
				if (fix) {
					/* inline fix-it: note + its own caret */
					fprintf(stderr, "    %s%s %s%s\n",
					    dcol(DC_BOLD DC_GREEN), msg_word_note(), fix,
					    dcol(DC_RESET));
					fprintf(stderr, "    %s", dcol(DC_GREEN));
					for (ci = 1; ci < loc->col; ci++)
						fputc(linebuf[ci-1] == '\t' ? '\t' : ' ', stderr);
					fputc('^', stderr);
					fprintf(stderr, "%s\n", dcol(DC_RESET));
				}
			}
			fclose(src);
		}
	}

	exit(1);
}

noreturn void
error(const struct location *loc, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	error_common(E_GENERIC, loc, 0, NULL, fmt, ap);
	va_end(ap);
}

void
error_code(enum errcode code, const struct location *loc, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	error_common(code, loc, 0, NULL, fmt, ap);
	va_end(ap);
}

void
error_tok_code(enum errcode code, const struct token *t, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	error_common(code, &t->loc, token_width(t), NULL, fmt, ap);
	va_end(ap);
}

void
error_fixit(enum errcode code, const struct token *t, const char *fix,
    const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	error_common(code, &t->loc, token_width(t), fix, fmt, ap);
	va_end(ap);
}

int warn_level = WARN_ALL;
bool warn_as_error;
int g_error_json;    /* --error-json: emit structured JSON diagnostics */
int g_error_explain; /* --explain: append a fix-hint suffix */

void
cc_warn(const struct location *loc, int kind, const char *fmt, ...)
{
	va_list ap;
	const char *efmt = fmt;   /* 原始英文格式串（供 --explain 分类匹配） */

	if (!(warn_level & kind))
		return;

	/* i18n: 正文按当前语言翻译；--explain 分类基于英文源串匹配，
	 * 提示文本随语言选择（en/zh 一致）。 */
	fmt = msg_tr(fmt);

	if (loc)
		fprintf(stderr, "%s%s:%zu:%zu:%s ", dcol(DC_CYAN), loc->file,
		        loc->line + 1, loc->col, dcol(DC_RESET));
	else
		fprintf(stderr, "%s%s: %s", dcol(DC_CYAN), argv0, dcol(DC_RESET));
	fprintf(stderr, "%s%s %s", dcol(DC_BOLD DC_YELLOW),
	        msg_word_warning(), dcol(DC_RESET));
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	/* --explain: append a localized fix-hint suffix. */
	if (g_error_explain) {
		const char *hint = msg_explain(efmt);
		if (hint)
			fprintf(stderr, "%s%s%s", dcol(DC_CYAN), hint, dcol(DC_RESET));
	}
	putc('\n', stderr);

	if (warn_as_error)
		error(loc, "warning treated as error");
}
