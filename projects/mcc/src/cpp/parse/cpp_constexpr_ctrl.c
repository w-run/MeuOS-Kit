/* cpp_constexpr_ctrl.c - m++ (C++) constexpr control-flow and structured binding.
 *
 * Stage C.3.3: split from cpp_constexpr.c.  C++17/20/23 control-flow
 * features that interact with constant evaluation:
 *   - if consteval / if !consteval (P1938, C++23)   → cpp_if_consteval
 *   - if constexpr (C++17)                          → cpp_if_constexpr
 *   - cpp_skip_branch (token-level branch discard)
 *   - structured binding (C++17)                    → cpp_struct_binding
 *
 * Cross-file entry points (all extern in cpp_internal.h or via local
 * extern in the calling module):
 *   cpp_if_consteval   (called from stmt.c)
 *   cpp_if_constexpr   (called from stmt.c)
 *   cpp_struct_binding (called from decl.c)
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

/* Cross-file: skip_branch forward declaration (forward called by
 * cpp_if_consteval / cpp_if_constexpr before its definition below). */
void cpp_skip_branch(void);

/* C++23 `if consteval { ... } else { ... }` (P1938) and `if !consteval`.
 *
 * The branch is selected by whether the statement is in a constant-
 * evaluated context: the constexpr statement interpreter runs with
 * g_cpp_cexpr_depth > 0 and takes the consteval branch; ordinary runtime
 * parsing (depth == 0) takes the else branch for `if consteval` (and the
 * then branch for `if !consteval`).  The unselected branch is skipped at
 * the token level; its well-formedness is not checked (matching the
 * codebase's if-constexpr leniency). */
void
cpp_if_consteval(struct func *f, struct scope *s, bool negate)
{
	extern void stmt(struct func *, struct scope *);
	extern void next(void);

	bool constant_ctx = g_cpp_cexpr_depth > 0;
	/* `if consteval`: take the then branch iff constant-evaluated;
	 * `if !consteval`: inverted */
	bool take_then = constant_ctx != negate;

	if (take_then) {
		stmt(f, s);
		if (tok.kind == TELSE) {
			next();
			cpp_skip_branch();
		}
	} else {
		cpp_skip_branch();
		if (tok.kind == TELSE) {
			next();
			stmt(f, s);
		}
	}
}

/* C++17 `if constexpr (cond) { ... } else { ... }`.
 *
 * The condition is parsed by the C `if` handling and handed here.  It must
 * be a compile-time constant; only the selected branch is parsed (through
 * stmt()), the other is skipped at the token level so it is never
 * instantiated (the whole point of if constexpr inside templates). */
void
cpp_if_constexpr(struct func *f, struct expr *cond, struct scope *s)
{
	extern void stmt(struct func *, struct scope *);
	extern void next(void);
	extern struct expr *expr(struct scope *);
	extern struct expr *eval(struct expr *);

	unsigned long long v = 0;
	bool have = false;

	if (cond) {
		struct expr *r = eval(cond);
		/* eval() folds in place and returns the same node */
		if (r->kind == EXPRCONST && (r->type->prop & PROPSCALAR)) {
			/* P1401: the condition is contextually converted to bool,
			 * so any scalar constant works — including pointer/nullptr
			 * constants (`if constexpr (p)` for a pointer constant p). */
			v = r->u.constant.u;
			have = true;
		}
	}
	if (!have)
		error_code(E_DECL, &tok.loc, "if constexpr condition is not a constant expression");
	next(); /* consume ')' after the condition */
	{
		bool take_then = v != 0;
		if (take_then) {
			/* parse the then-branch normally */
			stmt(f, s);
			/* skip the else-branch, if present (`else` is a C keyword,
			 * TELSE; cpp_tok_kind only sees identifiers) */
			if (tok.kind == TELSE) {
				next();
				cpp_skip_branch();
			}
		} else {
			/* discard the then-branch, parse the else-branch */
			cpp_skip_branch();
			if (tok.kind == TELSE) {
				next();
				stmt(f, s);
			}
		}
	}
}

/* Skip the statement that currently begins at `tok` (a compound `{...}`
 * statement or a single statement), leaving the token stream positioned
 * after it.  Used to discard the unselected branch of `if constexpr`. */
void
cpp_skip_branch(void)
{
	extern void next(void);

	if (tok.kind == TLBRACE) {
		int depth = 0;
		for (;;) {
			if (tok.kind == TLBRACE)
				++depth;
			else if (tok.kind == TRBRACE) {
				--depth;
				if (depth == 0) {
					next();
					break;
				}
			} else if (tok.kind == TEOF) {
				error_code(E_DECL, &tok.loc, "unterminated 'if constexpr' branch");
				break;
			}
			next();
		}
	} else {
		/* a single non-compound statement: skip one statement.  For the
		 * common `if constexpr (c) stmt;` we skip to the next ';' at the
		 * current nesting depth. */
		int depth = 0;
		for (;;) {
			if (tok.kind == TLBRACE)
				++depth;
			else if (tok.kind == TRBRACE) {
				if (depth == 0)
					break;
				--depth;
			} else if (tok.kind == TSEMICOLON && depth == 0) {
				next();
				break;
			} else if (tok.kind == TEOF)
				break;
			next();
		}
	}
}

/* C++17 structured binding: `auto [x, y] = p;`.
 *
 * Creates a hidden object initialized from the initializer expression,
 * then binds each name to a copy of the corresponding member
 * (`x = p.first`, `y = p.second`), so the names are value bindings.
 * Returns true if handled (the whole declarator + initializer consumed). */
bool
cpp_struct_binding(struct func *f, struct scope *s, struct qualtype base)
{
	extern struct decl *mkdecl(char *, enum declkind, struct type *,
	    enum typequal, enum linkage);
	extern void funcinit(struct func *, struct decl *, struct init *,
	    bool);
	extern struct value *funcexpr(struct func *, struct expr *);
	extern struct init *mkinit(unsigned long long, unsigned long long,
	    struct bitfield, struct expr *);
	extern struct expr *mkconstexpr(struct type *, unsigned long long);
	extern struct expr *exprconvert(struct expr *, struct type *);
	extern void next(void);
	extern struct expr *assignexpr(struct scope *);
	extern struct expr *mkexpr(enum exprkind, struct type *, struct expr *);
	extern struct expr *mkunaryexpr(enum tokenkind, struct expr *);
	extern struct expr *mkbinaryexpr(struct location *, enum tokenkind,
	    struct expr *, struct expr *);
	extern struct type typeauto;

	char *names[32];
	int n = 0;
	struct member *m;
	struct expr *init, *e;
	struct decl *obj, *bd;
	struct type *t;
	int i;

	if (!f)
		return false;
	next(); /* consume '[' */
	while (tok.kind != TRBRACK) {
		if (tok.kind < TIDENT || n >= (int)countof(names))
			error_code(E_DECL, &tok.loc, "bad structured-binding name list");
		names[n++] = (char *)tokenstr(tok.kind);
		next();
		if (tok.kind == TRBRACK)
			break;
		expect(TCOMMA, "',' or ']' in structured binding");
	}
	next(); /* consume ']' */
	if (n == 0)
		error_code(E_DECL, &tok.loc, "empty structured binding");
	expect(TASSIGN, "after structured binding");
	init = assignexpr(s);
	if (!init || !init->type || init->type->kind == TYPEVOID ||
	    init->type == &typeauto)
		error_code(E_DECL, &tok.loc, "structured binding requires an object initializer");
	t = init->type;
	if (t->kind != TYPESTRUCT && t->kind != TYPEUNION)
		error_code(E_CTYPE, &tok.loc, "structured binding requires a class type");
	{
		int nm = 0;
		for (m = t->u.structunion.members; m; m = m->next)
			if (m->name)
				++nm;
		if (nm != n)
			error_code(E_DECL, &tok.loc,
			    "structured binding names (%d) do not match member count (%d)",
			    n, nm);
	}
	expect(TSEMICOLON, "after structured binding");

	/* hidden object: allocate storage, copy the initializer in */
	obj = mkdecl("__sb", DECLOBJECT, t, QUALNONE, LINKNONE);
	obj->u.obj.storage = SDAUTO;
	funcinit(f, obj, NULL, false);
	{
		extern struct expr *mkassignexpr(struct expr *, struct expr *);
		struct expr *lhs = mkexpr(EXPRIDENT, t, NULL);
		lhs->lvalue = true;
		lhs->u.ident.decl = obj;
		funcexpr(f, mkassignexpr(lhs, init));
	}
	/* bind each name to a copy of the corresponding member */
	for (m = t->u.structunion.members, i = 0; m && i < n; m = m->next) {
		struct member *mi = m;
		char *nm;
		struct expr *load, *lhs;
		if (!mi->name)
			continue;
		nm = names[i];
		bd = mkdecl(nm, DECLOBJECT, mi->type, mi->qual, LINKNONE);
		bd->u.obj.storage = SDAUTO;
		funcinit(f, bd, NULL, false);
		scopeputdecl(s, bd);
		/* load = *( (T*)&obj + offset ) */
		lhs = mkexpr(EXPRIDENT, t, NULL);
		lhs->lvalue = true;
		lhs->u.ident.decl = obj;
		load = mkbinaryexpr(&(struct location){0}, TADD,
		    exprconvert(mkunaryexpr(TBAND, lhs), &typeulong),
		    mkconstexpr(&typeulong, mi->offset));
		load->type = mkpointertype(mi->type, mi->qual);
		load = mkunaryexpr(TMUL, load);
		load->lvalue = true;
		/* bd = load */
		lhs = mkexpr(EXPRIDENT, mi->type, NULL);
		lhs->lvalue = true;
		lhs->u.ident.decl = bd;
		funcexpr(f, mkassignexpr(lhs, load));
		++i;
	}
	return true;
}
