/* cpp_requires.c — m++ (C++) requires-expressions and concept constraints.
 *
 * Parser/evaluator for C++20 requires-expressions (``requires { ... }``,
 * ``requires (params) { ... }``) and the constraint-expression evaluator
 * (``eval_constraint``) that concept uses and requires-clauses lower to.
 * Entry point cpp_requires_expr is exported; cpp_check_constraint and
 * cpp_requires_span_len are called from the template-instantiation and
 * function-template paths in cpp_parse.c.
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

/* Maximum constraint expansion depth (guards against recursive
 * concept definitions referencing each other).  This is a runaway guard,
 * not a conformance limit: legitimate deep template code (long concept
 * reference chains, `Concept<Concept<...>>` nesting) routinely exceeds a
 * couple of dozen levels, so the bound is set well above the constant
 * expression recursion limit (64) rather than at it. */
#define MAX_CONSTRAINT_DEPTH 256

#define MAX_CONSTRAINT_DEPTH 256

static bool eval_constraint(struct token *c, size_t n, struct scope *bs);

/* Split the argument token span `args[0..nargs)` of a concept use
 * `Name < args... >` on top-level commas.  Returns a heap array of
 * 2 * (*nout) offsets: argument k spans args[o[2*k] .. o[2*k + 1]).
 * An argument may be several tokens long (`unsigned int`), so the
 * parameter -> argument mapping cannot be a plain index. */
static size_t *
concept_arg_spans(struct token *args, size_t nargs, size_t *nout)
{
	size_t *o = NULL, n = 0, cap = 0, k, start = 0;
	int d = 0;

	for (k = 0; k <= nargs; k++) {
		if (k == nargs || (d == 0 && args[k].kind == TCOMMA)) {
			if (n + 2 > cap) {
				cap = cap ? cap * 2 : 8;
				o = xreallocarray(o, cap, sizeof *o);
			}
			o[n++] = start;
			o[n++] = k;
			start = k + 1;
			continue;
		}
		if (args[k].kind == TLESS || args[k].kind == TLPAREN ||
		    args[k].kind == TLBRACK)
			++d;
		else if ((args[k].kind == TGREATER ||
		    args[k].kind == TRPAREN || args[k].kind == TRBRACK) &&
		    d > 0)
			--d;
	}
	*nout = n / 2;
	return o;
}

/* Expand a concept body: substitute the body's template parameters with
 * the argument tokens and recursively expand any concept uses inside the
 * body.  Returns a heap-allocated token array kept alive through the
 * tokpush/expr below. */
static struct token *
expand_concept_body(struct cpp_template *con, struct token *args,
    size_t nargs, struct scope *bs, size_t *outn, int depth)
{
	struct token *out = NULL, *sub = NULL;
	size_t on = 0, cap = 0, sn = 0, scap = 0;
	size_t i;
	size_t nspan;
	size_t *span = concept_arg_spans(args, nargs, &nspan);

	/* pass 1: substitute the concept's own parameter names by their
	 * argument spans, so the body is fully concrete w.r.t. this use.
	 * A later pass handles concept uses inside the body; because the
	 * substitution happened first, their argument tokens are already
	 * this use's actual types, not this body's parameter names. */
	for (i = 0; i < con->ntoks; i++) {
		struct token t = con->toks[i];
		if (t.kind >= TIDENT) {
			struct cpp_tmpl_param *pp;
			size_t k = 0;
			for (pp = con->params; pp; pp = pp->next, ++k)
				if (strcmp(pp->name, tokenstr(t.kind)) == 0)
					break;
			if (pp && k < nspan) {
				/* emit the whole argument span: the argument
				 * may be a multi-token type (`unsigned int`,
				 * `const char *`, …), not just one token */
				for (size_t q = span[2 * k]; q < span[2 * k + 1];
				    q++) {
					if (sn >= scap) {
						scap = scap ? scap * 2 : 32;
						sub = xreallocarray(sub, scap,
						    sizeof *sub);
					}
					sub[sn++] = args[q];
				}
				continue;
			}
		}
		if (sn >= scap) {
			scap = scap ? scap * 2 : 32;
			sub = xreallocarray(sub, scap, sizeof *sub);
		}
		sub[sn++] = t;
	}

	/* pass 2: expand concept uses in the substituted body stream. */
	for (i = 0; i < sn; i++) {
		if (sub[i].kind >= TIDENT && i + 2 < sn &&
		    sub[i + 1].kind == TLESS) {
			struct cpp_template *csub;
			for (csub = g_cpp_templates; csub; csub = csub->next)
				if (csub->is_concept &&
				    strcmp(csub->name, tokenstr(sub[i].kind)) ==
				    0)
					break;
			if (csub) {
				if (depth >= MAX_CONSTRAINT_DEPTH)
					error_code(E_TEMPLATE, &tok.loc,
					    "requires-clause evaluation too deep: "
					    "concept reference chain exceeds %d levels",
					    MAX_CONSTRAINT_DEPTH);
				size_t j = i + 2;
				int d = 1;
				while (j < sn && d > 0) {
					if (sub[j].kind == TLESS)
						++d;
					else if (sub[j].kind == TGREATER)
						--d;
					++j;
				}
				{
					struct token *body;
					size_t bn;
					body = expand_concept_body(csub,
					    &sub[i + 2], j - 1 - (i + 2),
					    bs, &bn, depth + 1);
					for (size_t q = 0; q < bn; q++) {
						if (on >= cap) {
							cap = cap ? cap * 2 : 32;
							out = xreallocarray(out, cap, sizeof *out);
						}
						out[on++] = body[q];
					}
				}
				i = j - 1;
				continue;
			}
		}
		if (on >= cap) {
			cap = cap ? cap * 2 : 32;
			out = xreallocarray(out, cap, sizeof *out);
		}
		out[on++] = sub[i];
	}
	free(sub);
	free(span);
	*outn = on;
	return out;
}

/* Expand a constraint token stream, replacing concept uses `Name<args>`
 * with their (recursively expanded) concept bodies. */
static struct token *
expand_constraint_tokens(struct token *c, size_t n, struct scope *bs,
    size_t *outn)
{
	struct token *out = NULL;
	size_t on = 0, cap = 0;
	size_t i;

	for (i = 0; i < n; ) {
		if (c[i].kind >= TIDENT && i + 2 < n &&
		    c[i + 1].kind == TLESS) {
			struct cpp_template *con;
			for (con = g_cpp_templates; con; con = con->next)
				if (con->is_concept &&
				    strcmp(con->name, tokenstr(c[i].kind)) == 0)
					break;
			if (con) {
				size_t j = i + 2;
				int d = 1;
				while (j < n && d > 0) {
					if (c[j].kind == TLESS)
						++d;
					else if (c[j].kind == TGREATER)
						--d;
					++j;
				}
				{
					struct token *body;
					size_t bn;
					body = expand_concept_body(con,
					    &c[i + 2], j - 1 - (i + 2),
					    bs, &bn, 0);
					for (size_t q = 0; q < bn; q++) {
						if (on >= cap) {
							cap = cap ? cap * 2 : 64;
							out = xreallocarray(out, cap, sizeof *out);
						}
						out[on++] = body[q];
					}
				}
				i = j;
				continue;
			}
		}
		if (on >= cap) {
			cap = cap ? cap * 2 : 64;
			out = xreallocarray(out, cap, sizeof *out);
		}
		out[on++] = c[i];
		++i;
	}
	*outn = on;
	return out;
}

/* Evaluate one concept use `Name<args>` by expanding it into plain
 * constant-expression tokens and folding the result. */
static bool
eval_concept_use(struct token *c, size_t n, struct scope *bs)
{
	struct token cur;
	struct expr *e;
	struct token *exp;
	size_t en;

	/* C++20 literal constraint: `requires true` / `requires false` are
	 * not concept uses — evaluate them directly (they arrive as a
	 * single TTRUE/TFALSE token, both keyword kinds < TIDENT). */
	if (n == 1 && c[0].kind == TTRUE)
		return true;
	if (n == 1 && c[0].kind == TFALSE)
		return false;
	if (n == 0 || c[0].kind < TIDENT)
		error_code(E_TEMPLATE, &tok.loc, "requires-clause must name a concept");
	exp = expand_constraint_tokens(c, n, bs, &en);
	if (!exp || !en)
		error_code(E_TEMPLATE, &tok.loc, "requires-clause must name a concept");

	{
		extern struct expr *expr(struct scope *);
		extern struct expr *eval(struct expr *);
		extern void tokpush(struct token *, size_t);
		size_t d = tokctx_depth();
		struct token guard = {0};
		cur = tok;
		/* a guard after the expanded expression so expr() stops at the
		 * end of it instead of falling through to the buffered token
		 * below (cur / the caller's context) */
		guard.kind = TSEMICOLON;
		tokpush(&guard, 1);
		tokpush(exp, en);
		next();
		e = expr(bs);
		/* discard any unconsumed expansion tokens, then resume at the
		 * token that was current before the evaluation */
		tokctx_rewind(d);
		tokpush(&cur, 1);
		next();
		e = eval(e);
		if (!e || !(e->type->prop & PROPINT) || e->kind != EXPRCONST)
			error_code(E_TEMPLATE, &tok.loc,
			    "requires-clause is not a constant boolean expression");
		return e->u.constant.u != 0;
	}
}

/* Recursively evaluate a constraint expression: `A && B`, `A || B`,
 * `!A`, or a single concept use `Name<args>`. */
static bool
eval_constraint(struct token *c, size_t n, struct scope *bs)
{
	size_t i;
	int depth = 0;

	if (!c || !n)
		return true;
	for (i = 0; i < n; i++) {
		if (c[i].kind == TLESS || c[i].kind == TLPAREN ||
		    c[i].kind == TLBRACK)
			++depth;
		else if ((c[i].kind == TGREATER || c[i].kind == TRPAREN ||
		    c[i].kind == TRBRACK) && depth > 0)
			--depth;
		else if (depth == 0) {
			if (c[i].kind == TLAND)
				return eval_constraint(c, i, bs) &&
				    eval_constraint(c + i + 1, n - i - 1, bs);
			if (c[i].kind == TLOR)
				return eval_constraint(c, i, bs) ||
				    eval_constraint(c + i + 1, n - i - 1, bs);
			if (c[i].kind == TLNOT)
				return !eval_constraint(c + i + 1, n - i - 1, bs);
		}
	}
	return eval_concept_use(c, n, bs);
}

bool
cpp_check_constraint(struct cpp_template *tmpl, struct scope *bs)
{
	return eval_constraint(tmpl->constraint, tmpl->nconstraint, bs);
}

/* --- C++20 requires-expressions --------------------------------------
 *
 * `requires { reqs }` and `requires (params) { reqs }` are boolean
 * constant expressions that hold when every requirement is well-formed.
 * The expression is buffered as a token span (so a failed trial leaves
 * the source stream positioned after the whole expression), then each
 * requirement is checked in a trial parse: an ill-formed requirement
 * makes the whole expression false instead of a hard error.
 *
 * Requirement kinds:
 *   simple       `a + a;`                      (expression must parse)
 *   type         `typename T::value_type;`     (type must name a type)
 *   compound     `{ e } -> Concept<U>;`        (e parses; Concept<U> holds
 *                                               for decltype(e))
 *   nested       `requires Concept<T>;`        (constraint must hold)
 */

/* Copy the requires-expression token span starting at the current token
 * (`requires` keyword) through its closing '}'.  Nested braces / parens
 * (compound-requirement bodies, nested requires, lambda bodies in
 * requirements) are tracked so the outer '}' is found correctly.  Fills
 * *out with the tokens (the caller owns the allocation) and advances the
 * stream past the closing '}'; returns the number of tokens. */
size_t
cpp_requires_span_len(struct token **out)
{
	int depth = 0, req_brace = 0;
	size_t n = 0, cap = 16;
	struct token *sp = xmalloc(cap * sizeof *sp);

	for (;;) {
		if (tok.kind == TEOF)
			break;
		if (n >= cap) {
			cap *= 2;
			sp = xreallocarray(sp, cap, sizeof *sp);
		}
		sp[n++] = tok;
		if (depth == 0 && tok.kind == TLBRACE) {
			if (req_brace) {
				++depth;
			} else {
				/* the body's opening brace */
				req_brace = 1;
				++depth;
			}
		} else if (depth > 0) {
			if (tok.kind == TLBRACE)
				++depth;
			else if (tok.kind == TRBRACE && --depth == 0)
				break;
		}
		next();
	}
	*out = sp;
	return n;
}

/* The token `t` is a `typename`/`class` (C++ tag) keyword? */
static bool
cpp_is_tag_kw(enum tokenkind k)
{
	return cpp_tok_kind() == CPP_TTYPENAME || cpp_tok_kind() == CPP_TCLASS;
}

/* Is `name` (an identifier) usable as a type in `s` — i.e. a typedef /
 * template parameter / class name?  Used by the type-requirement fallback
 * that checks `T::member` where T is a template parameter. */
static bool
cpp_ident_is_type(struct scope *s, const char *name)
{
	struct decl *d = scopegetdecl(s, name, 1);
	struct type *t;

	if (d && d->kind == DECLTYPE)
		return true;
	t = scopegettag(s, name, 1);
	if (t && (t->kind == TYPESTRUCT || t->kind == TYPEUNION))
		return true;
	/* a namespace is not a type */
	if (d && d->kind == DECLNAMESPACE)
		return false;
	return false;
}

/* Type requirement `typename X` / `typename T::value_type`: check that
 * the tokens name a valid type.  `typename()` handles the non-dependent
 * cases (`typename int`, `typename MyClass`); for `T::member` (T a
 * template parameter bound to a concrete class in `s`) we check that
 * member is a typedef of that class.  Returns true when satisfied. */
static bool
cpp_req_type_ok(struct scope *s, struct token *sp, size_t n)
{
	struct expr *toeval = NULL;
	enum typequal tq = QUALNONE;
	struct type *t = NULL;

	/* skip the `typename` keyword (not a C type specifier) */
	if (n > 0 && cpp_classify_token(sp[0]) == CPP_TTYPENAME) {
		sp++;
		n--;
	}
	if (n == 0)
		return false;

	/* `T :: member` with T a known class: the member is a nested
	 * typedef of the bound class.  (Higher levels — `A::B::member` or
	 * `T::U::member` — are not handled yet.)  m++ stores struct members
	 * (including in-class typedefs) in the aggregate member list, so look
	 * the name up there rather than in the class scope. */
	if (n >= 3 && sp[1].kind == TCOLONCOLON &&
	    cpp_ident_is_type(s, tokenstr(sp[0].kind))) {
		struct decl *td = scopegetdecl(s, tokenstr(sp[0].kind), 1);
		struct type *ct;
		if (td && td->kind == DECLTYPE)
			ct = td->type;
		else
			ct = scopegettag(s, tokenstr(sp[0].kind), 1);
		if (ct && (ct->kind == TYPESTRUCT || ct->kind == TYPEUNION)) {
			unsigned long long off = 0;
			return typemember(ct, tokenstr(sp[2].kind), &off) != NULL;
		}
		return false;
	}

	/* replay the span and parse it as a type-name; errors longjmp to the
	 * enclosing trial and report failure.  The parse must consume the
	 * whole span: `typename void::value_type` parses `void` and leaves
	 * `::value_type`, which must not count as naming a type. */
	{
		size_t d = tokctx_depth();
		struct token guard = {0};
		bool complete;
		guard.kind = TSEMICOLON;
		tokpush(&guard, 1);
		tokpush(sp, n);
		next();
		t = typename(s, &tq, &toeval);
		complete = tok.kind == TSEMICOLON;
		(void)tq; (void)toeval;
		tokctx_rewind(d);
		if (!complete)
			return false;
	}
	return t != NULL;
}

/* Parse `(param1, param2, ...)` into the requires scope `rs`.  The whole
 * parameter clause `sp[0..n)` (starting at '(') is replayed so each
 * parameter becomes a scope entry the requirement body can reference; an
 * ill-formed list makes the whole requires-expression false (the enclosing
 * trial catches the error).  Returns the span offset just past the ')'. */
static size_t
cpp_req_params(struct scope *rs, struct token *sp, size_t n)
{
	size_t i = 1;

	if (n == 0 || sp[0].kind != TLPAREN)
		return 0; /* no parameter clause */
	{
		size_t d = tokctx_depth();
		struct token guard = {0};
		guard.kind = TSEMICOLON;
		tokpush(&guard, 1);
		tokpush(sp, n);
		next(); /* tok = '(' */
		next(); /* skip '('; parameter() parses each parameter */
		while (tok.kind != TRPAREN) {
			struct decl *pd;
			pd = parameter(rs);
			/* parameter() returns the decl; register it in the requires
			 * scope so the requirement body can reference it */
			if (pd && pd->name)
				scopeputdecl(rs, pd);
			if (tok.kind == TRPAREN)
				break;
			expect(TCOMMA, "or ')' in requires parameter list");
		}
		next(); /* consume ')' */
		tokctx_rewind(d);
	}
	/* number of span tokens consumed: everything through the matching ')' */
	{
		int d = 0;
		for (; i < n; i++) {
			if (sp[i].kind == TLPAREN || sp[i].kind == TLESS ||
			    sp[i].kind == TLBRACK)
				++d;
			else if (sp[i].kind == TRPAREN || sp[i].kind == TGREATER ||
			    sp[i].kind == TRBRACK) {
				if (d > 0)
					--d;
				else if (sp[i].kind == TRPAREN)
					break;
			}
		}
	}
	return i < n ? i + 1 : n;
}

/* Trial-parse a single requirement span (simple expression).  Returns
 * true when the expression parses without an error. */
static bool
cpp_req_simple(struct scope *rs, struct token *sp, size_t n)
{
	struct expr *e;
	size_t d = tokctx_depth();
	struct token guard = {0};

	/* a guard after the requirement span: the outer requires-expression
	 * span is still buffered below in the token context, and an
	 * expression parser must not fall through to its leftover tokens */
	guard.kind = TSEMICOLON;
	tokpush(&guard, 1);
	tokpush(sp, n);
	next();
	e = expr(rs);
	tokctx_rewind(d);
	return e != NULL;
}

/* Compound requirement `{ e } -> Constraint` / `{ e }`: validate e and an
 * optional trailing constraint (a concept-id or a type) against decltype(e). */
static bool
cpp_req_compound(struct scope *rs, struct token *sp, size_t n)
{
	struct expr *e;
	size_t close = 0;
	int d = 0;
	size_t i;

	if (n == 0 || sp[0].kind != TLBRACE)
		return false;
	/* find the matching '}' of the braced expression */
	for (i = 1; i < n; i++) {
		if (sp[i].kind == TLBRACE)
			++d;
		else if (sp[i].kind == TRBRACE) {
			if (d == 0) {
				close = i;
				break;
			}
			--d;
		}
	}
	if (close == 0)
		return false;

	/* the braced expression must parse (inner expression, excluding the
	 * enclosing braces) */
	{
		size_t d2 = tokctx_depth();
		struct token guard = {0};
		guard.kind = TSEMICOLON;
		tokpush(&guard, 1);
		tokpush(sp + 1, close - 1 > 0 ? close - 1 : 0);
		next();
		e = expr(rs);
		tokctx_rewind(d2);
	}
	if (!e)
		return false;

	/* optional `noexcept` specifier, then optional `-> TypeConstraint` */
	{
		size_t j = close + 1;
		/* `noexcept` or `noexcept(...)` after the braced expression:
		 * a compound requirement may require the expression be
		 * non-throwing.  We only need to skip it syntactically here
		 * (the well-formedness check is the expression parse above). */
		if (j < n && sp[j].kind == CPP_TNOEXCEPT) {
			j++;
			if (j < n && sp[j].kind == TLPAREN) {
				int nd = 1;
				j++;
				while (j < n && nd > 0) {
					if (sp[j].kind == TLPAREN)
						++nd;
					else if (sp[j].kind == TRPAREN &&
					    --nd == 0) {
						j++;
						break;
					}
					j++;
				}
			}
		}
		/* `{ e }` or `{ e } noexcept` : satisfied if the expression
		 * parsed (already checked above); no return-type constraint. */
		if (j >= n || sp[j].kind != TARROW)
			return true;
		size_t cn = n - (j + 1);
		struct token *ct = &sp[j + 1];
		/* a concept-id constraint: `C<T>` / `C<T1, T2>` */
		if (cn >= 1 && ct[0].kind >= TIDENT) {
			struct cpp_template *con;
			for (con = g_cpp_templates; con; con = con->next)
				if (con->is_concept && con->nparams > 0 &&
				    strcmp(con->name, tokenstr(ct[0].kind)) == 0)
					break;
			if (con && cn >= 3 && ct[1].kind == TLESS) {
				/* evaluate `C<decltype(e)>` */
				char iname[64];
				struct token *ntok;
				size_t k;
				struct token tc;
				struct decl *td;
				int nd = 0;
				snprintf(iname, sizeof iname, "__req%d",
				    ++g_cpp_lambda_count);
				/* a placeholder DECLTYPE decl in the requires
				 * scope: the constraint's first argument becomes
				 * the expression type */
				td = mkdecl(iname, DECLTYPE, e->type,
				    QUALNONE, LINKNONE);
				scopeputdecl(rs, td);
				ntok = xmalloc(cn * sizeof *ntok);
				memcpy(ntok, ct, cn * sizeof *ntok);
				ntok[2].kind = tokenget(iname, strlen(iname));
				/* validate the span is a plain concept-id */
				for (k = 3; k < cn; k++) {
					if (ntok[k].kind == TLESS)
						++nd;
					else if (ntok[k].kind == TGREATER) {
						if (nd == 0)
							break;
						--nd;
					} else if (nd == 0 &&
					    (ntok[k].kind == TSEMICOLON ||
					     ntok[k].kind == TRBRACE))
						break;
				}
				if (k == cn) {
					bool r = eval_concept_use(ntok, cn, rs);
					free(ntok);
					return r;
				}				free(ntok);
				return false;
			} else if (con) {
				/* `-> Concept` shorthand: a one-place concept
				 * constrained by the expression type:
				 * `Concept<decltype(e)>`. */
				struct token ntok[4] = {0};
				char iname[64];
				struct decl *td;
				snprintf(iname, sizeof iname, "__req%d",
				    ++g_cpp_lambda_count);
				td = mkdecl(iname, DECLTYPE, e->type,
				    QUALNONE, LINKNONE);
				scopeputdecl(rs, td);
				ntok[0] = ct[0];
				ntok[1].kind = TLESS;
				ntok[1].loc = ct[0].loc;
				ntok[2].kind = tokenget(iname, strlen(iname));
				ntok[2].loc = ct[0].loc;
				ntok[3].kind = TGREATER;
				ntok[3].loc = ct[0].loc;
				return eval_concept_use(ntok, 4, rs);
			}
		}
		/* `-> decltype(expr)` : a return-type requirement whose
		 * type-constraint is a decltype-specifier.  The compound
		 * expression's type must match the decltype'd expression's
		 * type (`{ e } -> decltype(e2)` holds iff the two types are
		 * the same).  We evaluate `e2` in the requires scope (where the
		 * parameters are visible) and compare types. */
		if (cn >= 1 && cpp_classify_token(ct[0]) == CPP_TDECLTYPE) {
			size_t k = 1, close = 0;
			int d = 0;
			if (cn < 3 || ct[1].kind != TLPAREN)
				return false;
			for (k = 1; k < cn; k++) {
				if (ct[k].kind == TLPAREN)
					++d;
				else if (ct[k].kind == TRPAREN && --d == 0) {
					close = k;
					break;
				}
			}
			if (close == 0)
				return false;
			{
				extern struct expr *expr(struct scope *);
				size_t d2 = tokctx_depth();
				struct token guard = {0};
				struct expr *e2;
				guard.kind = TSEMICOLON;
				tokpush(&guard, 1);
				tokpush(ct + 2, close - 2);
				next();
				e2 = expr(rs);
				tokctx_rewind(d2);
				if (!e2 || !e2->type)
					return false;
				return typesame(e->type, e2->type);
			}
		}
		/* otherwise a type constraint: the type must be valid */
		return cpp_req_type_ok(rs, ct, cn);
	}
}

/* Split a requires body `{ r1; r2; ... }` into requirement spans.
 * `body` excludes the outer braces.  Each span ends at (and excludes) the
 * terminating ';'.  Returns the number of spans and fills *spans (caller
 * frees); *nspans holds the count. */
static size_t *
cpp_req_split(struct token *body, size_t nbody, size_t *nspans)
{
	size_t *spans = NULL, ns = 0, cap = 0, start = 0;
	int d = 0;
	size_t i;

	for (i = 0; i <= nbody; i++) {
		if (i == nbody || (d == 0 && body[i].kind == TSEMICOLON)) {
			size_t len = i - start;
			if (len > 0) {
				if (ns + 2 > cap) {
					cap = cap ? cap * 2 : 8;
					spans = xreallocarray(spans, cap, sizeof *spans);
				}
				spans[ns++] = start;
				spans[ns++] = len;
			}
			start = i + 1;
			continue;
		}
		if (body[i].kind == TLBRACE)
			++d;
		else if (body[i].kind == TRBRACE && d > 0)
			--d;
	}
	*nspans = ns / 2;
	return spans;
}

/* Evaluate a buffered requires-expression.  `sp[0..n)` spans the whole
 * expression including the `requires` keyword.  Returns true when all
 * requirements are satisfied. */
static bool
cpp_requires_eval(struct scope *s, struct token *sp, size_t n)
{
	struct scope *rs;
	size_t i, nbody = 0;
	struct token *body;
	bool ok;

	if (n == 0)
		return false;
	/* `requires` keyword */
	i = 1;
	if (i < n && sp[i].kind == TLPAREN)
		/* cpp_req_params returns the sub-span offset just past the ')';
		 * the whole-span index is 1 (for the `requires` keyword) plus it */
		i = 1 + cpp_req_params(s, &sp[i], n - i);
	if (i >= n || sp[i].kind != TLBRACE)
		return false; /* malformed: no requirement body */
	++i; /* skip '{' */
	body = &sp[i];
	nbody = n - i - 1; /* exclude the trailing '}' */

	/* a trial scope for the parameters; discarded after the check */
	rs = mkscope(s);
	ok = true;
	if (nbody > 0) {
		size_t nspans, *spans = cpp_req_split(body, nbody, &nspans);
		size_t k;
		for (k = 0; k < nspans; k++) {
			struct token *rq = &body[spans[2 * k]];
			size_t rn = spans[2 * k + 1];
			bool r;
			if (rn == 0)
				continue;
			if (cpp_classify_token(rq[0]) == CPP_TTYPENAME)
				r = cpp_req_type_ok(rs, rq, rn);
			else if (rq[0].kind == TLBRACE)
				r = cpp_req_compound(rs, rq, rn);
			else if (cpp_classify_token(rq[0]) == CPP_TREQUIRES)
				/* nested requirement: `requires C<T>;` */
				r = eval_constraint(&rq[1], rn - 1, rs);
			else
				r = cpp_req_simple(rs, rq, rn);
			if (!r) {
				ok = false;
				break;
			}
		}
		free(spans);
	}
	delscope(rs);
	return ok;
}

/* Entry point: parse a requires-expression at the current token and
 * return its boolean constant value.  Consumes the whole expression.
 *
 * The expression is buffered as a token span, then the span is replayed
 * (pushed onto the token context) for evaluation under a trial.  On both
 * success and failure the token context is restored to its pre-replay
 * depth, so the caller (which has already consumed the requires-expression
 * tokens) resumes at the token after the expression regardless of what the
 * trial did. */
struct expr *
cpp_requires_expr(struct scope *s)
{
	struct token *sp;
	struct token after;
	size_t n, depth;
	bool result;
	struct expr *e;
	jmp_buf env;

	/* buffer the whole expression first so a failed check still leaves
	 * the stream positioned after it */
	n = cpp_requires_span_len(&sp);
	if (n == 0)
		error_code(E_CTYPE, &tok.loc, "malformed requires-expression");
	/* `after` is the first token past the requires-expression; the caller
	 * resumes here.  Captured before the trial, because a failed trial
	 * may leave the global token anywhere. */
	next(); /* advance past the closing '}' to the token after it */
	after = tok;
	depth = tokctx_depth();

	/* evaluate the buffered expression under a trial: replay the span,
	 * then restore the context; any parse/type error makes it false.
	 * cpp_trial_begin/end save and restore the enclosing trial's jump
	 * buffer (and depth) so a failed trial unwinds here with result false
	 * rather than rethrowing into the caller. */
	if (setjmp(env) == 0) {
		cpp_trial_begin(env);
		tokpush(sp, n);
		next();
		result = cpp_requires_eval(s, sp, n);
		cpp_trial_end(env);
	} else {
		/* an error was raised inside the trial; restore the trial
		 * bookkeeping and report the expression as false. */
		cpp_trial_end(env);
		result = false;
	}
	/* restore the stream to just past the requires-expression: discard
	 * the trial's replayed tokens and set the global token to the one
	 * after the expression (the trial never advanced the source stream,
	 * so `after` is still the next source token). */
	tokctx_rewind(depth);
	tok = after;
	free(sp);

	e = mkexpr(EXPRCONST, &typebool, NULL);
	e->u.constant.u = result;
	return e;
}
