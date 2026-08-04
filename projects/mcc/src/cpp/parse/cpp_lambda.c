/* cpp_lambda.c — m++ (C++) lambda-expression lowering.
 *
 * C++11/C++14/17/20 lambdas: gather captures, synthesize a closure
 * class ``__lambdaN`` (replayed through cpp_class_decl), emit the
 * ``operator()`` member (a function template for generic lambdas), and
 * lower the lambda object to a closure decl.  Entry point cpp_lambda_expr
 * is called from the postfix-expression lowering; cpp_is_lambda_closure
 * is exported for the C front-end (decl.c / eval.c).
 *
 * Extracted from cpp_parse.c (split into per-domain submodules).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "mcc.h"
#include "cpp.h"
#include "cpp_internal.h"
#include "../../c/parse/decl_internal.h"
#include "../../c/parse/expr_internal.h"

/* One captured variable of a lambda (`x` in `[x]`). */

struct cpp_lambda_cap {
	const char *name;      /* capture name (also the closure member name) */
	struct type *t;        /* captured variable's type */
	struct decl *d;        /* the enclosing-scope variable decl (NULL when
	                          the capture resolves to an outer closure's
	                          member — defect T) */
	struct expr *arg;      /* lvalue of the captured entity: the local
	                          variable's identifier, or `(*this).m` for an
	                          outer closure member */
	bool by_ref;           /* `[&x]` reference capture */
};

/* Token-stream builder for the synthesized closure-class definition. */
void
cpp_tb(struct token *buf, size_t *n, struct token tmpl, enum tokenkind k,
       const char *name)
{
	struct token *t = &buf[*n];
	*t = tmpl;
	if (name)
		t->kind = tokenget(name, strlen(name));
	else
		t->kind = k;
	++*n;
}

/* Should a captured value be copy-constructed through its class's
 * constructor rather than bit-copied?  A class-typed capture (`[c]` where
 * `c` is a class object with a user constructor) must run the copy ctor —
 * defect S — otherwise construction side effects (refcounts, deep copies,
 * logging) are silently lost.  Such captures are initialized via a ctor
 * initializer-list item `c(__cN)` so the member-init path selects the
 * copy/move ctor by overload resolution; scalars and classes without a
 * user ctor keep the plain bitwise body assignment. */
static bool
cpp_lambda_cap_needs_ctor_init(const struct cpp_lambda_cap *cap)
{
	struct type *t = cap->t;
	if (!t || (t->kind != TYPESTRUCT && t->kind != TYPEUNION))
		return false;
	return cpp_has_ctor(t, t->u.structunion.tag);
}

/* Is `t` the closure class of a lambda (`__lambdaN`)?  A no-capture
 * closure is an empty, constant-constructible object, so its closure
 * object may serve as a static/constant initializer — file-scope
 * `auto f = [](...){...};` and `constexpr auto f = [](...){...};`. */
bool
cpp_is_lambda_closure(const struct type *t)
{
	const char *tag;

	if (!t || (t->kind != TYPESTRUCT && t->kind != TYPEUNION))
		return false;
	tag = t->u.structunion.tag;
	return tag && strncmp(tag, "__lambda", 8) == 0;
}

/* Gather the default captures of a lambda: every automatic object
 * visible in the enclosing scopes (from `s` up to, but excluding, the
 * file scope) that is not already in `caps[0..*ncap)`, appended either
 * by reference (`[&]`) or by value (`[=]`).  A reference variable
 * `T &r` is captured by reference as `T &` (of the referent) or by
 * value as `T` (the referent's value). */
static void
cpp_lambda_default_captures(struct scope *s, struct cpp_lambda_cap *caps,
    int *ncap, int capmax, bool by_ref)
{
	extern struct scope filescope;
	struct scope *sc;

	for (sc = s; sc && sc != &filescope; sc = sc->parent) {
		struct map *m = &sc->decls;
		size_t i;
		if (!m->len)
			continue;
		for (i = 0; i < m->cap; ++i) {
			struct decl *d;
			struct expr *ie;
			bool seen = false;
			int j;

			if (!m->keys[i].str)
				continue;
			d = m->vals[i].p;
			if (!d || d->kind != DECLOBJECT)
				continue;
			if (d->linkage != LINKNONE || d->u.obj.storage != SDAUTO)
				continue;
			/* the implicit this parameter is not capturable */
			if (d->name && strcmp(d->name, "this") == 0)
				continue;
			/* a default *by-value* capture skips lambda closure objects: the
			 * synthesized closure class has no copy ctor, so `[=]` cannot
			 * copy an enclosing lambda.  (`[&]` may still capture its
			 * reference.) */
			if (!by_ref && d->type && cpp_is_lambda_closure(d->type))
				continue;
			for (j = 0; j < *ncap; ++j)
				if (strcmp(caps[j].name, d->name) == 0) {
					seen = true;
					break;
				}
			if (seen)
				continue;
			if (*ncap >= capmax)
				error_code(E_SYNTAX, &tok.loc,
				    "too many captures in lambda");
			caps[*ncap].name = d->name;
			caps[*ncap].d = d;
			caps[*ncap].by_ref = by_ref;
			ie = mkexpr(EXPRIDENT, d->type, NULL);
			ie->qual = d->qual;
			ie->lvalue = true;
			ie->u.ident.decl = d;
			if (by_ref && d->type && d->type->isref) {
				/* `[&]` on a reference variable captures the referent:
				 * member `T &`, ctor arg `&*r` */
				caps[*ncap].t = d->type;
				caps[*ncap].arg = mkunaryexpr(TMUL, ie);
				caps[*ncap].arg->lvalue = true;
			} else if (by_ref) {
				caps[*ncap].t = mkpointertype(d->type, QUALNONE);
				caps[*ncap].t->isref = true; /* T & */
				caps[*ncap].arg = ie;
			} else if (d->type && d->type->isref) {
				/* `[=]` on a reference variable captures the referent's
				 * value */
				caps[*ncap].t = d->type->base;
				caps[*ncap].arg = mkunaryexpr(TMUL, ie);
				caps[*ncap].arg->lvalue = true;
			} else {
				caps[*ncap].t = d->type;
				caps[*ncap].arg = ie;
			}
			++*ncap;
		}
	}
}

/* Parse a C++11 lambda expression `[captures](params) -> ret { body }` and
 * lower it to an anonymous closure class (`__lambdaN`) whose `operator()`
 * is the lambda body and whose members are the by-value captures; returns
 * a freshly constructed closure object (an anonymous temporary).
 *
 * The closure class is defined by replaying a synthesized
 * `class __lambdaN { ... }` through cpp_class_decl, reusing the existing
 * member/constructor/operator machinery.  By-reference captures
 * (`[&x]`, `[&]`), default by-value captures (`[=]`), init-captures
 * (`[n = expr]`) and mixed captures (`[=, &y]`, `[&, y]`) are
 * supported; generic (auto) parameters use the same mechanism. */
struct expr *
cpp_lambda_expr(struct scope *s)
{
	extern struct func *curfunc;
	extern struct decl *mkdecl(char *, enum declkind, struct type *,
	    enum typequal, enum linkage);
	extern void funcinit(struct func *, struct decl *, struct init *,
	    bool);
	extern void tokpush(struct token *, size_t);

	struct cpp_lambda_cap caps[32];
	int ncap = 0;
	struct token *ptoks = NULL, *rtoks = NULL, *btoks = NULL;
	size_t pn = 0, pcap = 0, rn = 0, rcap = 0, bn = 0, bcap = 0;
	/* C++20 lambda template parameter list: `[]<typename T>(T x) { ... }`.
	 * Buffered like the parameter list, with `<`/`>` nesting for templates
	 * that themselves contain template parameters (e.g. `template<typename T,
	 * template<typename> class Container>`). */
	struct token *ttoks = NULL;
	size_t tn2 = 0, tcap = 0;
	struct token *wtoks;
	size_t wn = 0, wcap;
	struct token tmpl = tok;
	char tagname[64], tn[32], cn[32];
	struct token cur;
	struct decl *td;
	struct type *ct;
	struct expr *e, *args, **ae;
	struct decl *tmp;
	int i;

	/* --- capture list `[ x, &y, &, =, n = expr ]` ---
	 * C++11/14 captures:
	 *   `[x]`        by-value capture of local x
	 *   `[&x]`       by-reference capture of local x
	 *   `[&]`        default by-reference capture (all enclosing locals)
	 *   `[=]`        default by-value capture (all enclosing locals)
	 *   `[n = expr]` init-capture: new closure member n from expr
	 *   `[&n = expr]` reference init-capture
	 *   `[=, &y]` / `[&, y]`  default plus per-variable override
	 *
	 * A by-reference capture lowers as: the closure member is declared
	 * `T &` (cpp_member_ident auto-dereferences it inside the operator()
	 * body), the synthesized ctor takes a plain `T *` parameter (so it
	 * does not auto-dereference), the ctor binds it through the
	 * initializer-list item `name(__cN)` (`*(this + off) = __cN`), and
	 * the closure object is constructed with `&x`. */
	next(); /* consume '[' */
	{
		bool def_ref = false, def_val = false, lead_ref = false;

		/* leading default-capture specifier: `[&]` / `[=]`, optionally
		 * followed by explicit captures (`[&, y]`, `[=, &y]`).  When the
		 * specifier is directly followed by a capture name (`[&x]`) it is
		 * a by-reference capture of that variable instead. */
		if (tok.kind == TBAND || tok.kind == TASSIGN) {
			bool br = tok.kind == TBAND;
			next();
			if (tok.kind == TRBRACK || tok.kind == TCOMMA) {
				def_ref = br;
				def_val = !br;
				if (tok.kind == TCOMMA)
					next();
			} else {
				if (!br)
					error_code(E_SYNTAX, &tok.loc,
					    "expected ']' or ',' after '=' in lambda capture list");
				lead_ref = true; /* `[&name`: by-reference capture */
			}
		}
		while (tok.kind != TRBRACK) {
			bool by_ref = lead_ref;
			lead_ref = false;
			if (tok.kind == TBAND) {
				by_ref = true;
				next();
				if (tok.kind == TRBRACK || tok.kind == TCOMMA)
					error_code(E_SYNTAX, &tok.loc,
					    "expected capture name after '&' in lambda capture list");
			}
			if (tok.kind < TIDENT)
				error_code(E_SYNTAX, &tok.loc,
				    "expected capture name in lambda capture list");
			if (ncap >= (int)countof(caps))
				error_code(E_SYNTAX, &tok.loc, "too many captures in lambda");
			caps[ncap].name = tokenstr(tok.kind);
			caps[ncap].by_ref = by_ref;
			next();
			if (tok.kind == TASSIGN) {
				/* init-capture `[n = expr]` / `[&n = expr]`: the name is not
				 * an enclosing variable — it introduces a new closure member
				 * initialized from the expression. */
				next(); /* consume '=' */
				caps[ncap].d = NULL;
				caps[ncap].arg = assignexpr(s);
				if (by_ref) {
					caps[ncap].t = mkpointertype(caps[ncap].arg->type,
					    QUALNONE);
					caps[ncap].t->isref = true; /* T & */
				} else {
					caps[ncap].t = caps[ncap].arg->type;
				}
				++ncap;
			} else {
				caps[ncap].d = scopegetdecl(s, caps[ncap].name, 1);
				if (caps[ncap].d && caps[ncap].d->kind == DECLOBJECT) {
					/* ordinary capture of an enclosing-scope local */
					struct decl *d = caps[ncap].d;
					if (by_ref && d->type && d->type->isref) {
						/* `[&r]` where `r` is `T &`: capture the referent —
						 * member `T &`, ctor arg `&*r` */
						struct expr *ie = mkexpr(EXPRIDENT, d->type, NULL);
						ie->qual = d->qual;
						ie->lvalue = true;
						ie->u.ident.decl = d;
						caps[ncap].t = d->type;
						caps[ncap].arg = mkunaryexpr(TMUL, ie);
						caps[ncap].arg->lvalue = true;
					} else if (by_ref) {
						caps[ncap].t = mkpointertype(d->type, QUALNONE);
						caps[ncap].t->isref = true; /* T & */
						caps[ncap].arg = mkexpr(EXPRIDENT, d->type, NULL);
						caps[ncap].arg->qual = d->qual;
						caps[ncap].arg->lvalue = true;
						caps[ncap].arg->u.ident.decl = d;
					} else if (d->type && d->type->isref) {
						/* `[r]` on a reference variable captures the
						 * referent's value */
						struct expr *ie = mkexpr(EXPRIDENT, d->type, NULL);
						ie->qual = d->qual;
						ie->lvalue = true;
						ie->u.ident.decl = d;
						caps[ncap].t = d->type->base;
						caps[ncap].arg = mkunaryexpr(TMUL, ie);
						caps[ncap].arg->lvalue = true;
					} else {
						caps[ncap].t = d->type;
						caps[ncap].arg = mkexpr(EXPRIDENT, d->type, NULL);
						caps[ncap].arg->qual = d->qual;
						caps[ncap].arg->lvalue = true;
						caps[ncap].arg->u.ident.decl = d;
					}
				} else {
					/* defect T: an outer lambda's captured variable lowers to
					 * a member of its closure class, not a local; resolve it
					 * the same way a bare member name in the operator() body
					 * is resolved — as `(*this).name` of the current method's
					 * class.  This lets an inner lambda re-capture anything the
					 * outer one captured. */
					struct expr *me = cpp_member_ident(s, caps[ncap].name);
					if (!me || me->type->kind == TYPEFUNC) {
						/* a member function (or non-member) is not capturable;
						 * clear any pending member-call state cpp_member_ident
						 * may have recorded */
						g_cpp_member_this = NULL;
						g_cpp_member_class = NULL;
						g_cpp_member_name = NULL;
						g_cpp_member_const = false;
						error_code(E_DECL, &tok.loc,
						    "cannot capture variable '%s'", caps[ncap].name);
					}
					caps[ncap].d = NULL;
					if (by_ref) {
						caps[ncap].t = mkpointertype(me->type, QUALNONE);
						caps[ncap].t->isref = true; /* T & */
					} else {
						caps[ncap].t = me->type;
					}
					caps[ncap].arg = me;
				}
				++ncap;
			}
			if (tok.kind == TRBRACK)
				break;
			expect(TCOMMA, "',' or ']' in lambda capture list");
		}
		next(); /* consume ']' */

		/* default captures `[&]` / `[=]`: gather every automatic object of
		 * the enclosing scopes that was not explicitly captured above. */
		if (def_ref || def_val)
			cpp_lambda_default_captures(s, caps, &ncap,
			    (int)countof(caps), def_ref);
	}

	/* --- C++20 lambda template parameter list `[captures]<typename T>(T x)`
	 * (optional; buffer through the matching '>') --- */
	if (tok.kind == TLESS) {
		int tdepth = 0;
		for (;;) {
			if (tn2 >= tcap) {
				tcap = tcap ? tcap * 2 : 16;
				ttoks = xreallocarray(ttoks, tcap, sizeof *ttoks);
			}
			ttoks[tn2++] = tok;
			if (tok.kind == TLESS)
				++tdepth;
			else if (tok.kind == TGREATER) {
				--tdepth;
				if (tdepth == 0) {
					next();
					break;
				}
			}
			next();
		}
	}

	/* --- parameter list `( params )` (optional in C++; buffer through
	 * the matching ')') --- */
	if (tok.kind == TLPAREN) {
		int pdepth = 0;
		for (;;) {
			if (pn >= pcap) {
				pcap = pcap ? pcap * 2 : 16;
				ptoks = xreallocarray(ptoks, pcap, sizeof *ptoks);
			}
			ptoks[pn++] = tok;
			if (tok.kind == TLPAREN)
				++pdepth;
			else if (tok.kind == TRPAREN) {
				--pdepth;
				if (pdepth == 0) {
					next();
					break;
				}
			}
			next();
		}
	} /* else: `[] { ... }` / `[] -> ret { ... }` — empty parameter list */

	/* --- optional `-> ret` return type (buffer up to the body) --- */
	if (tok.kind == TARROW) {
		next(); /* consume '->' */
		while (tok.kind != TLBRACE && tok.kind != TEOF) {
			if (rn >= rcap) {
				rcap = rcap ? rcap * 2 : 16;
				rtoks = xreallocarray(rtoks, rcap, sizeof *rtoks);
			}
			rtoks[rn++] = tok;
			next();
		}
	}

	/* --- function body `{ ... }` --- */
	{
		int bd = 0;
		if (tok.kind != TLBRACE)
			error_code(E_SYNTAX, &tok.loc, "expected lambda body");
		for (;;) {
			if (bn >= bcap) {
				bcap = bcap ? bcap * 2 : 32;
				btoks = xreallocarray(btoks, bcap, sizeof *btoks);
			}
			btoks[bn++] = tok;
			if (tok.kind == TLBRACE)
				++bd;
			else if (tok.kind == TRBRACE) {
				--bd;
				if (bd == 0) {
					next();
					break;
				}
			}
			next();
		}
	}

	/* --- synthesize the closure class `class __lambdaN { ... };` ---
	 * Defined at file scope (like a real C++ closure type) so the
	 * operator() body's name lookup does not see the enclosing function's
	 * locals (which would shadow the captured members). */
	{
		extern struct scope filescope;
		snprintf(tagname, sizeof tagname, "__lambda%d", g_cpp_lambda_count++);
		for (i = 0; i < ncap; ++i) {
			/* per-capture DECLTYPE `__lti` bound to the captured type;
			 * the closure member declaration `__lti cap_i` is typed.  A
			 * by-reference capture additionally gets `__ltpi` bound to
			 * `T *` for the synthesized ctor's parameter. */
			snprintf(tn, sizeof tn, "__lt%d", i);
			td = mkdecl(xmalloc(strlen(tn) + 1), DECLTYPE, caps[i].t,
			    QUALNONE, LINKNONE);
			strcpy((char *)td->name, tn);
			scopeputdecl(&filescope, td);
			if (caps[i].by_ref) {
				snprintf(tn, sizeof tn, "__ltp%d", i);
				td = mkdecl(xmalloc(strlen(tn) + 1), DECLTYPE,
				    mkpointertype(caps[i].t->base, QUALNONE),
				    QUALNONE, LINKNONE);
				strcpy((char *)td->name, tn);
				scopeputdecl(&filescope, td);
			}
		}
	}
	wcap = 64 + (size_t)ncap * 32 + pn + rn + bn + 32;
	wtoks = xmalloc(wcap * sizeof *wtoks);

	cpp_tb(wtoks, &wn, tmpl, 0, "class");
	cpp_tb(wtoks, &wn, tmpl, 0, tagname);
	cpp_tb(wtoks, &wn, tmpl, TLBRACE, NULL);
	cpp_tb(wtoks, &wn, tmpl, 0, "public");
	cpp_tb(wtoks, &wn, tmpl, TCOLON, NULL);
	for (i = 0; i < ncap; ++i) {
		snprintf(tn, sizeof tn, "__lt%d", i);
		cpp_tb(wtoks, &wn, tmpl, 0, tn);
		cpp_tb(wtoks, &wn, tmpl, 0, caps[i].name);
		cpp_tb(wtoks, &wn, tmpl, TSEMICOLON, NULL);
	}
	/* synthesized constructor
	 * `__lambdaN(__lt0 __c0, ...) : classcap(__c0) { scalarcap = __c1; }`
	 * Class-typed captures with a user constructor go through the ctor
	 * initializer list so the member-init path runs the copy/move ctor
	 * chosen by overload resolution (defect S); scalars and POD captures
	 * keep the plain assignment (a bit copy is their correct semantics). */
	cpp_tb(wtoks, &wn, tmpl, 0, tagname);
	cpp_tb(wtoks, &wn, tmpl, TLPAREN, NULL);
	for (i = 0; i < ncap; ++i) {
		if (i)
			cpp_tb(wtoks, &wn, tmpl, TCOMMA, NULL);
		/* by-reference captures take a plain `T *` parameter so it does
		 * not auto-dereference when bound by the ctor init list */
		snprintf(tn, sizeof tn, caps[i].by_ref ? "__ltp%d" : "__lt%d", i);
		cpp_tb(wtoks, &wn, tmpl, 0, tn);
		snprintf(cn, sizeof cn, "__c%d", i);
		cpp_tb(wtoks, &wn, tmpl, 0, cn);
	}
	cpp_tb(wtoks, &wn, tmpl, TRPAREN, NULL);
	{
		bool first = true;
		for (i = 0; i < ncap; ++i) {
			/* by-reference captures must bind in the init list too: a
			 * reference member can only be initialized, never assigned */
			if (!cpp_lambda_cap_needs_ctor_init(&caps[i]) &&
			    !caps[i].by_ref)
				continue;
			cpp_tb(wtoks, &wn, tmpl, first ? TCOLON : TCOMMA, NULL);
			first = false;
			cpp_tb(wtoks, &wn, tmpl, 0, caps[i].name);
			cpp_tb(wtoks, &wn, tmpl, TLPAREN, NULL);
			snprintf(cn, sizeof cn, "__c%d", i);
			cpp_tb(wtoks, &wn, tmpl, 0, cn);
			cpp_tb(wtoks, &wn, tmpl, TRPAREN, NULL);
		}
	}
	cpp_tb(wtoks, &wn, tmpl, TLBRACE, NULL);
	for (i = 0; i < ncap; ++i) {
		if (cpp_lambda_cap_needs_ctor_init(&caps[i]) || caps[i].by_ref)
			continue; /* already initialized by the init list */
		cpp_tb(wtoks, &wn, tmpl, 0, caps[i].name);
		cpp_tb(wtoks, &wn, tmpl, TASSIGN, NULL);
		snprintf(cn, sizeof cn, "__c%d", i);
		cpp_tb(wtoks, &wn, tmpl, 0, cn);
		cpp_tb(wtoks, &wn, tmpl, TSEMICOLON, NULL);
	}
	cpp_tb(wtoks, &wn, tmpl, TRBRACE, NULL);
	/* `operator()(params) { body }` — `ret` is the explicit `->` type if
	 * given, otherwise `auto` (deduced from the body's return).  A C++14
	 * generic lambda (`[](auto x) {...}`) has an `auto` parameter: the
	 * operator() becomes a function template (`template<typename __T0>
	 * operator()(__T0 x)`) so each call-site argument type instantiates
	 * its own version.  A C++20 explicit template parameter list
	 * (`[]<typename T>(T x)`) is emitted before the auto-parameter
	 * template (if any). */
	{
		bool generic = false;
		int i;
		for (i = 0; i < (int)pn; ++i)
			if (ptoks[i].kind == TAUTO)
				generic = true;
		/* C++20 explicit template parameter list, e.g. `<typename T>`. */
		if (tn2) {
			cpp_tb(wtoks, &wn, tmpl, 0, "template");
			/* emit the buffered tokens (starting with '<', ending with '>') */
			memcpy(wtoks + wn, ttoks, tn2 * sizeof *ttoks);
			wn += tn2;
		}
		if (generic) {
			cpp_tb(wtoks, &wn, tmpl, 0, "template");
			cpp_tb(wtoks, &wn, tmpl, TLESS, NULL);
			cpp_tb(wtoks, &wn, tmpl, 0, "typename");
			cpp_tb(wtoks, &wn, tmpl, 0, "__T0");
			cpp_tb(wtoks, &wn, tmpl, TGREATER, NULL);
		}
	}
	if (rn) {
		memcpy(wtoks + wn, rtoks, rn * sizeof *rtoks);
		wn += rn;
	} else {
		cpp_tb(wtoks, &wn, tmpl, 0, "auto");
	}
	cpp_tb(wtoks, &wn, tmpl, 0, "operator");
	cpp_tb(wtoks, &wn, tmpl, TLPAREN, NULL);
	cpp_tb(wtoks, &wn, tmpl, TRPAREN, NULL);
	if (pn) {
		/* copy the parameter tokens, replacing `auto` with the template
		 * type parameter `__T0` (generic lambda) */
		size_t i;
		for (i = 0; i < pn; ++i) {
			if (ptoks[i].kind == TAUTO)
				cpp_tb(wtoks, &wn, tmpl, 0, "__T0");
			else
				wtoks[wn++] = ptoks[i];
		}
	}
	/* the lambda's operator() is const by default (C++11 [expr.prim.lambda]p5,
	 * unless `mutable`): a const closure object — e.g. a `constexpr` or
	 * `const` lambda variable — must be callable through the const
	 * overload (`operator_clK...`), and by-value captures remain readable
	 * through a const `this`. */
	cpp_tb(wtoks, &wn, tmpl, 0, "const");
	if (bn) {
		memcpy(wtoks + wn, btoks, bn * sizeof *btoks);
		wn += bn;
	}
	cpp_tb(wtoks, &wn, tmpl, TRBRACE, NULL); /* close class body */
	cpp_tb(wtoks, &wn, tmpl, TSEMICOLON, NULL);

	/* replay the synthesized definition through cpp_class_decl.  The
	 * closure's operator() body is parsed by flush_pending_methods, which
	 * clobbers curfunc; restore it so the construction below (and the
	 * enclosing function's parsing) still targets the right function. */
	{
		extern struct scope filescope;
		struct func *saved_cur = curfunc;
		cur = tok;
		tokpush(&cur, 1);
		tokpush(wtoks, wn);
		next();
		cpp_class_decl(&filescope);
		curfunc = saved_cur;
	}

	ct = scopegettag(&filescope, tagname, 1);
	if (!ct)
		error_code(E_TEMPLATE, &tok.loc, "lambda closure class '%s' was not created", tagname);

	/* --- construct the closure object (anonymous temporary) --- */
	tmp = mkdecl("tmp", DECLOBJECT, ct, QUALNONE, LINKNONE);
	args = NULL;
	ae = &args;
	for (i = 0; i < ncap; ++i) {
		/* the capture's lvalue: a local variable's identifier, or an
		 * outer closure member `(*this).m` (defect T).  A by-reference
		 * capture passes the address (`T *` ctor parameter). */
		struct expr *cap = caps[i].arg;
		if (caps[i].by_ref)
			cap = mkunaryexpr(TBAND, cap);
		*ae = cap;
		ae = &cap->next;
	}
	if (!curfunc) {
		/* file-scope lambda: the closure object has static storage.
		 * A no-capture closure is an empty object — constant-
		 * constructible — so it needs no runtime construction; the
		 * closure object folds to a constant, which makes file-scope
		 * `auto f = [](...){...};` a valid static initializer and
		 * `constexpr auto f = [](...){...};` satisfy the constant-
		 * initializer requirement.  Capturing lambdas at file scope
		 * would require dynamic initialization (deferred to
		 * __mxx_global_var_init) and are not supported yet. */
		tmp->u.obj.storage = SDSTATIC;
		tmp->value = mkglobal(tmp);
		if (ncap == 0) {
			tmp->u.obj.constval = 0;
			tmp->u.obj.has_constval = true;
		} else {
			error_code(E_DECL, &tok.loc,
			    "file-scope lambda with captures is not supported yet");
		}
	} else {
		tmp->u.obj.storage = SDAUTO;
		funcinit(curfunc, tmp, NULL, false); /* allocate storage */
		cpp_emit_ctor_call(curfunc, tmp, args);
	}

	e = mkexpr(EXPRIDENT, ct, NULL);
	e->lvalue = true;
	e->u.ident.decl = tmp;
	return e;
}
