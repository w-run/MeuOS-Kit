/* cpp_constexpr.c - m++ (C++) constexpr function body buffering.
 *
 * Stage C.3.3: split into per-domain submodules:
 *   - cpp_constexpr_agg.c    aggregate-object mini-memory model
 *   - cpp_constexpr_eval.c   multi-statement constexpr interpreter +
 *                            cpp_constexpr_eval entry point
 *   - cpp_constexpr_ctrl.c   if constexpr / if consteval / structured binding
 *
 * This file retains the constexpr-function body-buffering logic:
 *   struct cpp_cexpr_fn, g_cpp_cexpr_fns, g_cpp_cexpr_depth, g_cexpr_body,
 *   cpp_buffer_constexpr_body.
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

/* --- C++ constexpr functions (body buffering) ------------------------- */

/* Registry of buffered constexpr-function bodies; traversed by the
 * evaluator (cpp_constexpr_eval.c) when folding a constant-context call. */
struct cpp_cexpr_fn *g_cpp_cexpr_fns;

/* Recursion depth limit for the constexpr evaluator. */
int g_cpp_cexpr_depth;

/* Non-zero while a C23 `constexpr` function definition body is being parsed
 * (the runtime-definition replay in decl.c).  The call-expression parser
 * consults it to reject calls to non-constexpr functions, which would make
 * the body non-constant-foldable (C23 6.7.1, constexpr function body
 * constraints). */
int g_cexpr_body;

/* Buffer a constexpr function's `{ ... }` body (called from decl() with
 * tok positioned on '{'), then replay it so the normal runtime definition
 * is still emitted by the caller's stmt(). */
void
cpp_buffer_constexpr_body(struct decl *d)
{
	extern void tokpush(struct token *, size_t);

	struct cpp_cexpr_fn *fn;
	struct decl *pd;
	size_t cap = 0, ntok = 0;
	int bd = 0, n = 0;

	fn = xmalloc(sizeof *fn);
	fn->fd = d;
	fn->nparams = 0;
	for (pd = d->type->u.func.params; pd; pd = pd->next)
		++fn->nparams;
	fn->params = xmalloc(fn->nparams * sizeof *fn->params);
	fn->ptypes = xmalloc(fn->nparams * sizeof *fn->ptypes);
	for (pd = d->type->u.func.params; pd; pd = pd->next) {
		fn->params[n] = pd->name ? pd->name : (char *)"";
		fn->ptypes[n] = pd->type;
		++n;
	}
	fn->toks = NULL;
	fn->ntoks = 0;
	fn->tmpl_params = NULL;
	fn->tmpl_types = NULL;
	fn->tmpl_vals = NULL;
	fn->tmpl_isval = NULL;
	fn->ntmpl = 0;
	/* Capture the active template instantiation's parameter bindings (if
	 * this constexpr function is a template instantiation) so the constant
	 * evaluator can resolve type usages of the template parameters. */
	if (g_cpp_cexpr_tmpl_n > 0) {
		int i;
		fn->ntmpl = g_cpp_cexpr_tmpl_n;
		fn->tmpl_params = xmalloc(fn->ntmpl * sizeof *fn->tmpl_params);
		fn->tmpl_types = xmalloc(fn->ntmpl * sizeof *fn->tmpl_types);
		fn->tmpl_vals = xmalloc(fn->ntmpl * sizeof *fn->tmpl_vals);
		fn->tmpl_isval = xmalloc(fn->ntmpl * sizeof *fn->tmpl_isval);
		for (i = 0; i < fn->ntmpl; ++i) {
			fn->tmpl_params[i] = strdup(g_cpp_cexpr_tmpl_params[i]);
			fn->tmpl_types[i] = g_cpp_cexpr_tmpl_types[i];
			fn->tmpl_vals[i] = g_cpp_cexpr_tmpl_vals[i];
			fn->tmpl_isval[i] = g_cpp_cexpr_tmpl_isval[i];
		}
		g_cpp_cexpr_tmpl_n = 0;
	}
	while (tok.kind != TEOF) {
		if (ntok >= cap) {
			cap = cap ? cap * 2 : 32;
			fn->toks = xreallocarray(fn->toks, cap, sizeof *fn->toks);
		}
		/* copy the token; the scanner reuses its literal buffer on the
		 * next next(), so keep a private copy of `lit` for the replay */
		{
			struct token tt = tok;
			if (tt.lit)
				tt.lit = strdup(tt.lit);
			fn->toks[ntok++] = tt;
		}
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
	fn->ntoks = ntok;
	/* Replay the body in front of the token that follows it so the
	 * caller's stmt() parses the runtime definition and the parse then
	 * continues at the right stream position.  The trailing token must
	 * outlive this function (stmt() consumes it only after we return),
	 * so it is copied to the heap rather than pushed as a stack local. */
	{
		struct token *trail = xmalloc(sizeof *trail);
		*trail = tok;
		if (trail->lit)
			trail->lit = strdup(trail->lit);
		tokpush(trail, 1);
		tokpush(fn->toks, fn->ntoks);
		next(); /* position tok at '{' */
	}
	fn->next = g_cpp_cexpr_fns;
	g_cpp_cexpr_fns = fn;
}