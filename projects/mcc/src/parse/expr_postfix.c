/* parse/expr_postfix.c -- postfix expressions (the second grammar tier).
 *
 * Implements postfixexpr() which handles function calls, array subscripts,
 * member access, ++/-- on lvalues, and statement expressions. mkincdecexpr()
 * builds the actual ++/-- node shared with the unary tier. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"
#include "mcc.h"
#include "expr_internal.h"

struct expr *mkexpr(enum exprkind, struct type *, struct expr *);
struct expr *mkunaryexpr(enum tokenkind, struct expr *);
struct expr *mkbinaryexpr(struct location *, enum tokenkind, struct expr *, struct expr *);
struct expr *assignexpr(struct scope *);
struct expr *castexpr(struct scope *);
struct expr *primaryexpr(struct scope *);
struct expr *unaryexpr(struct scope *);

/* Forward decl for sibling files. */
struct expr *assignexpr(struct scope *);
struct expr *castexpr(struct scope *);
struct expr *primaryexpr(struct scope *);
struct expr *unaryexpr(struct scope *);

struct expr *
mkincdecexpr(enum tokenkind op, struct expr *base, bool post)
{
	struct expr *e;

	if (!base->lvalue)
		error(&tok.loc, "operand of '%s' operator must be an lvalue", tokenstr(op));
	if (base->qual & QUALCONST)
		error(&tok.loc, "operand of '%s' operator is const qualified", tokenstr(op));
	e = mkexpr(EXPRINCDEC, base->type, base);
	e->op = op;
	e->u.incdec.post = post;
	return e;
}
struct expr *
postfixexpr(struct scope *s, struct expr *r)
{
	struct expr *e, *arr, *idx, *tmp, **end;
	struct type *t;
	struct decl *p;
	struct member *m;
	unsigned long long offset;
	enum typequal tq;
	enum tokenkind op;
	bool lvalue;

	if (!r)
		r = primaryexpr(s);
	for (;;) {
		switch (tok.kind) {
		case TLBRACK:  /* subscript */
			next();
			arr = r;
			idx = expr(s);
			if (arr->type->kind != TYPEPOINTER) {
				if (idx->type->kind != TYPEPOINTER)
					error(&tok.loc, "either array or index must be pointer type");
				tmp = arr;
				arr = idx;
				idx = tmp;
			}
			if (arr->type->base->incomplete)
				error(&tok.loc, "array is pointer to incomplete type");
			if (!(idx->type->prop & PROPINT))
				error(&tok.loc, "index is not an integer type");
			e = mkunaryexpr(TMUL, mkbinaryexpr(&tok.loc, TADD, arr, idx));
			expect(TRBRACK, "after array index");
			break;
		case TLPAREN:  /* function call */
			next();
			if (r->kind == EXPRIDENT && r->u.ident.decl->kind == DECLBUILTIN) {
				e = builtinfunc(s, r->u.ident.decl->u.builtin);
				expect(TRPAREN, "after builtin parameters");
				break;
			}
			if (r->type->kind != TYPEPOINTER || r->type->base->kind != TYPEFUNC)
				error(&tok.loc, "called object is not a function");
			t = r->type->base;
			e = mkexpr(EXPRCALL, t->base, r);
			e->u.call.args = NULL;
			e->u.call.nargs = 0;
			p = t->u.func.params;
			end = &e->u.call.args;
			/* C++ member call: prepend the this object as the first
			 * argument (lowered from `obj.meth(...)`). */
			{
				extern struct expr *g_cpp_member_this;
				if (g_cpp_member_this) {
					*end = g_cpp_member_this;
					end = &(*end)->next;
					++e->u.call.nargs;
					g_cpp_member_this = NULL;
					if (p)
						p = p->next;
				}
			}
			while (tok.kind != TRPAREN) {
				if (e->u.call.args)
					expect(TCOMMA, "or ')' after function call argument");
				if (!p && !t->u.func.isvararg)
					error(&tok.loc, "too many arguments for function call");
				*end = assignexpr(s);
				if (t->u.func.isvararg && !p)
					*end = exprpromote(*end);
				else
					*end = exprassign(*end, p->type);
				end = &(*end)->next;
				++e->u.call.nargs;
				if (p)
					p = p->next;
			}
			if (p && !t->u.func.isvararg)
				error(&tok.loc, "not enough arguments for function call");
			e = decay(e);
			next();
			break;
		case TPERIOD:
			r = mkunaryexpr(TBAND, r);
			/* fallthrough */
		case TARROW:
			op = tok.kind;
			if (r->type->kind != TYPEPOINTER)
				error(&tok.loc, "'%s' operator must be applied to pointer to struct/union", tokenstr(op));
			t = r->type->base;
			tq = r->type->qual;
			if (t->kind != TYPESTRUCT && t->kind != TYPEUNION)
				error(&tok.loc, "'%s' operator must be applied to pointer to struct/union", tokenstr(op));
			next();
			if (tok.kind < TIDENT)
				error(&tok.loc, "expected identifier after '%s' operator", tokenstr(op));
			lvalue = op == TARROW || r->base->lvalue;
			offset = 0;
			m = typemember(t, tokenstr(tok.kind), &offset);
			if (!m)
				error(&tok.loc, "struct/union has no member named '%s'", tok.lit);
			/* C++ member function call: `obj.meth` lowers to a call of
			 * `Class_meth` with the object address as the implicit this
			 * argument.  We build an identifier referencing the mangled
			 * free function and remember the this object so the call
			 * lowering (TLPAREN) can prepend it as an argument. */
			if (m->type && m->type->kind == TYPEFUNC) {
				extern bool cpp_is_member_function(struct type *,
				    const char *);
				extern const char *cpp_mangled_name(struct type *,
				    const char *, char *, size_t);
				extern struct expr *g_cpp_member_this;
				char mname[256];
				struct decl *fd;
				if (cpp_is_member_function(t, m->name)) {
					cpp_mangled_name(t, m->name, mname, sizeof mname);
					fd = scopegetdecl(s, mname, 1);
					if (fd && fd->kind == DECLFUNC) {
						e = mkexpr(EXPRIDENT, fd->type, NULL);
						e->u.ident.decl = fd;
						g_cpp_member_this = r; /* &obj */
						next();
						break;
					}
				}
			}
			r = mkbinaryexpr(&tok.loc, TADD, exprconvert(r, &typeulong), mkconstexpr(&typeulong, offset));
			r->type = mkpointertype(m->type, tq | m->qual);
			r = mkunaryexpr(TMUL, r);
			r->lvalue = lvalue;
			if (m->bits.before || m->bits.after) {
				e = mkexpr(EXPRBITFIELD, r->type, r);
				e->lvalue = lvalue;
				e->u.bitfield.bits = m->bits;
			} else {
				e = r;
			}
			next();
			break;
		case TINC:
		case TDEC:
			e = mkincdecexpr(tok.kind, r, true);
			next();
			break;
		default:
			return r;
		}
		r = e;
	}
}
