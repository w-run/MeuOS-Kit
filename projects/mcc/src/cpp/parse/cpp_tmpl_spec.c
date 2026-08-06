/* cpp_tmpl_spec.c - m++ (C++) template specialization parsers.
 *
 * Stage C.3.2: split from cpp_tmpl_decl.c.  Class template partial
 * specialization and function template explicit specialization.
 *
 * Cross-file entry points (both called from cpp_template_decl in
 * cpp_tmpl_decl.c, made non-static):
 *   cpp_class_specialization
 *   cpp_function_specialization
 *   cpp_spec_type_of  (internal helpers)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "mcc.h"
#include "cpp.h"
static struct type *
#include "cpp_internal.h"
#include "../../c/parse/decl_internal.h"
#include "../../c/parse/expr_internal.h"

cpp_spec_type_of(struct scope *s, const char *nm)
{
	if (strcmp(nm, "int") == 0)     return &typeint;
	if (strcmp(nm, "char") == 0)    return &typechar;
	if (strcmp(nm, "long") == 0)    return &typelong;
	if (strcmp(nm, "short") == 0)   return &typeshort;
	if (strcmp(nm, "double") == 0)  return &typedouble;
	if (strcmp(nm, "float") == 0)   return &typefloat;
	{
		struct type *t = scopegettag(s, nm, true);
		if (t) return t;
	}
	return NULL;
}

/* Class-template explicit specialization: `template <> class Tag<args> {
 * ... };`.  The primary class template (`Tag<T>`) was already declared; an
 * empty `<>` supplies all template args explicitly on the class's
 * template-id, and the specialization's class body replaces the primary's
 * for that exact argument combination.  We lower it to a concrete class
 * named by the same mangled tag the generic instantiation uses (e.g.
 * `Box_i`), define it through the normal C++ class parser, then cache it
 * as the primary template's pre-registered class instantiation so a use
 * `Box<int>` hits the specialization instead of the generic body.  This is
 * the class analogue of cpp_function_specialization. */
void
cpp_class_specialization(struct scope *s)
{
	struct token *toks = NULL, *rtoks = NULL;
	size_t ntoks = 0, cap = 0;
	int bd = 0, i, clsidx = -1, nameidx = -1, exp_start = -1, exp_end = -1;
	int depth, rn = 0;
	struct cpp_template *tmpl;
	struct type *argt[16];
	int narg = 0;
	char key[128], tag[128], code[64];

	/* buffer the whole specialization class definition: `class Tag<args> {
	 * ... }` (optionally `;`) */
	for (;;) {
		if (bd == 0 && (tok.kind == TSEMICOLON)) {
			next();
			break;
		}
		if (ntoks >= cap) {
			cap = cap ? cap * 2 : 128;
			toks = xreallocarray(toks, cap, sizeof *toks);
		}
		toks[ntoks++] = tok;
		if (tok.kind == TLBRACE)
			++bd;
		else if (tok.kind == TRBRACE)
			--bd;
		next();
		if (bd == 0 && toks[ntoks - 1].kind == TRBRACE)
			break;
		if (tok.kind == TEOF)
			break;
	}
	if (ntoks == 0)
		error_code(E_TEMPLATE, &tok.loc,
		    "empty explicit class template specialization");
	if (tok.kind == TSEMICOLON)
		next(); /* consume the class definition's trailing ';' */

	/* find the class-key and the class name immediately after it.  The
	 * class-key (struct/class/union) is a C keyword token; the class name
	 * is the first plain identifier after it. */
	if (ntoks < 2)
		error_code(E_TEMPLATE, &tok.loc,
		    "malformed class template specialization");
	clsidx = 0;
	for (i = 1; i < (int)ntoks; ++i) {
		const char *nm;
		if (toks[i].kind < TIDENT)
			continue;
		nm = tokenstr(toks[i].kind);
		if (cpp_classify_ident(nm, strlen(nm)) != CPP_TNONE)
			continue; /* access specifier? skip */
		nameidx = i;
		if (i + 1 < (int)ntoks && toks[i + 1].kind == TLESS)
			exp_start = i + 1;
		break;
	}
	if (nameidx < 0 || exp_start < 0)
		error_code(E_TEMPLATE, &tok.loc,
		    "class template specialization needs explicit template arguments");
	depth = 0;
	for (i = exp_start; i < (int)ntoks; ++i) {
		if (toks[i].kind == TLESS)
			++depth;
		else if (toks[i].kind == TGREATER) {
			--depth;
			if (depth == 0) {
				exp_end = i;
				break;
			}
		}
	}
	if (exp_end < 0)
		error_code(E_TEMPLATE, &tok.loc,
		    "unterminated explicit template argument list");

	/* resolve the explicit args and find the primary class template */
	for (i = exp_start + 1; i < exp_end; ++i) {
		const char *nm;
		if (toks[i].kind == TCOMMA || toks[i].kind == TLESS ||
		    toks[i].kind == TGREATER)
			continue;
		nm = tokenstr(toks[i].kind);
		argt[narg] = cpp_spec_type_of(s, nm);
		if (!argt[narg])
			error_code(E_TEMPLATE, &tok.loc,
			    "cannot resolve explicit specialization argument '%s'", nm);
		if (narg < 15)
			++narg;
	}
	if (narg == 0)
		error_code(E_TEMPLATE, &tok.loc,
		    "class template specialization needs at least one explicit argument");
	tmpl = NULL;
	for (tmpl = g_cpp_templates; tmpl; tmpl = tmpl->next)
		if (tmpl->is_class &&
		    strcmp(tmpl->name, tokenstr(toks[nameidx].kind)) == 0)
			break;
	if (!tmpl)
		error_code(E_TEMPLATE, &tok.loc,
		    "explicit specialization of undeclared class template '%s'",
		    tokenstr(toks[nameidx].kind));

	/* mangled tag/key: Tag + "_" + mangle(arg) each (one per explicit arg,
	 * exactly as cpp_tmpl_class_do_inst computes) */
	snprintf(key, sizeof key, "%s", tokenstr(toks[nameidx].kind));
	for (i = 0; i < narg; ++i) {
		cpp_mangle_type(argt[i], code, sizeof code);
		strncat(key, "_", sizeof key - strlen(key) - 1);
		strncat(key, code, sizeof key - strlen(key) - 1);
	}
	snprintf(tag, sizeof tag, "%s", key);

	/* rewrite: `class Tag < args > {...}` -> `class tag_i {...};` — rename
	 * the class-name token (and any ctor/dtor tokens spelling the original
	 * tag) to the mangled tag, drop the `<args>` from the template-id, and
	 * append a trailing ';' so the replay does not run into the caller's
	 * stream (mirrors cpp_tmpl_class_do_inst). */
	rtoks = xreallocarray(NULL, ntoks + 1, sizeof *rtoks);
	for (i = 0; i < (int)ntoks; ++i) {
		const char *nm;
		if (i == nameidx) {
			rtoks[rn++] = toks[i];
			rtoks[rn - 1].kind = tokenget(tag, strlen(tag));
			continue;
		}
		if (i > nameidx && i <= exp_end)
			continue; /* the `< args >` */
		if (toks[i].kind >= TIDENT) {
			nm = tokenstr(toks[i].kind);
			if (cpp_classify_ident(nm, strlen(nm)) == CPP_TNONE &&
			    strcmp(nm, tokenstr(toks[nameidx].kind)) == 0) {
				rtoks[rn++] = toks[i];
				rtoks[rn - 1].kind = tokenget(tag, strlen(tag));
				continue;
			}
		}
		rtoks[rn++] = toks[i];
	}
	rtoks[rn].kind = TSEMICOLON;
	rtoks[rn].space = false;
	++rn;

	/* replay the concrete class definition, then cache it as the primary
	 * template's pre-registered instantiation for this key. */
	{
		struct token *repl = rtoks;
		struct token cur = tok;
		size_t d = tokctx_depth();
		extern struct func *curfunc;
		struct func *saved_cf = curfunc;
		bool saved_inst = g_cpp_tmpl_instantiating;
		tokpush(&cur, 1);
		tokpush(repl, rn);
		next();
		g_cpp_tmpl_instantiating = true;
		cpp_class_decl(&filescope);
		g_cpp_tmpl_instantiating = saved_inst;
		curfunc = saved_cf;
		tok = cur;
		tokctx_rewind(d);
	}
	{
		struct cpp_tmpl_cls_inst *ci;
		struct type *t = scopegettag(&filescope, tag, 1);
		if (!t)
			error_code(E_TEMPLATE, &tok.loc,
			    "class template specialization produced no class");
		/* pre-register so a later `Box<int>` use hits the specialization.
		 * Prepend to the front (so it shadows any generic instantiation
		 * of the same key already materialised), and fix cls_insts_end to
		 * the new tail so a subsequent generic append does not clobber
		 * this head node (it used to point at the old empty head slot). */
		ci = xmalloc(sizeof(*ci));
		snprintf(ci->key, sizeof ci->key, "%s", key);
		ci->t = t;
		ci->next = tmpl->cls_insts;
		tmpl->cls_insts = ci;
		if (!ci->next)
			tmpl->cls_insts_end = &ci->next;
	}
	free(rtoks);
	free(toks);
}

/* Function-template explicit specialization: `template <> ret
 * name<args>(params) { body }`.  The primary template (`name`) was already
 * declared with `template <...>`.  With an empty `<>` the template args are
 * supplied explicitly on the function's template-id; the specialization's
 * body replaces the primary's for those exact argument types.  We lower it
 * by rewriting the declaration into a plain function named by the same
 * mangled instantiation key the generic path computes, then register that
 * function at the FRONT of the primary template's inst cache so a matching
 * call (`name(args)`) hits the specialization before it tries to instantiate
 * the primary body. */
void
cpp_function_specialization(struct scope *s)
{
	struct token *toks = NULL, *rew = NULL;
	size_t ntoks = 0, cap = 0, nw = 0;
	int bd = 0, i, np = 0, exp_start = -1, exp_end = -1, fnidx = -1;
	int depth;
	struct cpp_template *tmpl;
	struct type *argt[16];
	int narg = 0;
	char key[128], code[64];

	/* buffer the whole specialization declaration (ret .. {..}; or ret ..; ) */
	for (;;) {
		if (bd == 0 && tok.kind == TSEMICOLON) {
			next();
			break;
		}
		if (ntoks >= cap) {
			cap = cap ? cap * 2 : 128;
			toks = xreallocarray(toks, cap, sizeof *toks);
		}
		toks[ntoks++] = tok;
		if (tok.kind == TLBRACE)
			++bd;
		else if (tok.kind == TRBRACE)
			--bd;
		next();
		if (bd == 0 && toks[ntoks - 1].kind == TRBRACE)
			break;
		if (tok.kind == TEOF)
			break;
	}
	if (ntoks == 0)
		error_code(E_TEMPLATE, &tok.loc,
		    "empty explicit template specialization");

	/* find the function name and its `name<...>` template-id in the buffer.
	 * The first plain identifier is the function name; if it is followed by
	 * a '<' that begins the explicit template-argument list. */
	for (i = 0; i < (int)ntoks; ++i) {
		const char *nm;
		if (toks[i].kind < TIDENT)
			continue;
		nm = tokenstr(toks[i].kind);
		if (cpp_classify_ident(nm, strlen(nm)) != CPP_TNONE)
			continue; /* keyword: a return-type keyword */
		fnidx = i;
		if (i + 1 < (int)ntoks && toks[i + 1].kind == TLESS)
			exp_start = i + 1;
		break;
	}
	if (fnidx < 0)
		error_code(E_TEMPLATE, &tok.loc,
		    "explicit specialization needs a function name");
	if (exp_start < 0)
		error_code(E_TEMPLATE, &tok.loc,
		    "explicit specialization needs explicit template arguments");
	/* matching '>' of the template-id (depth 1 relative to the opened '<') */
	depth = 0;
	for (i = exp_start; i < (int)ntoks; ++i) {
		if (toks[i].kind == TLESS)
			++depth;
		else if (toks[i].kind == TGREATER) {
			--depth;
			if (depth == 0) {
				exp_end = i;
				break;
			}
		}
	}
	if (exp_end < 0)
		error_code(E_TEMPLATE, &tok.loc,
		    "unterminated explicit template argument list");

	/* resolve each explicit argument token to a type and mangle it into
	 * the specialization's key (name + "_" + mangle(type)...). */
	tmpl = NULL;
	for (tmpl = g_cpp_templates; tmpl; tmpl = tmpl->next)
		if (strcmp(tmpl->name, tokenstr(toks[fnidx].kind)) == 0)
			break;
	if (!tmpl)
		error_code(E_TEMPLATE, &tok.loc,
		    "explicit specialization of undeclared template '%s'",
		    tokenstr(toks[fnidx].kind));
	/* resolve each explicit template-argument token to a concrete type
	 * (`int` -> &typeint, `long` -> &typelong, a user tag).  These become
	 * the specialization's bound parameter types. */
	for (i = exp_start + 1; i < exp_end; ++i) {
		const char *nm;
		if (toks[i].kind == TCOMMA || toks[i].kind == TLESS ||
		    toks[i].kind == TGREATER)
			continue;
		nm = tokenstr(toks[i].kind);
		argt[narg] = cpp_spec_type_of(s, nm);
		if (!argt[narg])
			error_code(E_TEMPLATE, &tok.loc,
			    "cannot resolve explicit specialization argument '%s'", nm);
		if (narg < 15)
			++narg;
	}
	if (narg == 0)
		error_code(E_TEMPLATE, &tok.loc,
		    "explicit specialization needs at least one explicit argument");
	snprintf(key, sizeof key, "%s", tokenstr(toks[fnidx].kind));
	/* The generic instantiation key (cpp_tmpl_find_or_instantiate)
	 * appends ONE mangle per function/argument position that deduce maps
	 * to a template parameter (linearly over the call argument list), not
	 * per template parameter — so `T f(T a, T b)` yields key `f_i_i` for
	 * two same-typed params.  To make a specialization match, we must
	 * reproduce that: count the specialization's function parameters and
	 * append one mangle each, resolving the argument type for parameter i
	 * as the i-th explicit arg (cycling the last for params beyond the
	 * explicit list, which is what deduce does for a trailing single
	 * param). */
	{
		/* find the function parameter list `(...)` after the template-id `>` */
		int plp = -1, prp = -1, pdepth = 0, nc = 0;
		for (i = exp_end + 1; i < (int)ntoks; ++i) {
			if (toks[i].kind == TLPAREN && plp < 0) {
				plp = i;
				break;
			}
		}
		if (plp >= 0) {
			for (i = plp; i < (int)ntoks; ++i) {
				if (toks[i].kind == TLPAREN)
					++pdepth;
				else if (toks[i].kind == TRPAREN) {
					--pdepth;
					if (pdepth == 0) {
						prp = i;
						break;
					}
				}
			}
		}
		/* np = number of function parameters: `()` -> 0, else commas+1.
		 * The generic key appends one mangle per parameter, cycling the
		 * last explicit arg for params beyond the explicit list (deduce
		 * maps a trailing single template param to every extra arg). */
		if (plp < 0 || prp == plp + 1)
			np = 0;                    /* no param list, or () */
		else {
			for (i = plp + 1; i < prp; ++i)
				if (toks[i].kind == TCOMMA)
					++nc;
			np = nc + 1;
		}
		for (i = 0; i < np; ++i) {
			int aidx = (i < narg) ? i : narg - 1;
			cpp_mangle_type(argt[aidx], code, sizeof code);
			strncat(key, "_", sizeof key - strlen(key) - 1);
			strncat(key, code, sizeof key - strlen(key) - 1);
		}
	}

	/* rewrite: replace `name<args...>` with the mangled `name_key` (the
	 * same name the generic instantiation uses, so the cache key matches),
	 * and skip the `name < args >` tokens entirely. */
	rew = xreallocarray(NULL, ntoks + 64, sizeof *rew);
	for (i = 0; i < (int)ntoks; ++i) {
		struct token t;
		if (i == fnidx) {
			t = toks[i];
			t.kind = tokenget(key, strlen(key)); /* mangled function name */
			rew[nw++] = t;
			continue;
		}
		if (i > fnidx && i <= exp_end) /* `name <args>` or the args */
			continue;
		rew[nw++] = toks[i];
	}

	/* replay the rewritten plain function definition, then look the
	 * mangled-name function up in the current scope.  The guard token
	 * (the token after the specialization's body) is pushed so the replay
	 * always has a defined terminus when the rewritten stream is
	 * exhausted — mirroring the abbreviated-template replay pattern. */
	{
		struct token guard = tok;
		tokpush(&guard, 1);
		tokpush(rew, nw);
		next();
		decl(s, NULL);
	}
	{
		struct decl *fd = scopegetdecl(s, key, 1);
		if (fd && fd->kind == DECLFUNC) {
			struct cpp_tmpl_inst *inst = xmalloc(sizeof *inst);
			snprintf(inst->key, sizeof inst->key, "%s", key);
			inst->fn = fd;
			inst->next = tmpl->insts;
			tmpl->insts = inst;
		}
	}
	free(rew);
	free(toks);
}

/* Parse `template < typename T, class U, ... >` and buffer the following
 * declaration.  Nothing is defined yet; instantiation happens on first
 * use with concrete type arguments.  `owner` is the enclosing class for
 * a member template (`template<...> T get() {...}` inside a class body),
 * or NULL for a file-scope function/class template. */
void
