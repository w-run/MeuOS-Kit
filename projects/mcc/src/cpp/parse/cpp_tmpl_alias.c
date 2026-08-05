/* cpp_tmpl_alias.c - m++ (C++) template placeholder, argument and alias
 * handling.
 *
 * Parses explicit template arguments (cpp_tmpl_explicit_parse /
 * cpp_tmpl_const_arg), registers and instantiates template aliases
 * (cpp_register_alias / cpp_tmpl_alias_instantiate / cpp_template_alias),
 * and builds the placeholder callee for a pending template call
 * (cpp_tmpl_dummy_callee / cpp_tmpl_placeholder).  Extracted from
 * cpp_parse.c.
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


struct decl *
cpp_tmpl_dummy_callee(void)
{
	static struct type *fn;
	static struct decl *d;

	if (!d) {
		fn = mktype(TYPEFUNC, 0);
		fn->base = &typevoid;
		fn->u.func.isvararg = false;
		fn->u.func.params = NULL;
		fn->u.func.nparam = 0;
		d = mkdecl("__tmpl", DECLFUNC, fn, QUALNONE, LINKNONE);
		d->value = mkglobal(d);
	}
	return d;
}

/* primaryexpr helper: `name` is an undeclared identifier that names a
 * function template.  Record the pending template call and return a
 * placeholder callee; the TLPAREN lowering performs the instantiation
 * once the argument types are known. */
struct expr *
cpp_tmpl_placeholder(const char *name)
{
	struct expr *e;

	if (g_cpp_tmpl_depth >= 64)
		error_code(E_TEMPLATE, &tok.loc, "template call nesting too deep");
	g_cpp_tmpl_stack[g_cpp_tmpl_depth++] = name;
	e = mkexpr(EXPRIDENT, mkpointertype(cpp_tmpl_dummy_callee()->type, QUALNONE),
	           NULL);
	e->u.ident.decl = cpp_tmpl_dummy_callee();
	e->lvalue = false;
	return e;
}

/* Parse explicit template arguments after a function-template name:
 * `f<int, 42, N>(...)`.  Each argument is either a type (parsed with
 * typename()) or a non-type constant expression (folded to a value).
 * The leading explicit arguments fill the template parameters before any
 * call-site deduction.  On return `tok` is positioned just past the
 * closing '>'. */
struct expr *cpp_tmpl_const_arg(struct scope *s);

void
cpp_tmpl_explicit_parse(struct scope *s)
{
	extern struct type *typename(struct scope *, enum typequal *,
	    struct expr **);
	extern struct cpp_template *cpp_tmpl_find(const char *);
	enum typequal tq;
	struct expr *toeval;
	struct cpp_template *tmpl;
	struct cpp_tmpl_param *p = NULL;

	g_cpp_tmpl_expl_n = 0;
	expect(TLESS, "after template name");
	/* Consult the template's parameter kinds so each explicit argument is
	 * parsed directly as a type or a constant value (mirrors the class
	 * template instantiation).  A type-dependent NTTP (`template<typename
	 * T, T N>`) falls out naturally: N is a value of the *argument's*
	 * concrete type, recorded here and bound at instantiation. */
	int saved_depth;
	if (g_cpp_tmpl_depth > 0)
		tmpl = cpp_tmpl_find(g_cpp_tmpl_stack[g_cpp_tmpl_depth - 1]);
	else
		tmpl = NULL;
	if (tmpl)
		p = tmpl->params;
	/* Hide the pending template name while parsing the explicit-argument
	 * expressions.  `arrsize<sq(3)>()` pushes `arrsize` as pending; an
	 * inner call `sq(3)` in an argument would otherwise have its `(`
	 * lowered by cpp_tmpl_instantiate, which pops the *outer* name and
	 * instantiates `arrsize` with `sq(3)` as its argument list
	 * ("too many arguments").  Setting depth 0 here lets inner calls
	 * resolve as plain expressions; the saved depth is restored so the
	 * outer `<...>(...)` still instantiates the template at its `(`. */
	saved_depth = g_cpp_tmpl_depth;
	g_cpp_tmpl_depth = 0;
	while (tok.kind != TGREATER) {
		if (g_cpp_tmpl_expl_n >= 16)
			error_code(E_SYNTAX, &tok.loc, "too many template arguments");
		if (p && p->is_nttp) {
			/* non-type parameter: a constant integer expression */
			struct expr *ev = cpp_tmpl_const_arg(s);
			g_cpp_tmpl_expl_types[g_cpp_tmpl_expl_n] = ev->type;
			g_cpp_tmpl_expl_vals[g_cpp_tmpl_expl_n] =
			    ev->u.constant.u;
			g_cpp_tmpl_expl_isval[g_cpp_tmpl_expl_n] = true;
		} else {
			tq = QUALNONE;
			toeval = NULL;
			struct type *tt = typename(s, &tq, &toeval);
			if (!tt || toeval)
				error_code(E_TEMPLATE, &tok.loc,
				    "template argument is not a type");
			g_cpp_tmpl_expl_types[g_cpp_tmpl_expl_n] = tt;
			g_cpp_tmpl_expl_isval[g_cpp_tmpl_expl_n] = false;
		}
		++g_cpp_tmpl_expl_n;
		if (p)
			p = p->next;
		if (tok.kind == TGREATER)
			break;
		expect(TCOMMA, "',' or '>' in template argument list");
	}
	next(); /* consume '>' */
	g_cpp_tmpl_depth = saved_depth;   /* re-expose pending outer template */
}

struct expr *
cpp_tmpl_const_arg(struct scope *s)
{
	extern struct expr *condexpr(struct scope *);
	extern struct expr *eval(struct expr *);
	extern void tokpush(struct token *, size_t);
	struct token buf[256];
	size_t bn = 0;
	int depth = 0;
	struct token sep;

	for (;;) {
		if (bn >= 256)
			error_code(E_TEMPLATE, &tok.loc, "template argument too long");
		if (tok.kind == TLPAREN || tok.kind == TLBRACK)
			++depth;
		else if (tok.kind == TRPAREN || tok.kind == TRBRACK) {
			if (depth)
				--depth;
		} else if ((tok.kind == TGREATER || tok.kind == TCOMMA) &&
		    depth == 0) {
			sep = tok;
			break;
		}
		buf[bn++] = tok;
		next();
	}
	if (!bn)
		error_code(E_SYNTAX, &tok.loc, "expected template argument");
	{
		/* replay the buffered expression in front of a guard so the
		 * parser stops at the end of it instead of consuming the source
		 * '>' (a binary operator) that follows */
		size_t d = tokctx_depth();
		struct token guard = {0};
		guard.kind = TSEMICOLON;
		tokpush(&guard, 1);
		tokpush(buf, bn);
		next(); /* position tok at the first buffered token */
		struct expr *ev = eval(condexpr(s));
		if (!ev || ev->kind != EXPRCONST || !(ev->type->prop & PROPINT))
			error_code(E_TEMPLATE, &tok.loc,
			    "non-type template argument must be a constant integer expression");
		/* restore the stream to the source '>' (never consumed) */
		tokctx_rewind(d);
		tok = sep;
		return ev;
	}
}

/* Append one token to a growing token buffer (used to accumulate the
 * per-parameter constraints of `template<Concept T>` declarations). */
void
cpp_constraint_add(struct token **buf, size_t *n, size_t *cap, struct token t)
{
	if (*n >= *cap) {
		*cap = *cap ? *cap * 2 : 16;
		*buf = xreallocarray(*buf, *cap, sizeof **buf);
	}
	(*buf)[(*n)++] = t;
}

/* C++11 template type alias: `template<...> using Name = Type;` (e.g.
 * `template<typename T> using Vec = T;`).  The alias body is a type
 * expression over the template parameters; each use `Name<Args...>`
 * substitutes the argument types and resolves to the indicated type.
 * Registered at declaration time; instantiated on first use (see
 * cpp_tmpl_alias_instantiate). */
struct cpp_tmpl_alias {
	const char *name;
	int nparams;
	const char **param_names;
	struct token *toks;     /* alias type expression tokens up to ';' */
	size_t ntoks;
	struct cpp_tmpl_alias *next;
};
static struct cpp_tmpl_alias *g_cpp_aliases;

/* Register a template type alias `template<...> using Name = <body>;`.
 * `params`/`nparams` are the template parameter names; `toks/ntoks` is
 * the buffered alias-type expression (everything after `=` up to `;`). */
void
cpp_register_alias(const char *name, const char **params, int nparams,
                   struct token *toks, size_t ntoks)
{
	struct cpp_tmpl_alias *a = xmalloc(sizeof *a);
	int i;
	a->name = xmalloc(strlen(name) + 1);
	strcpy((char *)a->name, name);
	a->nparams = nparams;
	a->param_names = xmalloc((size_t)nparams * sizeof *a->param_names);
	for (i = 0; i < nparams; i++) {
		a->param_names[i] = params[i] ? xmalloc(strlen(params[i]) + 1)
		    : NULL;
		if (a->param_names[i])
			strcpy((char *)a->param_names[i], params[i]);
	}
	a->toks = xmalloc(ntoks * sizeof *a->toks);
	a->ntoks = ntoks;
	memcpy(a->toks, toks, ntoks * sizeof *a->toks);
	a->next = g_cpp_aliases;
	g_cpp_aliases = a;
}

/* Look up a template type alias by name (or NULL). */
bool
cpp_tmpl_alias_lookup(const char *name)
{
	struct cpp_tmpl_alias *a;
	for (a = g_cpp_aliases; a; a = a->next)
		if (strcmp(a->name, name) == 0)
			return true;
	return false;
}

/* Find the registered alias with the given name. */
struct cpp_tmpl_alias *
cpp_find_alias(const char *name)
{
	struct cpp_tmpl_alias *a;
	for (a = g_cpp_aliases; a; a = a->next)
		if (strcmp(a->name, name) == 0)
			return a;
	return NULL;
}

/* Instantiate a template type alias `Name<Args...>` and return the
 * resolved type.  Parses the explicit `<...>` argument types (each as a
 * `typename()`), binds each template parameter to its argument type in a
 * temporary scope, then re-parses the buffered alias-body type in that
 * scope.  Returns NULL when it is not this kind of alias use. */
struct type *
cpp_tmpl_alias_instantiate(struct scope *s, const char *name)
{
	extern struct type *typename(struct scope *, enum typequal *,
	    struct expr **);
	extern struct scope *mkscope(struct scope *);
	extern void scopeputdecl(struct scope *, struct decl *);
	extern struct decl *mkdecl(char *, enum declkind, struct type *,
	    enum typequal, enum linkage);
	extern void tokpush(struct token *, size_t);
	struct cpp_tmpl_alias *a = cpp_find_alias(name);
	struct type *args[16];
	struct scope *bs;
	struct type *result = NULL;
	int i, n = 0;

	if (!a)
		return NULL;
	expect(TLESS, "after template alias name");
	if (a->nparams > 16)
		error_code(E_SYNTAX, &tok.loc, "template alias '%s' has too many parameters", name);
	for (;;) {
		if (n >= a->nparams)
			error_code(E_SYNTAX, &tok.loc, "too many template arguments for alias '%s'", name);
		{
			enum typequal tq = QUALNONE;
			struct expr *toeval = NULL;
			args[n] = typename(s, &tq, &toeval);
			if (!args[n] || toeval)
				error_code(E_TEMPLATE, &tok.loc, "template alias argument is not a type");
		}
		++n;
		if (tok.kind == TGREATER)
			break;
		expect(TCOMMA, "',' or '>' in template alias argument list");
	}
	next(); /* consume '>' */
	if (n < a->nparams)
		error_code(E_TEMPLATE, &tok.loc, "too few template arguments for alias '%s'", name);

	/* Save the stream position after '>' so the alias-body replay can be
	 * rewound without losing the source token that follows `Name<...>`
	 * (the declarator), like the class-template instantiation path. */
	{
		struct token after_gt = tok;
		/* bind the template parameters as DECLTYPE names, then resolve
		 * the alias body in that context */
		bs = mkscope(s);
		for (i = 0; i < a->nparams; i++)
			scopeputdecl(bs, mkdecl((char *)a->param_names[i], DECLTYPE,
			    args[i], QUALNONE, LINKNONE));
		{
			size_t d = tokctx_depth();
			struct token guard = {0};
			guard.kind = TSEMICOLON;
			tokpush(&guard, 1);
			tokpush(a->toks, a->ntoks);
			next();
			{
				enum typequal tq = QUALNONE;
				struct expr *toeval = NULL;
				result = typename(bs, &tq, &toeval);
			}
			tokctx_rewind(d);
			tok = after_gt;
		}
	}
	return result;
}

/* Parse `template<...> using Name = Type;` and register the alias.
 * Called from cpp_template_decl after the parameter list is parsed and
 * the current token is the `using` keyword. */
void
cpp_template_alias(struct cpp_template *tmpl)
{
	const char **pnames;
	const char *aname;
	struct token *atoks = NULL;
	size_t an = 0, acap = 0, i;
	struct cpp_tmpl_param *p;
	extern void next(void);

	next(); /* consume 'using' */
	if (tok.kind < TIDENT)
		error_code(E_SYNTAX, &tok.loc, "expected alias name after 'using'");
	aname = tokenstr(tok.kind);
	next(); /* consume alias name */
	expect(TASSIGN, "after alias name in using alias declaration");

	/* collect template parameter names from tmpl */
	pnames = xmalloc((size_t)tmpl->nparams * sizeof *pnames);
	i = 0;
	for (p = tmpl->params; p; p = p->next, i++)
		pnames[i] = p->name;

	/* buffer the alias type expression up to ';' */
	for (;;) {
		if (tok.kind == TSEMICOLON)
			break;
		if (tok.kind == TEOF)
			error_code(E_SYNTAX, &tok.loc, "unterminated template alias declaration");
		if (an >= acap) {
			acap = acap ? acap * 2 : 32;
			atoks = xreallocarray(atoks, acap, sizeof *atoks);
		}
		atoks[an++] = tok;
		next();
	}
	next(); /* consume ';' */
	cpp_register_alias(aname, pnames, tmpl->nparams, atoks, an);
}

/* Parse `template < typename T, class U, ... >` and buffer the following
 * declaration.  Nothing is defined yet; instantiation happens on first
 * use with concrete type arguments.  `owner` is the enclosing class for
 * a member template (`template<...> T get() {...}` inside a class body),
 * or NULL for a file-scope function/class template. */
