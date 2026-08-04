/* cpp_freeop.c — m++ (C++) free-function operator overloads and
 * user-defined literals.
 *
 * Non-member operator overloads (`Vec operator+(Vec, Vec)`) lower to a
 * free `operator_pl` function, and user-defined literal suffixes
 * (``123_km``) lower to a UDL call.  cpp_parse_free_operator is exported
 * to the C front-end (decl.c); the UDL helpers are referenced from
 * expr_primary.c.
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

/* Non-member operator overload: `Vec operator+(Vec a, Vec b) {...}` */
void
cpp_parse_free_operator(struct scope *s, struct qualtype base)
{
	extern struct func *mkfunc(struct decl *, char *, struct type *,
	    struct scope *);
	extern void delfunc(struct func *);
	extern void stmt(struct func *, struct scope *);
	extern void emitfunc(struct func *, struct scope *, bool);
	extern struct scope *delscope(struct scope *);

	const char *opcode;
	char mname[64], *pmangled;
	struct type *ft;
	struct decl *pd, *d, **pend;
	struct decl *nd;
	struct scope *fs;
	struct func *f;

	if (cpp_tok_kind() != CPP_TOPERATOR)
		error_code(E_SYNTAX, &tok.loc, "expected 'operator'");
	next(); /* consume 'operator' */
	/* C++11 user-defined literal: `operator""_km` — the `""` string
	 * literal token is followed by the user suffix identifier (`_km`).
	 * Lowered to the free-function name `operator_udl_km` (standard
	 * non-`_` suffixes are reserved and never reach here). */
	if (tok.kind == TSTRINGLIT) {
		const char *sfx;
		if (strcmp(tok.lit, "\"\"") != 0)
			error_code(E_OVERLOAD, &tok.loc,
			    "user-defined literal must use operator\"\"");
		next(); /* consume "" */
		if (tok.kind < TIDENT)
			error_code(E_SYNTAX, &tok.loc,
			    "expected suffix identifier after operator\"\"");
		sfx = tokenstr(tok.kind);
		if (sfx[0] != '_')
			error_code(E_OVERLOAD, &tok.loc,
			    "user-defined literal suffix must begin with '_'");
		next(); /* consume the suffix identifier */
		snprintf(mname, sizeof mname, "operator_udl_%s", sfx + 1);
	} else {
		opcode = cpp_op_mangle(tok.kind);
		if (!opcode)
			error_code(E_OVERLOAD, &tok.loc, "unsupported operator for overloading");
		next(); /* consume the operator token */
		/* operator()/operator[]: the closing ')' / ']' of the operator
		 * token follows; the next '(' is the parameter list. */
		if (strcmp(opcode, "cl") == 0)
			expect(TRPAREN, "after 'operator()'");
		else if (strcmp(opcode, "ix") == 0)
			expect(TRBRACK, "after 'operator[]'");
		snprintf(mname, sizeof mname, "operator_%s", opcode);
	}

	ft = mktype(TYPEFUNC, 0);
	ft->qual = QUALNONE;
	ft->base = base.type; /* return type */
	ft->u.func.isvararg = false;
	ft->u.func.params = NULL;
	ft->u.func.nparam = 0;
	pend = &ft->u.func.params;
	if (tok.kind == TLPAREN) {
		next();
		while (tok.kind != TRPAREN) {
			pd = parameter(s);
			*pend = pd;
			pend = &pd->next;
			++ft->u.func.nparam;
			if (tok.kind == TRPAREN)
				break;
			expect(TCOMMA, "or ')' after operator parameter");
		}
		next(); /* consume ')' */
	}
	/* mkdecl/scopeputdecl keep the name pointer; persist it off the
	 * stack (token strings from the C parser are stable, ours are not). */
	pmangled = xmalloc(strlen(mname) + 1);
	strcpy(pmangled, mname);

	d = scopegetdecl(s, mname, false);
	if (d && d->kind != DECLFUNC)
		error_code(E_REDEF, &tok.loc, "'%s' redeclared with different kind", mname);
	if (d && d->type && !typecompatible(ft, d->type))
		error_code(E_REDEF, &tok.loc, "'%s' redeclared with incompatible type", mname);
	if (!d) {
		d = mkdecl(pmangled, DECLFUNC, ft, QUALNONE, LINKEXTERN);
		scopeputdecl(s, d);
	} else {
		d->type = typecomposite(ft, d->type);
		free(pmangled);
	}
	d->value = mkglobal(d);

	if (tok.kind != TLBRACE) {
		if (tok.kind == TSEMICOLON)
			next();
		return; /* declaration only */
	}

	/* function definition: mirror the non-class body path */
	fs = mkscope(s);
	for (pd = ft->u.func.params; pd; pd = pd->next)
		if (pd->name) /* unnamed parameters have no name to bind */
			scopeputdecl(fs, pd);
	f = mkfunc(d, d->name, d->type, fs);
	stmt(f, fs);
	emitfunc(f, fs, d->linkage == LINKEXTERN || d->linkage == LINKC);
	delscope(fs);
	delfunc(f);
	d->defined = true;
}

/* C++11 user-defined literal suffix detection.  Given the token text of a
 * number literal (e.g. "1.5_km", "123_km2", "0x1_0000"), return a pointer
 * to the `_suffix` part, or NULL when the text carries no UDL suffix.
 * A UDL suffix is a `_` followed by an identifier start (alphabetic or
 * underscore); a `_` followed by a digit is a C++14 digit separator
 * (`0x1_0000`) and not a suffix.  Scanning back from the end, the first
 * `_` decides: the suffix, if any, is the final `_identifier` run. */
const char *
cpp_udl_suffix_of(const char *lit)
{
	size_t n = strlen(lit);
	size_t i;

	if (n < 2)
		return NULL;
	for (i = n; i > 1; --i) {
		if (lit[i - 1] == '_') {
			unsigned char c = lit[i];
			if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			    c == '_')
				return &lit[i - 1];
			return NULL; /* digit separator / non-suffix '_' */
		}
	}
	return NULL;
}

/* Lower a user-defined literal to a call: `1.5_km` -> `operator_udl_km(e)`.
 * The literal argument `arg` is already parsed (a numeric constant, or the
 * decayed pointer of a string literal).  The UDL function's parameter list
 * drives the binding: a string UDL takes (const char*, size_t), so the
 * literal length (excluding NUL) is appended when a second parameter is
 * declared.  Returns NULL when no matching operator is in scope. */
struct expr *
cpp_udl_literal_call(struct scope *s, const char *sfx, struct expr *arg)
{
	extern struct scope filescope;

	char mname[128];
	struct decl *fd, *pp;
	struct expr *fn, *call, **end;

	snprintf(mname, sizeof mname, "operator_udl_%s", sfx + 1);
	fd = scopegetdecl(s, mname, 1);
	if (!fd || fd->kind != DECLFUNC)
		return NULL;
	fn = mkexpr(EXPRIDENT, fd->type, NULL);
	fn->u.ident.decl = fd;
	fn = decay(fn); /* &operator_udl_km */

	call = mkexpr(EXPRCALL, fd->type->base, fn);
	call->u.call.args = NULL;
	call->u.call.nargs = 0;
	end = &call->u.call.args;
	pp = fd->type->u.func.params;
	{
		struct expr *a = arg;
		if (pp && pp->type && pp->type->isref)
			a = mkunaryexpr(TBAND, arg);
		*end = exprassign(a, pp ? pp->type : NULL);
		end = &(*end)->next;
		++call->u.call.nargs;
	}
	pp = pp ? pp->next : NULL;
	if (pp) {
		/* string UDL: append the literal length (chars, minus NUL) */
		unsigned long long len = 0;
		if (arg->kind == EXPRUNARY && arg->base &&
		    arg->base->kind == EXPRSTRING)
			len = arg->base->u.string.size - 1;
		*end = exprassign(mkconstexpr(&typeulong, len), pp->type);
		++call->u.call.nargs;
	}
	return call;
}
