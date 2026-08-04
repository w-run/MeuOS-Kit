/* cpp_method.c — m++ (C++) member-function body buffering and definition.
 *
 * During two-phase class parsing, method bodies are buffered as tokens
 * and replayed after the class layout is known (cpp_parse_method_body /
 * buffer_method_body / flush_pending_methods); cpp_define_method defines
 * a (possibly templated) method as a mangled free function with an
 * implicit `this`.  Also lower default `operator<=>` and the token-stream
 * builder used for inherited-ctor synthesis.
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

static struct cpp_pending_method *g_cpp_pending_methods;
static struct cpp_pending_method **g_cpp_pending_methods_end =
    &g_cpp_pending_methods;

bool g_cpp_tmpl_instantiating;
static struct cpp_pending_method *g_cpp_deferred_methods;
static struct cpp_pending_method **g_cpp_deferred_end =
    &g_cpp_deferred_methods;

struct decl *g_cpp_tmpl_binds[16];
int g_cpp_tmpl_nbinds;

/* Parse one method body (replayed token stream is positioned at '{'). */
static void
cpp_parse_method_body(struct cpp_pending_method *pm)
{
	extern struct func *mkfunc(struct decl *, char *, struct type *,
	    struct scope *);
	extern void delfunc(struct func *);
	extern void stmt(struct func *, struct scope *);
	extern void emitfunc(struct func *, struct scope *, bool);
	extern void funchlt(struct func *);
	extern struct scope *delscope(struct scope *);

	struct scope *fs;
	struct decl *nd;
	struct func *f;

	fs = mkscope(pm->s);
	for (nd = pm->mtype->u.func.params; nd; nd = nd->next)
		if (nd->name) /* unnamed parameters (`B(int)`) have no name to bind */
			scopeputdecl(fs, nd);

	/* method-body context is saved/restored so a nested method-body parse
	 * (an inner class or a lambda closure defined inside this body) does
	 * not clobber the outer context mid-parse */
	{
		struct cpp_method_ctx saved = g_cpp_method;
		g_cpp_method.class_type = pm->classt;
		g_cpp_method.this_decl = pm->is_static ? NULL : pm->thisd;
		g_cpp_method.active = true;

		f = mkfunc(pm->d, pm->d->name, pm->d->type, fs);
		/* constructor: parse the init list (`: Base(v), m(v)`) if any,
		 * then run base-class constructors before the body */
		if (strcmp(pm->mname, pm->tag) == 0) {
			g_cpp_init_items = NULL;
			g_cpp_init_end = &g_cpp_init_items;
			cpp_parse_init_list(f, fs);
			cpp_emit_base_ctor(f);
			/* point every vptr at this class's vtable (the base ctor just set
			 * them to the base view; the complete object needs the final one) */
			cpp_init_vptrs(f, pm->classt, cpp_this_expr());
		}
		stmt(f, fs);
		/* destructor: run base-class destructors after the body (the
		 * derived body runs first, then each base's body via its own
		 * `Base_dtor`, recursively — reverse construction order) */
		if (strcmp(pm->mname, "dtor") == 0)
			cpp_emit_base_dtor(f);
		if (pm->d->u.func.isnoreturn)
			funchlt(f);
		/* C++14 `auto` return type: backfill the type deduced from the
		 * body's return statement(s). */
		if (pm->d->type->base == &typeauto) {
			if (!g_cpp_auto_ret_type)
				error_code(E_DECL, &tok.loc, "'auto' member function '%s' has no return statement to deduce its type from", pm->mname);
			pm->d->type->base = g_cpp_auto_ret_type;
			g_cpp_auto_ret_type = NULL;
			g_cpp_auto_ret_func = NULL;
		}
		emitfunc(f, fs, pm->d->linkage == LINKEXTERN || pm->d->linkage == LINKC);
		delscope(fs);
		delfunc(f);
		pm->d->defined = true;

		g_cpp_method = saved;
	}
}

/* Collect the `{...}` body tokens and queue the method for replay once
 * the enclosing class body is complete. */
static void
buffer_method_body(struct scope *s, struct type *classt, struct type *mtype,
                   struct decl *thisd, struct decl *d,
                   const char *mname, const char *tag, bool is_static)
{
	struct cpp_pending_method *pm;
	size_t cap = 0;
	int bd = 0;

	pm = xmalloc(sizeof(*pm));
	pm->toks = NULL;
	pm->ntoks = 0;
	pm->mname = mname;
	pm->tag = tag;
	pm->classt = classt;
	pm->mtype = mtype;
	pm->thisd = thisd;
	pm->d = d;
	pm->s = s;
	pm->is_static = is_static;
	pm->nbinds = 0;
	pm->next = NULL;
	if (g_cpp_tmpl_instantiating) {
		pm->nbinds = g_cpp_tmpl_nbinds;
		memcpy(pm->binds, g_cpp_tmpl_binds,
		    (size_t)g_cpp_tmpl_nbinds * sizeof *pm->binds);
	}

	/* a ctor init list (`: Base(v) { ... }`) has no opening brace right
	 * after the declarator: buffer through the body's closing '}'.
	 * `seen` tracks whether we have entered the body yet: the tokens
	 * between ':' and the first '{' (the init items and their argument
	 * expressions) must be buffered too, but brace depth only starts
	 * counting at the body's '{'. */
	if (tok.kind == TCOLON) {
		bool seen = false;
		do {
			if (pm->ntoks >= cap) {
				cap = cap ? cap * 2 : 64;
				pm->toks = xreallocarray(pm->toks, cap, sizeof *pm->toks);
			}
			pm->toks[pm->ntoks++] = tok;
			if (tok.kind == TLBRACE) {
				++bd;
				seen = true;
			} else if (tok.kind == TRBRACE) {
				--bd;
			}
			next();
		} while ((!seen || bd > 0) && tok.kind != TEOF);
		*g_cpp_pending_methods_end = pm;
		g_cpp_pending_methods_end = &pm->next;
		return;
	}

	do {
		if (pm->ntoks >= cap) {
			cap = cap ? cap * 2 : 64;
			pm->toks = xreallocarray(pm->toks, cap, sizeof *pm->toks);
		}
		pm->toks[pm->ntoks++] = tok;
		if (tok.kind == TLBRACE)
			++bd;
		else if (tok.kind == TRBRACE)
			--bd;
		next();
	} while (bd > 0 && tok.kind != TEOF);

	*g_cpp_pending_methods_end = pm;
	g_cpp_pending_methods_end = &pm->next;
}

/* Replay all buffered method bodies (called after the class layout is
 * fixed). */
void
flush_pending_methods(void)
{
	extern void tokpush(struct token *, size_t);
	struct cpp_pending_method *head, *pm;

	/* D2: during a class-template instantiation, defer every method body
	 * instead of parsing it now.  The deferred bodies are parsed lazily
	 * (cpp_ensure_method_defined) when the member is actually called, so
	 * an unused member with an ill-formed body does not fail the
	 * instantiation. */
	if (g_cpp_tmpl_instantiating) {
		if (g_cpp_pending_methods) {
			*g_cpp_deferred_end = g_cpp_pending_methods;
			while (*g_cpp_deferred_end)
				g_cpp_deferred_end = &(*g_cpp_deferred_end)->next;
			g_cpp_pending_methods = NULL;
			g_cpp_pending_methods_end = &g_cpp_pending_methods;
		}
		return;
	}

	/* Detach the pending list first: a method body that defines an inner
	 * class (or a lambda closure) queues more methods, and an inner
	 * class's flush must not re-process the methods already being handled
	 * here. */
	head = g_cpp_pending_methods;
	g_cpp_pending_methods = NULL;
	g_cpp_pending_methods_end = &g_cpp_pending_methods;

	for (pm = head; pm; pm = pm->next) {
		/* the class-body-following token (e.g. ';') currently sits in the
		 * global tok; push it back so the replayed body is parsed in front
		 * of it and next() returns to it afterwards. */
		struct token cur = tok;
		tokpush(&cur, 1);
		tokpush(pm->toks, pm->ntoks);
		next(); /* position tok at the first replayed token ('{') */
		cpp_parse_method_body(pm);
	}
}

/* Parse a deferred method body (D2) now that the member is actually
 * used.  `fd` is the mangled member decl whose body was deferred during
 * a class-template instantiation; the matching buffered body is removed
 * from the deferred table and parsed.  Returns true when the member is
 * defined afterwards (already-defined members are a no-op success). */
bool
cpp_ensure_method_defined(struct decl *fd)
{
	struct cpp_pending_method *pm, **prev;

	if (!fd || fd->defined)
		return true;
	for (prev = &g_cpp_deferred_methods; (pm = *prev); prev = &pm->next)
		if (pm->d == fd) {
			*prev = pm->next;
			/* removing the tail node would leave g_cpp_deferred_end
			 * dangling (later flushes would append to a detached node
			 * and lose the methods); re-anchor it to the new tail */
			if (!pm->next)
				g_cpp_deferred_end = prev;
			{
				/* Unlike flush_pending_methods (parse-phase), this may
				 * run mid-emit (funcexpr EXPRCALL), so the replayed body
				 * must not disturb the caller's token stream or the
				 * current function being emitted. */
				extern void tokpush(struct token *, size_t);
				extern struct func *curfunc;
				extern struct scope filescope;
				struct func *saved_cf = curfunc;
				struct token cur = tok;
				size_t depth = tokctx_depth();
				int bi;
				/* Re-install this instantiation's template parameter
				 * bindings: a later instantiation of the same template
				 * (`C<int,5>` then `C<int,7>`) has since overwritten the
				 * file-scope names, and this body must see its own. */
				for (bi = 0; bi < pm->nbinds; ++bi)
					scopeputdecl(&filescope, pm->binds[bi]);
				tokpush(&cur, 1);
				tokpush(pm->toks, pm->ntoks);
				next(); /* position tok at the first replayed token ('{') */
				cpp_parse_method_body(pm);
				tokctx_rewind(depth);
				tok = cur;
				curfunc = saved_cf;
			}
			free(pm->toks);
			free(pm);
			return true;
		}
	return fd->defined;
}


/* Define a member function as an out-of-line free function named
 * `ClassName_method` (class_tag is the enclosing struct/class tag).
 * Reuses the C function-definition machinery (mkdecl/mkfunc/stmt) via a
 * small clone of decl()'s DECLFUNC path.  The implicit `this` parameter
 * (Class *) is prepended to the mangled signature; inside the body, bare
 * member names resolve to `(*this).name` via cpp_member_ident. */
void
cpp_define_method(struct scope *s, struct type *funct, const char *mname,
                  const char *class_tag, bool is_const, bool is_static,
                  bool is_virtual)
{
	extern struct decl *mkdecl(char *, enum declkind, struct type *,
	    enum typequal, enum linkage);

	char mangled[256];
	char *pmangled;
	struct type *mtype, *classt;
	struct decl *d, *thisd, *cur, *nd, **end;

	if (!class_tag || !mname)
		return;

	{
		struct scope *qns = cpp_take_qual_ns();
		classt = scopegettag(qns ? qns : s, class_tag, true);
	}
	if (!classt || (classt->kind != TYPESTRUCT && classt->kind != TYPEUNION))
		error_code(E_CTYPE, &tok.loc, "'%s' is not a class type", class_tag);

	/* C++ virtual member (C.2.5): register the slot identity in the
	 * class's own_virtuals list (deduped across in-class decl + out-of-line
	 * definition); cpp_compute_vtable lays out the final slot indices.
	 * An override keeps the virtual-ness of the base method even without
	 * an explicit `virtual` keyword, so look the signature up in the
	 * bases' vtable layouts too. */
	{
		bool eff_virtual = is_virtual;
		char key[256];
		struct member *bm;

		g_cpp_define_virtual = false;
		if (!is_static) {
			cpp_vkey(mname, funct, is_const, key, sizeof key);
			if (!eff_virtual) {
				for (bm = classt->u.structunion.members; bm; bm = bm->next)
					if (!bm->name && bm->type &&
					    (bm->type->kind == TYPESTRUCT ||
					     bm->type->kind == TYPEUNION) &&
					    cpp_find_final(bm->type, key, NULL, NULL)) {
						eff_virtual = true;
						break;
					}
			}
			if (eff_virtual) {
				struct cpp_vslot *vs, **ve;
				for (vs = classt->u.structunion.own_virtuals; vs; vs = vs->next)
					if (strcmp(vs->key, key) == 0)
						break;
				if (!vs) {
					vs = xmalloc(sizeof *vs);
					vs->name = xmalloc(strlen(mname) + 1);
					strcpy((char *)vs->name, mname);
					memcpy(vs->key, key, sizeof key);
					vs->m = NULL;
					vs->owner = classt;
					vs->index = -1;
					vs->next = NULL;
					ve = &classt->u.structunion.own_virtuals;
					while (*ve)
						ve = &(*ve)->next;
					*ve = vs;
				}
				classt->u.structunion.own_poly = true;
				g_cpp_define_virtual = true;
			}
		}
	}

	/* deducing-this (P0847): a leading `this X& self` explicit object
	 * parameter replaces the implicit `this`.  It is excluded from the
	 * mangled overload signature (the object is passed like `this`), its
	 * cv-qualification selects the const "K" form, and an `X&&` object
	 * param gets a trailing "V" so the rvalue overload mangles apart from
	 * the lvalue `X&` form (the call site tries the V variant for
	 * temporary objects). */
	{
		struct decl *exobj = cpp_explicit_obj_take();
		bool has_exobj = exobj && funct->u.func.params == exobj;
		bool eff_const = is_const ||
		    (has_exobj && exobj->type->kind == TYPEPOINTER &&
		     (exobj->type->qual & QUALCONST));
		bool exobj_rref = has_exobj && exobj->type->isrref;
		struct decl *params = has_exobj ? funct->u.func.params->next
		                                : funct->u.func.params;

		snprintf(mangled, sizeof mangled, "%s_%s", class_tag, mname);
		/* const member functions get a distinct mangled name so a const
		 * object can only call const methods */
		if (eff_const)
			strncat(mangled, "K", sizeof mangled - strlen(mangled) - 1);
		/* overload resolution: append the encoded explicit parameter types
		 * (`Class_method_ii`); no-arg methods keep the bare mangled name */
		for (cur = params; cur; cur = cur->next) {
			char code[64];
			cpp_mangle_type(cur->type, code, sizeof code);
			strncat(mangled, code, sizeof mangled - strlen(mangled) - 1);
		}
		/* static members get a distinct mangled name (no `this`); the S
		 * goes after the parameter encoding to match
		 * cpp_mangled_name_args + "S" */
		if (is_static)
			strncat(mangled, "S", sizeof mangled - strlen(mangled) - 1);
		/* rvalue-object (`this X&& self`) overload: trailing V */
		if (exobj_rref)
			strncat(mangled, "V", sizeof mangled - strlen(mangled) - 1);
		/* mkdecl/scopeputdecl keep the name pointer; persist it off the
		 * stack (the C parser's token strings are stable, ours is not). */
		pmangled = xmalloc(strlen(mangled) + 1);
		strcpy(pmangled, mangled);

		/* Build the mangled function type:
		 * `Class_method(Class *this, args...) -> funct->base` (or just
		 * `Class_method(args...)` for a static member).  The declarator
		 * already parsed the explicit params into funct; we copy those
		 * decls so funct (kept in the member list for call lowering) and
		 * mtype don't share the same decl chain. */
		mtype = mktype(TYPEFUNC, 0);
		mtype->base = funct->base;
		mtype->qual = funct->qual;
		mtype->prop |= funct->prop;
		mtype->align = funct->align;
		mtype->u.func.isvararg = funct->u.func.isvararg;
		mtype->u.func.params = NULL;
		mtype->u.func.nparam = 0;
		thisd = NULL;
		end = &mtype->u.func.params;
		if (!is_static) {
			if (has_exobj) {
				/* deducing-this: the explicit object parameter is mtype
				 * param[0], so the method-body scope binds `self` (a
				 * reference that auto-derefs on use); the call site
				 * passes &obj, which is its lowered pointer.  The body's
				 * `this` resolves to the object parameter too
				 * (cpp_this_expr emits &(*self)). */
				struct decl *objd = mkdecl(exobj->name, DECLOBJECT,
				    exobj->type, exobj->qual, LINKNONE);
				objd->u.obj.storage = SDAUTO;
				*end = objd;
				end = &objd->next;
				++mtype->u.func.nparam;
				thisd = objd;
			} else {
				thisd = mkdecl("this", DECLOBJECT,
				    mkpointertype(classt,
				        eff_const ? QUALCONST : QUALNONE),
				    QUALNONE, LINKNONE);
				thisd->u.obj.storage = SDAUTO;
				*end = thisd;
				end = &thisd->next;
				++mtype->u.func.nparam;
			}
		}
		for (cur = params; cur; cur = cur->next) {
			nd = mkdecl(cur->name, DECLOBJECT, cur->type, cur->qual,
			            LINKNONE);
			nd->u.obj.storage = SDAUTO;
			*end = nd;
			end = &nd->next;
			++mtype->u.func.nparam;
		}
	}

	/* Register the mangled function symbol in the class's scope (the
	 * namespace scope for `namespace n { class C { ... }; }`) so the
	 * call lowering (postfixexpr TPERIOD) can resolve it from the
	 * object's class type. */
	{
		struct scope *ms = classt->scope ? classt->scope : s;
		d = scopegetdecl(ms, mangled, false);
		if (d && d->kind != DECLFUNC)
			error_code(E_REDEF, &tok.loc, "'%s' redeclared with different kind", mangled);
		if (d && d->type && !typecompatible(mtype, d->type))
			error_code(E_REDEF, &tok.loc, "'%s' redeclared with incompatible type", mangled);
		if (d && d->defined)
			error_tok_code(E_REDEF, &tok, "redefinition of member function '%s'", mangled);
		if (!d) {
			d = mkdecl(pmangled, DECLFUNC, mtype, QUALNONE, LINKEXTERN);
			scopeputdecl(ms, d);
		} else {
			d->type = typecomposite(mtype, d->type);
			free(pmangled);
		}
	}
	d->value = mkglobal(d);

	/* a ctor init list (`: Base(v)`) also means a function definition; the
	 * buffered body (from the ':' through the closing '}') is parsed by
	 * cpp_parse_method_body, which consumes the init list first */
	if (tok.kind != TLBRACE && tok.kind != TCOLON) {
		if (tok.kind == TSEMICOLON)
			next();
		return; /* declaration only */
	}

	/* Function definition.  Inside a class body the layout is not fixed
	 * yet, so buffer the body tokens and parse it after the class closes
	 * (two-phase: method bodies may use members declared later). */
	if (g_cpp_class_parsing) {
		buffer_method_body(s, classt, mtype, thisd, d, mname, class_tag,
		                   is_static);
		return;
	}
	{
		struct cpp_pending_method pm;
		pm.toks = NULL;
		pm.ntoks = 0;
		pm.mname = mname;
		pm.tag = class_tag;
		pm.classt = classt;
		pm.mtype = mtype;
		pm.thisd = thisd;
		pm.d = d;
		pm.s = s;
		pm.is_static = is_static;
		cpp_parse_method_body(&pm);
	}
}

/* Append one token to a synthesized token stream (cpp_synth_default_spaceship). */
void
cpp_ss_addtok(struct token **toks, size_t *n, enum tokenkind k,
    const char *lit, struct location loc)
{
	extern int tokenget(const void *, size_t);

	*toks = xreallocarray(*toks, *n + 1, sizeof **toks);
	/* identifiers get their per-name kind via tokenget (the same path
	 * the scanner uses): all identifiers share the TIDENT enum value,
	 * but the macro table and name lookups key on the per-name kind */
	if (k == TIDENT && lit)
		k = tokenget(lit, strlen(lit));
	(*toks)[*n].kind = k;
	(*toks)[*n].lit = (char *)lit;
	(*toks)[*n].loc = loc;
	(*toks)[*n].hide = false;
	(*toks)[*n].space = false;
	++*n;
}

/* Synthesize the body of a C++20 defaulted `<=>` (P0515):
 * `auto operator<=>(const T&) const = default;` compares every
 * non-static data member in declaration order and returns the first
 * non-zero result, else 0:
 *
 *     { if (x <=> rhs.x != 0) return x <=> rhs.x; ... return 0; }
 *
 * The synthesized body is tokenized and replayed through the normal
 * method-body path (cpp_define_method buffers it while the class body is
 * still being parsed and flushes it after layout), so the implicit `this`
 * and bare-member lowering (cpp_member_ident) apply as usual.  An unnamed
 * parameter (`const T&`) is given an internal name so the body can
 * reference the right-hand operand.  Base-class subobjects (anonymous
 * members) and static data members (not in the member list) are skipped. */
void
cpp_synth_default_spaceship(struct scope *s, struct type *funct,
    const char *mname, const char *class_tag, bool is_const)
{
	extern void tokpush(struct token *, size_t);
	extern void next(void);

	struct type *ct;
	struct member *m;
	struct decl *pd = funct->u.func.params;
	const char *rhs = "__mcc_ss_rhs";
	struct token *toks = NULL;
	size_t n = 0;
	struct location loc = tok.loc;

	/* the synthesized body refers to the RHS operand by parameter name;
	 * an unnamed parameter gets an internal name so the comparison can
	 * reference it */
	if (pd && !pd->name) {
		pd->name = xmalloc(strlen(rhs) + 1);
		strcpy((char *)pd->name, rhs);
	} else if (pd && pd->name) {
		rhs = pd->name;
	}
	if (!pd)
		error_code(E_DECL, &tok.loc,
		    "defaulted 'operator<=>' must take a parameter");

	ct = scopegettag(s, class_tag, true);
	cpp_ss_addtok(&toks, &n, TLBRACE, NULL, loc);
	if (ct) {
		for (m = ct->u.structunion.members; m; m = m->next) {
			/* skip member functions; base-class subobjects are
			 * anonymous members (no name to compare by) */
			if (!m->name || m->type->kind == TYPEFUNC)
				continue;
			/* if (m <=> rhs.m != 0) return m <=> rhs.m; */
			cpp_ss_addtok(&toks, &n, TIF, NULL, loc);
			cpp_ss_addtok(&toks, &n, TLPAREN, NULL, loc);
			cpp_ss_addtok(&toks, &n, TIDENT, m->name, loc);
			cpp_ss_addtok(&toks, &n, TSPACESHIP, NULL, loc);
			cpp_ss_addtok(&toks, &n, TIDENT, rhs, loc);
			cpp_ss_addtok(&toks, &n, TPERIOD, NULL, loc);
			cpp_ss_addtok(&toks, &n, TIDENT, m->name, loc);
			cpp_ss_addtok(&toks, &n, TNEQ, NULL, loc);
			cpp_ss_addtok(&toks, &n, TNUMBER, "0", loc);
			cpp_ss_addtok(&toks, &n, TRPAREN, NULL, loc);
			cpp_ss_addtok(&toks, &n, TRETURN, NULL, loc);
			cpp_ss_addtok(&toks, &n, TIDENT, m->name, loc);
			cpp_ss_addtok(&toks, &n, TSPACESHIP, NULL, loc);
			cpp_ss_addtok(&toks, &n, TIDENT, rhs, loc);
			cpp_ss_addtok(&toks, &n, TPERIOD, NULL, loc);
			cpp_ss_addtok(&toks, &n, TIDENT, m->name, loc);
			cpp_ss_addtok(&toks, &n, TSEMICOLON, NULL, loc);
		}
	}
	cpp_ss_addtok(&toks, &n, TRETURN, NULL, loc);
	cpp_ss_addtok(&toks, &n, TNUMBER, "0", loc);
	cpp_ss_addtok(&toks, &n, TSEMICOLON, NULL, loc);
	cpp_ss_addtok(&toks, &n, TRBRACE, NULL, loc);

	/* replay through the normal method-definition path: keep the token
	 * after the consumed ';' (the next class member or '}') on the
	 * stream, push the synthesized body in front, and let
	 * cpp_define_method buffer it (in-class, g_cpp_class_parsing) or
	 * parse it directly */
	{
		struct token cur = tok;
		tokpush(&cur, 1);
		tokpush(toks, n);
		next(); /* position at the body's '{' */
		cpp_define_method(s, funct, mname, class_tag, is_const, false,
		    false);
	}
	free(toks);
}
