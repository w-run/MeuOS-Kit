/* parse/expr_generic.c -- _Generic selection and int constants.
 *
 * Implements generic() which parses the C11 _Generic selection expression
 * (a type-keyed switch over the controlling expression's type) and
 * intconstexpr() which evaluates an integer constant expression -- used
 * for case labels, bit-field widths, _Static_assert, enum values, and
 * array sizes. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"
#include "mcc.h"
#include "expr_internal.h"

struct expr *assignexpr(struct scope *);
struct expr *condexpr(struct scope *);
struct expr *generic(struct scope *);

struct expr *assignexpr(struct scope *);

struct expr *
generic(struct scope *s)
{
	struct expr *e, *match = NULL, *def = NULL;
	struct type *t, *want;

	next();
	expect(TLPAREN, "after '_Generic'");
	e = assignexpr(s);
	expect(TCOMMA, "after generic selector expression");
	want = e->type;
	delexpr(e);
	do {
		if (consume(TDEFAULT)) {
			if (def)
				error(&tok.loc, "multiple default expressions in generic association list");
			expect(TCOLON, "after 'default'");
			def = assignexpr(s);
		} else {
			t = typename(s, NULL, NULL);
			if (!t)
				error(&tok.loc, "expected typename for generic association");
			if (t->kind == TYPEFUNC)
				error(&tok.loc, "generic association must have object type");
			if (t->incomplete)
				error(&tok.loc, "generic association must have complete type");
			if (t->prop & PROPVM)
				error(&tok.loc, "generic association has variably modified type");
			expect(TCOLON, "after type name");
			e = assignexpr(s);
			if (typecompatible(t, want)) {
				if (match)
					error(&tok.loc, "generic selector matches multiple associations");
				match = e;
			} else {
				delexpr(e);
			}
		}
	} while (consume(TCOMMA));
	expect(TRPAREN, "after generic assocation list");
	if (!match) {
		if (!def)
			error(&tok.loc, "generic selector matches no associations and no default was specified");
		match = def;
	} else if (def) {
		delexpr(def);
	}
	return match;
}
unsigned long long
intconstexpr(struct scope *s, bool allowneg)
{
	struct expr *e;

	e = eval(condexpr(s));
	if (e->kind != EXPRCONST || !(e->type->prop & PROPINT))
		error(&tok.loc, "not an integer constant expression");
	if (!allowneg && e->type->u.arith.issigned && e->u.constant.u >> 63)
		error(&tok.loc, "integer constant expression cannot be negative");
	return e->u.constant.u;
}
