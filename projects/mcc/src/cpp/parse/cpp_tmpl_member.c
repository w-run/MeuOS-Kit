/* cpp_tmpl_member.c — m++ (C++) member templates (template methods of a
 * class).
 *
 * ``obj.get<int>(...)`` member-template lowering: record explicit
 * template arguments (cpp_tmpl_member_pend) and instantiate the member
 * template call (cpp_tmpl_member_instantiate), reusing the template
 * registry (g_cpp_templates) and the dummy-callee machinery from
 * cpp_parse.c.  Entry points cpp_tmpl_member / cpp_tmpl_member_pend /
 * cpp_tmpl_member_instantiate / cpp_tmpl_member_placeholder are exported.
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

/* --- C.2.8 member templates (template methods in a class) ------------- */

/* Explicit template arguments `<int>` recorded by cpp_tmpl_member_pend;
 * consumed by cpp_tmpl_member_instantiate. */
static struct type *g_cpp_tmpl_member_args[16];
static int g_cpp_tmpl_member_nargs;

/* Is `name` a template member function of class `t`?  Member templates
 * are registered during class-body parsing (owner == the class type). */
bool
cpp_tmpl_member(struct type *t, const char *name)
{
	struct cpp_template *tmpl;

	if (!t || !name)
		return false;
	for (tmpl = g_cpp_templates; tmpl; tmpl = tmpl->next)
		if (tmpl->is_member && tmpl->owner == t &&
		    strcmp(tmpl->name, name) == 0)
			return true;
	return false;
}

/* Dummy callee expression for a pending member-template call (satisfies
 * the TLPAREN "called object" checks until the instantiation replaces
 * it). */
struct expr *
cpp_tmpl_member_placeholder(void)
{
	struct expr *e;

	e = mkexpr(EXPRIDENT, mkpointertype(cpp_tmpl_dummy_callee()->type,
	    QUALNONE), NULL);
	e->u.ident.decl = cpp_tmpl_dummy_callee();
	e->lvalue = false;
	return e;
}

/* Record a pending member-template call.  `tok` is positioned on the
 * member name; it is consumed, along with the optional explicit template
 * argument list `<T1, T2>` (the caller has confirmed the member is a
 * template).  After this returns, `tok` is positioned on the '(' of the
 * call. */
void
cpp_tmpl_member_pend(struct type *t, const char *name)
{
	extern struct type *typename(struct scope *, enum typequal *,
	    struct expr **);
	extern struct scope filescope;

	g_cpp_member_class = t;
	g_cpp_member_name = name;
	g_cpp_member_tmpl = true;
	g_cpp_tmpl_member_nargs = 0;

	next(); /* consume the member name */
	if (tok.kind != TLESS)
		return; /* no explicit template arguments: deduce from args */
	next(); /* consume '<' */
	while (tok.kind != TGREATER) {
		enum typequal tq = QUALNONE;
		struct expr *toeval = NULL;
		if (g_cpp_tmpl_member_nargs >= (int)countof(g_cpp_tmpl_member_args))
			error_code(E_SYNTAX, &tok.loc, "too many explicit template arguments");
		g_cpp_tmpl_member_args[g_cpp_tmpl_member_nargs++] =
		    typename(&filescope, &tq, &toeval);
		if (tok.kind == TGREATER)
			break;
		expect(TCOMMA, "',' or '>' in template argument list");
	}
	next(); /* consume '>' */
}

/* Instantiate the pending member-template call (`obj.get<int>(...)`):
 * bind the template parameters (explicit `<...>` args first, the rest
 * deduced positionally from the call arguments), replay the buffered
 * declaration with the method name mangled to `Class_method_codes`, and
 * define it as a member function (with the implicit `this`).  Returns the
 * decayed callee expression for the instantiated function. */
struct expr *
cpp_tmpl_member_instantiate(struct scope *s, struct expr *thisp,
                            struct expr *arglist)
{
	extern struct qualtype declspecs(struct scope *, enum storageclass *,
	    enum funcspec *, int *);
	extern struct qualtype declarator(struct scope *, struct qualtype,
	    char **, int *, struct scope **, bool, struct attr *);
	extern struct scope *delscope(struct scope *);

	struct cpp_template *tmpl;
	struct cpp_tmpl_inst *inst;
	struct cpp_tmpl_param *p;
	struct type *types[16];
	char mname[128], key[128], sym[256];
	struct scope *bs;
	struct decl *fd, *td;
	struct token cur;
	struct expr *e;
	int i, n;
	bool found;
	struct type *owner;
	const char *name;
	const char *tag;

	owner = g_cpp_member_class;
	name = g_cpp_member_name;
	(void)thisp;
	if (!owner || !name)
		return NULL;
	tag = owner->u.structunion.tag;
	if (!tag)
		error_code(E_TEMPLATE, &tok.loc, "member template of an unnamed class is not supported");

	for (tmpl = g_cpp_templates; tmpl; tmpl = tmpl->next)
		if (tmpl->is_member && tmpl->owner == owner &&
		    strcmp(tmpl->name, name) == 0)
			break;
	if (!tmpl)
		error_code(E_DECL, &tok.loc, "no template member function '%s' in class '%s'",
		    name, tag);
	if (tmpl->nparams > 16)
		error_code(E_SYNTAX, &tok.loc, "template member '%s' has too many parameters",
		    name);

	/* explicit `<...>` args fill the leading parameters; the rest are
	 * deduced positionally from the call-site argument types */
	n = g_cpp_tmpl_member_nargs;
	for (i = 0; i < n; ++i)
		types[i] = g_cpp_tmpl_member_args[i];
	{
		struct expr *a;
		for (a = arglist; a && i < tmpl->nparams; a = a->next, ++i)
			types[i] = a->type;
	}
	if (i < tmpl->nparams)
		error_code(E_TEMPLATE, &tok.loc,
		    "too few template arguments for template member '%s'", name);

	/* mangled member name + cache key: `get_i` / `Wrapper_get_i` (keyed
	 * on the template type codes; the full symbol — with the explicit
	 * parameter codes that cpp_define_method appends — is built after
	 * the declarator is parsed). */
	snprintf(mname, sizeof mname, "%s", name);
	for (i = 0; i < tmpl->nparams; ++i) {
		char code[64];
		cpp_mangle_type(types[i], code, sizeof code);
		strncat(mname, "_", sizeof mname - strlen(mname) - 1);
		strncat(mname, code, sizeof mname - strlen(mname) - 1);
	}
	snprintf(key, sizeof key, "%s_%s", tag, mname);
	snprintf(sym, sizeof sym, "%s", key);

	for (inst = tmpl->insts; inst; inst = inst->next)
		if (strcmp(inst->key, key) == 0) {
			e = mkexpr(EXPRIDENT, inst->fn->type, NULL);
			e->u.ident.decl = inst->fn;
			e->lvalue = false;
			return decay(e);
		}

	/* instantiate: bind parameters as type names, replay the buffered
	 * declaration with the method token renamed to the mangled name.
	 * The declaration is parsed with declspecs/declarator and routed to
	 * cpp_define_method (which adds the implicit `this` parameter), so
	 * the replayed `T get_i() {...}` defines `Wrapper_get_i`.
	 * Scope base: the owner class's declaration scope (not the call-site
	 * scope `s`) so the method body resolves captures/members via the
	 * class (cpp_member_ident) instead of seeing the caller's locals. */
	bs = mkscope(owner->scope ? owner->scope : s);
	for (p = tmpl->params, i = 0; p; p = p->next, ++i) {
		td = mkdecl((char *)p->name, DECLTYPE, types[i], QUALNONE, LINKNONE);
		scopeputdecl(bs, td);
	}
	{
		int rn = tmpl->ntoks;
		struct token *rtoks = xmalloc(rn * sizeof *rtoks);
		memcpy(rtoks, tmpl->toks, rn * sizeof *rtoks);
		found = false;
		for (i = 0; i < rn; ++i) {
			const char *nm;
			if (rtoks[i].kind < TIDENT)
				continue;
			nm = tokenstr(rtoks[i].kind);
			/* `operator` overload template: `template<typename T> auto
			 * operator()(...)`.  Replace `operator` + its trailing token
			 * (e.g. `()`) with the mangled method name so declarator sees
			 * a plain `mname(...)` declarator. */
			if (cpp_classify_ident(nm, strlen(nm)) == CPP_TOPERATOR) {
				rtoks[i].kind = tokenget(mname, strlen(mname));
				found = true;
				/* `operator()` is `operator` `(` `)`: drop both so the
				 * mangled name becomes a plain `mname(...)` declarator;
				 * a token-operator (`operator+`) is just `operator` `+`
				 * — drop the single operand too. */
				if (i + 1 < rn && rtoks[i + 1].kind == TLPAREN) {
					int drop = 2; /* '(' ')' */
					if (i + 2 < rn && rtoks[i + 2].kind == TRPAREN)
						memmove(rtoks + i + 1, rtoks + i + 3,
						    (rn - i - 3) * sizeof *rtoks);
					else
						memmove(rtoks + i + 1, rtoks + i + 2,
						    (rn - i - 2) * sizeof *rtoks);
					rn -= drop;
				}
				break;
			}
			if (cpp_classify_ident(nm, strlen(nm)) != CPP_TNONE)
				continue;
			bool is_param = false;
			for (p = tmpl->params; p; p = p->next)
				if (strcmp(p->name, nm) == 0) {
					is_param = true;
					break;
				}
			if (!is_param) {
				rtoks[i].kind = tokenget(mname, strlen(mname));
				found = true;
				break;
			}
		}
		if (!found)
			error_code(E_TEMPLATE, &tok.loc, "cannot locate member name in template '%s'", name);
		cur = tok;
		tokpush(&cur, 1);
		tokpush(rtoks, rn);
	}
	next();
	{
		struct qualtype base, mt;
		enum storageclass sc = SCNONE;
		enum funcspec fs = 0;
		char *dname;
		int align = 0;

		base = declspecs(bs, &sc, &fs, &align);
		if (!base.type)
			error_code(E_CTYPE, &tok.loc, "no type in template member declaration");
		mt = declarator(bs, base, &dname, &align, NULL, false, NULL);
		if (mt.type->kind != TYPEFUNC)
			error_code(E_DECL, &tok.loc, "template member '%s' is not a function", name);
		/* a trailing `const` (e.g. a lambda's const operator()) is a
		 * member cv-qualifier, not part of the C declarator grammar:
		 * consume it here and pass it through so the const overload
		 * (`Class_methodK<args>`) is instantiated, matching the
		 * non-template member path (struct_decl.c). */
		bool mconst = false;
		if (tok.kind == TCONST) {
			mconst = true;
			next();
		}
		cpp_define_method(bs, mt.type, mname, tag, mconst,
		    (sc & SCSTATIC) != 0, false);
		/* cpp_define_method appends the encoded explicit parameter types
		 * to the mangled symbol (`Wrapper_add_i` + `i` -> `Wrapper_add_ii`),
		 * so build the full symbol name the same way for the lookup. */
		{
			struct decl *cur;
			snprintf(sym, sizeof sym, "%s_%s", tag, mname);
			if (mconst)
				strncat(sym, "K", sizeof sym - strlen(sym) - 1);
			for (cur = mt.type->u.func.params; cur; cur = cur->next) {
				char code[64];
				cpp_mangle_type(cur->type, code, sizeof code);
				strncat(sym, code, sizeof sym - strlen(sym) - 1);
			}
		}
	}
	delscope(bs);

	/* cpp_define_method registered the mangled function in the class's
	 * declaration scope */
	fd = scopegetdecl(owner->scope ? owner->scope : s, sym, 1);
	if (!fd || fd->kind != DECLFUNC)
		error_code(E_DECL, &tok.loc, "template member '%s' instantiation produced no function", name);

	inst = xmalloc(sizeof(*inst));
	snprintf(inst->key, sizeof inst->key, "%s", key);
	inst->fn = fd;
	inst->next = NULL;
	*tmpl->insts_end = inst;
	tmpl->insts_end = &inst->next;

	e = mkexpr(EXPRIDENT, fd->type, NULL);
	e->u.ident.decl = fd;
	e->lvalue = false;
	return decay(e);
}
