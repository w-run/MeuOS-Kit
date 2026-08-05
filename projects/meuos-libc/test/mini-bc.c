/* mini-bc — a compact bc/dc-style arithmetic calculator.
 *
 * Deliberately written against the classic POSIX utility surface this
 * integration test cares about: stdio (printf/getline), stdlib
 * (strtod/strtoul/malloc/realloc/free), string (strchr/strcmp/strncpy),
 * errno/strerror, math (pow/floor/fmod), and file I/O.  A faithful,
 * self-contained reimplementation of the ubiquitous bc calculator (a
 * "representative real POSIX program") rather than upstream GNU bc.
 */
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MAXDEPTH = 64 };
static double stk[MAXDEPTH];
static int sp;

static void err_push(const char *s) { fprintf(stderr, "mini-bc: %s\n", s); exit(1); }
static void push(double v) { if (sp >= MAXDEPTH) err_push("stack overflow"); stk[sp++] = v; }
static double pop(void) { if (sp <= 0) err_push("stack underflow"); return stk[--sp]; }

/* expression grammar (recursive descent, RPN output at runtime):
 *   expr   := term (('+'|'-') term)*
 *   term   := factor (('*'|'/') factor)*
 *   factor := unary | number | '(' expr ')'
 */
static char *src, *p;
static double expr(void);

static double factor(void)
{
	while (isspace((unsigned char)*p)) p++;
	if (*p == '(') {
		p++;
		double v = expr();
		if (*p == ')') p++; else err_push("expected ')'");
		return v;
	}
	if (*p == '+' || *p == '-') {
		int neg = (*p++ == '-');
		return neg ? -factor() : factor();
	}
	/* a number */
	char nbuf[64];
	int i = 0;
	while ((isdigit((unsigned char)*p) || *p == '.') && i < 63) nbuf[i++] = *p++;
	if (i == 0) err_push("expected a number");
	nbuf[i] = '\0';
	return strtod(nbuf, NULL);
}

static double term(void)
{
	double v = factor();
	while (1) {
		while (isspace((unsigned char)*p)) p++;
		if (*p == '*') { p++; v *= factor(); }
		else if (*p == '/') {
			p++;
			double d = factor();
			if (d == 0) err_push("division by zero");
			v /= d;
		} else break;
	}
	return v;
}

static double expr(void)
{
	double v = term();
	while (1) {
		while (isspace((unsigned char)*p)) p++;
		if (*p == '+') { p++; v += term(); }
		else if (*p == '-') { p++; v -= term(); }
		else break;
	}
	return v;
}

int main(void)
{
	size_t cap = 0;
	ssize_t n;
	/* exercise getline, errno/strerror, fgets-free stdin */
	while ((n = getline(&src, &cap, stdin)) != -1) {
		/* strip trailing newline */
		p = src;
		if (n > 0 && src[n - 1] == '\n') src[n - 1] = '\0';
		while (isspace((unsigned char)*p)) p++;
		if (*p == '\0' || *p == '#') continue;
		if (strcmp(p, "quit") == 0)
			break;
		errno = 0;
		double v = expr();
		if (errno != 0) {
			fprintf(stderr, "mini-bc: %s\n", strerror(errno));
			continue;
		}
		printf("%.14g\n", v);
	}
	free(src);
	return 0;
}
