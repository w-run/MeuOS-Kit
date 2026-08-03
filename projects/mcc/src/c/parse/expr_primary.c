/* parse/expr_primary.c -- primary expressions (the leaf of the grammar).
 *
 * Implements primaryexpr() which dispatches on the current token to
 * produce a leaf expression: identifier, constant, string, parenthesised
 * expression, generic selection, builtin call, statement expression, or
 * compound literal. designator() and builtinfunc() are helpers used
 * from initialiser and constant-expression paths respectively. */
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
#include "cpp/cpp_tokens.h"

/* designator() is file-local: only builtinfunc() in this translation
 * unit calls it. inttype() lives in expr_literal.c and is exported
 * via expr_internal.h, so no forward decl is needed here. */
static void designator(struct scope *, struct type *, unsigned long long *);

struct expr *
primaryexpr(struct scope *s)
{
	struct expr *e;
	struct decl *d;
	struct type *t;
	char *src, *end;
	uint_least32_t chr;
	unsigned long long val;
	bool hexoct, ordinary;
	int base;

	switch (tok.kind) {
	case TSTRINGLIT:
		e = mkexpr(EXPRSTRING, NULL, NULL);
		t = stringconcat(&e->u.string, false);
		e->type = mkarraytype(t, QUALNONE, e->u.string.size);
		e->lvalue = true;
		e = decay(e);
		break;
	case TCHARCONST:
		src = tok.lit;
		ordinary = false;
		switch (*src) {
		case 'L': ++src; t = targ->typewchar; break;
		case 'u': ++src; t = *src == '8' ? ++src, &typeuchar : &typeushort; break;
		case 'U': ++src; t = &typeuint; break;
		default: t = &typeuchar, ordinary = true;
		}
		assert(*src == '\'');
		++src;
		src += decodechar(src, &chr, &hexoct, "character constant", &tok.loc);
		if (hexoct && !typehasint(t, chr, false))
			error(&tok.loc, "character constant escape is out of range");
		val = chr;
		if (ordinary) {
			if (typechar.u.arith.issigned)
				val = (val ^ 0x80) - 0x80;
			t = &typeint;
		}
		e = mkconstexpr(t, val);
		if (*src != '\'')
			error(&tok.loc, "character constant contains more than one character: %c", *src);
		next();
		break;
	case TNUMBER:
		e = mkexpr(EXPRCONST, NULL, NULL);
		if (tok.lit[0] == '0') {
			switch (tok.lit[1]) {
			case 'x': case 'X': base = 16; break;
			case 'b': case 'B': base = 2; break;
			default: base = 8; break;
			}
		} else {
			base = 10;
		}
		/* C23: 100f / 42F float suffix without '.' or exponent (6.4.4.2).
		 * Only add fF for decimal; hex with .pP already works. */
		if (strpbrk(tok.lit, base == 16 ? ".pP" : ".eEfF")) {
			/* floating constant */
			e->u.constant.f = strtod(tok.lit, &end);
			if (end == tok.lit)
				error(&tok.loc, "invalid floating constant '%s'", tok.lit);
			if (!end[0])
				e->type = &typedouble;
			else if ((end[0] == 'f' || end[0] == 'F') && !end[1])
				e->type = &typefloat;
			else if ((end[0] == 'l' || end[0] == 'L') && !end[1])
				e->type = &typeldouble;
			else
				error(&tok.loc, "invalid floating constant suffix '%s'", end);
		} else {
			src = tok.lit;
			if (base == 2)
				src += 2;
			/* integer constant */
			e->u.constant.u = strtoull(src, &end, base);
			if (end == src)
				error(&tok.loc, "invalid integer constant '%s'", tok.lit);
			e->type = inttype(e->u.constant.u, base == 10, end);
		}
		next();
		break;
	case TTRUE:
	case TFALSE:
		e = mkexpr(EXPRCONST, &typebool, NULL);
		e->u.constant.u = tok.kind == TTRUE;
		next();
		break;
	case TNULLPTR:
		e = mkexpr(EXPRCONST, &typenullptr, NULL);
		e->u.constant.u = 0;
		next();
		break;
	case TLPAREN:
		next();
		if (tok.kind == TLBRACE) {
			/* GNU statement expression ({...}) */
			e = parse_stmt_expr_body(s);
			expect(TRPAREN, "after statement expression");
			break;
		}
		e = expr(s);
		expect(TRPAREN, "after expression");
		break;
	case T_GENERIC:
		e = generic(s);
		break;
	case T__PRAGMA__:
		/* _Pragma("string") — C99/C23 pragma operator, treated as no-op */
		next();
		expect(TLPAREN, "after _Pragma");
		if (tok.kind == TSTRINGLIT) next();
		expect(TRPAREN, "after _Pragma argument");
		e = mkexpr(EXPRCONST, &typevoid, NULL);
		e->u.constant.u = 0;
		break;
	default:
		if (tok.kind >= TIDENT) {
			/* C++20 requires-expression: `requires { ... }` /
			 * `requires (params) { reqs }` is a boolean constant
			 * expression (true when every requirement holds). */
			extern int g_lang;
			extern enum cpp_tokenkind cpp_tok_kind(void);
			extern struct expr *cpp_requires_expr(struct scope *);
			if (g_lang == 1 && cpp_tok_kind() == CPP_TREQUIRES)
				return cpp_requires_expr(s);
			/* C++ temporary-object construction: `Vec(expr)`.  A class
			 * tag followed by '(' is a constructor call (the tag can't be
			 * a function name). */
			{
				extern int g_lang;
				extern struct expr *cpp_temp_construct(struct scope *,
				    struct type *);
				if (g_lang == 1) {
					struct type *ct = scopegettag(s,
					    tokenstr(tok.kind), 1);
					if (ct && (ct->kind == TYPESTRUCT ||
					           ct->kind == TYPEUNION)) {
						struct token saved = tok;
						next();
						if (tok.kind == TLPAREN) {
							/* tok is '(' already; the constructor
							 * lowering consumes it */
							e = cpp_temp_construct(s, ct);
							if (e)
								break;
							tok = saved;
						} else if (tok.kind == TCOLONCOLON) {
							/* `Class::static_method(args)` — no this */
							extern void cpp_mangled_name_args(
							    struct type *, const char *,
							    struct expr *, char *, size_t);
							next(); /* consume '::' */
							if (tok.kind < TIDENT)
								error(&tok.loc,
								    "expected member name after '::'");
							{
								const char *m = tokenstr(tok.kind);
								struct expr *args = NULL, **ae = &args;
								char mname[256], mangled[288];
								struct decl *sfd;
								next(); /* consume member name */
								if (tok.kind == TLPAREN) {
									next(); /* consume '(' */
									while (tok.kind != TRPAREN) {
										if (args)
											expect(TCOMMA,
											    "or ')' after static call argument");
										*ae = assignexpr(s);
										ae = &(*ae)->next;
									}
									next(); /* consume ')' */
								}
								cpp_mangled_name_args(ct, m, args,
								    mname, sizeof mname);
								snprintf(mangled, sizeof mangled,
								    "%sS", mname);
								sfd = scopegetdecl(
								    ct->scope ? ct->scope : s,
								    mangled, 1);
								if (sfd && sfd->kind == DECLFUNC) {
									extern struct expr *decay(
									    struct expr *);
									extern struct expr *exprassign(
									    struct expr *, struct type *);
									struct expr *fn, *call, *a, **end;
									struct decl *pp;
									fn = mkexpr(EXPRIDENT,
									    sfd->type, NULL);
									fn->u.ident.decl = sfd;
									fn = decay(fn);
									call = mkexpr(EXPRCALL,
									    sfd->type->base, fn);
									call->u.call.args = NULL;
									call->u.call.nargs = 0;
									end = &call->u.call.args;
									pp = sfd->type->u.func.params;
									for (a = args; a; a = a->next) {
										if (!pp && !sfd->type->u.func.isvararg)
											error(&tok.loc,
											    "too many arguments for function call");
										if (sfd->type->u.func.isvararg && !pp)
											*end = exprpromote(a);
										else
											*end = exprassign(a,
											    pp->type);
										end = &(*end)->next;
										++call->u.call.nargs;
										if (pp)
											pp = pp->next;
									}
									if (pp && !sfd->type->u.func.isvararg)
										error(&tok.loc,
										    "not enough arguments for function call");
									e = call;
									break;
								}
								/* static data member access:
								 * `Class::count` -> Class_count */
								{
									char dmangled[256];
									struct decl *dd;
									snprintf(dmangled, sizeof dmangled,
									    "%s_%s", ct->u.structunion.tag, m);
									dd = scopegetdecl(
									    ct->scope ? ct->scope : s,
									    dmangled, 1);
									if (dd && dd->kind == DECLOBJECT) {
										e = mkexpr(EXPRIDENT,
										    dd->type, NULL);
										e->qual = dd->qual;
										e->lvalue = true;
										e->u.ident.decl = dd;
										break;
									}
								}
								/* not a static member: restore */
							}
							{
								struct token cur = tok;
								tokpush(&cur, 1);
								tok = saved;
							}
						} else {
							struct token cur = tok;
							tokpush(&cur, 1);
							tok = saved;
						}
					}
				}
			}
			/* C++ `this` (a keyword): the method-body `this` pointer.
			 * `this` is an identifier to the C lexer and cannot name a
			 * user variable, so intercepting it here is unambiguous. */
			extern int g_lang;
			if (g_lang == 1 &&
			    strcmp(tokenstr(tok.kind), "this") == 0) {
				extern struct expr *cpp_this_expr(void);
				e = cpp_this_expr();
				if (e) {
					next();
					break;
				}
			}
			d = scopegetdecl(s, tokenstr(tok.kind), 1);
			if (!d) {
				/* `using namespace foo;` makes foo's members visible */
				extern struct decl *cpp_lookup_visible(struct scope *,
				    const char *);
				extern int g_lang;
				if (g_lang == 1)
					d = cpp_lookup_visible(s, tokenstr(tok.kind));
			}
			if (d && d->kind == DECLNAMESPACE) {
				/* C++ namespace-qualified name, possibly multi-level:
				 * `ns1::ns2::name`. */
				struct scope *nss = d->u.ns;
				struct decl *md = NULL;
				next(); /* consume namespace name */
				for (;;) {
					if (tok.kind != TCOLONCOLON)
						error(&tok.loc, "expected '::' after namespace name");
					next(); /* consume '::' */
					if (tok.kind < TIDENT)
						error(&tok.loc, "expected name after '::'");
					md = scopegetdecl(nss, tokenstr(tok.kind), 1);
					if (!md)
						error(&tok.loc, "no member named '%s' in namespace '%s'",
						      tokenstr(tok.kind), d->name);
					next();
					if (md->kind != DECLNAMESPACE)
						break;
					nss = md->u.ns;
				}
				e = mkexpr(EXPRIDENT, md->type, NULL);
				e->qual = md->qual;
				e->lvalue = md->kind == DECLOBJECT;
				e->u.ident.decl = md;
				if (md->kind != DECLBUILTIN)
					e = decay(e);
				break;
			}
			if (!d) {
				/* C++ method body: a bare class-member name resolves
				 * to `(*this).name` (or a member call). */
				extern struct expr *cpp_member_ident(struct scope *,
				    const char *);
				e = cpp_member_ident(s, tokenstr(tok.kind));
				if (e) {
					next();
					break;
				}
				/* C++ function template: `max(...)` — the identifier is
				 * undeclared but names a template; the TLPAREN lowering
				 * instantiates it from the argument types. */
				extern int g_lang;
				extern const char *cpp_tmpl_lookup(const char *);
				extern struct expr *cpp_tmpl_placeholder(const char *);
				if (g_lang == 1 && cpp_tmpl_lookup(tokenstr(tok.kind))) {
					e = cpp_tmpl_placeholder(tokenstr(tok.kind));
					next();
					/* explicit template arguments: `f<int, 42>(...)` —
					 * types and/or non-type constant expressions.  The
					 * TLPAREN lowering later instantiates from these
					 * plus any remaining call-site arguments. */
					if (tok.kind == TLESS) {
						extern void cpp_tmpl_explicit_parse(
						    struct scope *);
						cpp_tmpl_explicit_parse(s);
					}
					break;
				}
				error(&tok.loc, "undeclared identifier: %s", tokenstr(tok.kind));
			}
			e = mkexpr(EXPRIDENT, d->type, NULL);
			e->qual = d->qual;
			e->lvalue = d->kind == DECLOBJECT;
			e->u.ident.decl = d;
			if (d->kind != DECLBUILTIN)
				e = decay(e);
			/* C++ reference: the identifier denotes the referent, so
			 * dereference the hidden pointer (`o` -> `*o`). */
			if (d->type && d->type->isref && d->kind == DECLOBJECT) {
				e = mkunaryexpr(TMUL, e);
				e->lvalue = true;
			}
			next();
			break;
		case TLBRACK: {
			/* C++ lambda expression: `[captures](params) -> ret { body }`.
			 * The array-subscript form is a postfix operator, so a `[`
			 * here (primary position) is always a lambda. */
			extern int g_lang;
			extern struct expr *cpp_lambda_expr(struct scope *);
			if (g_lang == 1)
				return cpp_lambda_expr(s);
			break;
		}
		}
		error(&tok.loc, "expected primary expression");
	}

	return e;
}
static void
designator(struct scope *s, struct type *t, unsigned long long *offset)
{
	char *name;
	struct member *m;
	unsigned long long i;

	for (;;) {
		switch (tok.kind) {
		case TLBRACK:
			if (t->kind != TYPEARRAY)
				error(&tok.loc, "index designator is only valid for array types");
			next();
			i = intconstexpr(s, false);
			expect(TRBRACK, "for index designator");
			t = t->base;
			*offset += i * t->size;
			break;
		case TPERIOD:
			if (t->kind != TYPESTRUCT && t->kind != TYPEUNION)
				error(&tok.loc, "member designator only valid for struct/union types");
			next();
			name = expect(TIDENT, "for member designator");
			m = typemember(t, name, offset);
			if (!m)
				error(&tok.loc, "%s has no member named '%s'", t->kind == TYPEUNION ? "union" : "struct", name);
			t = m->type;
			break;
		default:
			return;
		}
	}
}
struct expr *
builtinfunc(struct scope *s, enum builtinkind kind)
{
	struct expr *e, *toeval;
	struct type *t;
	struct member *m;
	char *name;
	unsigned long long offset;

	switch (kind) {
	case BUILTINALLOCA:
		e = exprassign(assignexpr(s), &typeulong);
		e = mkexpr(EXPRBUILTIN, mkpointertype(&typevoid, QUALNONE), e);
		e->u.builtin.kind = BUILTINALLOCA;
		break;
	case BUILTINCONSTANTP:
		e = mkconstexpr(&typeint, eval(condexpr(s))->kind == EXPRCONST);
		break;
	case BUILTINEXPECT:
		/* just a no-op for now */
		/* TODO: check that the expression and the expected value have type 'long' */
		e = assignexpr(s);
		expect(TCOMMA, "after expression");
		delexpr(assignexpr(s));
		break;
	case BUILTININFF:
		e = mkexpr(EXPRCONST, &typefloat, NULL);
		/* TODO: use INFINITY here when we can handle musl's math.h */
		e->u.constant.f = strtod("inf", NULL);
		break;
	case BUILTINNANF:
		e = assignexpr(s);
		if (!e->decayed || e->base->kind != EXPRSTRING || e->base->u.string.size > 1)
			error(&tok.loc, "__builtin_nanf currently only supports empty string literals");
		e = mkexpr(EXPRCONST, &typefloat, NULL);
		/* TODO: use NAN here when we can handle musl's math.h */
		e->u.constant.f = strtod("nan", NULL);
		break;
	case BUILTINOFFSETOF:
		t = typename(s, NULL, NULL);
		expect(TCOMMA, "after type name");
		name = expect(TIDENT, "after ','");
		if (t->kind != TYPESTRUCT && t->kind != TYPEUNION)
			error(&tok.loc, "type is not a struct/union type");
		offset = 0;
		m = typemember(t, name, &offset);
		if (!m)
			error(&tok.loc, "struct/union has no member named '%s'", name);
		designator(s, m->type, &offset);
		e = mkconstexpr(&typeulong, offset);
		break;
	case BUILTINTYPESCOMPATIBLEP:
		t = typename(s, NULL, NULL);
		expect(TCOMMA, "after type name");
		e = mkconstexpr(&typeint, typecompatible(t, typename(s, NULL, NULL)));
		break;
	case BUILTINUNREACHABLE:
		e = mkexpr(EXPRBUILTIN, &typevoid, NULL);
		e->u.builtin.kind = BUILTINUNREACHABLE;
		break;
	case BUILTINVAARG:
		e = mkexpr(EXPRBUILTIN, NULL, assignexpr(s));
		e->u.builtin.kind = BUILTINVAARG;
		if (!typesame(e->base->type, typeadjvalist))
			error(&tok.loc, "va_arg argument must have type va_list");
		if (typeadjvalist == targ->typevalist)
			e->base = mkunaryexpr(TBAND, e->base);
		expect(TCOMMA, "after va_list");
		e->type = typename(s, &e->qual, &toeval);
		e->toeval = toeval;
		break;
	case BUILTINVACOPY:
		e = mkexpr(EXPRASSIGN, &typevoid, NULL);
		e->u.assign.l = assignexpr(s);
		if (!typesame(e->u.assign.l->type, typeadjvalist))
			error(&tok.loc, "va_copy destination must have type va_list");
		if (typeadjvalist != targ->typevalist)
			e->u.assign.l = mkunaryexpr(TMUL, e->u.assign.l);
		expect(TCOMMA, "after target va_list");
		e->u.assign.r = assignexpr(s);
		if (!typesame(e->u.assign.r->type, typeadjvalist))
			error(&tok.loc, "va_copy source must have type va_list");
		if (typeadjvalist != targ->typevalist)
			e->u.assign.r = mkunaryexpr(TMUL, e->u.assign.r);
		break;
	case BUILTINVAEND:
		e = assignexpr(s);
		/* chibicc: __builtin_va_end is a no-op and performs no type
		 * checking on its argument.  mcc previously required
		 * e->type == typeadjvalist, which both rejected valid uses on
		 * targets where va_list decays differently and reported the
		 * diagnostic against the macro-body token (stdarg.h) instead of
		 * the call site.  Drop the constraint to match chibicc's no-op
		 * semantics, which also unlocks varargs.c. */
		e = mkexpr(EXPRCAST, &typevoid, e);
		break;
	case BUILTINVASTART:
		e = mkexpr(EXPRBUILTIN, &typevoid, assignexpr(s));
		e->u.builtin.kind = BUILTINVASTART;
		if (!typesame(e->base->type, typeadjvalist))
			error(&tok.loc, "va_start argument must have type va_list");
		if (typeadjvalist == targ->typevalist)
			e->base = mkunaryexpr(TBAND, e->base);
		if (consume(TCOMMA))
			delexpr(assignexpr(s));
		break;
	case BUILTINATOMICFETCHADD:
	case BUILTINATOMICFETCHSUB:
	case BUILTINATOMICFETCHAND:
	case BUILTINATOMICFETCHOR:
	case BUILTINATOMICFETCHXOR:
	case BUILTINATOMICEXCHANGE:
		/* __builtin_atomic_fetch_{add,sub}(ptr, value, memory_order).
		 * The memory-order expression is deliberately parsed and discarded:
		 * this first compiler-runtime lowering provides seq_cst semantics. */
		e = mkexpr(EXPRBUILTIN, NULL, assignexpr(s));
		if (e->base->type->kind != TYPEPOINTER ||
		    !(e->base->type->qual & QUALATOMIC))
			error(&tok.loc, "atomic fetch operation requires pointer to _Atomic object");
		e->type = e->base->type->base;
		expect(TCOMMA, "after atomic object");
		e->base->next = exprassign(assignexpr(s), e->type);
		expect(TCOMMA, "after atomic operand");
		delexpr(assignexpr(s));
		e->u.builtin.kind = kind;
		break;
	case BUILTINATOMICCOMPAREEXCHANGE:
		/* (object, expected-pointer, desired, weak, success-order,
		 * failure-order).  The runtime writes the observed value through
		 * expected on failure, as required by C11. */
		e = mkexpr(EXPRBUILTIN, &typeint, assignexpr(s));
		if (e->base->type->kind != TYPEPOINTER ||
		    !(e->base->type->qual & QUALATOMIC))
			error(&tok.loc, "atomic compare exchange requires pointer to _Atomic object");
		expect(TCOMMA, "after atomic object");
		e->base->next = assignexpr(s);
		if (e->base->next->type->kind != TYPEPOINTER)
			error(&tok.loc, "atomic compare exchange expected argument must be a pointer");
		expect(TCOMMA, "after expected value");
		e->base->next->next = exprassign(assignexpr(s), e->base->type->base);
		expect(TCOMMA, "after desired value");
		delexpr(assignexpr(s)); /* weak */
		expect(TCOMMA, "after weak argument");
		delexpr(assignexpr(s)); /* success order */
		expect(TCOMMA, "after success memory order");
		delexpr(assignexpr(s)); /* failure order */
		e->u.builtin.kind = kind;
		break;
	default:
		fatal("internal error; unknown builtin");
	}
	return e;
}
