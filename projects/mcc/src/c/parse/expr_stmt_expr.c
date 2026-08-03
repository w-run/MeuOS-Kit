/* parse/expr_stmt_expr.c — GNU statement expression ({...}) parser.
 *
 * Implements parse_stmt_expr_body(), called from castexpr() /
 * primaryexpr() when the parser sees `({` after consuming '('.
 *
 * The body is parsed as a compound statement.  Declarations are saved
 * as items for deferred funcinit() (IR-alloc and init-eval at IR-gen
 * time).  Control-flow statements (if/while/for/switch/return/...)
 * and non-last expression statements are emitted to curfunc immediately
 * so that IR ordering matches source order.  Only the LAST expression
 * statement is kept as an expr sub-tree for deferred funcexpr().
 *
 * References:
 *   https://gcc.gnu.org/onlinedocs/gcc/Statement-Exprs.html
 */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"
#include "mcc.h"
#include "expr_internal.h"
#include "decl_internal.h"

struct expr *
parse_stmt_expr_body(struct scope *s)
{
	struct expr *last_expr = NULL;
	struct qualtype base, qt;
	enum storageclass sc;
	enum funcspec fs;
	struct decl *d;
	struct init *init;
	bool hasinit;
	char *name;
	int align;

	next(); /* consume '{' after '(' */

	s = mkscope(s);

	for (;;) {
		/* Skip _Static_assert */
		if (staticassert(s))
			continue;

		/* Skip labels */
		if (tok.kind == TCASE) {
			next();
			intconstexpr(s, true);
			expect(TCOLON, "after case expression");
			continue;
		}
		if (tok.kind == TDEFAULT) {
			next();
			expect(TCOLON, "after 'default'");
			continue;
		}
		if (tok.kind >= TIDENT && peek(TCOLON)) {
			(void)tokenstr(tok.kind);
			next();
			next(); /* consume ':' */
			continue;
		}

		/* Closing brace terminates the block */
		if (tok.kind == TRBRACE)
			break;

		/* Empty statement */
		if (tok.kind == TSEMICOLON) {
			next();
			continue;
		}

		/* Try declaration */
		base = declspecs(s, &sc, &fs, &align);
		if (base.type) {
			for (;;) {
				qt = declarator(s, base, &name, &align, NULL, false, NULL);
				if (sc & SCTYPEDEF) {
					struct decl *prior = scopegetdecl(s, name, false);
					if (!prior)
						scopeputdecl(s, mkdecl(name, DECLTYPE, qt.type, qt.qual, LINKNONE));
					else if (!typesame(prior->type, qt.type) || prior->qual != qt.qual)
						error(&tok.loc, "typedef '%s' redefined with different type", name);
				} else {
					d = mkdecl(name, DECLOBJECT, qt.type, qt.qual,
					           sc & SCSTATIC ? LINKINTERN : LINKNONE);
					d->u.obj.storage = sc & SCSTATIC ? SDSTATIC : SDAUTO;
					d->u.obj.align = align;
					scopeputdecl(s, d);

					init = NULL;
					hasinit = false;
					if (consume(TASSIGN)) {
						init = parseinit(s, d->type);
						hasinit = true;
					}

					/* Allocate and initialise storage immediately so that
					 * subsequent funcexpr() calls for side-effect exprs
					 * can reference d->value (set by funcalloc inside
					 * funcinit).  No item is saved for IR gen. */
					if (curfunc)
						funcinit(curfunc, d, init, hasinit);
				}
				if (consume(TSEMICOLON))
					break;
				expect(TCOMMA, "or ';' after declarator");
			}
			continue;
		}

		/* Control-flow and compound statements — delegate to stmt()
		 * which emits IR into curfunc immediately (parser-time IR). */
		if (tok.kind == TIF   || tok.kind == TWHILE  || tok.kind == TDO ||
		    tok.kind == TFOR  || tok.kind == TSWITCH || tok.kind == TGOTO ||
		    tok.kind == TCONTINUE || tok.kind == TBREAK ||
		    tok.kind == TRETURN || tok.kind == TLBRACE ||
		    tok.kind == T__ASM__) {
			if (!curfunc)
				error(&tok.loc, "control-flow in statement expression requires function context");
			stmt(curfunc, s);
			continue;
		}

		/* Expression statement.
		 * Non-last expressions are emitted to curfunc immediately
		 * (parser-time IR, preserving source order).  The LAST
		 * expression is saved for deferred funcexpr() in IR gen. */
		{
			struct expr *e = expr(s);
			expect(TSEMICOLON, "after expression in statement expression");

			/* Peek at next token: if closing brace, this is the result */
			if (tok.kind == TRBRACE) {
				last_expr = e;
			} else {
				/* Side-effect expression — emit IR now to keep
				 * correct ordering with stmt()-emitted IR. */
				if (curfunc) {
					funcexpr(curfunc, e);
				}
				delexpr(e);
			}
		}
	}

	next(); /* consume '}' */
	s = delscope(s);

	{
		struct type *result_type = last_expr ? last_expr->type : &typevoid;
		struct expr *e = mkexpr(EXPRSTMTEXPR, result_type, NULL);
		e->u.stmt_expr.items = NULL;
		e->u.stmt_expr.last_expr = last_expr;
		return e;
	}
}
