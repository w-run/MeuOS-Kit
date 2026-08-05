/* cpp_classtmpl.c — m++ (C++) class-template instantiation.
 *
 * Instantiate a class template `Foo<int>` (or CTAD `Foo v(...)`) by
 * replaying the buffered declaration with each parameter bound to a
 * concrete type.  Entry points cpp_tmpl_class_lookup /
 * cpp_tmpl_class_instantiate / cpp_tmpl_class_ctad are exported;
 * cpp_tmpl_class_do_inst is module-internal.
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

/* --- C.2.8 class templates (instantiate-on-first-use) ------------------ */


const char *
cpp_tmpl_class_lookup(const char *name)
{
	struct cpp_template *t;

	for (t = g_cpp_templates; t; t = t->next)
		if (t->is_class && strcmp(t->name, name) == 0)
			return t->name;
	return NULL;
}

/* Match a class partial-specialization pattern `template<typename T> struct
 * Foo<T*> { ... }` against concrete template args (`Foo<int*>`).  For the
 * common pointer pattern `T*`, a concrete arg X matches iff X is a pointer
 * type; the pattern parameter T binds to the pointed-to type (base of X).
 * Returns true and fills `subst[0..]` with the bound types (one per pattern
 * parameter).  Non-pointer patterns are not yet handled (fall back to the
 * primary instantiation). */
static bool
cpp_tmpl_cls_partial_match(struct cpp_tmpl_partial *par,
                           struct type **args, int n,
                           struct type **subst, int *nsubst)
{
	if (n < 1)
		return false;
	/* `T *` pattern: two tokens [param-name, TMUL] */
	if (par->npatargs == 2 && par->patargs[1].kind == TMUL &&
	    par->patargs[0].kind >= TIDENT) {
		if (args[0]->kind != TYPEPOINTER)
			return false;
		subst[0] = args[0]->base;
		*nsubst = 1;
		return true;
	}
	return false;
}

/* Instantiate a matched partial specialization: replay the partial's class
 * body with its own template parameters bound to the substituted concrete
 * types, naming the resulting class by the PRIMARY template's mangled key
 * (so `Foo<int*>` lands on the same cache entry regardless of which
 * definition produced it).  Mirrors cpp_tmpl_class_do_inst's replay. */
static struct type *
cpp_tmpl_cls_partial_inst(struct scope *s, struct cpp_template *tmpl,
                          struct cpp_tmpl_partial *par,
                          struct type **subst, int nsubst,
                          const char *key)
{
	extern struct scope filescope;
	struct cpp_tmpl_param *pp;
	struct decl *td;
	struct type *t;
	struct token *rtoks;
	size_t depth;
	struct token cur;
	int rn, i;

	/* bind the partial's parameters to the matched concrete types */
	g_cpp_tmpl_nbinds = 0;
	for (pp = par->params, i = 0; pp; pp = pp->next, ++i) {
		struct type *bt = i < nsubst ? subst[i] : &typevoid;
		td = mkdecl((char *)pp->name, DECLTYPE, bt, QUALNONE, LINKNONE);
		scopeputdecl(&filescope, td);
		if (g_cpp_tmpl_nbinds < 16)
			g_cpp_tmpl_binds[g_cpp_tmpl_nbinds++] = td;
	}
	/* build `struct <key> <body> ;` replay; rename the class-name token and
	 * any constructor/destructor tokens (which spell the primary class name)
	 * to the mangled key so they define the instantiated class's ctors. */
	rn = par->ntoks + 3;
	rtoks = xmalloc(rn * sizeof *rtoks);
	rtoks[0].kind = tokenget("struct", strlen("struct"));
	rtoks[0].space = false;
	rtoks[1].kind = tokenget(key, strlen(key));
	rtoks[1].space = true;
	for (i = 0; i < (int)par->ntoks; ++i) {
		const char *nm;
		if (par->toks[i].kind >= TIDENT) {
			nm = tokenstr(par->toks[i].kind);
			if (cpp_classify_ident(nm, strlen(nm)) == CPP_TNONE &&
			    strcmp(nm, tmpl->name) == 0) {
				rtoks[2 + i] = par->toks[i];
				rtoks[2 + i].kind = tokenget(key, strlen(key));
				continue;
			}
		}
		rtoks[2 + i] = par->toks[i];
	}
	rtoks[rn - 1].kind = TSEMICOLON;
	rtoks[rn - 1].space = false;

	depth = tokctx_depth();
	cur = tok;
	tokpush(&cur, 1);
	tokpush(rtoks, rn);
	next();
	{
		extern struct func *curfunc;
		struct func *saved_cf = curfunc;
		bool saved_inst = g_cpp_tmpl_instantiating;
		g_cpp_tmpl_instantiating = true;
		cpp_class_decl(&filescope);
		g_cpp_tmpl_instantiating = saved_inst;
		curfunc = saved_cf;
	}
	tok = cur;
	tokctx_rewind(depth);

	t = scopegettag(&filescope, key, 1);
	if (t) {
		struct cpp_tmpl_cls_inst *ci = xmalloc(sizeof *ci);
		snprintf(ci->key, sizeof ci->key, "%s", key);
		ci->t = t;
		ci->next = tmpl->cls_insts;
		tmpl->cls_insts = ci;
		if (!ci->next)
			tmpl->cls_insts_end = &ci->next;
	}
	free(rtoks);
	return t;
}

/* Instantiate a class template with the given type arguments: replay the
 * buffered `class Foo { ... }` declaration (with each parameter bound and
 * the tag renamed to the mangled instantiation name `Foo_<codes>`) through
 * cpp_class_decl.  Returns the instantiated class type (cached per key). */
static struct type *
cpp_tmpl_class_do_inst(struct scope *s, struct cpp_template *tmpl,
                       struct type **args, unsigned long long *nttp)
{
	extern struct scope filescope;
	struct cpp_tmpl_cls_inst *ci;
	struct cpp_tmpl_param *p;
	struct type *t;
	char key[128], tag[128];
	struct token cur;
	struct decl *td;
	size_t depth;
	int i;

	/* mangled tag name: Foo + "_" + type codes / NTTP values (Foo_i,
	 * Arr_3) */
	snprintf(key, sizeof key, "%s", tmpl->name);
	for (i = 0; i < tmpl->nparams; ++i) {
		char code[64];
		if (tmpl_param_is_nttp(tmpl, i))
			snprintf(code, sizeof code, "%llu", nttp[i]);
		else
			cpp_mangle_type(args[i], code, sizeof code);
		strncat(key, "_", sizeof key - strlen(key) - 1);
		strncat(key, code, sizeof key - strlen(key) - 1);
	}
	snprintf(tag, sizeof tag, "%s", key);

	for (ci = tmpl->cls_insts; ci; ci = ci->next)
		if (strcmp(ci->key, key) == 0)
			return ci->t;

	/* class-template partial specialization: try to match the concrete args
	 * against each registered pattern (`Foo<T*>` matching `Foo<int*>`); if a
	 * pattern matches, instantiate that partial's body instead of the primary
	 * (more specific), register under the same key, and return. */
	{
		struct cpp_tmpl_partial *par;
		for (par = tmpl->partials; par; par = par->next) {
			struct type *subst[16];
			int nsubst = 0;
			if (cpp_tmpl_cls_partial_match(par, args, tmpl->nparams,
			                               subst, &nsubst)) {
				struct type *pt = cpp_tmpl_cls_partial_inst(s, tmpl,
				    par, subst, nsubst, key);
				if (pt)
					return pt;
			}
		}
	}

	/* bind the parameters as type names / constants (re-put replaces the
	 * previous binding; the names are generic template params and stay
	 * benignly in file scope) */
	g_cpp_tmpl_nbinds = 0;
	for (p = tmpl->params, i = 0; p; p = p->next, ++i) {
		if (p->is_nttp) {
			td = mkdecl((char *)p->name, DECLCONST,
			    p->nttp_type ? p->nttp_type : args[i],
			    QUALNONE, LINKNONE);
			td->u.enumconst = nttp[i];
			td->value = mkintconst(nttp[i]);
		} else {
			td = mkdecl((char *)p->name, DECLTYPE, args[i],
			    QUALNONE, LINKNONE);
		}
		scopeputdecl(&filescope, td);
		/* Snapshot this binding so a deferred method body can be
		 * re-bound when parsed later (see cpp_ensure_method_defined).
		 * `binds[16]` matches the template parameter limit enforced
		 * above (tmpl->nparams > 16 is rejected as a syntax error),
		 * so the buffer can never be overrun here. */
		if (g_cpp_tmpl_nbinds < 16)
			g_cpp_tmpl_binds[g_cpp_tmpl_nbinds++] = td;
	}
	/* C++20: a constrained type parameter (`template<Concept T> class`)
	 * must be satisfied by the instantiation's arguments. */
	if (!cpp_check_constraint(tmpl, &filescope))
		error_code(E_TEMPLATE, &tok.loc,
		    "class template '%s' instantiated with a type that does not satisfy its requires-clause",
		    tmpl->name);
	/* rename the class-name token to the mangled tag, and every
	 * constructor/destructor token (which spells the original class name)
	 * so struct_decl recognizes them as the class's own constructors.
	 * The buffered tokens are shared across instantiations, so rename on
	 * a private copy. */
	{
		/* append a trailing ';' so cpp_class_decl's replay does not run
		 * past the class definition into the caller's token stream (the
		 * template buffer stops at the closing '}' without it) */
		struct token *rtoks = xmalloc((tmpl->ntoks + 1) * sizeof *rtoks);
		bool found = false;
		int rn = tmpl->ntoks;
		memcpy(rtoks, tmpl->toks, tmpl->ntoks * sizeof *rtoks);
		rtoks[rn].kind = TSEMICOLON;
		rtoks[rn].space = false;
		++rn;
		for (i = 0; i < rn; ++i) {
			const char *nm;
			if (rtoks[i].kind < TIDENT)
				continue;
			nm = tokenstr(rtoks[i].kind);
			if (cpp_classify_ident(nm, strlen(nm)) != CPP_TNONE)
				continue;
			bool is_param = false;
			for (p = tmpl->params; p; p = p->next)
				if (strcmp(p->name, nm) == 0) {
					is_param = true;
					break;
				}
			if (!is_param && strcmp(nm, tmpl->name) == 0) {
				/* the class name itself or a constructor/destructor */
				rtoks[i].kind = tokenget(tag, strlen(tag));
				found = true;
				/* Injected-class-name written as a template-id
				 * (`Box<T>` inside `template<class T> struct Box`):
				 * the name is now the mangled tag, so the trailing
				 * `<...>` would be parsed as a stray less-than and
				 * break the declarator.  Drop the argument list —
				 * within its own definition `Box<T>` denotes exactly
				 * this instantiation.  Only elide when every argument
				 * token is one of the template's own parameters, so a
				 * genuine comparison (`Box_i < x`) is left alone. */
				if (i + 1 < rn && rtoks[i + 1].kind == TLESS) {
					int depth2 = 0, j, k;
					bool argsonly = true;
					for (j = i + 1; j < rn; ++j) {
						if (rtoks[j].kind == TLESS) {
							++depth2;
							continue;
						}
						if (rtoks[j].kind == TGREATER) {
							if (--depth2 == 0)
								break;
							continue;
						}
						if (rtoks[j].kind == TCOMMA)
							continue;
						if (rtoks[j].kind >= TIDENT) {
							const char *an = tokenstr(rtoks[j].kind);
							bool ok = false;
							for (p = tmpl->params; p; p = p->next)
								if (strcmp(p->name, an) == 0) {
									ok = true;
									break;
								}
							if (ok)
								continue;
						}
						argsonly = false;
						break;
					}
					if (argsonly && depth2 == 0 && j < rn) {
						/* splice out rtoks[i+1 .. j] (`<`..`>`) */
						int ndrop = j - i;
						for (k = i + 1; k + ndrop < rn; ++k)
							rtoks[k] = rtoks[k + ndrop];
						rn -= ndrop;
					}
				}
			}
		}
		if (!found)
			error_code(E_TEMPLATE, &tok.loc, "cannot locate class name in template '%s'", tmpl->name);

		/* replay `class Foo_i { ... }` to define the instantiated class.
		 * rtoks stays alive (tokpush stores pointers) until cpp_class_decl
		 * consumes it below; deliberately not freed (bounded).  Record the
		 * token-context depth so the replay's unconsumed tokens (class
		 * method bodies are buffered, not consumed) can be discarded
		 * afterwards instead of leaking into the caller's stream. */
		depth = tokctx_depth();
		cur = tok;
		tokpush(&cur, 1);
		tokpush(rtoks, rn);
	}
	next();
	{
		/* cpp_class_decl replays the class definition; constructor bodies
		 * are parsed by flush_pending_methods, which switches curfunc.
		 * Restore it so the caller (a declaration mid-parse, e.g. CTAD)
		 * keeps targeting the right function. */
		extern struct func *curfunc;
		struct func *saved_cf = curfunc;
		bool saved_inst = g_cpp_tmpl_instantiating;
		g_cpp_tmpl_instantiating = true;
		cpp_class_decl(&filescope);
		g_cpp_tmpl_instantiating = saved_inst;
		curfunc = saved_cf;
	}
	/* restore the caller's token position (the replayed definition has
	 * been consumed; cur was the caller's token pushed ahead of rtoks)
	 * and drop any unconsumed replay tokens */
	tok = cur;
	tokctx_rewind(depth);

	t = scopegettag(&filescope, tag, 1);
	if (!t)
		error_code(E_TEMPLATE, &tok.loc, "class template '%s' instantiation produced no class", tmpl->name);

	ci = xmalloc(sizeof(*ci));
	snprintf(ci->key, sizeof ci->key, "%s", key);
	ci->t = t;
	ci->next = NULL;
	*tmpl->cls_insts_end = ci;
	tmpl->cls_insts_end = &ci->next;
	return t;
}

/* Instantiate a class template: `Foo<...>` (tok positioned at '<').
 * Parses the explicit template arguments, then delegates to
 * cpp_tmpl_class_do_inst. */
struct type *
cpp_tmpl_class_instantiate(struct scope *s, const char *name)
{
	extern struct type *typename(struct scope *, enum typequal *,
	    struct expr **);
	struct cpp_template *tmpl;
	struct cpp_tmpl_param *p;
	struct type *args[16];
	unsigned long long nttp_vals[16];
	char key[128], tag[128];
	struct token cur;
	struct decl *td;
	enum typequal tq;
	struct expr *toeval;
	int i, n = 0;

	for (tmpl = g_cpp_templates; tmpl; tmpl = tmpl->next)
		if (tmpl->is_class && strcmp(tmpl->name, name) == 0)
			break;
	if (!tmpl)
		return NULL;
	if (tmpl->nparams > 16)
		error_code(E_SYNTAX, &tok.loc, "template '%s' has too many parameters", name);

	/* explicit template arguments `<T1, 42, ...>`: types for type
	 * parameters, constant expressions for non-type parameters.  An
	 * empty `<>` (C++11 `Box<>` with a defaulted first parameter) and
	 * omitted trailing arguments fall back to the defaults recorded in
	 * each parameter's deftoks. */
	expect(TLESS, "after class template name");
	while (tok.kind != TGREATER) {
		if (n >= tmpl->nparams)
			error_code(E_SYNTAX, &tok.loc, "too many template arguments for class template '%s'", name);
		tq = QUALNONE;
		toeval = NULL;
		if (tmpl_param_is_nttp(tmpl, n)) {
			struct expr *ev = cpp_tmpl_const_arg(s);
			args[n] = ev->type;
			nttp_vals[n] = ev->u.constant.u;
		} else {
			args[n] = typename(s, &tq, &toeval);
		}
		++n;
		if (tok.kind == TGREATER)
			break;
		expect(TCOMMA, "',' or '>' in class template argument list");
	}
	next(); /* consume '>' */
	/* apply default template arguments for the remaining parameters */
	if (n < tmpl->nparams) {
		extern struct scope *mkscope(struct scope *);
		extern void scopeputdecl(struct scope *, struct decl *);
		extern struct decl *mkdecl(char *, enum declkind,
		    struct type *, enum typequal, enum linkage);
		extern struct expr *condexpr(struct scope *);
		extern struct expr *eval(struct expr *);
		extern void tokpush(struct token *, size_t);
		struct scope *ds = mkscope(s);
		struct cpp_tmpl_param *pp;
		struct token post_args = tok; /* restore the stream after '>' */
		int j;
		/* bind the already-resolved params (defaults may reference an
		 * earlier type parameter) */
		for (pp = tmpl->params, j = 0; pp && j < n; pp = pp->next, ++j) {
			if (pp->is_nttp)
				scopeputdecl(ds, mkdecl((char *)pp->name,
				    DECLCONST, args[j], QUALNONE, LINKNONE));
			else
				scopeputdecl(ds, mkdecl((char *)pp->name,
				    DECLTYPE, args[j], QUALNONE, LINKNONE));
		}
		for (; n < tmpl->nparams; ++n, pp = pp ? pp->next : NULL) {
			if (!pp || !pp->deftoks || pp->ndeftoks == 0)
				error_code(E_TEMPLATE, &tok.loc,
				    "too few template arguments for class template '%s'", name);
			size_t d = tokctx_depth();
			struct token guard2 = {0};
			guard2.kind = TSEMICOLON;
			tokpush(&guard2, 1);
			tokpush(pp->deftoks, pp->ndeftoks);
			next();
			if (pp->is_nttp) {
				struct expr *ev = eval(condexpr(ds));
				tokctx_rewind(d);
				tok = post_args;
				if (!ev || ev->kind != EXPRCONST ||
				    !(ev->type->prop & PROPINT))
					error_code(E_TEMPLATE, &pp->deftoks[0].loc,
					    "default template argument is not a constant integer expression");
				args[n] = ev->type;
				nttp_vals[n] = ev->u.constant.u;
			} else {
				enum typequal dq = QUALNONE;
				struct expr *dtoe = NULL;
				struct type *dt = typename(ds, &dq, &dtoe);
				tokctx_rewind(d);
				tok = post_args;
				if (!dt || dtoe)
					error_code(E_TEMPLATE, &pp->deftoks[0].loc,
					    "default template argument is not a type");
				args[n] = dt;
			}
		}
	}

	(void)p;
	(void)key;
	(void)tag;
	(void)cur;
	(void)td;
	return cpp_tmpl_class_do_inst(s, tmpl, args, nttp_vals);
}

/* C++17 CTAD: `Vec v(a, b)` — a class template used without explicit
 * arguments deduces its template parameters from the constructor-call
 * argument types (each argument maps positionally to one parameter,
 * unwrapping references/pointers to the value type).  Looks up the
 * template by `name` and instantiates it with the deduced arguments. */
struct type *
cpp_tmpl_class_ctad(struct scope *s, const char *name, struct expr *args)
{
	struct cpp_template *tmpl;
	struct cpp_tmpl_param *p;
	struct type *types[16];
	unsigned long long nttp_vals[16];
	struct expr *a;
	int i = 0;

	for (tmpl = g_cpp_templates; tmpl; tmpl = tmpl->next)
		if (tmpl->is_class && strcmp(tmpl->name, name) == 0)
			break;
	if (!tmpl)
		return NULL;
	if (tmpl->nparams > 16)
		error_code(E_SYNTAX, &tok.loc, "template '%s' has too many parameters", name);

	/* deduce one template argument per call argument, positionally */
	for (p = tmpl->params, a = args; a && i < tmpl->nparams;
	    a = a->next, ++i, p = p ? p->next : NULL) {
		struct type *at = a->type;
		if (p && p->is_nttp) {
			/* NTTP via CTAD is not deduced from a value here;
			 * the constructor argument's constant value would be
			 * needed — fall back to its type (rare in practice). */
			nttp_vals[i] = 0;
			types[i] = at;
			continue;
		}
		/* unwrap pointer to the pointee for `T *d` parameters */
		while (at && at->kind == TYPEPOINTER && !at->isref)
			at = at->base;
		if (!at)
			at = &typeint;
		types[i] = at;
	}
	if (i < tmpl->nparams)
		error_code(E_TEMPLATE, &tok.loc,
		    "cannot deduce template argument %d of '%s' (too few constructor arguments)",
		    i + 1, name);

	return cpp_tmpl_class_do_inst(s, tmpl, types, nttp_vals);
}
