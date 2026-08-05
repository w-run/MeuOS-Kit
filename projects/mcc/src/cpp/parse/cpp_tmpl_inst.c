/* cpp_tmpl_inst.c - m++ (C++) function-template instantiation.
 *
 * Deduces template arguments (cpp_tmpl_deduce) and instantiates a
 * function template on first use (cpp_tmpl_find_or_instantiate /
 * cpp_tmpl_instantiate).  Entry point cpp_tmpl_instantiate is exported
 * to the postfix-expression lowering.  Extracted from cpp_parse.c.
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


static bool
cpp_tmpl_deduce(struct cpp_template *tmpl, struct scope *s,
                struct expr *arglist,
                struct type **out, int *nout, unsigned long long *nttp_vals)
{
	struct cpp_tmpl_param *p;
	struct expr *a;
	int nfix = 0;
	int i = 0;

	for (p = tmpl->params; p && !p->is_pack; p = p->next)
		++nfix;
	/* explicit template arguments fill the leading parameters first */
	for (p = tmpl->params; i < g_cpp_tmpl_expl_n && p; ++i, p = p->next) {
		if (g_cpp_tmpl_expl_isval[i]) {
			out[i] = g_cpp_tmpl_expl_types[i];
			if (nttp_vals)
				nttp_vals[i] = g_cpp_tmpl_expl_vals[i];
		} else {
			out[i] = g_cpp_tmpl_expl_types[i];
		}
	}
	for (a = arglist; a; a = a->next, ++i) {
		if (i < 16) {
			if (p && p->is_nttp) {
				/* non-type template argument must be a constant
				 * integer expression; fold and record its value */
				extern struct expr *eval(struct expr *);
				struct expr *ev = eval(a);
				if (!ev || ev->kind != EXPRCONST ||
				    !(ev->type->prop & PROPINT))
					error_code(E_TEMPLATE, &tok.loc,
					    "non-type template argument must be a constant integer expression");
				out[i] = ev->type;
				if (nttp_vals)
					nttp_vals[i] = ev->u.constant.u;
			} else {
				out[i] = a->type;
			}
		} else {
			error_code(E_SYNTAX, &tok.loc, "too many arguments for template '%s'", tmpl->name);
		}
		if (p)
			p = p->next;
	}
	/* C++11 default template arguments: for remaining fixed parameters
	 * that carry a default (`template<typename T = int>`), evaluate the
	 * buffered default tokens to fill the missing argument. */
	if (i < nfix) {
		extern struct scope *mkscope(struct scope *);
		extern void scopeputdecl(struct scope *, struct decl *);
		extern struct decl *mkdecl(char *, enum declkind,
		    struct type *, enum typequal, enum linkage);
		extern struct type *typename(struct scope *, enum typequal *,
		    struct expr **);
		extern struct expr *condexpr(struct scope *);
		extern struct expr *eval(struct expr *);
		extern void tokpush(struct token *, size_t);
		struct scope *ds = mkscope(s);
		/* bind the already-resolved type parameters so a default may
		 * reference an earlier parameter (`template<typename T,
		 * typename U = T>`) */
		struct token deduce_tok = tok;
		int k;
		for (k = 0; k < i && k < nfix; k++) {
			struct cpp_tmpl_param *pp = tmpl->params;
			int j;
			for (j = 0; j < k && pp; j++, pp = pp->next)
				;
			if (!pp)
				break;
			if (pp->is_nttp)
				scopeputdecl(ds, mkdecl((char *)pp->name,
				    DECLCONST, out[k], QUALNONE, LINKNONE));
			else
				scopeputdecl(ds, mkdecl((char *)pp->name,
				    DECLTYPE, out[k], QUALNONE, LINKNONE));
		}
		for (; i < nfix && p; p = p->next, ++i) {
			if (!p->deftoks || p->ndeftoks == 0)
				return false; /* too few arguments, no default */
			size_t d = tokctx_depth();
			struct token guard = {0};
			guard.kind = TSEMICOLON;
			tokpush(&guard, 1);
			tokpush(p->deftoks, p->ndeftoks);
			next(); /* position tok at the first default token */
			if (p->is_nttp) {
				/* value default: `template<int N = 5>` */
				struct expr *ev = eval(condexpr(ds));
				tokctx_rewind(d);
				tok = deduce_tok;
				if (!ev || ev->kind != EXPRCONST ||
				    !(ev->type->prop & PROPINT))
					error_code(E_TEMPLATE, &p->deftoks[0].loc,
					    "default template argument is not a constant integer expression");
				out[i] = ev->type;
				if (nttp_vals)
					nttp_vals[i] = ev->u.constant.u;
			} else {
				/* type default: `template<typename T = int>` */
				enum typequal tq = QUALNONE;
				struct expr *toeval = NULL;
				struct type *tt = typename(ds, &tq, &toeval);
				tokctx_rewind(d);
				tok = deduce_tok;
				if (!tt || toeval)
					error_code(E_TEMPLATE, &p->deftoks[0].loc,
					    "default template argument is not a type");
				out[i] = tt;
			}
		}
	}
	if (i < nfix)
		return false; /* too few arguments for the fixed parameters */
	if (i > 16)
		error_code(E_SYNTAX, &tok.loc, "too many arguments for template '%s'", tmpl->name);
	*nout = i;
	return true;
}


/* Find the DECLFUNC for the instantiation of template `name` with the
 * given call-site arguments, instantiating it (replaying the buffered
 * declaration with each parameter bound to a concrete type) on first use. */
static struct decl *
cpp_tmpl_find_or_instantiate(struct scope *s, const char *name,
                             struct expr *arglist)
{
	struct cpp_template *tmpl;
	struct cpp_tmpl_inst *inst;
	struct cpp_tmpl_param *p;
	struct type *types[16];
	unsigned long long nttp_vals[16];
	char key[128], fnname[128];
	struct scope *bs;
	struct decl *fd, *td;
	struct token cur;
	int i, nt = 0;
	bool found;

	for (tmpl = g_cpp_templates; tmpl; tmpl = tmpl->next)
		if (strcmp(tmpl->name, name) == 0)
			break;
	if (!tmpl)
		return NULL;
	if (tmpl->nparams > 16)
		error_code(E_SYNTAX, &tok.loc, "template '%s' has too many parameters", name);
	if (!cpp_tmpl_deduce(tmpl, s, arglist, types, &nt, nttp_vals))
		error_code(E_TEMPLATE, &tok.loc, "too few arguments for template '%s'", name);

	/* mangled name: name + "_" + type codes / NTTP values (e.g. max_i,
	 * cpp20_nttp_42).  NTTP arguments are distinct by value, so the
	 * cache key must encode the value, not just the type. */
	snprintf(key, sizeof key, "%s", name);
	for (i = 0; i < nt; ++i) {
		char code[64];
		if (tmpl_param_is_nttp(tmpl, i))
			snprintf(code, sizeof code, "%llu", nttp_vals[i]);
		else
			cpp_mangle_type(types[i], code, sizeof code);
		strncat(key, "_", sizeof key - strlen(key) - 1);
		strncat(key, code, sizeof key - strlen(key) - 1);
	}
	snprintf(fnname, sizeof fnname, "%s", key);

	for (inst = tmpl->insts; inst; inst = inst->next)
		if (strcmp(inst->key, key) == 0)
			return inst->fn;

	/* instantiate: bind parameters as type names / constants, replay the
	 * declaration.  The buffered tokens are shared across instantiations,
	 * so rename on a private copy. */
	bs = mkscope(s);
	{
		int nfix = 0, npack = 0;
		/* fixed parameters bind positionally; a trailing parameter pack
		 * binds its first element as a placeholder (the per-element types
		 * are registered as __tp0..__tpN-1 for the pack expansion below) */
		for (p = tmpl->params, i = 0; p && !p->is_pack; p = p->next, ++i, ++nfix) {
			if (p->is_nttp) {
			/* non-type parameter: bind the constant value.
			 * u.enumconst feeds the constant evaluator, value feeds
			 * the IR emitter. */
			td = mkdecl((char *)p->name, DECLCONST,
			    p->nttp_type ? p->nttp_type : types[i],
			    QUALNONE, LINKNONE);
			td->u.enumconst = nttp_vals[i];
			td->value = mkintconst(nttp_vals[i]);
			} else {
				td = mkdecl((char *)p->name, DECLTYPE, types[i],
				    QUALNONE, LINKNONE);
			}
			scopeputdecl(bs, td);
		}
		if (p && p->is_pack) {
			npack = nt - nfix;
			td = mkdecl((char *)p->name, DECLTYPE,
			    nfix < nt ? types[nfix] : &typevoid, QUALNONE, LINKNONE);
			scopeputdecl(bs, td);
			for (i = 0; i < npack; ++i) {
				char *tname = xmalloc(32);
				snprintf(tname, 32, "__tp%d", i);
				td = mkdecl(tname, DECLTYPE, types[nfix + i], QUALNONE, LINKNONE);
				scopeputdecl(bs, td);
			}
			/* make the pack size visible to sizeof...(Args) while the
			 * replay below is parsed */
			if (g_cpp_pack_depth < (int)countof(g_cpp_pack_stack))
				g_cpp_pack_stack[g_cpp_pack_depth++] = npack;
		}
	}
	{
		struct token *rtoks = xmalloc(tmpl->ntoks * sizeof *rtoks);
		size_t ntoks = tmpl->ntoks;
		memcpy(rtoks, tmpl->toks, tmpl->ntoks * sizeof *rtoks);
		/* variadic pack expansion on a private copy:
		 *   `Args ... args` (a pack in the parameter list) becomes
		 *       `__tp0 args_0 , __tp1 args_1 , ...`
		 *   `args ...` (a pack used as call arguments) becomes
		 *       `args_0 , args_1 , ...` */
		{
			const char *ptname = NULL;
			const char *pack_var = NULL;
			int nfix = 0, npack;
			struct cpp_tmpl_param *pp;
			for (pp = tmpl->params; pp; pp = pp->next)
				if (pp->is_pack) {
					ptname = pp->name;
					break;
				}
			if (ptname) {
				struct token *wtoks = xmalloc((ntoks + 16 * 8 + 8) * sizeof *wtoks);
				size_t wn = 0;
				for (pp = tmpl->params; pp && !pp->is_pack; pp = pp->next)
					++nfix;
				npack = nt - nfix; /* pack element count */
				/* Discover the pack *variable* name (`args` in
				 * `template<...> auto f(A... args)`) by scanning for
				 * the parameter-list pack `ptname ... NAME`.  Needed
				 * by the fold-expression expansion below. */
				{
					size_t si;
					for (si = 0; si < ntoks; si++)
						if (rtoks[si].kind >= TIDENT &&
						    strcmp(tokenstr(rtoks[si].kind), ptname) == 0 &&
						    si + 2 < ntoks &&
						    rtoks[si + 1].kind == TELLIPSIS &&
						    rtoks[si + 2].kind >= TIDENT) {
							pack_var = tokenstr(rtoks[si + 2].kind);
							break;
						}
				}
				/* C++17 fold expressions over the pack: expand any
				 * `(... op pack)` / `(pack op ...)` / `(pack op ... op
				 * init)` shape into a fully-parenthesized binary chain
				 * over pack_0..pack_{n-1}. */
				if (pack_var && npack > 0) {
					struct token *folds = NULL;
					size_t fn = 0;
					cpp_expand_folds(rtoks, ntoks, pack_var, npack,
					    &folds, &fn);
					if (folds) {
						free(rtoks);
						rtoks = folds;
						ntoks = fn;
					}
				}
				for (i = 0; i < (int)ntoks; ++i) {
					const char *nm = rtoks[i].kind >= TIDENT
					    ? tokenstr(rtoks[i].kind) : NULL;
					if (nm && strcmp(nm, ptname) == 0 &&
					    i + 2 < (int)ntoks &&
					    rtoks[i + 1].kind == TELLIPSIS &&
					    rtoks[i + 2].kind >= TIDENT) {
						/* parameter-list pack: `Args ... args` */
						pack_var = tokenstr(rtoks[i + 2].kind);
						for (int k = 0; k < npack; ++k) {
							char tn[32], vn[32];
							struct token tt = rtoks[i];
							if (k) {
								tt.kind = TCOMMA;
								wtoks[wn++] = tt;
							}
							snprintf(tn, sizeof tn, "__tp%d", k);
							tt.kind = tokenget(tn, strlen(tn));
							wtoks[wn++] = tt;
							snprintf(vn, sizeof vn, "%s_%d", pack_var, k);
							tt.kind = tokenget(vn, strlen(vn));
							wtoks[wn++] = tt;
						}
						i += 2;
						continue;
					}
					if (pack_var && nm && strcmp(nm, pack_var) == 0 &&
					    i + 1 < (int)ntoks && rtoks[i + 1].kind == TELLIPSIS) {
						/* call-site pack: `args ...` */
						for (int k = 0; k < npack; ++k) {
							char vn[32];
							struct token vt = rtoks[i];
							if (k) {
								vt.kind = TCOMMA;
								wtoks[wn++] = vt;
							}
							snprintf(vn, sizeof vn, "%s_%d", pack_var, k);
							vt.kind = tokenget(vn, strlen(vn));
							wtoks[wn++] = vt;
						}
						++i;
						continue;
					}
					wtoks[wn++] = rtoks[i];
				}
				free(rtoks);
				rtoks = wtoks;
				ntoks = wn;
			}
		}
		/* rename the function token to the mangled instantiation name (the
		 * parser resolves identifiers by their interned token kind, so
		 * intern the mangled name and swap the kind) */
		found = false;
		for (i = 0; i < (int)ntoks; ++i) {
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
			if (!is_param) {
				rtoks[i].kind = tokenget(fnname, strlen(fnname));
				found = true;
				break;
			}
		}
		if (!found)
			error_code(E_DECL, &tok.loc, "cannot locate function name in template '%s'", name);

		cur = tok;
		tokpush(&cur, 1);
		tokpush(rtoks, ntoks);
		/* rtoks stays alive (tokpush stores pointers) until decl() consumes
		 * it below; deliberately not freed (bounded per-instantiation). */
	}
	next();
	/* C++20 requires-clause: the constraint must hold for the deduced
	 * types, otherwise the instantiation is ill-formed. */
	{
		bool cok = cpp_check_constraint(tmpl, bs);
			if (!cok)
			error_code(E_TEMPLATE, &tok.loc,
			    "template '%s' instantiated with a type that does not satisfy its requires-clause",
			    name);
	}
	/* Expose this instantiation's template parameter type bindings so a
	 * constexpr body buffered inside decl() can capture them for the
	 * constant evaluator.  Saved and restored across nested instantiations
	 * (a constexpr template body may call another template) so the outer
	 * function keeps its own parameters. */
	{
		const char *sv_p[16];
		struct type *sv_t[16];
		unsigned long long sv_v[16];
		bool sv_iv[16];
		int sv_n = g_cpp_cexpr_tmpl_n, k, nfix = 0;
		struct cpp_tmpl_param *pp;
		for (k = 0; k < sv_n; ++k) {
			sv_p[k] = g_cpp_cexpr_tmpl_params[k];
			sv_t[k] = g_cpp_cexpr_tmpl_types[k];
			sv_v[k] = g_cpp_cexpr_tmpl_vals[k];
			sv_iv[k] = g_cpp_cexpr_tmpl_isval[k];
		}
		g_cpp_cexpr_tmpl_n = 0;
		/* count fixed (non-pack) parameters; the trailing pack binds its
		 * first element as a placeholder at this index */
		for (pp = tmpl->params; pp; pp = pp->next)
			if (!pp->is_pack)
				++nfix;
		{
			int pi = 0;
			for (pp = tmpl->params; pp && g_cpp_cexpr_tmpl_n < 16;
			     pp = pp->next, ++pi) {
				int ti = pp->is_pack ? nfix : pi;
				if (ti >= nt)
					break;
				g_cpp_cexpr_tmpl_params[g_cpp_cexpr_tmpl_n] = pp->name;
				g_cpp_cexpr_tmpl_types[g_cpp_cexpr_tmpl_n] = types[ti];
				if (pp->is_nttp) {
					g_cpp_cexpr_tmpl_isval[g_cpp_cexpr_tmpl_n] = true;
					g_cpp_cexpr_tmpl_vals[g_cpp_cexpr_tmpl_n] =
					    nttp_vals[ti];
				} else {
					g_cpp_cexpr_tmpl_isval[g_cpp_cexpr_tmpl_n] = false;
				}
				++g_cpp_cexpr_tmpl_n;
			}
		}
		if (!decl(bs, NULL))
			error_code(E_TEMPLATE, &tok.loc, "failed to instantiate template '%s'", name);
		g_cpp_cexpr_tmpl_n = sv_n;
		for (k = 0; k < sv_n; ++k) {
			g_cpp_cexpr_tmpl_params[k] = sv_p[k];
			g_cpp_cexpr_tmpl_types[k] = sv_t[k];
			g_cpp_cexpr_tmpl_vals[k] = sv_v[k];
			g_cpp_cexpr_tmpl_isval[k] = sv_iv[k];
		}
	}
	if (g_cpp_pack_depth > 0)
		--g_cpp_pack_depth;

	fd = scopegetdecl(bs, fnname, 1);
	if (!fd || fd->kind != DECLFUNC)
		error_code(E_DECL, &tok.loc, "template '%s' instantiation produced no function", name);
	/* re-register at file scope so later uses of the same instantiation
	 * (and cross-function calls) resolve the symbol */
	scopeputdecl(s, fd);
	delscope(bs);

	inst = xmalloc(sizeof(*inst));
	snprintf(inst->key, sizeof inst->key, "%s", key);
	inst->fn = fd;
	inst->next = NULL;
	*tmpl->insts_end = inst;
	tmpl->insts_end = &inst->next;
	return fd;
}

/* TLPAREN helper: when a pending template call is recorded (set by
 * cpp_tmpl_placeholder), instantiate the template from the argument types
 * and return the decayed callee expression; clears the pending state. */
struct expr *
cpp_tmpl_instantiate(struct scope *s, struct expr *arglist)
{
	const char *nm;
	struct decl *fd;
	struct expr *e;

	if (!g_cpp_tmpl_depth)
		return NULL;
	/* pop before instantiating: the replay of the template body parses
	 * nested calls whose TLPAREN lowering pops their own pending names */
	nm = g_cpp_tmpl_stack[--g_cpp_tmpl_depth];
	fd = cpp_tmpl_find_or_instantiate(s, nm, arglist);
	g_cpp_tmpl_expl_n = 0; /* explicit args are consumed by the instance */
	if (!fd)
		return NULL;
	e = mkexpr(EXPRIDENT, fd->type, NULL);
	e->u.ident.decl = fd;
	e->lvalue = false;
	return decay(e);
}
