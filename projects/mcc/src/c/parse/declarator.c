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
#include "expr_internal.h"
#include "cpp/cpp_tokens.h"

/* Does the current token start a C++ constructor-call argument list
 * rather than a function parameter declaration?  `Point p(3)` — a numeric
 * literal (or any expression token that cannot begin a parameter
 * declaration) means object construction, not a function declarator. */
/* Could the current token begin a type-specifier (for disambiguating a
 * `this X& self` deducing-this parameter from a `this` ctor argument)? */
static bool
cpp_is_type_start(struct scope *s)
{
	if (tok.kind >= TIDENT)
		return istypename(s, tokenstr(tok.kind));
	switch (tok.kind) {
	case TCONST:
	case TVOLATILE:
	case TCONSTEXPR:
	case TINT:
	case TCHAR:
	case TSHORT:
	case TLONG:
	case TFLOAT:
	case TDOUBLE:
	case TSIGNED:
	case TUNSIGNED:
	case TBOOL:
	case TVOID:
	case TSTRUCT:
	case TUNION:
	case TENUM:
	case TRESTRICT:
		return true;
	default:
		return false;
	}
}

static bool
is_ctor_expr_start(struct scope *s)
{
	/* Plain identifiers carry dynamic token kinds (>= TIDENT); only
	 * keywords use their static enum values.  A non-typename identifier
	 * cannot begin a parameter declaration, so it starts constructor
	 * arguments instead (e.g. `Vec r(v)`). */
	if (tok.kind >= TIDENT) {
		extern int g_lang;
		if (g_lang == 1 &&
		    strcmp(tokenstr(tok.kind), "this") == 0) {
			/* `this` alone or as an expression is a valid ctor argument
			 * (`Vec v(this)`, `Vec v(this->n)`); `this` followed by a
			 * type-specifier is a C++23 deducing-this explicit object
			 * parameter (`void f(this X& self)`), which is NOT a ctor
			 * call. */
			struct token save = tok;
			struct token pending;
			bool deducing;
			next();
			deducing = cpp_is_type_start(s);
			/* rewind: re-queue the token after `this` from a local
			 * snapshot (tokpush stores the address, so it must not
			 * point at the global `tok`, which is restored below) */
			pending = tok;
			tok = save;
			tokpush(&pending, 1);
			return !deducing;
		}
		return !istypename(s, tokenstr(tok.kind));
	}
	switch (tok.kind) {
	case TNUMBER:
	case TSTRINGLIT:
	case TCHARCONST:
	case TLPAREN:
	case TSUB:
	case TBNOT:
	case TLNOT:
	case TMUL:
	case TBAND:
		return true;
	default:
		return false;
	}
}

void
declaratortypes(struct scope *s, struct list *result, char **name, int *align, struct scope **funcscope, bool allowabstract, struct attr *attrout)
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
	/* C++ reference declarator: `T &name` is a reference type (a pointer
	 * that auto-dereferences in expressions).  `T &&name` is an rvalue
	 * reference (binds only to temporaries).  The lexer tokenizes `&&` as
	 * a single TLAND token, so both `&` (TBAND) and `&&` (TLAND) start a
	 * reference declarator; in this context they cannot be a binary
	 * logical-and operator. */
	extern int g_lang;
	while (g_lang == 1 && (tok.kind == TBAND || tok.kind == TLAND)) {
		t = mkpointertype(NULL, QUALNONE);
		t->isref = true;
		/* `&&` (TLAND) = rvalue reference; distinguish it from a single
		 * `&` so the mangled signature can tell the overloads apart. */
		t->isrref = tok.kind == TLAND;
		next();
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
		declaratortypes(s, result, name, align, funcscope, allowabstract, attrout);
		expect(TRPAREN, "after parenthesized declarator");
		allowedattr = -1;
		break;
	default:
		if (tok.kind >= TIDENT) {
			if (!name)
				error_code(E_SYNTAX, &tok.loc, "identifier not allowed in abstract declarator");
			*name = tokenstr(tok.kind);
			next();
			/* C++ qualified method name: `Class::method` or
			 * `ns::Class::method` lowers to the same mangled
			 * `Class_method` symbol used by in-class definitions, so
			 * out-of-line definitions match the in-class declaration. */
			extern int g_lang;
			if (g_lang == 1 && tok.kind == TCOLONCOLON) {
				extern void cpp_set_qual_class(const char *);
				extern void cpp_set_qual_ns(struct scope *);
				struct decl *nsd = scopegetdecl(s, *name, 1);
				if (nsd && nsd->kind == DECLNAMESPACE) {
					/* namespace-qualified: ns::Class::method */
					struct scope *cur = nsd->u.ns;
					next(); /* consume first '::' */
					for (;;) {
						struct decl *d;
						struct type *ct;
						const char *comp;
						if (tok.kind < TIDENT)
							error_code(E_SYNTAX, &tok.loc, "expected name after '::'");
						comp = tokenstr(tok.kind);
						d = scopegetdecl(cur, comp, 1);
						ct = scopegettag(cur, comp, 1);
						if (d && d->kind == DECLNAMESPACE) {
							cur = d->u.ns;
							next();
							expect(TCOLONCOLON, "after namespace name");
							continue;
						}
						if (ct && (ct->kind == TYPESTRUCT || ct->kind == TYPEUNION)) {
							char *mname;
							const char *qclass = ct->u.structunion.tag;
							next(); /* consume class name */
							expect(TCOLONCOLON, "after class name");
							if (tok.kind < TIDENT)
								error_code(E_SYNTAX, &tok.loc, "expected member name after '::'");
							mname = xmalloc(strlen(qclass) + strlen(tokenstr(tok.kind)) + 2);
							sprintf(mname, "%s_%s", qclass, tokenstr(tok.kind));
							*name = mname;
							cpp_set_qual_class(qclass);
							cpp_set_qual_ns(cur);
							next();
							break;
						}
						error_code(E_CTYPE, &tok.loc, "'%s' is not a class or namespace", comp);
					}
				} else {
					const char *qclass = *name;
					char *mname;
					next(); /* consume '::' */
					if (tok.kind < TIDENT)
						error_code(E_SYNTAX, &tok.loc, "expected member name after '::'");
					mname = xmalloc(strlen(qclass) + strlen(tokenstr(tok.kind)) + 2);
					sprintf(mname, "%s_%s", qclass, tokenstr(tok.kind));
					*name = mname;
					cpp_set_qual_class(qclass);
					next();
				}
			}
		} else if (!allowabstract) {
			error_code(E_SYNTAX, &tok.loc, "expected '(' or identifier");
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
			/* C++ constructor-call arguments: `Point p(3, 4);` — the
			 * name was already parsed and the parenthesized list starts
			 * with an expression, not a parameter declaration.  Collect
			 * the argument expressions and signal decl() to build an
			 * object instead of a function; the base type must be a
			 * class (decl() verifies). */
			extern int g_lang;
			if (g_lang == 1 && tok.kind != TRPAREN && is_ctor_expr_start(s)) {
				struct expr *arge;
				extern void cpp_ctor_args_begin(void);
				extern void cpp_ctor_args_add(struct expr *);
				extern void cpp_ctor_set_active(void);
			cpp_ctor_args_begin();
				while (tok.kind != TRPAREN) {
					arge = assignexpr(s);
					cpp_ctor_args_add(arge);
					if (tok.kind == TRPAREN)
						break;
					expect(TCOMMA, "or ')' after constructor argument");
				}
				next(); /* consume ')' */
				cpp_ctor_set_active();
				/* leave result empty: declarator() returns the base
				 * (class) type as an object declaration */
				return;
			}
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
				/* C++23 deducing this (P0847): a leading `this X& self`
				 * explicit object parameter.  `this` is an identifier to
				 * the C lexer; only the first parameter may be one. */
				if (g_lang == 1 && t->u.func.nparam == 0 &&
				    tok.kind >= TIDENT &&
				    strcmp(tokenstr(tok.kind), "this") == 0) {
					extern void cpp_explicit_obj_begin(void);
					extern void cpp_explicit_obj_set(struct decl *);
					cpp_explicit_obj_begin();
					next(); /* consume 'this' */
					d = parameter(s);
					cpp_explicit_obj_set(d);
				} else {
					d = parameter(s);
				}
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
			/* C++ noexcept specifier: `int f() noexcept { ... }`.  The
			 * keyword is a C++-only identifier, classified by the C++
			 * lexer layer.  Consume it and record the flag on the
			 * function type.  (noexcept is not stored in the symbol
			 * table — it's a type-level attribute.) */
			if (g_lang == 1 && tok.kind >= TIDENT) {
				extern enum cpp_tokenkind cpp_tok_kind(void);
				if (cpp_tok_kind() == CPP_TNOEXCEPT) {
					t->u.func.is_noexcept = true;
					next();
				}
			}
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
					error_code(E_CTYPE, &tok.loc, "array length expression must have integer type");
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
				error_code(E_CTYPE, &tok.loc, "attribute not allowed after parenthesized declarator");
			/* GNU attributes: allow all recognized GNU attrs */
			{
				enum attrkind gnu_allowed = (enum attrkind)(ATTRALIGNED | ATTRSECTION |
				    ATTRWEAK | ATTRUSED | ATTRNOINLINE | ATTRALWAYSINLINE |
				    ATTRCONSTRUCTOR | ATTRDESTRUCTOR | ATTRPACKED | ATTRNORETURN |
				    ATTRDEPRECATED);
				gnuattr(&a, gnu_allowed);
			}
		attr:
			/* attribute applies to identifier if ptr->prev == result, otherwise type ptr->prev */
			if (ptr->prev == result) {
				if (a.kind & ATTRALIGNED && a.align > *align)
					*align = a.align;
			}
			if (attrout)
				attrout->kind |= a.kind;
			break;
		default:
			return;
		}
	}
}
struct qualtype
declarator(struct scope *s, struct qualtype base, char **name, int *align, struct scope **funcscope, bool allowabstract, struct attr *attrout)
{
	struct type *t;
	enum typequal tq;
	struct expr *e;
	struct list result = {&result, &result}, *l, *prev;
	struct attr da = {0};

	if (funcscope)
		*funcscope = NULL;
	declaratortypes(s, &result, name, align, funcscope, allowabstract, &da);
	if (attrout)
		attrout->kind |= da.kind;
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
				error_code(E_DECL, &tok.loc, "function declarator specifies function return type");
			if (base.type->kind == TYPEARRAY)
				error_code(E_DECL, &tok.loc, "function declarator specifies array return type");
			break;
		case TYPEARRAY:
			if (base.type->incomplete)
				error_code(E_INCOMPLETE, &tok.loc, "array element has incomplete type");
			if (base.type->kind == TYPEFUNC)
				error_code(E_DECL, &tok.loc, "array element has function type");
			if (t->u.array.ptrqual) {
				/* TODO: check if we are in a function prototype
				if (?)
					error_code(E_DECL, &tok.loc, "array type has qualifiers outside of a function prototype");
				*/
				if (prev != &result)
					error_code(E_CTYPE, &tok.loc, "nested array type has qualifiers");
			}
			t->align = base.type->align;
			t->size = 0;
			if (t->u.array.length) {
				e = eval(t->u.array.length);
				if (e->kind == EXPRCONST && base.type->size) {
					if (e->type->u.arith.issigned && e->u.constant.u >> 63)
						error_code(E_CTYPE, &tok.loc, "array length must be non-negative");
					if (e->u.constant.u > ULLONG_MAX / base.type->size)
						error_code(E_CTYPE, &tok.loc, "array length is too large");
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

	/* C++ constructor-call declarator: the base (class) type is the
	 * object type; flag it to decl() with a sentinel expr. */
	extern int g_lang;
	if (g_lang == 1) {
		extern bool cpp_ctor_is_active(void);
		extern void cpp_ctor_clear_active(void);
		if (cpp_ctor_is_active()) {
			base.expr = (struct expr *)1; /* ctor-call sentinel */
			cpp_ctor_clear_active();
		}
	}

	base.kind = da.kind;
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
		error_code(E_CTYPE, &tok.loc, "no type in parameter declaration");
	if (sc && sc != SCREGISTER)
		error_code(E_DECL, &tok.loc, "parameter declaration has invalid storage-class specifier");
	t = declarator(s, t, &name, NULL, NULL, true, NULL);
	t.type = typeadjust(t.type, &t.qual);
	d = mkdecl(name, DECLOBJECT, t.type, t.qual, LINKNONE);
	d->u.obj.storage = SDAUTO;
	return d;
}
