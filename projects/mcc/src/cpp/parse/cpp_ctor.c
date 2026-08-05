/* cpp_ctor.c — m++ (C++) constructor/destructor emission.
 *
 * Emits default/parameterized constructor calls, base-constructor and
 * init-list handling, destructor emission (including __mxx_global_var_init
 * runtime teardown), and scope-object destruction order.  Entry points
 * cpp_has_ctor / cpp_emit_default_ctor / cpp_has_dtor / cpp_emit_dtor /
 * cpp_emit_scope_dtors / cpp_emit_ctor_call are exported; members
 * cpp_emit_base_ctor / cpp_parse_init_list / cpp_emit_global_dtor are
 * called from the method-body replay in cpp_parse.c.
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

/* Internal module state (call-before-definition forward declarations). */
static void emit_base_ctors_for(struct func *f, struct type *classt,
                                struct expr *thisp);
static void emit_base_dtors_for(struct func *f, struct type *classt,
                                struct expr *thisp);
static bool has_base(struct type *t);

bool
cpp_is_member_function(struct type *t, const char *name)
{
	return cpp_method_member(t, name, NULL) != NULL;
}

/* Class-qualified member lookup for `obj.Base::get()`: resolve `name`
 * in `t`'s scope, where the qualified class's own members shadow base
 * members, then each direct base in turn (same shadowing rule
 * recursively).  `*offset` accumulates the byte offset from `t`'s start
 * (in caller units).  Unlike typemember (which recurses into a base as
 * soon as it is seen), this makes the qualified class's own member win
 * over an inherited one of the same name. */
struct member *
cpp_qualified_member(struct type *t, const char *name,
    unsigned long long *offset)
{
	struct member *m, *sub;

	if (!t || (t->kind != TYPESTRUCT && t->kind != TYPEUNION))
		return NULL;
	for (m = t->u.structunion.members; m; m = m->next) {
		if (m->name && strcmp(m->name, name) == 0) {
			*offset += m->offset;
			return m;
		}
	}
	for (m = t->u.structunion.members; m; m = m->next) {
		if (m->name || !m->type ||
		    (m->type->kind != TYPESTRUCT && m->type->kind != TYPEUNION))
			continue;
		sub = cpp_qualified_member(m->type, name, offset);
		if (sub) {
			*offset += m->offset;
			return sub;
		}
	}
	return NULL;
}

/* Is class `t` derived from (or identical to) class `base`?  The base
 * subobject appears as an anonymous member of the derived class. */
bool
cpp_is_derived(struct type *t, struct type *base)
{
	struct member *m;

	if (!t || !base || (t->kind != TYPESTRUCT && t->kind != TYPEUNION))
		return false;
	if (t == base)
		return true;
	for (m = t->u.structunion.members; m; m = m->next) {
		if (!m->name && cpp_is_derived(m->type, base))
			return true;
	}
	return false;
}

/* Does class `t` (tag `tag`) define a constructor?  A constructor is a
 * function member whose name equals the class tag (lowered to
 * `Class_Class`). */
bool
cpp_has_ctor(struct type *t, const char *tag)
{
	if (!t || (t->kind != TYPESTRUCT && t->kind != TYPEUNION) || !tag)
		return false;
	return cpp_is_member_function(t, tag);
}

/* Emit a call to the default constructor of class-typed local `d` after
 * its storage is laid out (`Counter c;` -> `Counter_Class(&c)`).  No-op
 * for non-class types, classes without a constructor, or global objects
 * (static init is a later stage).  Returns whether a call was emitted. */
bool
cpp_emit_default_ctor(struct func *f, struct decl *d)
{
	extern struct value *funcexpr(struct func *, struct expr *);
	extern struct scope filescope;

	struct type *t = d->type;
	const char *tag;
	char mname[256];
	struct decl *fd;
	struct expr *fn, *obj, *call;

	if (!f || !d || d->u.obj.storage != SDAUTO)
		return false;
	tag = t ? t->u.structunion.tag : NULL;
	if (!tag)
		return false;

	obj = mkexpr(EXPRIDENT, d->type, NULL);
	obj->qual = d->qual;
	obj->lvalue = true;
	obj->u.ident.decl = d;
	obj = mkunaryexpr(TBAND, obj); /* &obj */

	/* A class with no user constructor of its own still needs its base
	 * subobjects constructed and its vptrs installed. */
	if (!cpp_has_ctor(t, tag)) {
		bool any = false;
		if (has_base(t)) {
			emit_base_ctors_for(f, t, obj);
			any = true;
		}
		if (t->u.structunion.poly) {
			cpp_init_vptrs(f, t, obj);
			any = true;
		}
		return any;
	}

	snprintf(mname, sizeof mname, "%s_%s", tag, tag);
	fd = scopegetdecl(t->scope ? t->scope : &filescope, mname, true);
	if (!fd || fd->kind != DECLFUNC)
		return false;

	fn = mkexpr(EXPRIDENT, fd->type, NULL);
	fn->u.ident.decl = fd;
	fn = decay(fn); /* &Class_Class */

	call = mkexpr(EXPRCALL, &typevoid, fn);
	call->u.call.args = obj;
	call->u.call.nargs = 1;
	funcexpr(f, call);
	return true;
}

/* Emit calls to the user default constructors of the direct base
 * subobjects of `classt` (each at its layout offset from `thisp`).
 * A base/member with no user constructor still needs its own bases and
 * data members constructed, so the walk recurses (the vptrs of the whole
 * object are installed afterwards by cpp_init_vptrs). */
static void
emit_base_ctors_for(struct func *f, struct type *classt, struct expr *thisp)
{
	extern struct value *funcexpr(struct func *, struct expr *);
	extern struct scope filescope;

	struct member *m;
	struct type *bt;
	char mname[256];
	struct decl *fd;
	struct expr *fn, *call, *call_this;

	for (m = classt->u.structunion.members; m; m = m->next) {
		/* destructor marker members are not objects */
		if (m->name && m->name[0] == '~')
			continue;
		bt = m->type;
		if (!bt || (bt->kind != TYPESTRUCT && bt->kind != TYPEUNION)) {
			/* scalar member: no ctor call, but a ctor initializer-list
			 * item `: x(v)` must land the value — emit
			 * `*(this + offset) = v`.  Previously the item was dropped
			 * by the `continue`, leaving the scalar member unwritten
			 * (D4: by-value returns of such classes were garbage). */
			if (m->name && bt && bt->kind != TYPEFUNC) {
				struct cpp_init_item *it;
				for (it = g_cpp_init_items; it; it = it->next)
					if (strcmp(it->name, m->name) == 0)
						break;
				if (it && it->args) {
					struct expr *base = mkbinaryexpr(&tok.loc,
					    TADD, exprconvert(thisp, &typeulong),
					    mkconstexpr(&typeulong, m->offset));
					base->type = mkpointertype(bt, QUALNONE);
					struct expr *dst = mkunaryexpr(TMUL, base);
					dst->type = bt;
					dst->lvalue = true;
					funcexpr(f, mkassignexpr(dst, it->args));
				}
			}
			continue;
		}
		if (m->name && m->type && m->type->kind == TYPEFUNC)
			continue; /* member functions occupy no storage */
		if (m->offset) {
			/* this + subobject offset points at this base's subobject
			 * (or class-type member object), always relative to the
			 * original `this` */
			call_this = mkbinaryexpr(&tok.loc, TADD,
			    exprconvert(thisp, &typeulong),
			    mkconstexpr(&typeulong, m->offset));
			call_this->type = mkpointertype(bt, QUALNONE);
		} else {
			call_this = thisp;
		}
		/* A ctor-init-list item names this base/member: `Base(v)` /
		 * `m(v)`.  It may select a constructor overload, so resolve the
		 * mangled name from the argument expression types (like an
		 * object declaration `Point p(3)`).  A direct base is registered
		 * as an anonymous member, so its class tag is the name used in
		 * the init list. */
		{
			const char *key = m->name ? m->name :
			    (bt->u.structunion.tag ? bt->u.structunion.tag : NULL);
			struct cpp_init_item *match = NULL;
			if (key) {
				struct cpp_init_item *it;
				for (it = g_cpp_init_items; it; it = it->next)
					if (strcmp(it->name, key) == 0) {
						match = it;
						break;
					}
			}
			if (match && match->args) {
				/* explicit initializer: select the matching overload */
				char code[256];
				cpp_mangled_name_args(bt, bt->u.structunion.tag,
				    match->args, code, sizeof code, true);
				fd = scopegetdecl(bt->scope ? bt->scope : &filescope,
				    code, true);
				if (!fd || fd->kind != DECLFUNC) {
					cpp_mangled_name_args(bt, bt->u.structunion.tag,
					    match->args, code, sizeof code, false);
					fd = scopegetdecl(bt->scope ? bt->scope : &filescope,
					    code, true);
				}
				if (!fd || fd->kind != DECLFUNC)
					error_code(E_DECL, &tok.loc, "no matching constructor for '%s' in initializer list", key);
			} else {
				/* no explicit initializer: default construction */
				if (!bt->u.structunion.tag ||
				    !cpp_has_ctor(bt, bt->u.structunion.tag)) {
					if (has_base(bt))
						emit_base_ctors_for(f, bt, call_this);
					continue;
				}
				snprintf(mname, sizeof mname, "%s_%s",
				    bt->u.structunion.tag, bt->u.structunion.tag);
				fd = scopegetdecl(bt->scope ? bt->scope : &filescope,
				    mname, true);
				if (!fd || fd->kind != DECLFUNC)
					continue;
			}
			fn = mkexpr(EXPRIDENT, fd->type, NULL);
			fn->u.ident.decl = fd;
			fn = decay(fn); /* &Class_Class */
			call = mkexpr(EXPRCALL, &typevoid, fn);
			call->u.call.args = call_this;
			call->u.call.nargs = 1;
			if (match && match->args) {
				/* reference parameters (copy/move ctors) receive
				 * the address of the argument */
				struct decl *p = fd->type->u.func.params ?
				    fd->type->u.func.params->next : NULL;
				struct expr *a, **end = &call_this->next;
				for (a = match->args; a; a = a->next, p = p ? p->next : NULL) {
					struct expr *arg = a;
					if (p && p->type && p->type->isref)
						arg = mkunaryexpr(TBAND, a);
					*end = arg;
					end = &arg->next;
					++call->u.call.nargs;
				}
			}
			funcexpr(f, call);
		}
	}
}

/* Emit implicit base-class construction at the start of a derived-class
 * constructor: `class D : B { D() {...} }` first runs `B_B(&this)` for
 * each direct base with a user default constructor. */
static void cpp_emit_delegating_ctor(struct func *f);

void
cpp_emit_base_ctor(struct func *f)
{
	emit_base_ctors_for(f, g_cpp_method.class_type, cpp_this_expr());
	cpp_emit_delegating_ctor(f);
}

/* C++11 delegating constructor: `P() : P(7, 8) {}` forwards to another
 * constructor of the same class.  The init-list item names the class's own
 * tag (an entry that is not a base or member), so it is left unconsumed by
 * emit_base_ctors_for.  Emit `P_P(&this, args…)` for it.  In standard C++
 * a delegating ctor's own body must be empty; m++ does not enforce that,
 * but the forwarded ctor initialises all members either way. */
static void
cpp_emit_delegating_ctor(struct func *f)
{
	extern struct value *funcexpr(struct func *, struct expr *);
	extern struct scope filescope;
	struct type *classt = g_cpp_method.class_type;
	const char *tag = classt->u.structunion.tag;
	struct cpp_init_item *it;

	if (!tag)
		return;
	for (it = g_cpp_init_items; it; it = it->next) {
		struct expr *fn, *call, *this_ = cpp_this_expr();
		struct decl *fd, *p;
		char code[256];
		struct expr *a, **end;

		if (strcmp(it->name, tag) != 0)
			continue;
		/* this is the delegating item; build P_P(&this, args…) */
		cpp_mangled_name_args(classt, tag, it->args, code, sizeof code, true);
		fd = scopegetdecl(classt->scope ? classt->scope : &filescope,
		    code, true);
		if (!fd || fd->kind != DECLFUNC) {
			cpp_mangled_name_args(classt, tag, it->args,
			    code, sizeof code, false);
			fd = scopegetdecl(classt->scope ? classt->scope : &filescope,
			    code, true);
		}
		if (!fd || fd->kind != DECLFUNC)
			error_code(E_DECL, &tok.loc,
			    "no matching constructor '%s' for delegating initializer", it->name);
		fn = mkexpr(EXPRIDENT, fd->type, NULL);
		fn->u.ident.decl = fd;
		fn = decay(fn);   /* &P_P */
		call = mkexpr(EXPRCALL, &typevoid, fn);
		call->u.call.args = this_;
		call->u.call.nargs = 1;
		p = fd->type->u.func.params ? fd->type->u.func.params->next : NULL;
		end = &this_->next;
		for (a = it->args; a; a = a->next, p = p ? p->next : NULL) {
			struct expr *arg = a;
			if (p && p->type && p->type->isref)
				arg = mkunaryexpr(TBAND, a);
			*end = arg;
			end = &arg->next;
			++call->u.call.nargs;
		}
		funcexpr(f, call);
		break;
	}
}

/* Parse a constructor initializer list `: Base(v), m(v * 2)` that sits
 * between the ctor's parameter list and its body.  Each item is a bare
 * name (base-class tag or data member) followed by parenthesized
 * argument expressions; the arguments are parsed as expressions and
 * kept for emit_base_ctors_for.  The trailing '{' of the body is left
 * for stmt() to consume. */
void
cpp_parse_init_list(struct func *f, struct scope *fs)
{
	extern struct expr *condexpr(struct scope *);
	struct cpp_init_item *it;

	if (tok.kind != TCOLON) {
		return;
	}
	next(); /* consume ':' */
	for (;;) {
		if (tok.kind < TIDENT)
			error_code(E_DECL, &tok.loc, "expected member or base name in ctor initializer list");
		it = xmalloc(sizeof *it);
		it->name = tokenstr(tok.kind);
		it->args = NULL;
		it->next = NULL;
		next();
		expect(TLPAREN, "'(' after initializer name");
		{
			/* one or more comma-separated argument expressions (parsed
			 * with condexpr so the ',' between init items is not eaten);
			 * an empty argument list (`Base()`) is valid too.
			 *
			 * The enclosing ctor's func (`f`) is not yet `curfunc` here
			 * (the body hasn't been parsed), but the argument expressions
			 * may contain `new` (e.g. `: p(new int(v))`), which needs an
			 * active func to emit its allocation/ctor instructions. */
			struct func *saved_curfunc = curfunc;
			struct expr *head = NULL, **ae = &head;
			curfunc = f;
			for (;;) {
				struct expr *a;
				if (tok.kind == TRPAREN)
					break;
				a = condexpr(fs);
				*ae = a;
				ae = &a->next;
				if (tok.kind != TCOMMA)
					break;
				next(); /* consume ',' between args */
			}
			curfunc = saved_curfunc;
			it->args = head;
		}
		expect(TRPAREN, "')' after initializer arguments");
		*g_cpp_init_end = it;
		g_cpp_init_end = &it->next;
		if (tok.kind == TCOMMA) {
			next();
			continue;
		}
		break;
	}
}

/* Emit calls to the user destructors of the base subobjects of `classt`,
 * each at its layout offset from `thisp`.  Called at the end of a
 * derived-class destructor body so destruction runs in reverse
 * construction order (derived body → each base's own `Base_dtor`, which
 * recurses into its bases).  A base with no user destructor still needs
 * its own bases destroyed, so the walk recurses. */
static void
emit_base_dtors_for(struct func *f, struct type *classt, struct expr *thisp)
{
	extern struct value *funcexpr(struct func *, struct expr *);
	extern struct scope filescope;

	struct member *m;
	struct type *bt;
	char mname[256];
	struct decl *fd;
	struct expr *fn, *call, *call_this;

	for (m = classt->u.structunion.members; m; m = m->next) {
		/* destructor marker members are not objects */
		if (m->name && m->name[0] == '~')
			continue;
		bt = m->type;
		if (!bt || (bt->kind != TYPESTRUCT && bt->kind != TYPEUNION))
			continue;
		if (m->name && m->type && m->type->kind == TYPEFUNC)
			continue; /* member functions occupy no storage */
		if (m->offset) {
			/* this + subobject offset points at this base's subobject
			 * (or class-type member object), always relative to the
			 * original `this` */
			call_this = mkbinaryexpr(&tok.loc, TADD,
			    exprconvert(thisp, &typeulong),
			    mkconstexpr(&typeulong, m->offset));
			call_this->type = mkpointertype(bt, QUALNONE);
		} else {
			call_this = thisp;
		}
		if (!bt->u.structunion.tag || !cpp_has_dtor(bt)) {
			/* no user destructor: still destroy its own bases */
			if (has_base(bt))
				emit_base_dtors_for(f, bt, call_this);
			continue;
		}
		snprintf(mname, sizeof mname, "%s_dtor", bt->u.structunion.tag);
		fd = scopegetdecl(bt->scope ? bt->scope : &filescope, mname, true);
		if (!fd || fd->kind != DECLFUNC)
			continue;
		fn = mkexpr(EXPRIDENT, fd->type, NULL);
		fn->u.ident.decl = fd;
		fn = decay(fn); /* &Base_dtor */
		call = mkexpr(EXPRCALL, &typevoid, fn);
		call->u.call.args = call_this;
		call->u.call.nargs = 1;
		funcexpr(f, call);
	}
}

/* Emit implicit base-class destruction at the end of a derived-class
 * destructor: `class D : B { ~D() {...} }` runs each direct base's
 * `B_dtor(&this)` after the derived body. */
void
cpp_emit_base_dtor(struct func *f)
{
	emit_base_dtors_for(f, g_cpp_method.class_type, cpp_this_expr());
}

/* Does `t` have at least one direct base class (anonymous subobject)? */
static bool
has_base(struct type *t)
{
	struct member *m;

	if (!t || (t->kind != TYPESTRUCT && t->kind != TYPEUNION))
		return false;
	for (m = t->u.structunion.members; m; m = m->next)
		if (!m->name)
			return true;
	return false;
}

/* Does class `t` define a destructor?  Destructors are registered under a
 * `~Class` marker member name (see structdecl's TILDE branch). */
bool
cpp_has_dtor(struct type *t)
{
	struct member *m;

	if (!t || (t->kind != TYPESTRUCT && t->kind != TYPEUNION))
		return false;
	for (m = t->u.structunion.members; m; m = m->next)
		if (m->name && m->name[0] == '~' &&
		    m->type && m->type->kind == TYPEFUNC)
			return true;
	return false;
}

/* Emit a call to the destructor of local class-typed `d` at end of scope
 * (`Class_dtor(&obj)`).  No-op for non-class types or classes without a
 * destructor.  Returns whether a call was emitted. */
bool
cpp_emit_dtor(struct func *f, struct decl *d)
{
	extern struct value *funcexpr(struct func *, struct expr *);
	extern struct scope filescope;

	struct type *t = d->type;
	const char *tag;
	char mname[256];
	struct decl *fd;
	struct expr *fn, *obj, *call;

	if (!f || !d || d->u.obj.storage != SDAUTO)
		return false;
	tag = t ? t->u.structunion.tag : NULL;
	if (!tag || !cpp_has_dtor(t))
		return false;
	snprintf(mname, sizeof mname, "%s_dtor", tag);
	fd = scopegetdecl(t->scope ? t->scope : &filescope, mname, true);
	if (!fd || fd->kind != DECLFUNC)
		return false;

	fn = mkexpr(EXPRIDENT, fd->type, NULL);
	fn->u.ident.decl = fd;
	fn = decay(fn); /* &Class_dtor */

	obj = mkexpr(EXPRIDENT, d->type, NULL);
	obj->qual = d->qual;
	obj->lvalue = true;
	obj->u.ident.decl = d;
	obj = mkunaryexpr(TBAND, obj); /* &obj */

	call = mkexpr(EXPRCALL, &typevoid, fn);
	call->u.call.args = obj;
	call->u.call.nargs = 1;
	funcexpr(f, call);
	d->dtor_done = true;
	return true;
}

/* Emit a destructor call for a global class object (`Class_dtor(&g)`)
 * into __mxx_global_var_fini.  No-op for classes without a destructor. */
void
cpp_emit_global_dtor(struct func *f, struct decl *d)
{
	extern struct value *funcexpr(struct func *, struct expr *);
	extern struct scope filescope;

	struct type *t = d->type;
	const char *tag;
	char mname[256];
	struct decl *fd;
	struct expr *fn, *obj, *call;

	if (!f || !d)
		return;
	tag = t ? t->u.structunion.tag : NULL;
	if (!tag || !cpp_has_dtor(t))
		return;
	snprintf(mname, sizeof mname, "%s_dtor", tag);
	fd = scopegetdecl(t->scope ? t->scope : &filescope, mname, true);
	if (!fd || fd->kind != DECLFUNC)
		return;

	fn = mkexpr(EXPRIDENT, fd->type, NULL);
	fn->u.ident.decl = fd;
	fn = decay(fn); /* &Class_dtor */

	obj = mkexpr(EXPRIDENT, d->type, NULL);
	obj->qual = d->qual;
	obj->lvalue = true;
	obj->u.ident.decl = d;
	obj = mkunaryexpr(TBAND, obj); /* &g */

	call = mkexpr(EXPRCALL, &typevoid, fn);
	call->u.call.args = obj;
	call->u.call.nargs = 1;
	funcexpr(f, call);
}

/* Emit destructor calls for all local class objects declared in scope `s`
 * when a block exits (reverse declaration order not yet implemented).
 * Called by stmt()'s compound-statement branch before delscope. */
void
cpp_emit_scope_dtors(struct func *f, struct scope *s)
{
	struct decl *d;

	if (!f || !s)
		return;
	/* objects is head-inserted in declaration order, so walking it
	 * front-to-back destroys in reverse declaration order (C++). */
	for (d = s->objects; d; d = d->next)
		if (!d->dtor_done)
			cpp_emit_dtor(f, d);
}

/* Emit a constructor call with explicit arguments for `Point p(3, 4);`
 * (`Point_Point(&p, 3, 4)`).  `args` is the expression list collected by
 * declarator(); the this address is prepended. */
void
cpp_emit_ctor_call(struct func *f, struct decl *d, struct expr *args)
{
	extern struct value *funcexpr(struct func *, struct expr *);
	extern struct scope filescope;

	struct type *t = d->type;
	const char *tag;
	char mname[256];
	struct decl *fd, *p;
	struct expr *fn, *obj, *call, *a, **end;
	size_t n = 0;

	if (!f || !d)
		return;
	tag = t ? t->u.structunion.tag : NULL;
	if (!tag || !cpp_has_ctor(t, tag)) {
		/* aggregate (no user ctor): direct-initialize the data members
		 * positionally — `P p(1, 2)` lowers to `p.a = 1; p.b = 2;` */
		struct expr *obj;
		struct member *m;
		struct expr *a;
		obj = mkexpr(EXPRIDENT, d->type, NULL);
		obj->qual = d->qual;
		obj->lvalue = true;
		obj->u.ident.decl = d;
		obj = mkunaryexpr(TBAND, obj); /* &p */
		for (m = t ? t->u.structunion.members : NULL, a = args;
		     m && a; m = m->next, a = a->next) {
			struct expr *base, *dst;
			if (m->name && m->name[0] == '~')
				continue;
			if (m->type && m->type->kind == TYPEFUNC)
				continue;
			base = mkbinaryexpr(&tok.loc, TADD,
			    exprconvert(obj, &typeulong),
			    mkconstexpr(&typeulong, m->offset));
			base->type = mkpointertype(m->type, QUALNONE);
			dst = mkunaryexpr(TMUL, base);
			dst->type = m->type;
			dst->lvalue = true;
			funcexpr(f, mkassignexpr(dst, a));
		}
		return;
	}
	/* overload resolution: append the encoded argument types (reference
	 * binding first for lvalue args, falling back to by-value) */
	{
		char code[256];
		cpp_mangled_name_args(t, tag, args, code, sizeof code, true);
		snprintf(mname, sizeof mname, "%s", code);
	}
	fd = scopegetdecl(t->scope ? t->scope : &filescope, mname, true);
	if (!fd || fd->kind != DECLFUNC) {
		char code2[256];
		cpp_mangled_name_args(t, tag, args, code2, sizeof code2, false);
		fd = scopegetdecl(t->scope ? t->scope : &filescope, code2, true);
		if (fd && fd->kind == DECLFUNC)
			snprintf(mname, sizeof mname, "%s", code2);
	}
	if (!fd || fd->kind != DECLFUNC) {
		error_code(E_DECL, &tok.loc, "no matching constructor for object '%s'", d->name);
		return;
	}

	fn = mkexpr(EXPRIDENT, fd->type, NULL);
	fn->u.ident.decl = fd;
	fn = decay(fn); /* &Point_Point */

	obj = mkexpr(EXPRIDENT, d->type, NULL);
	obj->qual = d->qual;
	obj->lvalue = true;
	obj->u.ident.decl = d;
	obj = mkunaryexpr(TBAND, obj); /* &p */

	call = mkexpr(EXPRCALL, &typevoid, fn);
	call->u.call.args = obj;
	call->u.call.nargs = 1;
	end = &obj->next;
	/* reference parameters (copy/move ctors: `Vec(Vec &o)`, `Vec(Vec &&o)`)
	 * receive the address of the argument, like member-function calls;
	 * by-value parameters receive the value. */
	for (a = args, p = fd->type->u.func.params ? fd->type->u.func.params->next : NULL;
	     a; a = a->next, p = p ? p->next : NULL) {
		struct expr *arg = a;
		if (p && p->type && p->type->isref)
			arg = mkunaryexpr(TBAND, a);
		*end = arg;
		end = &arg->next;
		++call->u.call.nargs;
	}
	(void)n;
	funcexpr(f, call);
}
