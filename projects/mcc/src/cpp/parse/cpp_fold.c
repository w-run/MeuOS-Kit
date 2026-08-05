/* cpp_fold.c — m++ (C++) fold-expression expansion.
 *
 * C++17 fold expressions (``(expr op ...)``, ``(... op expr)``,
 * ``(expr + ... + expr)``) over template parameter packs are expanded
 * into an explicit token stream.  Entry point cpp_expand_folds is called
 * by the requires-expression/template-argument code in cpp_parse.c.
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

/* --- C++17 fold expressions ------------------------------------------- */

/* Is `k` a foldable binary operator?  Fold expressions permit the
 * arithmetic, logical, bitwise, comparison, shift, comma, and pointer
 * member operators.  We treat any token that is not punctuation/a type
 * and has a two-operand form as foldable; parens/brackets/braces and
 * terminators are excluded. */
static bool
cpp_fold_isop(enum tokenkind k)
{
	switch (k) {
	case TADD: case TSUB: case TMUL: case TDIV: case TMOD:
	case TBAND: case TBOR: case TXOR: case TSHL: case TSHR:
	case TLAND: case TLOR: /* && || (logical) */
	case TEQL: case TNEQ: case TLESS: case TGREATER:
	case TLEQ: case TGEQ:
	case TCOMMA:
		return true;
	default:
		return false;
	}
}

/* Emit `pack_var_k` (the k-th element of the expanded pack) into the
 * output buffer. */
static void
cpp_fold_emit_elt(struct token **out, size_t *n, size_t *cap,
                  struct token tpl, const char *pack_var, int k)
{
	if (*n >= *cap) {
		*cap = *cap ? *cap * 2 : 64;
		*out = xreallocarray(*out, *cap, sizeof **out);
	}
	char vn[32];
	struct token t = tpl;
	snprintf(vn, sizeof vn, "%s_%d", pack_var, k);
	t.kind = tokenget(vn, strlen(vn));
	(*out)[(*n)++] = t;
}

/* Append one raw token to the output buffer. */
static void
cpp_fold_emit(struct token **out, size_t *n, size_t *cap, struct token t)
{
	if (*n >= *cap) {
		*cap = *cap ? *cap * 2 : 64;
		*out = xreallocarray(*out, *cap, sizeof **out);
	}
	(*out)[(*n)++] = t;
}

/* Expand a unary fold `( ... op pack )` (left) or `( pack op ... )`
 * (right) into a fully-parenthesized chain of binary operations over the
 * `npack` pack elements, mirroring C++17 semantics:
 *   (... op pack):  ((a0 op a1) op a2) ... op a_{n-1}
 *   (pack op ...):  a0 op (a1 op (... op a_{n-1}))
 */
static void
cpp_fold_unary(struct token **out, size_t *n, size_t *cap,
               struct token tpl, enum tokenkind op,
               const char *pack_var, int npack, bool right)
{
	int k;
	if (right) {
		/* right fold, innermost first: emit `a0 op (a1 op (... a_k))` */
		cpp_fold_emit_elt(out, n, cap, tpl, pack_var, 0);
		for (k = 1; k < npack; ++k) {
			struct token ot = tpl; ot.kind = op;
			struct token lp = tpl; lp.kind = TLPAREN;
			cpp_fold_emit(out, n, cap, ot);
			cpp_fold_emit(out, n, cap, lp);
			cpp_fold_emit_elt(out, n, cap, tpl, pack_var, k);
		}
		for (k = 1; k < npack; ++k) {
			struct token rp = tpl; rp.kind = TRPAREN;
			cpp_fold_emit(out, n, cap, rp);
		}
	} else {
		/* left fold: `((a0 op a1) op a2) ... op a_{n-1}` */
		cpp_fold_emit_elt(out, n, cap, tpl, pack_var, 0);
		for (k = 1; k < npack; ++k) {
			struct token ot = tpl; ot.kind = op;
			cpp_fold_emit(out, n, cap, ot);
			cpp_fold_emit_elt(out, n, cap, tpl, pack_var, k);
		}
	}
}

/* Scan `toks[0..n)` for fold-expression shapes involving `pack_var` and
 * emit an expanded token stream into a heap buffer (row `*outn`). */
void
cpp_expand_folds(struct token *toks, size_t n, const char *pack_var,
                 int npack, struct token **out, size_t *outn)
{
	struct token *res = NULL;
	size_t rn = 0, cap = 0;
	size_t i;
	(void)outn;

	for (i = 0; i < n;) {
		struct token t = toks[i];
		/* unary left fold: `( ... op pack )` */
		if (t.kind == TLPAREN && i + 4 < n &&
		    toks[i + 1].kind == TELLIPSIS &&
		    cpp_fold_isop(toks[i + 2].kind) &&
		    toks[i + 3].kind >= TIDENT &&
		    strcmp(tokenstr(toks[i + 3].kind), pack_var) == 0 &&
		    toks[i + 4].kind == TRPAREN) {
			cpp_fold_unary(&res, &rn, &cap, t,
			    toks[i + 2].kind, pack_var, npack, false);
			i += 5;
			continue;
		}
		/* unary right fold: `( pack op ... )` */
		if (t.kind == TLPAREN && i + 4 < n &&
		    toks[i + 1].kind >= TIDENT &&
		    strcmp(tokenstr(toks[i + 1].kind), pack_var) == 0 &&
		    cpp_fold_isop(toks[i + 2].kind) &&
		    toks[i + 3].kind == TELLIPSIS &&
		    toks[i + 4].kind == TRPAREN) {
			npack = npack > 0 ? npack : 1;
			cpp_fold_unary(&res, &rn, &cap, t,
			    toks[i + 2].kind, pack_var, npack, true);
			i += 5;
			continue;
		}
		/* binary fold, pack on the LEFT: `( pack op ... op init )` */
		if (t.kind == TLPAREN && i + 5 < n &&
		    toks[i + 1].kind >= TIDENT &&
		    strcmp(tokenstr(toks[i + 1].kind), pack_var) == 0 &&
		    cpp_fold_isop(toks[i + 2].kind) &&
		    toks[i + 3].kind == TELLIPSIS &&
		    cpp_fold_isop(toks[i + 4].kind)) {
			/* find the matching ')' — init is the tokens between
			 * the second op and the ')'. */
			size_t j;
			/* The pack must be on the left for this shape (the
			 * second operator runs before init).  Find init end. */
			for (j = i + 5; j < n; ++j)
				if (toks[j].kind == TRPAREN)
					break;
			if (j < n) {
				/* binary left fold:
				 *   ((... (pack_0 op pack_1) op ... op pack_{n-1}))
				 *      op init */
				cpp_fold_emit(&res, &rn, &cap,
				    (struct token){.kind = TLPAREN});
				cpp_fold_unary(&res, &rn, &cap, t,
				    toks[i + 2].kind, pack_var, npack, false);
				cpp_fold_emit(&res, &rn, &cap,
				    (struct token){.kind = TRPAREN});
				cpp_fold_emit(&res, &rn, &cap,
				    (struct token){.kind = toks[i + 4].kind});
				for (size_t k2 = i + 5; k2 < j; k2++)
					cpp_fold_emit(&res, &rn, &cap, toks[k2]);
				i = j + 1;
				continue;
			}
		}
		/* binary fold, pack on the RIGHT: `( init op ... op pack )` */
		if (t.kind == TLPAREN && i + 5 < n) {
			size_t ep; /* index of the '...' */
			for (ep = i + 1; ep + 2 < n; ++ep)
				if (toks[ep].kind == TELLIPSIS)
					break;
			/* shape: init op ... op pack ) — pack_var right after an
			 * op before the closing ')'. */
			if (ep + 3 < n && cpp_fold_isop(toks[ep - 1].kind) &&
			    cpp_fold_isop(toks[ep + 1].kind) &&
			    toks[ep + 2].kind >= TIDENT &&
			    strcmp(tokenstr(toks[ep + 2].kind), pack_var) == 0 &&
			    toks[ep + 3].kind == TRPAREN) {
				/* binary right fold:
				 *   init op (pack_0 op (pack_1 op (... op
				 *   pack_{n-1}))) */
				for (size_t k2 = i + 1; k2 < ep - 1; ++k2)
					cpp_fold_emit(&res, &rn, &cap, toks[k2]);
				cpp_fold_emit(&res, &rn, &cap,
				    (struct token){.kind = toks[ep - 1].kind});
				cpp_fold_unary(&res, &rn, &cap, t,
				    toks[ep + 1].kind, pack_var, npack, true);
				i = ep + 4;
				continue;
			}
		}
		cpp_fold_emit(&res, &rn, &cap, t);
		++i;
	}
	*out = res;
	*outn = rn;
}
