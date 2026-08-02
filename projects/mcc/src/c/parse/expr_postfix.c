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
	/* set when a `Class::member` qualification was recognized in a
	 * `.`/`->` member access (T_PERIOD/TARROW case) */
	bool qualified = false;

	/* C++ member-call lowering: `obj.meth(...)` records the object here
	 * and the call lowering (TLPAREN) prepends it as the this argument.
	 * A pending member call is cleared when THIS postfixexpr level exits
	 * without calling it (nested argument expressions run their own
	 * postfixexpr and must not clear an outer pending call). */
	extern struct expr *g_cpp_member_this;
	extern struct type *g_cpp_member_class;
	extern const char *g_cpp_member_name;
	extern int g_cpp_postfix_depth;
	extern void cpp_pending_clear_at_depth(int);
	extern void cpp_pending_set_placeholder(void);
	extern bool cpp_pending_was_placeholder(void);
	extern void cpp_pending_record_depth(void);

	++g_cpp_postfix_depth;
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
			if (r->type->kind != TYPEPOINTER || r->type->base->kind != TYPEFUNC) {
				/* C++ functor call: `obj(args...)` lowers to
				 * `obj.operator()(args...)` — a class object with an
				 * operator() member is callable.  Route through the
				 * existing member-call lowering (which resolves the
				 * overload from the argument types). */
				extern int g_lang;
				extern bool cpp_is_member_function(struct type *,
				    const char *);
				extern bool cpp_tmpl_member(struct type *,
				    const char *);
				extern bool g_cpp_member_tmpl;
				extern struct type *g_cpp_member_class;
				extern const char *g_cpp_member_name;
				extern bool g_cpp_member_const;
				if (g_lang == 1 &&
				    (r->type->kind == TYPESTRUCT ||
				     r->type->kind == TYPEUNION) &&
				    (cpp_is_member_function(r->type, "operator_cl") ||
				     cpp_tmpl_member(r->type, "operator_cl"))) {
					g_cpp_member_this = mkunaryexpr(TBAND, r);
					g_cpp_member_this->type =
					    mkpointertype(r->type, r->qual);
					g_cpp_member_class = r->type;
					g_cpp_member_name = "operator_cl";
					g_cpp_member_const =
					    (r->qual & QUALCONST) != 0;
					/* a generic lambda's operator() is a
					 * function template: instantiate it from
					 * the call-site argument types */
					if (!cpp_is_member_function(r->type,
					    "operator_cl"))
						g_cpp_member_tmpl = true;
					cpp_pending_record_depth();
				} else {
					error(&tok.loc, "called object is not a function");
				}
			}
			{
				/* Collect the argument expressions first so a C++ member
				 * call can resolve overloads from the actual argument
				 * types before building the EXPRCALL. */
				struct expr *arglist = NULL;
				struct expr **ae = &arglist;
				struct expr *a, *an;

				while (tok.kind != TRPAREN) {
					if (arglist)
						expect(TCOMMA, "or ')' after function call argument");
					*ae = assignexpr(s);
					ae = &(*ae)->next;
				}
				next(); /* consume ')' */
				/* C++ free-function overload resolution: `name(args...)`
				 * where `name` resolves to a file-scope (free) function.
				 * The plain-name decl is always the first-declared
				 * overload; re-resolve from the argument types via the
				 * parameter-encoded mangled name `name_<codes>` and
				 * switch the callee to the matching overload (falling
				 * back to the plain-name decl when no mangled variant
				 * matches, e.g. the first overload itself or a plain C
				 * function). */
				{
					extern int g_lang;
					extern void cpp_free_mangle_name_args(const char *,
					    struct expr *, char *, size_t, bool);
					/* the callee is a decayed function identifier
					 * (`&helper`); unwrap to the underlying decl */
					struct decl *cd = NULL;
					if (r->kind == EXPRIDENT)
						cd = r->u.ident.decl;
					else if (r->kind == EXPRUNARY && r->op == TBAND &&
					    r->base && r->base->kind == EXPRIDENT)
						cd = r->base->u.ident.decl;
					if (g_lang == 1 && !g_cpp_member_class && cd &&
					    cd->kind == DECLFUNC && cd->name) {
						const char *fname = cd->name;
						char m2[256];
						struct decl *fd2;
						/* reference overloads are preferred for lvalue
						 * arguments; fall back to by-value binding */
						cpp_free_mangle_name_args(fname, arglist, m2,
						    sizeof m2, true);
						fd2 = scopegetdecl(s, m2, 1);
						if (!fd2 || fd2->kind != DECLFUNC) {
							char mv[256];
							cpp_free_mangle_name_args(fname, arglist, mv,
							    sizeof mv, false);
							fd2 = scopegetdecl(s, mv, 1);
							if (fd2 && fd2->kind == DECLFUNC)
								snprintf(m2, sizeof m2, "%s", mv);
						}
						if (fd2 && fd2->kind == DECLFUNC && fd2 != cd) {
							r = mkexpr(EXPRIDENT, fd2->type, NULL);
							r->u.ident.decl = fd2;
							r = decay(r); /* &helper_ii */
						}
					}
				}
				/* C++ member-template call: `obj.get<int>(...)` was
				 * recorded by cpp_tmpl_member_pend; instantiate the
				 * template member from the explicit template arguments
				 * and the call-site argument types, replacing the
				 * placeholder callee. */
				{
					extern bool g_cpp_member_tmpl;
					if (g_cpp_member_tmpl) {
						extern struct expr *cpp_tmpl_member_instantiate(
						    struct scope *, struct expr *,
						    struct expr *);
						r = cpp_tmpl_member_instantiate(s,
						    g_cpp_member_this, arglist);
						g_cpp_member_class = NULL;
						g_cpp_member_name = NULL;
						g_cpp_member_tmpl = false;
					} else {
				/* C++ overload resolution: re-resolve the member
				 * function from the argument types. */
				{
					extern struct type *g_cpp_member_class;
					extern const char *g_cpp_member_name;
					extern bool g_cpp_member_const;
					if (g_cpp_member_class && g_cpp_member_name) {
						extern void cpp_mangled_name_args(struct type *,
						    const char *, struct expr *, char *, size_t,
						    bool);
						char mname2[256];
						struct decl *fd2;
						/* reference overloads are preferred for lvalue
						 * arguments; fall back to by-value binding */
						cpp_mangled_name_args(g_cpp_member_class,
						    g_cpp_member_name, arglist, mname2,
						    sizeof mname2, true);
						if (g_cpp_member_const)
							strncat(mname2, "K", sizeof mname2 - strlen(mname2) - 1);
						fd2 = scopegetdecl(g_cpp_member_class->scope
						    ? g_cpp_member_class->scope : s, mname2, 1);
						if (!fd2 || fd2->kind != DECLFUNC) {
							char mval2[256];
							cpp_mangled_name_args(g_cpp_member_class,
							    g_cpp_member_name, arglist, mval2,
							    sizeof mval2, false);
							if (g_cpp_member_const)
								strncat(mval2, "K", sizeof mval2 - strlen(mval2) - 1);
							fd2 = scopegetdecl(g_cpp_member_class->scope
							    ? g_cpp_member_class->scope : s, mval2, 1);
							if (fd2 && fd2->kind == DECLFUNC)
								snprintf(mname2, sizeof mname2, "%s", mval2);
						}
						/* a non-const object may call a const method */
						if (!g_cpp_member_const && (!fd2 || fd2->kind != DECLFUNC)) {
							char mk2[256];
							snprintf(mk2, sizeof mk2, "%sK", mname2);
							fd2 = scopegetdecl(g_cpp_member_class->scope
							    ? g_cpp_member_class->scope : s, mk2, 1);
							if (fd2 && fd2->kind == DECLFUNC)
								strncat(mname2, "K", sizeof mname2 - strlen(mname2) - 1);
						}
						if (!fd2 || fd2->kind != DECLFUNC)
							error(&tok.loc,
							    "no matching member function for '%s'",
							    g_cpp_member_name);
						t = fd2->type;
						r = mkexpr(EXPRIDENT, fd2->type, NULL);
						r->u.ident.decl = fd2;
						r = decay(r); /* &Class_method_i */
					}
					g_cpp_member_class = NULL;
					g_cpp_member_name = NULL;
				}
				}
				}
				/* C++ function template call: instantiate the template
				 * from the argument types (pending callee was recorded by
				 * cpp_tmpl_placeholder). */
				{
					extern struct expr *cpp_tmpl_instantiate(struct scope *,
					    struct expr *);
					struct expr *nf = cpp_tmpl_instantiate(s, arglist);
					if (nf)
						r = nf;
				}
				t = r->type->base;
				e = mkexpr(EXPRCALL, t->base, r);
				e->u.call.args = NULL;
				e->u.call.nargs = 0;
				p = t->u.func.params;
				end = &e->u.call.args;
				/* C++ member call: prepend the this object. */
				if (g_cpp_member_this) {
					*end = g_cpp_member_this;
					end = &(*end)->next;
					++e->u.call.nargs;
					g_cpp_member_this = NULL;
					if (p)
						p = p->next;
				}
				for (a = arglist; a; a = an) {
					struct expr *arg;
					an = a->next; /* exprassign may replace the node */
					if (!p && !t->u.func.isvararg)
						error(&tok.loc,
						    "too many arguments for function call");
					if (t->u.func.isvararg && !p) {
						arg = exprpromote(a);
					} else if (p->type->isref) {
						/* C++ reference param: bind the address */
						arg = exprassign(mkunaryexpr(TBAND, a), p->type);
					} else {
						arg = exprassign(a, p->type);
					}
					*end = arg;
					end = &(*end)->next;
					++e->u.call.nargs;
					if (p)
						p = p->next;
				}
				if (p && !t->u.func.isvararg)
					error(&tok.loc,
					    "not enough arguments for function call");
			}
			e = decay(e);
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
			qualified = false;
			/* C++ template member call: `obj.get<int>(...)` or
			 * `obj.get(...)`.  The `<` would otherwise be parsed as a
			 * comparison operator, so detect it here (peeking past the
			 * member name); the pending call is instantiated by the
			 * TLPAREN lowering from the explicit template arguments and
			 * the call-site argument types. */
			{
				extern bool cpp_tmpl_member(struct type *, const char *);
				extern void tokpush(struct token *, size_t);
				extern int g_lang;
				if (g_lang == 1 && cpp_tmpl_member(t, tokenstr(tok.kind))) {
					struct token old = tok;
					bool call;
					next();
					call = tok.kind == TLESS || tok.kind == TLPAREN;
					if (call) {
						extern void cpp_tmpl_member_pend(struct type *,
						    const char *);
						extern struct expr *cpp_tmpl_member_placeholder(void);
						extern struct expr *g_cpp_member_this;
						extern bool g_cpp_member_const;
						extern bool g_cpp_member_tmpl;
						struct token nxt = tok;
						const char *mname = tokenstr(old.kind);
						tok = old;
						tokpush(&nxt, 1);
						cpp_tmpl_member_pend(t, mname);
						g_cpp_member_this = r;
						g_cpp_member_class = t;
						g_cpp_member_name = mname;
						g_cpp_member_const = (tq & QUALCONST) != 0;
						g_cpp_member_tmpl = true;
						cpp_pending_record_depth();
						cpp_pending_set_placeholder();
						r = cpp_tmpl_member_placeholder();
						e = r; /* keep the post-switch `r = e` stable */
						break; /* tok is '('; TLPAREN instantiates */
					}
					{
						struct token nxt = tok;
						tok = old;
						tokpush(&nxt, 1);
					}
				}
			}
			/* C++ class-qualified member access: `obj.Base::get()` /
			 * `obj.Base::val`.  `Base` is a base class (or the object's
			 * own class) of the object type; the `::` selects the member
			 * in Base's scope and re-points `this` at the Base
			 * subobject, so a qualified call reaches the base
			 * implementation even when the derived class overrides it. */
			{
				extern int g_lang;
				extern bool cpp_is_derived(struct type *, struct type *);
				extern unsigned long long cpp_base_offset(struct type *,
				    struct type *);
				if (g_lang == 1) {
					struct type *qt = scopegettag(s,
					    tokenstr(tok.kind), 1);
					if (qt && (qt->kind == TYPESTRUCT ||
					    qt->kind == TYPEUNION) &&
					    cpp_is_derived(t, qt)) {
						struct token old = tok;
						next();
						if (tok.kind == TCOLONCOLON) {
							if (qt != t) {
								unsigned long long boff =
								    cpp_base_offset(t, qt);
								if (boff != (unsigned long long)-1) {
									r = mkbinaryexpr(&tok.loc, TADD,
									    exprconvert(r, &typeulong),
									    mkconstexpr(&typeulong, boff));
									r->type = mkpointertype(qt, tq);
								}
							}
							t = qt;
							qualified = true;
							next(); /* consume '::' */
							if (tok.kind < TIDENT)
								error(&tok.loc,
								    "expected member name after '::'");
							goto member_lookup;
						}
						{
							struct token nxt = tok;
							tok = old;
							tokpush(&nxt, 1);
						}
					}
				}
			}
		member_lookup:
			/* C++ multiple inheritance: a name defined by more than one
			 * base subobject is ambiguous (and the base-class subobjects
			 * are anonymous members here). */
			{
				extern int g_lang;
				extern bool cpp_member_ambiguous(struct type *,
				    const char *);
				if (g_lang == 1 &&
				    cpp_member_ambiguous(t, tokenstr(tok.kind)))
					error(&tok.loc, "request for member '%s' is "
					    "ambiguous (multiple base classes define it)",
					    tokenstr(tok.kind));
			}
			/* class-qualified lookup honors the qualified class's own
			 * members over inherited ones (`obj.Base::get()` reaches
			 * Base::get even when Mid/Der override it) */
			if (qualified) {
				extern struct member *cpp_qualified_member(struct type *,
				    const char *, unsigned long long *);
				m = cpp_qualified_member(t, tokenstr(tok.kind), &offset);
			} else {
				m = typemember(t, tokenstr(tok.kind), &offset);
			}
			if (!m)
				error(&tok.loc, "struct/union has no member named '%s'", tok.lit);
			/* C++ access control: private/protected members are only
			 * reachable from within the member's own class. */
			{
				extern int g_lang;
				extern bool cpp_member_accessible(struct type *,
				    struct member *);
				if (g_lang == 1 && !cpp_member_accessible(t, m))
					error(&tok.loc, "'%s' is not accessible from this context (member is private/protected)", tokenstr(tok.kind));
			}
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
				extern struct type *g_cpp_member_class;
				extern const char *g_cpp_member_name;
				extern void cpp_pending_record_depth(void);
				char mname[256];
				struct decl *fd;
				if (cpp_is_member_function(t, m->name)) {
					extern bool g_cpp_member_const;
					bool obj_const = (tq & QUALCONST) != 0;
					struct expr *thisp;
					/* C++ virtual call (C.2.5): indirect through the
					 * object's vtable — `(*(fn **)((*(void **)obj) +
					 * slot*8))(obj, args...)`.  The this object points at
					 * the defining class's subobject.
					 * A class-qualified call (`obj.Base::f()`) must NOT
					 * go through the vtable: it statically binds to the
					 * qualified class's implementation (defect H). */
					if (m->is_virtual && !qualified) {
						extern struct expr *cpp_make_vcall(struct expr *,
						    struct type *, struct member *, int);
						extern struct type *cpp_method_owner(struct type *,
						    const char *);
						struct type *owner;
						owner = cpp_method_owner(t, m->name);
						thisp = r;
						if (offset) {
							thisp = mkbinaryexpr(&tok.loc, TADD,
							    exprconvert(r, &typeulong),
							    mkconstexpr(&typeulong, offset));
							thisp->type = mkpointertype(owner, tq);
						}
						e = cpp_make_vcall(thisp, owner, m, m->vslot);
						g_cpp_member_this = thisp; /* &subobject */
						g_cpp_member_class = NULL; /* slot already resolved */
						g_cpp_member_name = NULL;
						g_cpp_member_const = obj_const;
						cpp_pending_record_depth();
						next();
						break;
					}
					cpp_mangled_name(t, m->name, mname, sizeof mname);
					if (obj_const)
						strncat(mname, "K", sizeof mname - strlen(mname) - 1);
					/* member symbols live in the class's declaration
					 * scope (namespace scope for `namespace n { class C }`) */
					fd = scopegetdecl(t->scope ? t->scope : s, mname, 1);
					/* a non-const object may call a const method */
					if (!obj_const && (!fd || fd->kind != DECLFUNC)) {
						char mk[256];
						snprintf(mk, sizeof mk, "%sK", mname);
						fd = scopegetdecl(t->scope ? t->scope : s, mk, 1);
						if (fd && fd->kind == DECLFUNC)
							strncat(mname, "K", sizeof mname - strlen(mname) - 1);
					}
					if (fd && fd->kind == DECLFUNC) {
						e = mkexpr(EXPRIDENT, fd->type, NULL);
						e->u.ident.decl = fd;
						e = decay(e); /* &Class_method */
					} else {
						/* overloaded member: symbols are argument-encoded
						 * (`Class_method_i`); the call lowering resolves
						 * them from the argument types, so use the member's
						 * own function type as a placeholder */
						e = mkexpr(EXPRIDENT, m->type, NULL);
						e->u.ident.decl = NULL;
						e = decay(e);
					}
					/* inherited method: this must point at the defining
					 * base's subobject (offset reported by typemember) */
					thisp = r;
					if (offset) {
						thisp = mkbinaryexpr(&tok.loc, TADD,
						    exprconvert(r, &typeulong),
						    mkconstexpr(&typeulong, offset));
						thisp->type = mkpointertype(t, tq);
					}
					g_cpp_member_this = thisp; /* &subobject */
					g_cpp_member_class = t;
					g_cpp_member_name = m->name;
					g_cpp_member_const = obj_const;
					cpp_pending_record_depth();
					if (fd == NULL || fd->kind != DECLFUNC)
						cpp_pending_set_placeholder();
					next();
					break;
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
			/* this postfixexpr level is done: a pending member call that
			 * was not followed by '(' is either a member-address use
			 * (`&obj.meth`) or dropped (no leak into a later unrelated
			 * call) */
			{
				extern bool cpp_pending_is_mine(int);
				extern struct type *g_cpp_member_class;
				extern const char *g_cpp_member_name;
				extern struct decl *cpp_find_unique_member(struct type *,
				    const char *, char *, size_t);
				if (g_cpp_member_this && cpp_pending_is_mine(g_cpp_postfix_depth)
				    && cpp_pending_was_placeholder()) {
					struct type *mt = g_cpp_member_class;
					const char *mn = g_cpp_member_name;
					struct decl *fd;
					char mm[256];
					if (mt && mn &&
					    (fd = cpp_find_unique_member(mt, mn, mm,
					        sizeof mm)) != NULL) {
						/* single overload: `&obj.meth` is the address of
						 * the one mangled member function */
						r = mkexpr(EXPRIDENT, fd->type, NULL);
						r->u.ident.decl = fd;
						r = decay(r);
						cpp_pending_clear_at_depth(g_cpp_postfix_depth);
						--g_cpp_postfix_depth;
						return r;
					}
					error(&tok.loc,
					    "address of overloaded member function is ambiguous (use an explicit cast)");
				}
			}
			cpp_pending_clear_at_depth(g_cpp_postfix_depth);
			--g_cpp_postfix_depth;
			return r;
		}
		r = e;
	}
}
