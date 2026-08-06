/* cpp_newdel_thunk.c - m++ (C++) exception payload thunks (phase 4).
 *
 * Stage C.3.1: split from cpp_newdel.c.  Per-class copy/dtor thunk
 * synthesis for non-trivial class exceptions (the runtime packs the
 * thrown object with optional copy/dtor helpers, so the front-end
 * pre-registers two free helper functions and emits their bodies at
 * end of translation unit):
 *   - exc_has_trivial_dtor / exc_has_user_dtor / exc_has_user_copy_ctor
 *     / exc_has_trivial_copy  (whether the runtime needs a thunk)
 *   - exc_base_offset / exc_member_is_base  (first base sub-object offset,
 *     used by both the thunk and the catch slicing)
 *   - exc_thunk_type / exc_register_thunks  (decl-side thunk pre-registration)
 *   - exc_emit_copy_thunk / exc_emit_dtor_thunk  (definition-side body emit)
 *   - cpp_emit_exc_thunks  (end-of-TU walker; called by cpp_parse.c)
 *   - cpp_synthesize_exc_copy_thunk / cpp_synthesize_exc_dtor_thunk
 *     (idempotent registration from the throw call site in cpp_newdel_exc.c)
 *
 * Uses cpp_ctor_expr from cpp_newdel_expr.c for overload resolution of
 * the user copy constructor inside the copy thunk body.
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

/* Cross-file: ctor overload resolution helper (defined in
 * cpp_newdel_expr.c). */
extern struct expr *cpp_ctor_expr(struct type *t, struct expr *thisp,
                                  struct expr *args);

/* Phase-4 object-payload helpers (exc-phase4-object-payload.md).
 * - exc_has_trivial_copy/dtor: whether the class type needs no user copy /
 *   destructor (trivial aggregate -> runtime memcpy / no destruction).
 *   A class with no declared constructors/destructors is trivial for the
 *   exception payload (we copy its bytes and never destroy it).
 * - exc_base_offset: offset of the first base subobject (for base-catch
 *   slicing); 0 for non-derived.
 * - cpp_exc_throw_call_scalar: the legacy class-throw path that emits
 *   `_meuos_exc_throw(tc, 0)` (type code only, no member payload). */
int exc_has_trivial_copy(struct type *t);
int exc_has_user_copy_ctor(struct type *t);
int exc_has_trivial_dtor(struct type *t);
int exc_base_offset(struct type *t);
static int exc_member_is_base(struct member *m);

int
exc_has_trivial_dtor(struct type *t)
{
	/* No user-declared destructor (a member whose name begins with '~' or
	 * an explicit dtor function) -> no destruction required on the payload. */
	return !cpp_has_dtor(t);
}

/* Does class `t` declare a user destructor (not inherited)?  Checks the
 * member list for a `~Class` member (name begins with '~', type is
 * TYPEFUNC), which the class-body parser inserts for a user-declared
 * destructor.  Unlike `cpp_has_dtor`, this does NOT return true for an
 * inherited destructor — the exception thunk must dispatch to the exact
 * type's own dtor function. */
int
exc_has_user_dtor(struct type *t)
{
	struct member *m;

	if (!t || (t->kind != TYPESTRUCT && t->kind != TYPEUNION))
		return 0;
	for (m = t->u.structunion.members; m; m = m->next)
		if (m->name && m->name[0] == '~' &&
		    m->type && m->type->kind == TYPEFUNC)
			return 1;
	return 0;
}

/* Does class `t` declare a user-defined copy constructor?  A copy ctor
 * is a member function named `tag` (the class name) with exactly one
 * explicit parameter that is a C++ reference to T (TYPEPOINTER with
 * isref, base = class type).  The member's function type stores the
 * user-declared parameters directly (no implicit `this` prepended).
 * Both `T(const T&)` and `T(T&)` match. */
int
exc_has_user_copy_ctor(struct type *t)
{
	struct member *m;
	const char *tag;

	if (!t || (t->kind != TYPESTRUCT && t->kind != TYPEUNION))
		return 0;
	tag = t->u.structunion.tag;
	if (!tag)
		return 0;
	for (m = t->u.structunion.members; m; m = m->next) {
		struct decl *p;
		if (!m->name || m->name[0] == '~')
			continue;
		if (strcmp(m->name, tag) != 0)
			continue;
		if (!m->type || m->type->kind != TYPEFUNC)
			continue;
		p = m->type->u.func.params;
		if (!p || !p->type || p->type->kind != TYPEPOINTER)
			continue;
		if (!p->type->isref)
			continue;
		if (p->type->base == t)
			return 1;
	}
	return 0;
}

int
exc_has_trivial_copy(struct type *t)
{
	/* Byte-copyable when the class declares no user copy ctor AND no
	 * user dtor.  If either exists the runtime needs real copy/dtor
	 * thunks to preserve the object across the throw. */
	return !exc_has_user_copy_ctor(t) && exc_has_trivial_dtor(t);
}

int
exc_base_offset(struct type *t)
{
	/* Offset of the first base sub-object within an instance of `t`, or 0
	 * when `t` has no base (so the throw carries the full object).  Bases
	 * are inserted by the class-body parser as anonymous members (name ==
	 * NULL) before any data member (see cpp_parse.c around `addmember`,
	 * with bases[]); for `struct D : B`, the first member is the Base
	 * subobject at offset 0; for `struct D : A, B` the first member is
	 * A at offset 0 (B sits at sizeof(A), reported here for the first
	 * base only).  The hidden vptr (cpp_insert_vptr) carries the name
	 * "__vptr", so the anonymous test naturally excludes it. */
	struct member *m;

	if (!t || (t->kind != TYPESTRUCT && t->kind != TYPEUNION))
		return 0;
	for (m = t->u.structunion.members; m; m = m->next) {
		if (exc_member_is_base(m))
			return (int)m->offset;
	}
	return 0;
}

/* A member is a base placeholder when the front-end registered it as the
 * carrier of a base-class sub-object: anonymous (name == NULL) and of a
 * class type (TYPESTRUCT / TYPEUNION).  Anonymous unions that are NOT a
 * base do not exist in mcc (the parser only inserts name==NULL members
 * for the base list), so this check is precise.  The hidden vptr member
 * inserted by cpp_insert_vptr has name "__vptr" and is excluded by the
 * anonymous check. */
static int
exc_member_is_base(struct member *m)
{
	if (!m || !m->type)
		return 0;
	if (m->name)
		return 0;
	return m->type->kind == TYPESTRUCT || m->type->kind == TYPEUNION;
}
/* --- exception payload thunk synthesis (phase 4b) ------------------- */

/* Per-class thunk records.  When a non-trivial class is thrown, the
 * front-end pre-registers two free helper functions and adds the record
 * to g_cpp_exc_thunks; the bodies are built lazily on first reference
 * (so we can call the user copy ctor / dtor via overload resolution with
 * the arguments in hand) and emitted at end of translation unit by
 * cpp_emit_exc_thunks.  One record per class type — repeated throws of
 * the same class share the same thunk pair.  The `struct cpp_exc_thunk`
 * definition lives in cpp_internal.h so the throw site (cpp_newdel_exc.c)
 * can dereference the registered copy/dtor decls. */
static struct cpp_exc_thunk *g_cpp_exc_thunks;
static struct cpp_exc_thunk **g_cpp_exc_thunks_end = &g_cpp_exc_thunks;

/* Build the function type for a thunk:
 *   copy: void f(void *dst, const void *src)
 *   dtor: void f(void *self)                     */
static struct type *
exc_thunk_type(struct type *t, int is_copy)
{
	(void)t;
	struct type *ft = mktype(TYPEFUNC, 0);
	struct decl *p;

	ft->base = &typevoid;
	ft->u.func.isvararg = false;
	ft->u.func.params = NULL;
	ft->u.func.nparam = 0;
	if (is_copy) {
		p = mkdecl("dst", DECLOBJECT,
		    mkpointertype(&typevoid, QUALNONE), QUALNONE, LINKNONE);
		p->u.obj.storage = SDAUTO;
		p->isparam = true;
		ft->u.func.params = p;
		p->next = mkdecl("src", DECLOBJECT,
		    mkpointertype(&typevoid, QUALCONST), QUALNONE, LINKNONE);
		p->next->u.obj.storage = SDAUTO;
		p->next->isparam = true;
		ft->u.func.nparam = 2;
	} else {
		p = mkdecl("self", DECLOBJECT,
		    mkpointertype(&typevoid, QUALNONE), QUALNONE, LINKNONE);
		p->u.obj.storage = SDAUTO;
		p->isparam = true;
		ft->u.func.params = p;
		ft->u.func.nparam = 1;
	}
	return ft;
}

/* Pre-register the thunk pair for class `t` in the file scope (the decls
 * need to be addressable from the throw site via a normal function call),
 * and queue the record for end-of-TU body emission.  Idempotent: a
 * second call for the same `t` returns the existing record.
 *
 * When the class has no user destructor (`exc_has_user_dtor` false) the
 * dtor thunk is NOT created — the dtor field remains NULL and the runtime
 * receives NULL (memcpy-only payload / no destruction needed). */
struct cpp_exc_thunk *
exc_register_thunks(struct type *t)
{
	extern struct scope filescope;
	struct cpp_exc_thunk *th;
	char *pname;
	const char *tag;
	char cname[256], dname[256];

	if (!t || (t->kind != TYPESTRUCT && t->kind != TYPEUNION))
		return NULL;
	tag = t->u.structunion.tag;
	if (!tag)
		return NULL;
	for (th = g_cpp_exc_thunks; th; th = th->next)
		if (th->t == t)
			return th;
	snprintf(cname, sizeof cname, "__meuos_exc_ms_copy_%s", tag);
	th = xmalloc(sizeof *th);
	th->t = t;
	pname = xmalloc(strlen(cname) + 1);
	strcpy(pname, cname);
	th->copy_fn = mkdecl(pname, DECLFUNC, exc_thunk_type(t, 1),
	    QUALNONE, LINKEXTERN);
	th->copy_fn->value = mkglobal(th->copy_fn);
	scopeputdecl(&filescope, th->copy_fn);
	if (exc_has_user_dtor(t)) {
		snprintf(dname, sizeof dname, "__meuos_exc_ms_dtor_%s", tag);
		pname = xmalloc(strlen(dname) + 1);
		strcpy(pname, dname);
		th->dtor_fn = mkdecl(pname, DECLFUNC, exc_thunk_type(t, 0),
		    QUALNONE, LINKEXTERN);
		th->dtor_fn->value = mkglobal(th->dtor_fn);
		scopeputdecl(&filescope, th->dtor_fn);
	} else {
		th->dtor_fn = NULL;
	}
	th->next = NULL;
	*g_cpp_exc_thunks_end = th;
	g_cpp_exc_thunks_end = &th->next;
	return th;
}

/* Build and emit the copy thunk for class  (already registered by
 * exc_register_thunks).  Body:  using
 * cpp_ctor_expr for overload-resolution-based copy ctor lookup. */
static void
exc_emit_copy_thunk(struct type *t, struct cpp_exc_thunk *th)
{
	extern struct scope filescope;
	extern struct func *mkfunc(struct decl *, char *, struct type *,
	    struct scope *);
	extern void delfunc(struct func *);
	extern void funcret(struct func *, struct value *);
	extern void stmt(struct func *, struct scope *);
	extern void emitfunc(struct func *, struct scope *, bool);
	extern struct scope *delscope(struct scope *);
	extern struct scope *mkscope(struct scope *);
	struct type *t_dst, *t_src, *pt;
	struct decl *dst_d, *src_d;
	struct expr *dst_e, *src_e, *deref, *thisp, *ctor;
	struct scope *fs;
	struct func *f;
	const char *tag = t->u.structunion.tag;

	fs = mkscope(&filescope);
	dst_d = th->copy_fn->type->u.func.params;
	src_d = dst_d->next;
	scopeputdecl(fs, dst_d);
	scopeputdecl(fs, src_d);
	f = mkfunc(th->copy_fn, th->copy_fn->name, th->copy_fn->type, fs);
	pt = mkpointertype(t, QUALNONE);
	t_dst = mkpointertype(&typevoid, QUALNONE);
	t_src = mkpointertype(&typevoid, QUALCONST);
	dst_e = mkexpr(EXPRIDENT, t_dst, NULL);
	dst_e->lvalue = true;
	dst_e->u.ident.decl = dst_d;
	dst_e = mkexpr(EXPRCAST, pt, dst_e);
	src_e = mkexpr(EXPRIDENT, t_src, NULL);
	src_e->lvalue = true;
	src_e->u.ident.decl = src_d;
	src_e = mkexpr(EXPRCAST, pt, src_e);
	deref = mkunaryexpr(TMUL, src_e);
	deref->lvalue = true;
	thisp = dst_e;
	ctor = cpp_ctor_expr(t, thisp, deref);
	if (!ctor)
		error_tok_code(E_DECL, &tok,
		    "no matching copy constructor for exception thunk of '%s'",
		    tag ? tag : "?");
	funcexpr(f, ctor);
	funcret(f, NULL);
	emitfunc(f, fs, true);
	delfunc(f);
	delscope(fs);
}

/* Build and emit the dtor thunk for class .  Body: . */
static void
exc_emit_dtor_thunk(struct type *t, struct cpp_exc_thunk *th)
{
	extern struct scope filescope;
	extern struct func *mkfunc(struct decl *, char *, struct type *,
	    struct scope *);
	extern void delfunc(struct func *);
	extern void funcret(struct func *, struct value *);
	extern void emitfunc(struct func *, struct scope *, bool);
	extern struct scope *delscope(struct scope *);
	extern struct scope *mkscope(struct scope *);
	struct type *t_self, *pt;
	struct decl *self_d;
	struct expr *self_e, *cast_e, *fn, *call;
	char mname[256];
	struct decl *fd;
	struct scope *fs;
	struct func *f;
	const char *tag = t->u.structunion.tag;

	fs = mkscope(&filescope);
	self_d = th->dtor_fn->type->u.func.params;
	scopeputdecl(fs, self_d);
	f = mkfunc(th->dtor_fn, th->dtor_fn->name, th->dtor_fn->type, fs);
	t_self = mkpointertype(&typevoid, QUALNONE);
	pt = mkpointertype(t, QUALNONE);
	self_e = mkexpr(EXPRIDENT, t_self, NULL);
	self_e->lvalue = true;
	self_e->u.ident.decl = self_d;
	cast_e = mkexpr(EXPRCAST, pt, self_e);
	snprintf(mname, sizeof mname, "%s_dtor", tag);
	fd = scopegetdecl(t->scope ? t->scope : &filescope, mname, true);
	if (!fd || fd->kind != DECLFUNC)
		error_tok_code(E_DECL, &tok,
		    "no destructor found for exception dtor thunk of '%s'", tag);
	fn = mkexpr(EXPRIDENT, fd->type, NULL);
	fn->u.ident.decl = fd;
	fn = decay(fn);
	call = mkexpr(EXPRCALL, &typevoid, fn);
	call->u.call.args = cast_e;
	call->u.call.nargs = 1;
	funcexpr(f, call);
	funcret(f, NULL);
	emitfunc(f, fs, true);
	delfunc(f);
	delscope(fs);
}

/* Walk the registered thunk records and emit each copy/dtor pair.  Called
 * at end of translation unit by cpp_parse.c after user code emission so
 * the user copy ctor / dtor definitions are already registered in the
 * class scope and resolvable by mangled-name lookup. */
void
cpp_emit_exc_thunks(void)
{
	struct cpp_exc_thunk *th;

	for (th = g_cpp_exc_thunks; th; th = th->next) {
		exc_emit_copy_thunk(th->t, th);
		if (th->dtor_fn)
			exc_emit_dtor_thunk(th->t, th);
	}
}

/* Register the thunk pair for class .  Called from cpp_exc_throw_call
 * when a non-trivial class throw is emitted.  The pre-registered decls
 * make &__meuos_exc_ms_copy_T / &__meuos_exc_ms_dtor_T resolvable at the
 * throw call site. */
void
cpp_synthesize_exc_copy_thunk(struct type *t)
{
	(void)exc_register_thunks(t);
}

void
cpp_synthesize_exc_dtor_thunk(struct type *t)
{
	(void)exc_register_thunks(t);
}
