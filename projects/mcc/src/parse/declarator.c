/* parse/declarator.c -- declarator grammar.
 *
 * Implements the "declarator" half of the C declaration grammar:
 *   declaratortypes() -> walks paren/star/brackets and produces a list
 *                        types (pointer/array/function)
 *   declarator()      -> the recursive-descent entry point
 *   parameter()       -> function-parameter parser
 *
 * All of these consume the qualtype produced by declspecs() (in
 * specs.c) and return either a derived type chain or a name. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include "util.h"
#include "mcc.h"
#include "decl_internal.h"

void
declaratortypes(struct scope *s, struct list *result, char **name, int *align, struct scope **funcscope, bool allowabstract)
{
	struct list *ptr;
	struct type *t;
	struct decl *d, **paramend;
	struct expr *e;
	struct attr a;
	enum typequal tq;
	int allowedattr;

	while (consume(TMUL)) {
		attr(NULL, 0);
		tq = QUALNONE;
		while (typequal(&tq))
			;
		t = mkpointertype(NULL, tq);
		listinsert(result, &t->link);
	}
	if (name)
		*name = NULL;
	a.kind = 0;
	ptr = result->next;
	switch (tok.kind) {
	case TLPAREN:
		next();
		if (allowabstract) {
			switch (tok.kind) {
			case TMUL:
			case TLPAREN:
				break;
			default:
				if (tok.kind >= TIDENT && !istypename(s, tokenstr(tok.kind)))
					break;
				goto func;
			}
		}
		declaratortypes(s, result, name, align, funcscope, allowabstract);
		expect(TRPAREN, "after parenthesized declarator");
		allowedattr = -1;
		break;
	default:
		if (tok.kind >= TIDENT) {
			if (!name)
				error(&tok.loc, "identifier not allowed in abstract declarator");
			*name = tokenstr(tok.kind);
			next();
		} else if (!allowabstract) {
			error(&tok.loc, "expected '(' or identifier");
		}
		/*
		the aligned attribute is used in the definition of
		max_align_t from gcc/clang (used with glibc),
		dragonflybsd, freebsd, and openbsd
		*/
		allowedattr = align ? ATTRALIGNED : (ATTRFALLTHROUGH | ATTRNODISCARD | ATTRMAYBEUNUSED | ATTRDEPRECATED);
	}
	for (;;) {
		switch (tok.kind) {
		case TLPAREN:  /* function declarator */
			next();
		func:
			t = mktype(TYPEFUNC, 0);
			t->qual = QUALNONE;
			t->u.func.isvararg = false;
			t->u.func.params = NULL;
			t->u.func.nparam = 0;
			paramend = &t->u.func.params;
			s = mkscope(s);
			d = NULL;
			do {
				if (consume(TELLIPSIS)) {
					t->u.func.isvararg = true;
					break;
				}
				if (tok.kind == TRPAREN)
					break;
				d = parameter(s);
				if (d->name)
					scopeputdecl(s, d);
				*paramend = d;
				paramend = &d->next;
				++t->u.func.nparam;
			} while (consume(TCOMMA));
			expect(TRPAREN, "to close function declarator");
			if (funcscope && ptr->prev == result) {
				/* we may need to re-open the scope later if this is a function definition */
				*funcscope = s;
				s = s->parent;
			} else {
				s = delscope(s);
			}
			if (t->u.func.nparam == 1 && !t->u.func.isvararg && d->type->kind == TYPEVOID && !d->name) {
				t->u.func.params = NULL;
				t->u.func.nparam = 0;
			}
			listinsert(ptr->prev, &t->link);
			allowedattr = 0;
			break;
		case TLBRACK:  /* array declarator */
			if (allowedattr != -1 && attr(&a, allowedattr))
				goto attr;
			next();
			t = mkarraytype(NULL, QUALNONE, 0);
			while (consume(TSTATIC) || typequal(&t->u.array.ptrqual))
				;
			if (tok.kind == TMUL && peek(TRBRACK)) {
				t->prop |= PROPVM;
				t->incomplete = false;
			} else if (!consume(TRBRACK)) {
				e = assignexpr(s);
				if (!(e->type->prop & PROPINT))
					error(&tok.loc, "array length expression must have integer type");
				t->u.array.length = e;
				t->incomplete = false;
				expect(TRBRACK, "after array length");
			}
			listinsert(ptr->prev, &t->link);
			allowedattr = 0;
			break;
		case T__EXTENSION__:
			next();
			break;
		case T__ATTRIBUTE__:
			if (allowedattr == -1)
				error(&tok.loc, "attribute not allowed after parenthesized declarator");
			gnuattr(&a, allowedattr);
		attr:
			/* attribute applies to identifier if ptr->prev == result, otherwise type ptr->prev */
			if (ptr->prev == result) {
				if (a.kind & ATTRALIGNED && a.align > *align)
					*align = a.align;
			}
			break;
		default:
			return;
		}
	}
}
struct qualtype
declarator(struct scope *s, struct qualtype base, char **name, int *align, struct scope **funcscope, bool allowabstract)
{
	struct type *t;
	enum typequal tq;
	struct expr *e;
	struct list result = {&result, &result}, *l, *prev;

	if (funcscope)
		*funcscope = NULL;
	declaratortypes(s, &result, name, align, funcscope, allowabstract);
	for (l = result.prev; l != &result; l = prev) {
		prev = l->prev;
		t = listelement(l, struct type, link);
		tq = t->qual;
		t->base = base.type;
		t->qual = base.qual;
		t->prop |= base.type->prop & PROPVM;
		switch (t->kind) {
		case TYPEFUNC:
			if (base.type->kind == TYPEFUNC)
				error(&tok.loc, "function declarator specifies function return type");
			if (base.type->kind == TYPEARRAY)
				error(&tok.loc, "function declarator specifies array return type");
			break;
		case TYPEARRAY:
			if (base.type->incomplete)
				error(&tok.loc, "array element has incomplete type");
			if (base.type->kind == TYPEFUNC)
				error(&tok.loc, "array element has function type");
			if (t->u.array.ptrqual) {
				/* TODO: check if we are in a function prototype
				if (?)
					error(&tok.loc, "array type has qualifiers outside of a function prototype");
				*/
				if (prev != &result)
					error(&tok.loc, "nested array type has qualifiers");
			}
			t->align = base.type->align;
			t->size = 0;
			if (t->u.array.length) {
				e = eval(t->u.array.length);
				if (e->kind == EXPRCONST && base.type->size) {
					if (e->type->u.arith.issigned && e->u.constant.u >> 63)
						error(&tok.loc, "array length must be non-negative");
					if (e->u.constant.u > ULLONG_MAX / base.type->size)
						error(&tok.loc, "array length is too large");
					t->size = base.type->size * e->u.constant.u;
				} else {
					t->prop |= PROPVM;
					t->u.array.length = e;
					t->u.array.size = NULL;
				}
			}
			break;
		}
		base.type = t;
		base.qual = tq;
	}

	return base;
}
struct decl *
parameter(struct scope *s)
{
	struct decl *d;
	char *name;
	struct qualtype t;
	enum storageclass sc;

	attr(NULL, 0);
	t = declspecs(s, &sc, NULL, NULL);
	if (!t.type)
		error(&tok.loc, "no type in parameter declaration");
	if (sc && sc != SCREGISTER)
		error(&tok.loc, "parameter declaration has invalid storage-class specifier");
	t = declarator(s, t, &name, NULL, NULL, true);
	t.type = typeadjust(t.type, &t.qual);
	d = mkdecl(name, DECLOBJECT, t.type, t.qual, LINKNONE);
	d->u.obj.storage = SDAUTO;
	return d;
}
