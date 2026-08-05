/* pp_expr.c - #if constant-expression arithmetic evaluator.
 *
 * Self-contained recursive-descent evaluator for the integral constant
 * expressions that appear after #if / #elif. It operates on an array of
 * preprocessor tokens (already macro-expanded and `defined`-resolved by
 * evalexpr() in pp.c) and produces a long long value. The only entry
 * point exported to pp.c is evalconst(); everything else is file-local.
 *
 * Split out of pp.c so the ~280-line pure evaluator is independently
 * readable and testable. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "util.h"
#include "mcc.h"
#include "pp_internal.h"


struct evalctx {
	struct token *t;
	size_t n, i;
};

static long long evalexpr_e(struct evalctx *);

static struct token *
evalcur(struct evalctx *e)
{
	return e->i < e->n ? &e->t[e->i] : NULL;
}

static void
evaladv(struct evalctx *e)
{
	++e->i;
}

static long long
parseint(const struct token *t)
{
	const char *lit = t->lit;
	if (lit[0] == '0' && (lit[1] == 'b' || lit[1] == 'B')) {
		unsigned long long val = 0;
		const char *s = lit + 2;
		while (*s == '0' || *s == '1')
			val = (val << 1) | (*s++ - '0');
		return (long long)val;
	}
	return strtoll(lit, NULL, 0);
}

static long long
parsecharconst(const struct token *t)
{
	const char *s = t->lit;
	long long v;

	if (!s || *s != '\'')
		return 0;
	++s;
	if (*s == '\\') {
		++s;
		switch (*s) {
		case 'n':  v = '\n'; break;
		case 't':  v = '\t'; break;
		case 'r':  v = '\r'; break;
		case '\\': v = '\\'; break;
		case '\'': v = '\''; break;
		case '"':  v = '"';  break;
		case 'a':  v = '\a'; break;
		case 'b':  v = '\b'; break;
		case 'f':  v = '\f'; break;
		case 'v':  v = '\v'; break;
		case 'x':  v = strtoll(s + 1, NULL, 16); break;
		default:
			if (*s >= '0' && *s <= '7')
				v = strtoll(s, NULL, 8);
			else
				v = (unsigned char)*s;
		}
	} else {
		v = (unsigned char)*s;
	}
	return v;
}

static long long
evalprimary(struct evalctx *e)
{
	struct token *t = evalcur(e);

	if (!t)
		error_code(E_SYNTAX, &tok.loc, "expected expression in #if directive");
	switch (t->kind) {
	case TNUMBER:
		{ long long v = parseint(t); evaladv(e); return v; }
	case TCHARCONST:
		{ long long v = parsecharconst(t); evaladv(e); return v; }
	case TLPAREN: {
		long long v;
		evaladv(e);
		v = evalexpr_e(e);
		t = evalcur(e);
		if (!t || t->kind != TRPAREN)
			error_code(E_SYNTAX, &tok.loc, "expected ')' in #if expression");
		evaladv(e);
		return v;
	}
	default:
		/* remaining identifiers evaluate to 0 */
		if (t->kind >= TIDENT) { evaladv(e); return 0; }
		error_code(E_SYNTAX, &tok.loc, "invalid token in #if expression: %s",
		      t->lit ? t->lit : tokenstr(t->kind));
	}
	return 0;
}

static long long
evalunary(struct evalctx *e)
{
	struct token *t = evalcur(e);
	if (!t)
		error_code(E_SYNTAX, &tok.loc, "expected expression in #if directive");
	switch (t->kind) {
	case TLNOT: evaladv(e); return !evalunary(e);
	case TBNOT: evaladv(e); return ~evalunary(e);
	case TADD:  evaladv(e); return  evalunary(e);
	case TSUB:  evaladv(e); return -evalunary(e);
	default:    return evalprimary(e);
	}
}

static long long
evalmul(struct evalctx *e)
{
	long long l = evalunary(e);
	for (;;) {
		struct token *t = evalcur(e);
		if (!t) return l;
		switch (t->kind) {
		case TMUL: evaladv(e); l = l * evalunary(e); break;
		case TDIV: { long long r; evaladv(e); r = evalunary(e); if (!r) error_code(E_SYNTAX, &tok.loc, "division by zero in #if"); l = l / r; break; }
		case TMOD: { long long r; evaladv(e); r = evalunary(e); if (!r) error_code(E_SYNTAX, &tok.loc, "modulo by zero in #if"); l = l % r; break; }
		default: return l;
		}
	}
}

static long long
evaladd(struct evalctx *e)
{
	long long l = evalmul(e);
	for (;;) {
		struct token *t = evalcur(e);
		if (!t) return l;
		switch (t->kind) {
		case TADD: evaladv(e); l = l + evalmul(e); break;
		case TSUB: evaladv(e); l = l - evalmul(e); break;
		default: return l;
		}
	}
}

static long long
evalshift(struct evalctx *e)
{
	long long l = evaladd(e);
	for (;;) {
		struct token *t = evalcur(e);
		if (!t) return l;
		switch (t->kind) {
		case TSHL: evaladv(e); l = (long long)((unsigned long long)l << evaladd(e)); break;
		case TSHR: evaladv(e); l = l >> evaladd(e); break;
		default: return l;
		}
	}
}

static long long
evalrel(struct evalctx *e)
{
	long long l = evalshift(e);
	for (;;) {
		struct token *t = evalcur(e);
		if (!t) return l;
		switch (t->kind) {
		case TLESS:    evaladv(e); l = l <  evalshift(e); break;
		case TGREATER: evaladv(e); l = l >  evalshift(e); break;
		case TLEQ:     evaladv(e); l = l <= evalshift(e); break;
		case TGEQ:      evaladv(e); l = l >= evalshift(e); break;
		default: return l;
		}
	}
}

static long long
evaleq(struct evalctx *e)
{
	long long l = evalrel(e);
	for (;;) {
		struct token *t = evalcur(e);
		if (!t) return l;
		switch (t->kind) {
		case TEQL: evaladv(e); l = (l == evalrel(e)); break;
		case TNEQ: evaladv(e); l = (l != evalrel(e)); break;
		default: return l;
		}
	}
}

static long long
evalband(struct evalctx *e)
{
	long long l = evaleq(e);
	for (;;) {
		struct token *t = evalcur(e);
		if (!t || t->kind != TBAND) return l;
		evaladv(e); l = l & evaleq(e);
	}
}

static long long
evalbxor(struct evalctx *e)
{
	long long l = evalband(e);
	for (;;) {
		struct token *t = evalcur(e);
		if (!t || t->kind != TXOR) return l;
		evaladv(e); l = l ^ evalband(e);
	}
}

static long long
evalbor(struct evalctx *e)
{
	long long l = evalbxor(e);
	for (;;) {
		struct token *t = evalcur(e);
		if (!t || t->kind != TBOR) return l;
		evaladv(e); l = l | evalbxor(e);
	}
}

static long long
evalland(struct evalctx *e)
{
	long long l = evalbor(e);
	for (;;) {
		struct token *t = evalcur(e);
		long long r;
		if (!t || t->kind != TLAND) return l;
		/* Do not let the host C operator short-circuit parsing of the RHS. */
		evaladv(e); r = evalbor(e); l = l && r;
	}
}

static long long
evallor(struct evalctx *e)
{
	long long l = evalland(e);
	for (;;) {
		struct token *t = evalcur(e);
		long long r;
		if (!t || t->kind != TLOR) return l;
		/* Do not let the host C operator short-circuit parsing of the RHS. */
		evaladv(e); r = evalland(e); l = l || r;
	}
}

static long long
evalexpr_e(struct evalctx *e)
{
	long long l = evallor(e);
	struct token *t = evalcur(e);
	if (t && t->kind == TQUESTION) {
		long long a, b;
		evaladv(e);
		a = evalexpr_e(e);
		t = evalcur(e);
		if (!t || t->kind != TCOLON)
			error_code(E_SYNTAX, &tok.loc, "expected ':' in #if conditional expression");
		evaladv(e);
		b = evalexpr_e(e);
		return l ? a : b;
	}
	return l;
}

long long
evalconst(struct token *t, size_t n)
{
	struct evalctx e = { t, n, 0 };
	long long v = evalexpr_e(&e);
	struct token *after = evalcur(&e);

	if (after)
		error_code(E_SYNTAX, &tok.loc, "unexpected token in #if expression: %s",
		      after->lit ? after->lit : tokenstr(after->kind));
	return v;
}
