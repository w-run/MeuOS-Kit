/* cpp_constexpr.c - C++ constexpr function evaluation (compile-time
 * folding of constant expressions, `if constexpr`, static_assert).
 *
 * Split out of cpp_parse.c (phase 1/2 of the large-file layering
 * refactor).  This module owns the constexpr interpreter: it replays a
 * buffered `{ return <expr> ; }` body with argument values bound, folds
 * constant expressions, and evaluates `if constexpr` / `if consteval`.
 *
 * Only the genuinely shared globals (g_cpp_cexpr_tmpl_*) are promoted
 * to cpp_internal.h; all per-evaluator state stays static here. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "mcc.h"
#include "cpp.h"
#include "cpp_internal.h"
#include "../../c/parse/decl_internal.h"
#include "../../c/parse/expr_internal.h"

/* Skip a not-taken `if`/`while`/`for` branch (used by the constexpr
 * evaluator and `if constexpr`).  Defined below; declared here so the
 * constexpr functions below it can call it. */
/* --- C++ constexpr functions (compile-time evaluation, phase 1/2) ------- */

/* A constexpr function whose body is buffered so a constant-context call
 * (`constexpr int v = sq(5);`, static_assert) can be folded by replaying
 * `{ return <expr> ; }` with the argument values bound. */
struct cpp_cexpr_fn {
	struct decl *fd;         /* the constexpr function decl */
	char **params;           /* parameter names */
	struct type **ptypes;    /* parameter types */
	int nparams;
	struct token *toks;      /* `{ return <expr> ; }` body tokens */
	size_t ntoks;
	/* Template parameter type bindings for a constexpr *template* function
	 * instantiation.  The body was buffered at instantiation time with the
	 * template parameter token (e.g. `T`) still literal, so the constant
	 * evaluator must re-bind each parameter name to its instantiated type
	 * (as a DECLTYPE) before replaying the body — otherwise `sizeof(T)` and
	 * other type usages of `T` fail with "undeclared identifier".  NULL /
	 * 0 for non-template constexpr functions. */
	const char **tmpl_params;
	struct type **tmpl_types;
	/* Non-type template parameter values bound at instantiation
	 * (`template<int N>`): tmpl_isval[i] marks a value parameter, and
	 * tmpl_vals[i] holds its integer value.  The evaluator re-binds
	 * value parameters as integer constants (DECLCONST). */
	unsigned long long *tmpl_vals;
	bool *tmpl_isval;
	int ntmpl;
	struct cpp_cexpr_fn *next;
};

static struct cpp_cexpr_fn *g_cpp_cexpr_fns;

/* Mini memory model for C++ constexpr aggregate objects: each initialized
 * member of a constexpr object records (object, byte-offset) -> value so
 * a constant-context member access `*(&s + offset)` can be folded
 * (phase-3 constexpr relaxation).  Populated by
 * cpp_record_cexpr_aggregate at the constexpr object's definition. */
struct cexp_obj_member {
	struct decl *obj;
	unsigned long long offset;
	unsigned long long val;
	struct cexp_obj_member *next;
};
static struct cexp_obj_member *g_cexp_obj_members;

/* Look up the stored constant value of `obj`'s member at `offset`, or
 * false when the object/member is not a recorded constexpr value. */
bool
cpp_cexpr_member_value(struct decl *obj, unsigned long long offset,
                       unsigned long long *out)
{
	struct cexp_obj_member *m;
	for (m = g_cexp_obj_members; m; m = m->next)
		if (m->obj == obj && m->offset == offset) {
			*out = m->val;
			return true;
		}
	return false;
}

/* Record the member values of a constexpr aggregate object from its
 * initializer list (`constexpr P p{1, 2}`).  Each init node spans
 * [start, end) bytes of the object and holds one element's expression. */
void
cpp_record_cexpr_aggregate(struct decl *d, struct init *init)
{
	extern struct expr *eval(struct expr *);
	struct init *it;

	if (!d || d->kind != DECLOBJECT || !init)
		return;
	for (it = init; it; it = it->next) {
		struct expr *e;
		if (!it->expr)
			continue;
		e = eval(it->expr);
		if (!e || e->kind != EXPRCONST || !(e->type->prop & PROPINT))
			continue;
		{
			struct cexp_obj_member *m = xmalloc(sizeof *m);
			m->obj = d;
			m->offset = it->start;
			m->val = e->u.constant.u;
			m->next = g_cexp_obj_members;
			g_cexp_obj_members = m;
		}
	}
}
/* Member values of a class object returned by a constexpr function call
 * (`constexpr P make_p(int x) { ... return p; }`, then `make_p(3).a`).
 * Keyed by the call expression node; recorded by cpp_constexpr_eval and
 * consulted by eval()'s member-access folding. */
struct cexp_ret_member {
	struct expr *call;
	unsigned long long offset;
	unsigned long long val;
	struct cexp_ret_member *next;
};
static struct cexp_ret_member *g_cexp_ret_members;

/* Look up a member of a constexpr call's class return value. */
bool
cpp_cexpr_ret_member_value(struct expr *call, unsigned long long offset,
                           unsigned long long *out)
{
	struct cexp_ret_member *m;
	for (m = g_cexp_ret_members; m; m = m->next)
		if (m->call == call && m->offset == offset) {
			*out = m->val;
			return true;
		}
	return false;
}

/* Record the members of `obj` (a class object whose aggregate members were
 * captured) as the return value of `call`. */
void
cpp_record_cexpr_return(struct expr *call, struct decl *obj)
{
	struct cexp_obj_member *m;
	for (m = g_cexp_obj_members; m; m = m->next)
		if (m->obj == obj) {
			struct cexp_ret_member *rm = xmalloc(sizeof *rm);
			rm->call = call;
			rm->offset = m->offset;
			rm->val = m->val;
			rm->next = g_cexp_ret_members;
			g_cexp_ret_members = rm;
		}
}

/* Copy the member values recorded for a constexpr call's class return
 * (`make_p(5)` -> `constexpr P q = make_p(5)`) onto `dst`'s mini-memory
 * entries, so `q.b` can fold.  Returns true when any member was copied. */
bool
cpp_copy_cexpr_return(struct expr *call, struct decl *dst)
{
	struct cexp_ret_member *m;
	bool any = false;
	if (!dst || dst->kind != DECLOBJECT)
		return false;
	for (m = g_cexp_ret_members; m; m = m->next)
		if (m->call == call) {
			struct cexp_obj_member *nm = xmalloc(sizeof *nm);
			nm->obj = dst;
			nm->offset = m->offset;
			nm->val = m->val;
			nm->next = g_cexp_obj_members;
			g_cexp_obj_members = nm;
			any = true;
		}
	return any;
}

int g_cpp_cexpr_depth;   /* recursion limit */

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

/* --- C++23 constexpr statement interpreter (P2242 multi-statement bodies)
 *
 * `cpp_constexpr_eval` replays a constexpr function's body tokens with the
 * argument values bound and folds `{ return <expr>; }`.  This interpreter
 * extends that to full statement sequences: local integer variables,
 * if/else, while/do/for loops, multiple returns, break/continue, and
 * assignments / ++-- to the local variables (including the compound
 * `a += b` lowering via an EXPRTEMP).  Anything it cannot fold to an
 * integer constant degrades to a normal runtime call (CEXP_FAIL), matching
 * the project's lenient constexpr/consteval philosophy.
 *
 * The interpreter shares the token-replay machinery: loop bodies are
 * buffered with brace balance and re-pushed per iteration; the token
 * context is rewound on exit so the caller's parse position is preserved.
 * Locals and parameters are bound as DECLOBJECT objects whose integer
 * value lives in u.obj.constval, so they parse as lvalues (for ++/=
 * statements) and are read live (never frozen in place, unlike eval()),
 * letting loop conditions see mutations. */

/* statement-interpretation status */
#define CEXP_OK      0
#define CEXP_RET     1
#define CEXP_FAIL    2
#define CEXP_BREAK   3
#define CEXP_CONT    4

/* per-evaluation loop-step budget: guards against compile-time hangs */
#define CEXP_MAX_STEPS 100000

/* Set by the RETURN statement interpreter when the returned value is a
 * class-typed local object whose aggregate members were captured; consumed
 * by cpp_constexpr_eval to record the call's class return value. */
static struct decl *g_cexpr_class_ret;

static int cpp_cexpr_stmt(struct scope *tmp, unsigned long long *ret,
                          int *steps);

/* Copy the current token into a growing buffer, keeping a private copy of
 * the literal, and advance. */
static void
cexp_tok_push(struct token **toks, size_t *cap, size_t *n)
{
	struct token t = tok;

	if (*n >= *cap) {
		*cap = *cap ? *cap * 2 : 16;
		*toks = xreallocarray(*toks, *cap, sizeof **toks);
	}
	(*toks)[*n] = t;
	if (t.lit)
		(*toks)[*n].lit = strdup(t.lit);
	(*n)++;
	next();
}

/* Copy a parenthesized `( ... )` group (balanced parens) into the buffer. */
static void
cexp_copy_paren(struct token **toks, size_t *cap, size_t *n)
{
	int pd = 0;

	do {
		if (tok.kind == TLPAREN)
			++pd;
		else if (tok.kind == TRPAREN)
			--pd;
		cexp_tok_push(toks, cap, n);
	} while (pd > 0 && tok.kind != TEOF);
}

/* Buffer one statement's tokens from the stream (starting at `tok`),
 * consuming them.  Handles compound `{...}`, control statements
 * (if/while/do/for with paren-balanced headers and recursive bodies), and
 * simple statements up to `;`.  The copy is suitable for tokpush() replay. */
static void cexp_buffer_stmt_rec(struct token **toks, size_t *cap, size_t *n);

static void
cexp_buffer_stmt(struct token **out, size_t *nout)
{
	struct token *toks = NULL;
	size_t cap = 0, n = 0;

	cexp_buffer_stmt_rec(&toks, &cap, &n);
	*out = toks;
	*nout = n;
}

/* recursive core: append one statement to the growing buffer */
static void
cexp_buffer_stmt_rec(struct token **toks, size_t *cap, size_t *n)
{
	if (tok.kind == TLBRACE) {
		int bd = 1;
		cexp_tok_push(toks, cap, n); /* '{' */
		while (bd > 0 && tok.kind != TEOF) {
			if (tok.kind == TLBRACE)
				++bd;
			else if (tok.kind == TRBRACE)
				--bd;
			cexp_tok_push(toks, cap, n);
		}
	} else if (tok.kind == TIF) {
		cexp_tok_push(toks, cap, n); /* 'if' */
		if (tok.kind == TCONSTEXPR)
			cexp_tok_push(toks, cap, n); /* 'if constexpr' */
		cexp_copy_paren(toks, cap, n);
		cexp_buffer_stmt_rec(toks, cap, n);  /* then branch */
		if (tok.kind == TELSE) {
			cexp_tok_push(toks, cap, n); /* 'else' */
			cexp_buffer_stmt_rec(toks, cap, n); /* else branch */
		}
	} else if (tok.kind == TWHILE || tok.kind == TFOR) {
		cexp_tok_push(toks, cap, n); /* keyword */
		cexp_copy_paren(toks, cap, n);
		cexp_buffer_stmt_rec(toks, cap, n); /* body */
	} else if (tok.kind == TDO) {
		cexp_tok_push(toks, cap, n); /* 'do' */
		cexp_buffer_stmt_rec(toks, cap, n); /* body */
		cexp_tok_push(toks, cap, n); /* 'while' */
		cexp_copy_paren(toks, cap, n);
		cexp_tok_push(toks, cap, n); /* ';' */
	} else {
		/* simple statement up to ';' */
		do {
			cexp_tok_push(toks, cap, n);
		} while (tok.kind != TSEMICOLON && tok.kind != TRBRACE &&
		         tok.kind != TEOF);
		if (tok.kind == TSEMICOLON)
			cexp_tok_push(toks, cap, n);
	}
}

/* Resolve an lvalue expression to the local integer variable it refers to
 * (a mutable DECLOBJECT bound by the interpreter, or a DECLCONST enum
 * constant), or NULL.  `temp` is the EXPRTEMP target for `*tmp`. */
static struct decl *
cpp_cexpr_lval(struct expr *l, struct decl *temp)
{
	if (l->kind == EXPRIDENT && l->u.ident.decl) {
		struct decl *d = l->u.ident.decl;
		if (d->kind == DECLCONST ||
		    (d->kind == DECLOBJECT && d->u.obj.has_constval))
			return d;
		return NULL;
	}
	if (l->kind == EXPRUNARY && l->op == TMUL && l->base &&
	    l->base->kind == EXPRTEMP)
		return temp;
	return NULL;
}

/* Recognize a scalar member access lvalue `*( (unsigned long)&obj + off )`
 * inside the constant-expression interpreter.  On success returns the
 * base struct/union object decl and its member's byte offset:
 *   lhs  = EXPRUNARY TMUL                            (* ...)
 *          base = EXPRBINARY TADD                    (addr + off)
 *                   l = EXPRCAST(to unsigned long)   (unsigned long)(...)
 *                       base = EXPRUNARY TBAND       (&obj)
 *                                base = EXPRIDENT    (obj)
 *                   r = EXPRCONST(off)
 * Returns NULL when `l` is not such a member access. */
static struct decl *
cpp_cexpr_member_lval(struct expr *l, unsigned long long *offset_out)
{
	struct expr *addr, *off;
	if (!l || l->kind != EXPRUNARY || l->op != TMUL || !l->base)
		return NULL;
	if (l->base->kind != EXPRBINARY || l->base->op != TADD)
		return NULL;
	addr = l->base->u.binary.l;
	off = l->base->u.binary.r;
	if (!off || off->kind != EXPRCONST || !(off->type->prop & PROPINT))
		return NULL;
	/* `(unsigned long)&obj` — the cast wraps a `&obj` unary */
	if (addr->kind != EXPRCAST || !addr->base)
		return NULL;
	if (addr->base->kind != EXPRUNARY || addr->base->op != TBAND ||
	    !addr->base->base)
		return NULL;
	if (addr->base->base->kind != EXPRIDENT || !addr->base->base->u.ident.decl)
		return NULL;
	{
		struct decl *d = addr->base->base->u.ident.decl;
		if (d->kind != DECLOBJECT)
			return NULL;
		*offset_out = off->u.constant.u;
		return d;
	}
}

/* Record a constant member value into a constexpr aggregate object's mini
 * memory model (so a later member access / return of the object folds it).
 * `obj`/`offset` identify the member; `val` is its constant value. */
void
cpp_record_cexpr_member(struct decl *obj, unsigned long long offset,
                        unsigned long long val)
{
	struct cexp_obj_member *m = xmalloc(sizeof *m);
	m->obj = obj;
	m->offset = offset;
	m->val = val;
	m->next = g_cexp_obj_members;
	g_cexp_obj_members = m;
}

/* Read/write the interpreter's integer value slot of a bound variable
 * (DECLOBJECT constval for locals/params, DECLCONST enumconst for
 * constants). */
static unsigned long long
cexp_var_get(struct decl *d)
{
	return d->kind == DECLCONST ? d->u.enumconst : d->u.obj.constval;
}

static void
cexp_var_set(struct decl *d, unsigned long long v)
{
	if (d->kind == DECLCONST)
		d->u.enumconst = v;
	else
		d->u.obj.constval = v;
}

/* Apply an integer binary operator, honoring the type's signedness (C
 * division truncates toward zero; unsigned ops wrap).  Returns false on
 * unsupported operators or division by zero. */
static bool
cpp_cexpr_binary(enum tokenkind op, struct type *t,
                 unsigned long long lv, unsigned long long rv,
                 unsigned long long *ret)
{
	bool sgn = t && (t->prop & PROPINT) && t->u.arith.issigned;
	long long l = (long long)lv, r = (long long)rv;

	switch (op) {
	case TMUL:  *ret = sgn ? (unsigned long long)(l * r) : lv * rv; break;
	case TDIV:
		if (rv == 0)
			return false;
		*ret = sgn ? (unsigned long long)(l / r) : lv / rv;
		break;
	case TMOD:
		if (rv == 0)
			return false;
		*ret = sgn ? (unsigned long long)(l % r) : lv % rv;
		break;
	case TADD:  *ret = sgn ? (unsigned long long)(l + r) : lv + rv; break;
	case TSUB:  *ret = sgn ? (unsigned long long)(l - r) : lv - rv; break;
	case TSHL:  *ret = lv << (rv & 63); break;
	case TSHR:
		*ret = sgn ? (unsigned long long)(l >> (rv & 63)) : lv >> (rv & 63);
		break;
	case TBAND: *ret = lv & rv; break;
	case TBOR:  *ret = lv | rv; break;
	case TXOR:  *ret = lv ^ rv; break;
	case TLESS:
		*ret = sgn ? (unsigned long long)(l < r) : (unsigned long long)(lv < rv);
		break;
	case TGREATER:
		*ret = sgn ? (unsigned long long)(l > r) : (unsigned long long)(lv > rv);
		break;
	case TLEQ:
		*ret = sgn ? (unsigned long long)(l <= r) : (unsigned long long)(lv <= rv);
		break;
	case TGEQ:
		*ret = sgn ? (unsigned long long)(l >= r) : (unsigned long long)(lv >= rv);
		break;
	case TEQL: *ret = lv == rv; break;
	case TNEQ: *ret = lv != rv; break;
	default:   return false;
	}
	return true;
}

/* Evaluate a constexpr-compatible expression tree without mutating it (the
 * standard eval() folds DECLCONST identifiers in place, which would freeze
 * loop variables at their first value).  Handles assignments, ++/-- and
 * conditionals that mutate local integer variables, and the compound
 * assignment `tmp = &local; *tmp op= rhs` lowering.  `temp` carries the
 * EXPRTEMP target decl across the compound-assign two-instruction pair.
 * Returns true and sets *ret when the expression folds to an integer. */
static bool
cpp_cexpr_value(struct expr *e, struct decl **temp, unsigned long long *ret)
{
	struct decl *cd;
	unsigned long long lv, rv;

	if (!e)
		return false;

	/* `*tmp` dereferences the compound-assign temp's target */
	if (e->kind == EXPRUNARY && e->op == TMUL && e->base &&
	    e->base->kind == EXPRTEMP) {
		if (!*temp)
			return false;
		*ret = cexp_var_get(*temp);
		return true;
	}
	if (e->kind == EXPRIDENT && e->u.ident.decl) {
		struct decl *d = e->u.ident.decl;
		if (d->kind == DECLCONST) {
			*ret = d->u.enumconst;
			return true;
		}
		/* local/param or global `constexpr` variable (folded value) */
		if (d->kind == DECLOBJECT && d->u.obj.has_constval) {
			*ret = d->u.obj.constval;
			return true;
		}
		return false;
	}
	if (e->kind == EXPRCONST && (e->type->prop & PROPINT)) {
		*ret = e->u.constant.u;
		return true;
	}
	switch (e->kind) {
	case EXPRUNARY:
		if (e->op == TSUB) {
			if (!cpp_cexpr_value(e->base, temp, &lv))
				return false;
			*ret = (unsigned long long)(-(long long)lv);
			return true;
		}
		return false;
	case EXPRCAST:
		return cpp_cexpr_value(e->base, temp, ret);
	case EXPRBINARY:
		if (!cpp_cexpr_value(e->u.binary.l, temp, &lv))
			return false;
		if (e->op == TLOR) {
			if (lv) {
				*ret = 1;
				return true;
			}
			if (!cpp_cexpr_value(e->u.binary.r, temp, &rv))
				return false;
			*ret = rv ? 1 : 0;
			return true;
		}
		if (e->op == TLAND) {
			if (!lv) {
				*ret = 0;
				return true;
			}
			if (!cpp_cexpr_value(e->u.binary.r, temp, &rv))
				return false;
			*ret = rv ? 1 : 0;
			return true;
		}
		if (!cpp_cexpr_value(e->u.binary.r, temp, &rv))
			return false;
		return cpp_cexpr_binary(e->op, e->type, lv, rv, ret);
	case EXPRCOND:
		if (!cpp_cexpr_value(e->base, temp, &lv))
			return false;
		return cpp_cexpr_value(lv ? e->u.cond.t : e->u.cond.f, temp, ret);
	case EXPRASSIGN: {
		cd = cpp_cexpr_lval(e->u.assign.l, *temp);
		if (cd && (cd->kind == DECLCONST ||
		    (cd->kind == DECLOBJECT && cd->u.obj.has_constval))) {
			if (!cpp_cexpr_value(e->u.assign.r, temp, ret))
				return false;
			cexp_var_set(cd, *ret);
			return true;
		}
		/* member-by-member assignment of a constexpr aggregate object
		 * (`p.x = 10;` in a constexpr function body): fold the RHS and
		 * record the member into the object's mini-memory so a later
		 * member access / return of `p` folds it. */
		{
			unsigned long long moff = 0;
			struct decl *obj = cpp_cexpr_member_lval(e->u.assign.l,
			    &moff);
			if (obj) {
				struct decl *mtemp = NULL;
				unsigned long long mval;
				if (cpp_cexpr_value(e->u.assign.r, &mtemp, &mval)) {
					cpp_record_cexpr_member(obj, moff, mval);
					return true;
				}
				return false;
			}
		}
		/* compound-assignment lowering: `tmp = &local` */
		if (e->u.assign.l->kind == EXPRTEMP &&
		    e->u.assign.r->kind == EXPRUNARY &&
		    e->u.assign.r->op == TBAND) {
			struct decl *t = cpp_cexpr_lval(e->u.assign.r->base, *temp);
			if (t && (t->kind == DECLCONST ||
			    (t->kind == DECLOBJECT && t->u.obj.has_constval))) {
				*temp = t;
				return true;
			}
		}
		return false;
	}
	case EXPRINCDEC:
		cd = cpp_cexpr_lval(e->base, *temp);
		if (!cd || (cd->kind != DECLCONST &&
		    !(cd->kind == DECLOBJECT && cd->u.obj.has_constval)))
			return false;
		*ret = cexp_var_get(cd);
		if (!e->u.incdec.post)
			*ret = e->op == TINC ? *ret + 1 : *ret - 1;
		cexp_var_set(cd, cexp_var_get(cd) + (e->op == TINC ? 1 : -1));
		return true;
	case EXPRCOMMA: {
		struct expr *it;
		for (it = e->base; it; it = it->next) {
			if (!cpp_cexpr_value(it, temp, ret))
				return false;
		}
		return true;
	}
	case EXPRCALL: {
		/* nested constexpr function call — fold via the evaluator */
		extern struct expr *cpp_constexpr_eval(struct expr *);
		extern void delexpr(struct expr *);
		struct expr *r = cpp_constexpr_eval(e);
		if (r) {
			*ret = r->u.constant.u;
			delexpr(r);
			return true;
		}
		return false;
	}
	default:
		return false;
	}
}

/* Interpret a `while (cond) body` loop. */
static int
cpp_cexpr_while(struct scope *tmp, unsigned long long *ret, int *steps)
{
	extern struct expr *expr(struct scope *);
	extern void tokpush(struct token *, size_t);
	extern size_t tokctx_depth(void);
	extern void tokctx_rewind(size_t);

	struct expr *cond;
	struct token *body = NULL;
	struct token cur;
	size_t nbody = 0;
	unsigned long long v;
	int st;

	next(); /* 'while' */
	expect(TLPAREN, "after 'while'");
	cond = expr(tmp);
	expect(TRPAREN, "after condition");
	cexp_buffer_stmt(&body, &nbody);
	cur = tok;
	for (;;) {
		if (++(*steps) > CEXP_MAX_STEPS)
			return CEXP_FAIL;
		{
			struct decl *temp = NULL;
			if (!cpp_cexpr_value(cond, &temp, &v))
				return CEXP_FAIL;
		}
		if (!v)
			break;
		{
			size_t base = tokctx_depth();
			tokpush(&cur, 1);
			tokpush(body, nbody);
			next();
			st = cpp_cexpr_stmt(tmp, ret, steps);
			tokctx_rewind(base);
			tok = cur;
		}
		if (st == CEXP_RET || st == CEXP_FAIL)
			return st;
		if (st == CEXP_BREAK)
			break;
		/* CEXP_OK / CEXP_CONT -> re-check the condition */
	}
	free(body);
	return CEXP_OK;
}

/* Interpret a `do body while (cond);` loop. */
static int
cpp_cexpr_dowhile(struct scope *tmp, unsigned long long *ret, int *steps)
{
	extern struct expr *expr(struct scope *);
	extern void tokpush(struct token *, size_t);
	extern size_t tokctx_depth(void);
	extern void tokctx_rewind(size_t);

	struct token *body = NULL;
	struct token cur;
	size_t nbody = 0;
	unsigned long long v;
	int st;
	struct expr *cond;

	next(); /* 'do' */
	cexp_buffer_stmt(&body, &nbody);
	expect(TWHILE, "after 'do' body");
	expect(TLPAREN, "after 'while'");
	cond = expr(tmp);
	expect(TRPAREN, "after condition");
	expect(TSEMICOLON, "after 'do' loop");
	cur = tok;
	for (;;) {
		if (++(*steps) > CEXP_MAX_STEPS)
			return CEXP_FAIL;
		{
			size_t base = tokctx_depth();
			tokpush(&cur, 1);
			tokpush(body, nbody);
			next();
			st = cpp_cexpr_stmt(tmp, ret, steps);
			tokctx_rewind(base);
			tok = cur;
		}
		if (st == CEXP_RET || st == CEXP_FAIL)
			return st;
		if (st == CEXP_BREAK)
			break;
		{
			struct decl *temp = NULL;
			if (!cpp_cexpr_value(cond, &temp, &v))
				return CEXP_FAIL;
		}
		if (!v)
			break;
	}
	free(body);
	return CEXP_OK;
}

/* Interpret a `for (init; cond; step) body` loop. */
static int
cpp_cexpr_for(struct scope *tmp, unsigned long long *ret, int *steps)
{
	extern struct expr *expr(struct scope *);

	struct expr *cond = NULL, *step = NULL;
	struct token *body = NULL;
	struct token cur;
	size_t nbody = 0;
	unsigned long long v;
	int st;

	next(); /* 'for' */
	expect(TLPAREN, "after 'for'");
	if (tok.kind != TSEMICOLON) {
		/* init statement: declaration (`int i = 0;`) or expression */
		st = cpp_cexpr_stmt(tmp, ret, steps);
		if (st != CEXP_OK)
			return st;
	} else {
		next();
	}
	if (tok.kind != TSEMICOLON)
		cond = expr(tmp);
	expect(TSEMICOLON, "after 'for' condition");
	if (tok.kind != TRPAREN)
		step = expr(tmp);
	expect(TRPAREN, "after 'for' step");
	cexp_buffer_stmt(&body, &nbody);
	cur = tok;
	for (;;) {
		if (++(*steps) > CEXP_MAX_STEPS)
			return CEXP_FAIL;
		if (cond) {
			struct decl *temp = NULL;
			if (!cpp_cexpr_value(cond, &temp, &v))
				return CEXP_FAIL;
			if (!v)
				break;
		}
		{
			size_t base = tokctx_depth();
			tokpush(&cur, 1);
			tokpush(body, nbody);
			next();
			st = cpp_cexpr_stmt(tmp, ret, steps);
			tokctx_rewind(base);
			tok = cur;
		}
		if (st == CEXP_RET || st == CEXP_FAIL)
			return st;
		if (st == CEXP_BREAK)
			break;
		/* CEXP_OK / CEXP_CONT -> run the step */
		if (step) {
			struct decl *temp = NULL;
			if (!cpp_cexpr_value(step, &temp, &v))
				return CEXP_FAIL;
		}
	}
	free(body);
	return CEXP_OK;
}

/* Interpret one statement from the token stream under a constant
 * evaluation.  Returns a CEXP_* status; *ret receives the folded value on
 * CEXP_RET.  `steps` is the shared loop-step budget. */
static int
cpp_cexpr_stmt(struct scope *tmp, unsigned long long *ret, int *steps)
{
	extern struct expr *expr(struct scope *);
	extern struct expr *assignexpr(struct scope *);
	extern struct qualtype declspecs(struct scope *, enum storageclass *,
	    enum funcspec *, int *);
	extern struct qualtype declarator(struct scope *, struct qualtype,
	    char **, int *, struct scope **, bool, struct attr *);
	extern struct decl *mkdecl(char *, enum declkind, struct type *,
	    enum typequal, enum linkage);
	extern void scopeputdecl(struct scope *, struct decl *);

	struct qualtype base;
	struct expr *e;
	enum storageclass sc = SCNONE;
	enum funcspec fs = 0;
	int align = 0;

	switch (tok.kind) {
	case TSEMICOLON:
		next();
		return CEXP_OK;
	case TLBRACE: {
		/* compound statement: interpret statements until '}' */
		int st;
		next(); /* '{' */
		for (;;) {
			if (tok.kind == TRBRACE) {
				next();
				return CEXP_OK;
			}
			st = cpp_cexpr_stmt(tmp, ret, steps);
			if (st != CEXP_OK)
				return st;
		}
	}
	case TRETURN: {
		next();
		if (tok.kind == TSEMICOLON) {
			next();
			return CEXP_FAIL; /* `return;` — no integer value */
		}
		e = expr(tmp);
		expect(TSEMICOLON, "after return expression");
		{
			struct decl *temp = NULL;
			/* class-typed return (`return p;` where p is a local whose
			 * aggregate members were captured): carry the object */
			if (e && e->kind == EXPRIDENT && e->u.ident.decl &&
			    e->u.ident.decl->kind == DECLOBJECT &&
			    e->u.ident.decl->type &&
			    (e->u.ident.decl->type->kind == TYPESTRUCT ||
			     e->u.ident.decl->type->kind == TYPEUNION)) {
				g_cexpr_class_ret = e->u.ident.decl;
				return CEXP_RET;
			}
			if (!cpp_cexpr_value(e, &temp, ret))
				return CEXP_FAIL;
		}
		return CEXP_RET;
	}
	case TIF: {
		bool consteval_if = false, negate = false;
		int st;
		struct decl *temp = NULL;
		unsigned long long v;

		next(); /* 'if' */
		if (tok.kind == TCONSTEXPR)
			next(); /* 'if constexpr' */
		else if (tok.kind == TLNOT) {
			/* `if ! consteval` — the only valid `!` right after `if` */
			next(); /* '!' */
			if (cpp_tok_kind() == CPP_TCONSTEVAL) {
				consteval_if = true;
				negate = true;
				next(); /* 'consteval' */
			} else {
				return CEXP_FAIL;
			}
		} else if (cpp_tok_kind() == CPP_TCONSTEVAL) {
			consteval_if = true;
			next(); /* 'consteval' */
		}
		if (consteval_if) {
			/* in constant evaluation we are in the consteval
			 * context: `if consteval` takes the then branch,
			 * `if !consteval` the else */
			if (!negate) {
				st = cpp_cexpr_stmt(tmp, ret, steps);
				if (st != CEXP_OK)
					return st;
				if (tok.kind == TELSE) {
					next();
					cpp_skip_branch();
				}
			} else {
				cpp_skip_branch();
				if (tok.kind == TELSE) {
					next();
					st = cpp_cexpr_stmt(tmp, ret, steps);
					if (st != CEXP_OK)
						return st;
				}
			}
			return CEXP_OK;
		}
		expect(TLPAREN, "after 'if'");
		e = expr(tmp);
		expect(TRPAREN, "after condition");
		if (!cpp_cexpr_value(e, &temp, &v))
			return CEXP_FAIL;
		if (v) {
			st = cpp_cexpr_stmt(tmp, ret, steps);
			if (st != CEXP_OK)
				return st;
			if (tok.kind == TELSE) {
				next();
				cpp_skip_branch();
			}
		} else {
			cpp_skip_branch();
			if (tok.kind == TELSE) {
				next();
				st = cpp_cexpr_stmt(tmp, ret, steps);
				if (st != CEXP_OK)
					return st;
			}
		}
		return CEXP_OK;
	}
	case TWHILE:
		return cpp_cexpr_while(tmp, ret, steps);
	case TDO:
		return cpp_cexpr_dowhile(tmp, ret, steps);
	case TFOR:
		return cpp_cexpr_for(tmp, ret, steps);
	case TBREAK:
		next();
		return CEXP_BREAK;
	case TCONTINUE:
		next();
		return CEXP_CONT;
	case TSWITCH:
	case TGOTO:
		/* not interpreted; degrade to a runtime call */
		return CEXP_FAIL;
	default:
		break;
	}

	/* declaration (`int s = <init>;`, `constexpr int s = ...;`) or
	 * expression statement */
	base = declspecs(tmp, &sc, &fs, &align);
		if (base.type) {
		char *name;
		struct qualtype qt;
		for (;;) {
			qt = declarator(tmp, base, &name, &align, NULL, false, NULL);
			if (!name || !qt.type)
				return CEXP_FAIL;
			if (consume(TASSIGN)) {
				struct decl *cd;
				if (qt.type && (qt.type->kind == TYPESTRUCT ||
				    qt.type->kind == TYPEUNION) &&
				    tok.kind == TLBRACE) {
					/* class-typed local with an aggregate
					 * initializer `P p = {x, x*2};` — capture
					 * each element's constant value in the mini
					 * memory model so a later member access /
					 * return of `p` can be folded. */
					extern struct init *parseinit(struct scope *,
					    struct type *);
					extern void cpp_record_cexpr_aggregate(
					    struct decl *, struct init *);
					struct init *init = parseinit(tmp, qt.type);
					cd = mkdecl(name, DECLOBJECT, qt.type,
					    qt.qual, LINKNONE);
					cd->u.obj.storage = SDAUTO;
					cpp_record_cexpr_aggregate(cd, init);
					scopeputdecl(tmp, cd);
				} else {
					struct decl *temp = NULL;
					unsigned long long v;
					e = assignexpr(tmp);
					if (!cpp_cexpr_value(e, &temp, &v))
						return CEXP_FAIL;
					/* bind as a mutable object (lvalue for ++/=) with
					 * the folded value cached in u.obj.constval */
					cd = mkdecl(name, DECLOBJECT, qt.type, qt.qual,
					            LINKNONE);
					cd->u.obj.storage = SDAUTO;
					cd->u.obj.has_constval = true;
					cd->u.obj.constval = v;
					scopeputdecl(tmp, cd);
				}
			} else {
				/* uninitialized local.  A struct/union local may be
				 * assigned member-by-member (`P p; p.x = 10;`) and
				 * then returned; bind it as a mutable object so the
				 * member assignments below can be folded.  Scalars
				 * and other types have no constant value yet, so
				 * they degrade to a runtime call. */
				if (qt.type && (qt.type->kind == TYPESTRUCT ||
				    qt.type->kind == TYPEUNION)) {
					struct decl *cd = mkdecl(name, DECLOBJECT,
					    qt.type, qt.qual, LINKNONE);
					cd->u.obj.storage = SDAUTO;
					scopeputdecl(tmp, cd);
				} else {
					return CEXP_FAIL;
				}
			}
			if (consume(TSEMICOLON))
				break;
			if (!consume(TCOMMA))
				return CEXP_FAIL;
		}
		return CEXP_OK;
	}

	/* expression statement */
	{
		struct decl *temp = NULL;
		unsigned long long v;
		e = expr(tmp);
		if (!cpp_cexpr_value(e, &temp, &v))
			return CEXP_FAIL;
		expect(TSEMICOLON, "after expression");
		return CEXP_OK;
	}
}

/* Run the statement interpreter over the replayed body; returns a fresh
 * EXPRCONST on a folded return, NULL when the body is not constant-
 * evaluable (the caller then keeps the runtime call). */
static struct expr *
cpp_cexpr_interpret(struct scope *tmp, unsigned long long *retv)
{
	extern struct expr *mkexpr(enum exprkind, struct type *, struct expr *);
	int steps = 0;
	int st = cpp_cexpr_stmt(tmp, retv, &steps);
	struct expr *e;

	if (st != CEXP_RET)
		return NULL;
	e = mkexpr(EXPRCONST, &typeint, NULL);
	e->u.constant.u = *retv;
	return e;
}

/* Fold a constexpr function call when the callee is constexpr and every
 * argument is an integer constant; returns a fresh EXPRCONST or NULL. */
struct expr *
cpp_constexpr_eval(struct expr *call)
{
	extern struct scope filescope;
	extern struct func *curfunc;
	extern struct decl *mkdecl(char *, enum declkind, struct type *,
	    enum typequal, enum linkage);
	extern void tokpush(struct token *, size_t);
	extern struct expr *expr(struct scope *);
	extern struct scope *mkscope(struct scope *);
	extern struct scope *delscope(struct scope *);
	extern void scopeputdecl(struct scope *, struct decl *);

	struct expr *callee = call ? call->base : NULL;
	struct decl *fd = NULL;
	struct cpp_cexpr_fn *fn;
	struct expr *a, *e;
	struct scope *tmp;
	struct token cur;
	unsigned long long args[16];
	int i, nargs = 0;

	if (call->kind != EXPRCALL || !callee)
		return NULL;
	if (callee->kind == EXPRUNARY && callee->op == TBAND &&
	    callee->base && callee->base->kind == EXPRIDENT)
		fd = callee->base->u.ident.decl;
	else if (callee->kind == EXPRIDENT)
		fd = callee->u.ident.decl;
	if (!fd || fd->kind != DECLFUNC || !fd->u.func.isconstexpr)
		return NULL;
	for (fn = g_cpp_cexpr_fns; fn; fn = fn->next)
		if (fn->fd == fd)
			break;
	if (!fn)
		return NULL;

	/* A member-function call `s.f(x)` reaches this evaluator with the
	 * object address prepended as a hidden `this` first argument (`&s`).
	 * Detect it, extract the object whose members the body reads, and skip
	 * that argument when matching the explicit parameter list. */
	struct decl *this_obj = NULL;
	{
		struct expr *a0 = call->u.call.args;
		if (a0 && a0->kind == EXPRUNARY && a0->op == TBAND &&
		    a0->base && a0->base->kind == EXPRIDENT) {
			struct decl *o = a0->base->u.ident.decl;
			if (o && o->kind == DECLOBJECT && o->type &&
			    (o->type->kind == TYPESTRUCT ||
			     o->type->kind == TYPEUNION))
				this_obj = o;
		}
	}
	for (a = call->u.call.args; a; a = a->next) {
		if (this_obj && a == call->u.call.args)
			continue;   /* skip the hidden `this` argument */
		/* use the interpreter's non-mutating evaluator so arguments
		 * referencing bound parameters fold in C as well as C++ mode
		 * (eval() only folds DECLOBJECT constval under g_lang == 1) */
		struct decl *temp = NULL;
		unsigned long long av;
		if (nargs >= 16 || !cpp_cexpr_value(a, &temp, &av))
			return NULL;
		args[nargs++] = av;
	}
	/* For a member call, the evaluator recorded fn->nparams including the
	 * hidden `this` parameter (the buffered member type carries it); the
	 * explicit arguments we collected (after skipping the call's `&obj`)
	 * must match only the remaining parameters. */
	int pbase = this_obj ? 1 : 0;
	if (nargs != fn->nparams - pbase)
		return NULL;
	if (g_cpp_cexpr_depth >= 64)
		error_code(E_DECL, &tok.loc, "constexpr evaluation recursion too deep");

	/* bind the parameters as integer constants and fold the body's
	 * return expression */
	tmp = mkscope(&filescope);
	for (i = pbase; i < fn->nparams; ++i) {
		/* bind parameters as mutable objects (function parameters are
		 * modifiable lvalues in C++; ++/-- and = on them must parse) */
		struct decl *pd = mkdecl(fn->params[i], DECLOBJECT,
		    fn->ptypes[i] ? fn->ptypes[i] : &typeint, QUALNONE,
		    LINKNONE);
		pd->u.obj.storage = SDAUTO;
		pd->u.obj.has_constval = true;
		pd->u.obj.constval = args[i - pbase];
		scopeputdecl(tmp, pd);
	}
	/* Bind the member variables of the object so a member-function body can
	 * fold `this` member accesses (`return a + x;` reading member `a`).
	 * Each named non-bitfield member of the object's type is re-declared
	 * with the object's recorded constant value. */
	if (this_obj) {
		struct member *m;
		for (m = this_obj->type->u.structunion.members; m; m = m->next) {
			unsigned long long mv;
			struct decl *md;
			if (!m->name || m->bits.before || m->bits.after)
				continue;
			if (!cpp_cexpr_member_value(this_obj, m->offset, &mv))
				continue;
			md = mkdecl(m->name, DECLOBJECT,
			    m->type ? m->type : &typeint, QUALNONE, LINKNONE);
			md->u.obj.storage = SDAUTO;
			md->u.obj.has_constval = true;
			md->u.obj.constval = mv;
			scopeputdecl(tmp, md);
		}
	}
	/* re-bind the template parameters of a constexpr *template* function so
	 * type usages (e.g. `sizeof(T)`) resolve during constant evaluation.
	 * This mirrors the runtime instantiation path, which binds the same
	 * parameters as DECLTYPE in the instantiation scope `bs`. */
	for (i = 0; fn->ntmpl && i < fn->ntmpl; ++i) {
		if (fn->tmpl_isval && fn->tmpl_isval[i]) {
			/* non-type template parameter: bind the constant value
			 * (the interpreter reads u.enumconst) */
			struct decl *td = mkdecl((char *)fn->tmpl_params[i],
			    DECLCONST, fn->tmpl_types[i], QUALNONE, LINKNONE);
			td->u.enumconst = fn->tmpl_vals[i];
			scopeputdecl(tmp, td);
		} else {
			struct decl *td = mkdecl((char *)fn->tmpl_params[i],
			    DECLTYPE, fn->tmpl_types[i], QUALNONE, LINKNONE);
			scopeputdecl(tmp, td);
		}
	}
	++g_cpp_cexpr_depth;
	{
		struct func *saved = curfunc;
		size_t base = tokctx_depth();
		unsigned long long retv;
		g_cexpr_class_ret = NULL;
		cur = tok;
		tokpush(&cur, 1);
		tokpush(fn->toks, fn->ntoks);
		next(); /* { */
		e = cpp_cexpr_interpret(tmp, &retv);
		/* discard any unconsumed replay tokens and restore the caller's
		 * parse position (the pushed `cur` is never read) */
		tokctx_rewind(base);
		tok = cur;
		curfunc = saved;
	}
	--g_cpp_cexpr_depth;

	if (g_cexpr_class_ret) {
		/* class-typed return: record the returned object's members keyed
		 * by this call so a member access `make_p(3).a` can fold it. */
		extern void cpp_record_cexpr_return(struct expr *, struct decl *);
		cpp_record_cexpr_return(call, g_cexpr_class_ret);
		g_cexpr_class_ret = NULL;
		e = NULL; /* no integer value; member folding uses the table */
	}
	if (e) {
		struct expr *res = xmalloc(sizeof *res);
		*res = *e;
		delexpr(e);
		delscope(tmp);
		return res;
	}
	delscope(tmp);
	return NULL;
}

