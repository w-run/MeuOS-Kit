/* parse/expr_unary.c -- unary expressions (the third grammar tier).
 *
 * Implements unaryexpr() (prefix ++/--, &, *, +, -, ~, !, sizeof, _Alignof)
 * and castexpr() (the cast operator and the fallthrough to postfix). */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"
#include "mcc.h"
#include "expr_internal.h"
#include "cpp/cpp_tokens.h"

struct expr *mkexpr(enum exprkind, struct type *, struct expr *);
struct expr *decay(struct expr *);
struct expr *castexpr(struct scope *);
struct expr *primaryexpr(struct scope *);
struct expr *unaryexpr(struct scope *);
struct expr *postfixexpr(struct scope *, struct expr *);

struct expr *unaryexpr(struct scope *);
struct expr *castexpr(struct scope *);
struct expr *postfixexpr(struct scope *, struct expr *);
struct expr *primaryexpr(struct scope *);
struct expr *unaryexpr(struct scope *);

struct expr *
unaryexpr(struct scope *s)
{
	enum tokenkind op;
	struct expr *e, *l;
	struct type *t;

	/* C++ new/delete expressions: `new T(args)` lowers to a malloc +
	 * constructor call, `delete p` to a destructor call + free.  Both
	 * are parsed by the C++ frontend (they need the class ctor/dtor
	 * machinery and the libc allocator). */
	{
		extern int g_lang;
		if (g_lang == 1) {
			extern enum cpp_tokenkind cpp_tok_kind(void);
			extern struct expr *cpp_parse_new_expr(struct scope *);
			extern struct expr *cpp_parse_delete_expr(struct scope *);
			enum cpp_tokenkind ck = cpp_tok_kind();
			if (ck == CPP_TNEW)
				return cpp_parse_new_expr(s);
			if (ck == CPP_TDELETE)
				return cpp_parse_delete_expr(s);
		}
	}
	op = tok.kind;
	switch (op) {
	case TINC:
	case TDEC:
		next();
		l = unaryexpr(s);
		e = mkincdecexpr(op, l, false);
		break;
	case TBAND:
	case TMUL:
		next();
		return mkunaryexpr(op, castexpr(s));
	case TADD:
		next();
		e = castexpr(s);
		if (!(e->type->prop & PROPARITH))
			error(&tok.loc, "operand of unary '+' operator must have arithmetic type");
		if (e->type->prop & PROPINT)
			e = exprpromote(e);
		break;
	case TSUB:
		next();
		e = castexpr(s);
		if (!(e->type->prop & PROPARITH))
			error(&tok.loc, "operand of unary '-' operator must have arithmetic type");
		if (e->type->prop & PROPINT)
			e = exprpromote(e);
		e = mkexpr(EXPRUNARY, e->type, e);
		e->op = op;
		break;
	case TBNOT:
		next();
		e = castexpr(s);
		if (!(e->type->prop & PROPINT))
			error(&tok.loc, "operand of '~' operator must have integer type");
		e = exprpromote(e);
		e = mkbinaryexpr(&tok.loc, TXOR, e, mkconstexpr(e->type, -1));
		break;
	case TLNOT:
		next();
		e = castexpr(s);
		if (!(e->type->prop & PROPSCALAR))
			error(&tok.loc, "operator '!' must have scalar operand");
		e = mkbinaryexpr(&tok.loc, TEQL, e, mkconstexpr(&typeint, 0));
		break;
	case TSIZEOF:
	case TALIGNOF:
	case T__ALIGNOF__:
		next();
		if (op == TSIZEOF && tok.kind == TELLIPSIS) {
			/* C++11 `sizeof...(Pack)`: the number of elements in a
			 * template parameter pack, resolved at instantiation-replay
			 * time by the variadic-template machinery. */
			extern int g_lang;
			extern int cpp_sizeof_pack(void);
			if (g_lang != 1)
				error(&tok.loc, "'...' cannot appear here");
			next(); /* consume ... */
			expect(TLPAREN, "after 'sizeof...'");
			if (tok.kind < TIDENT)
				error(&tok.loc, "expected pack name after 'sizeof...('");
			next(); /* consume the pack name */
			expect(TRPAREN, "after pack name in 'sizeof...'");
			e = mkconstexpr(&typeulong, cpp_sizeof_pack());
			break;
		}
		if (consume(TLPAREN)) {
			t = typename(s, NULL, NULL);
			if (t) {
				expect(TRPAREN, "after type name");
				/* might be part of a compound literal */
				if (op == TSIZEOF && tok.kind == TLBRACE)
					parseinit(s, t);
				e = NULL;
			} else {
				e = expr(s);
				expect(TRPAREN, "after expression");
				if (op == TSIZEOF)
					e = postfixexpr(s, e);
			}
		} else if (op == TSIZEOF) {
			t = NULL;
			e = unaryexpr(s);
		} else {
			error(&tok.loc, "expected ')' after 'alignof'");
		}
		if (!t) {
			if (e->decayed)
				e = e->base;
			if (e->kind == EXPRBITFIELD)
				error(&tok.loc, "%s operator applied to bitfield expression", tokenstr(op));
			t = e->type;
		}
		if (t->incomplete)
			error(&tok.loc, "%s operator applied to incomplete type", tokenstr(op));
		if (t->kind == TYPEFUNC)
			error(&tok.loc, "%s operator applied to function type", tokenstr(op));
		if (t->kind == TYPEARRAY && t->size == 0 && op == TSIZEOF) {
			e = mkexpr(EXPRSIZEOF, &typeulong, e);
			e->u.szof.type = t;
		} else {
			/*
			XXX: __alignof__ is not the same as alignof on 32-bit archs
			this needs to be considered if we gain support for those archs
			*/
			e = mkconstexpr(&typeulong, op == TSIZEOF ? t->size : t->align);
		}
		break;
	case T__REAL__:
	case T__IMAG__: {
		/* __real__ expr / __imag__ expr — complex real/imaginary part access */
		struct type *realt;

		next();
		e = unaryexpr(s);
		realt = e->type->base ? e->type->base : e->type;
		if (!realt)
			error(&tok.loc, "complex type has no base type");
		e = mkexpr(EXPRUNARY, realt, e);
		e->op = op;
		e->lvalue = true;
		break;
	}
	default:
		e = postfixexpr(s, NULL);
	}

	return e;
}
struct expr *
castexpr(struct scope *s)
{
	struct type *t, *ct;
	struct decl *d;
	enum typequal tq;
	struct expr *r, *e, **end, *toeval;

	ct = NULL;
	end = &r;
	while (consume(TLPAREN)) {
		tq = QUALNONE;
		t = typename(s, &tq, &toeval);
		if (!t) {
			/* GNU statement expression ({...}) — handled before
			 * falling through to parenthesised expression so that
			 * `({` inside function-call arguments is caught here
			 * (castexpr steals the leading `(` before primaryexpr
			 * ever sees it). */
			if (tok.kind == TLBRACE) {
				e = parse_stmt_expr_body(s);
				expect(TRPAREN, "after statement expression");
				e = postfixexpr(s, e);
				goto done;
			}
			e = expr(s);
			expect(TRPAREN, "after expression to match '('");
			e = postfixexpr(s, e);
			goto done;
		}
		expect(TRPAREN, "after type name");
		if (tok.kind == TLBRACE) {
			e = mkexpr(EXPRCOMPOUND, t, NULL);
			e->toeval = toeval;
			e->qual = tq;
			e->lvalue = true;
			d = mkdecl(NULL, DECLOBJECT, t, tq, LINKNONE);
			d->u.obj.storage = s == &filescope ? SDSTATIC : SDAUTO;
			e->u.compound.decl = d;
			e->u.compound.init = parseinit(s, t);
			e = postfixexpr(s, decay(e));
			goto done;
		}
		if (t != &typevoid && !(t->prop & PROPSCALAR))
			error(&tok.loc, "cast type must be scalar");
		e = mkexpr(EXPRCAST, t, NULL);
		e->toeval = toeval;
		*end = e;
		end = &e->base;
		ct = t;
	}
	e = unaryexpr(s);

done:
	if (ct && ct != &typevoid && !(e->type->prop & PROPSCALAR))
		error(&tok.loc, "cast operand must have scalar type");
	*end = e;
	return r;
}
