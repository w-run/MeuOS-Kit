/* cpp_newdel_exc.c - m++ (C++) exception runtime integration, throw, try/catch.
 *
 * Stage C.3.1: split from cpp_newdel.c.  C++ exception support on the
 * MeuOS libc setjmp/longjmp runtime (meuos_exc.h):
 *   - cpp_ensure_exc_fn  (runtime helper declarations:
 *                         _meuos_exc_throw / _meuos_exc_throw_obj /
 *                         _meuos_exc_try_begin / _meuos_exc_try_end /
 *                         _meuos_exc_caught_{type,value,obj,free,is_obj})
 *   - cpp_exc_typecode  (per-type integer discriminant + exc_tc_regs registry)
 *   - cpp_exc_throw_call_scalar / cpp_exc_throw_call / cpp_exc_throw_call2
 *     (throw lowering: scalar / class / bare-rethrow)
 *   - cpp_exc_helper_call  (try_begin / try_end / caught_* helpers)
 *   - exc_base_slice_offset / exc_slice_ptr
 *     (base sub-object slicing in catch(Base&) / catch(Base) by value)
 *   - cpp_exc_stmt  (the `try { ... } catch (T e) { ... }` statement)
 *   - cpp_parse_throw_expr  (the `throw expr;` / `throw;` expression)
 *
 * Cross-file entry points: cpp_parse_throw_expr (expr_unary.c),
 * cpp_exc_stmt (stmt.c).
 *
 * Uses exc_has_*, exc_base_offset, exc_register_thunks from
 * cpp_newdel_thunk.c.
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

/* Cross-file: per-class exception thunk queries (defined in
 * cpp_newdel_thunk.c).  Used by the throw lowering to decide between
 * the legacy zero-slot path, the trivial memcpy path, and the
 * non-trivial copy/dtor thunk path. */
extern int exc_has_trivial_copy(struct type *t);
extern int exc_has_trivial_dtor(struct type *t);
extern int exc_has_user_copy_ctor(struct type *t);
extern int exc_has_user_dtor(struct type *t);
extern int exc_base_offset(struct type *t);
extern struct cpp_exc_thunk *exc_register_thunks(struct type *t);

/* --- C++ exceptions (basic frontend support) ---------------------------- */

/* C++ exception runtime helpers (minimal, non-ABI self-owned interface).
 * The landingpad/unwind backend that would route a thrown exception to a
 * catch block is not yet implemented; `_meuos_exc_throw` arms the
 * exception slot and aborts for now (forward-compatible with a future
 * unwinder).  `_meuos_exc_typecode` maps a C++ type to an integer
 * discriminant used for catch-type matching. */
struct decl *
cpp_ensure_exc_fn(const char *name)
{
	extern struct scope filescope;
	struct decl *fd = scopegetdecl(&filescope, name, 1);
	if (fd && fd->kind == DECLFUNC)
		return fd;
	if (strcmp(name, "_meuos_exc_throw") == 0) {
		/* void _meuos_exc_throw(int typecode, unsigned long long value) */
		struct type *ft = mktype(TYPEFUNC, 0);
		struct decl *p2;
		ft->base = &typevoid;
		ft->u.func.isvararg = false;
		ft->u.func.nparam = 2;
		p2 = mkdecl("typecode", DECLOBJECT, &typeint, QUALNONE, LINKNONE);
		p2->u.obj.storage = SDAUTO;
		p2->next = mkdecl("value", DECLOBJECT, &typeullong, QUALNONE,
		    LINKNONE);
		p2->next->u.obj.storage = SDAUTO;
		ft->u.func.params = p2;
		fd = mkdecl("_meuos_exc_throw", DECLFUNC, ft, QUALNONE, LINKEXTERN);
		fd->u.func.isnoreturn = true;
	} else if (strcmp(name, "_meuos_exc_throw_obj") == 0) {
		/* void _meuos_exc_throw_obj(int typecode, size_t size, size_t align,
		 *     void (*copy)(void*,const void*), void (*dtor)(void*),
		 *     int offset_to_base, const void *obj)
		 * Phase-4 object payload (exc-phase4-object-payload.md).  libc
		 * runtime packs meta + heap-copies the object + destroys the source
		 * temporary.  copy/dtor are NULL for trivial classes (runtime uses
		 * memcpy / no dtor). */
		struct type *ft = mktype(TYPEFUNC, 0);
		struct decl *p1, *pp;
		ft->base = &typevoid;
		ft->u.func.isvararg = false;
		ft->u.func.nparam = 7;
		p1 = mkdecl("typecode", DECLOBJECT, &typeint, QUALNONE, LINKNONE);
		p1->u.obj.storage = SDAUTO;
		pp = mkdecl("size", DECLOBJECT, &typeulong, QUALNONE, LINKNONE);
		pp->u.obj.storage = SDAUTO;
		p1->next = pp;
		pp = mkdecl("align", DECLOBJECT, &typeulong, QUALNONE, LINKNONE);
		pp->u.obj.storage = SDAUTO;
		p1->next->next = pp;
		pp = mkdecl("copy", DECLOBJECT,
		    mkpointertype(mktype(TYPEFUNC, 0), QUALNONE), QUALNONE, LINKNONE);
		pp->u.obj.storage = SDAUTO;
		p1->next->next->next = pp;
		pp = mkdecl("dtor", DECLOBJECT,
		    mkpointertype(mktype(TYPEFUNC, 0), QUALNONE), QUALNONE, LINKNONE);
		pp->u.obj.storage = SDAUTO;
		p1->next->next->next->next = pp;
		pp = mkdecl("offset", DECLOBJECT, &typeint, QUALNONE, LINKNONE);
		pp->u.obj.storage = SDAUTO;
		p1->next->next->next->next->next = pp;
		pp = mkdecl("obj", DECLOBJECT,
		    mkpointertype(&typevoid, QUALNONE), QUALNONE, LINKNONE);
		pp->u.obj.storage = SDAUTO;
		p1->next->next->next->next->next->next = pp;
		ft->u.func.params = p1;
		fd = mkdecl("_meuos_exc_throw_obj", DECLFUNC, ft, QUALNONE,
		    LINKEXTERN);
		fd->u.func.isnoreturn = true;
	} else if (strcmp(name, "_meuos_exc_try_begin") == 0) {
		/* void _meuos_exc_try_begin(_meuos_exc_frame *) */
		struct type *ft = mktype(TYPEFUNC, 0);
		struct decl *p0;
		struct type *frame_t =
		    scopegettag(&filescope, "_meuos_exc_frame", true);
		ft->base = &typevoid;
		ft->u.func.isvararg = false;
		ft->u.func.nparam = 1;
		p0 = mkdecl("frame", DECLOBJECT,
		            frame_t ? mkpointertype(frame_t, QUALNONE)
		                    : mkpointertype(&typevoid, QUALNONE),
		            QUALNONE, LINKNONE);
		p0->u.obj.storage = SDAUTO;
		ft->u.func.params = p0;
		fd = mkdecl("_meuos_exc_try_begin", DECLFUNC, ft, QUALNONE,
		            LINKEXTERN);
	} else if (strcmp(name, "_meuos_exc_try_end") == 0) {
		struct type *ft = mktype(TYPEFUNC, 0);
		ft->base = &typevoid;
		ft->u.func.isvararg = false;
		ft->u.func.nparam = 0;
		fd = mkdecl("_meuos_exc_try_end", DECLFUNC, ft, QUALNONE,
		            LINKEXTERN);
	} else if (strcmp(name, "_meuos_exc_caught_type") == 0) {
		struct type *ft = mktype(TYPEFUNC, 0);
		ft->base = &typeint;
		ft->u.func.isvararg = false;
		ft->u.func.nparam = 0;
		fd = mkdecl("_meuos_exc_caught_type", DECLFUNC, ft, QUALNONE,
		            LINKEXTERN);
	} else if (strcmp(name, "_meuos_exc_caught_value") == 0) {
		struct type *ft = mktype(TYPEFUNC, 0);
		ft->base = &typeullong;
		ft->u.func.isvararg = false;
		ft->u.func.nparam = 0;
		fd = mkdecl("_meuos_exc_caught_value", DECLFUNC, ft, QUALNONE,
		            LINKEXTERN);
	} else if (strcmp(name, "_meuos_exc_caught_obj") == 0) {
		/* const void *_meuos_exc_caught_obj(void) — object payload */
		struct type *ft = mktype(TYPEFUNC, 0);
		ft->base = mkpointertype(&typevoid, QUALCONST);
		ft->u.func.isvararg = false;
		ft->u.func.nparam = 0;
		fd = mkdecl("_meuos_exc_caught_obj", DECLFUNC, ft, QUALNONE,
		            LINKEXTERN);
	} else if (strcmp(name, "_meuos_exc_caught_free") == 0) {
		/* void _meuos_exc_caught_free(void) */
		struct type *ft = mktype(TYPEFUNC, 0);
		ft->base = &typevoid;
		ft->u.func.isvararg = false;
		ft->u.func.nparam = 0;
		fd = mkdecl("_meuos_exc_caught_free", DECLFUNC, ft, QUALNONE,
		            LINKEXTERN);
	} else if (strcmp(name, "_meuos_exc_caught_is_obj") == 0) {
		/* int _meuos_exc_caught_is_obj(void) */
		struct type *ft = mktype(TYPEFUNC, 0);
		ft->base = &typeint;
		ft->u.func.isvararg = false;
		ft->u.func.nparam = 0;
		fd = mkdecl("_meuos_exc_caught_is_obj", DECLFUNC, ft, QUALNONE,
		            LINKEXTERN);
	} else {
		return NULL;
	}
	fd->value = mkglobal(fd);
	scopeputdecl(&filescope, fd);
	return fd;
}

/* Integer type discriminant for catch-type matching.
 *
 * Phase 2: a type->code registry.  Each distinct type (by struct type
 * pointer — builtin singletons like &typeint and the canonical struct type
 * from scopegettag) is assigned a stable, monotonically increasing code, so
 * a `throw` of type T and a `catch (T e)` resolve to the *same* code and
 * the front-end catch sequence matches on it.  This is pure-front-end: the
 * libc runtime keeps an opaque int slot (exc_typecode) and never needs to
 * change.  (Base-class catch / integer promotion widening is phase 3+.)
 */
struct exc_tc_reg { struct type *t; int code; };
static struct exc_tc_reg exc_tc_regs[64];
static int exc_tc_n = 0;

static int
cpp_exc_typecode(struct type *t)
{
	int i;
	for (i = 0; i < exc_tc_n; i++)
		if (exc_tc_regs[i].t == t)
			return exc_tc_regs[i].code;
	if (exc_tc_n >= 64)
		return 0;             /* fallback; table exhausted is unrealistic */
	exc_tc_regs[exc_tc_n].t = t;
	exc_tc_regs[exc_tc_n].code = exc_tc_n + 1;  /* code 0 reserved: no type */
	return exc_tc_regs[exc_tc_n++].code;
}

/* Offset of the `base` sub-object within an instance of class `derived`,
 * for base-subobject slicing in an exception catch.  The base appears as
 * an anonymous member (name==NULL); with multiple/chain inheritance the
 * base may sit at a non-zero offset (e.g. the 2nd base in `struct D : A,
 * B` is at offset sizeof(A)).  Recurses through nested anonymous base
 * members to sum the path.  Returns (unsigned long long)-1 when `derived`
 * is NOT derived from `base` (callers guard with cpp_is_derived first). */
static unsigned long long
exc_base_slice_offset(struct type *derived, struct type *base)
{
	struct member *m;

	if (!derived || !base)
		return (unsigned long long)-1;
	if (derived == base)
		return 0;
	if (derived->kind != TYPESTRUCT && derived->kind != TYPEUNION)
		return (unsigned long long)-1;
	for (m = derived->u.structunion.members; m; m = m->next) {
		unsigned long long sub;
		if (m->name || !m->type) /* virtual/regular members have a name */
			continue;
		if (m->type == base)
			return m->offset;
		sub = exc_base_slice_offset(m->type, base);
		if (sub != (unsigned long long)-1)
			return m->offset + sub;
	}
	return (unsigned long long)-1;
}

static struct expr *
cpp_exc_throw_call_scalar(struct type *t, struct expr *value)
{
	struct decl *fds = cpp_ensure_exc_fn("_meuos_exc_throw");
	struct expr *fn, *call, *a1, *a2;
	if (!fds)
		return mkexpr(EXPRCONST, &typevoid, NULL);
	fn = mkexpr(EXPRIDENT, fds->type, NULL);
	fn->u.ident.decl = fds;
	fn = decay(fn);
	call = mkexpr(EXPRCALL, &typevoid, fn);
	a1 = mkconstexpr(&typeint, cpp_exc_typecode(t));
	a2 = mkconstexpr(&typeullong, 0); /* class: type code only (legacy) */
	a1->next = a2;
	call->u.call.args = a1;
	call->u.call.nargs = 2;
	return call;
}

/* Build a call `_meuos_exc_throw(typecode, value)`. */
static struct expr *
cpp_exc_throw_call(struct type *t, struct expr *value)
{
	struct decl *fd = cpp_ensure_exc_fn("_meuos_exc_throw");
	struct expr *fn, *call, *a1, *a2;

	if (!fd)
		return mkexpr(EXPRCONST, &typevoid, NULL);
	fn = mkexpr(EXPRIDENT, fd->type, NULL);
	fn->u.ident.decl = fd;
	fn = decay(fn);
	call = mkexpr(EXPRCALL, &typevoid, fn);
	a1 = mkconstexpr(&typeint, cpp_exc_typecode(t));
	if (value && (value->type->kind == TYPESTRUCT ||
	              value->type->kind == TYPEUNION)) {
		/* Phase-4 object payload: when the object runtime is in scope,
		 * route a class-typed exception through _meuos_exc_throw_obj so the
		 * object travels (heap-copied by the runtime), not just the type
		 * code.  Trivial classes (no user copy ctor / no dtor) pass
		 * copy=NULL, dtor=NULL and the runtime performs a plain
		 * size-byte memcpy / no destruction (exc-phase4-object-payload.md).
		 * Fall back to the old zero-slot scheme when the runtime is not yet
		 * linked in so existing scalar/tests keep working. */
		struct decl *fdo;
		/* Use the object path ONLY when the program actually declares
		 * `_meuos_exc_throw_obj` (i.e. includes the phase-4 runtime).
		 * scopegetdecl (non-creating) checks the user's decl: if absent we
		 * must NOT synthesize+emit throw_obj, or programs that don't link
		 * the object runtime would get an undefined reference. */
		fdo = scopegetdecl(&filescope, "_meuos_exc_throw_obj", 1);
		if (fdo && fdo->kind == DECLFUNC) {
			struct expr *fn2, *call2, *arg, *aobj;
			struct type *pt = mkpointertype(t, QUALNONE);
			struct type *tcode_t = &typeint;
			int ndecl = exc_has_trivial_copy(t) && exc_has_trivial_dtor(t);
			struct cpp_exc_thunk *th = NULL;
			(void)call; (void)fd; (void)a2;
			fn2 = mkexpr(EXPRIDENT, fdo->type, NULL);
			fn2->u.ident.decl = fdo;
			fn2 = decay(fn2);
			call2 = mkexpr(EXPRCALL, &typevoid, fn2);
			arg = mkconstexpr(tcode_t, cpp_exc_typecode(t));
			arg->next = mkconstexpr(&typeulong, t->size);
			arg->next->next = mkconstexpr(&typeulong, t->align);
			if (!ndecl) {
				/* Non-trivial payload: only classes with a user copy
				 * ctor get thunks.  When only a dtor exists (no copy
				 * ctor) we cannot safely copy the payload, so fall
				 * back to the zero-slot scheme immediately. */
				if (exc_has_user_copy_ctor(t)) {
					struct expr *ce, *de;
					th = exc_register_thunks(t);
					if (th) {
						ce = mkexpr(EXPRIDENT, th->copy_fn->type, NULL);
						ce->u.ident.decl = th->copy_fn;
						ce = decay(ce);
						/* dtor: pass the dtor thunk address only when the
						 * class has a user destructor; NULL tells the runtime
						 * no destruction is needed (the class is trivially
						 * destructible even though it has a user copy ctor,
						 * e.g. OnlyCopy in the test). */
						if (exc_has_user_dtor(t)) {
							de = mkexpr(EXPRIDENT, th->dtor_fn->type, NULL);
							de->u.ident.decl = th->dtor_fn;
							de = decay(de);
						} else {
							de = mkconstexpr(
							    mkpointertype(mktype(TYPEFUNC, 0), QUALNONE),
							    0);
						}
						arg->next->next->next = ce;
						arg->next->next->next->next = de;
						arg->next->next->next->next->next =
						    mkconstexpr(&typeint, exc_base_offset(t));
						aobj = mkunaryexpr(TBAND, value);
						aobj->type = pt;
						arg->next->next->next->next->next->next = aobj;
						call2->u.call.args = arg;
						call2->u.call.nargs = 7;
						return call2;
					}
				}
				/* fall back to the legacy zero-slot path. */
				return cpp_exc_throw_call_scalar(t, value);
			}
			arg->next->next->next =
			    mkconstexpr(mkpointertype(mktype(TYPEFUNC, 0), QUALNONE), 0);
			arg->next->next->next->next =
			    mkconstexpr(mkpointertype(mktype(TYPEFUNC, 0), QUALNONE), 0);
			arg->next->next->next->next->next =
			    mkconstexpr(&typeint, exc_base_offset(t));
			/* obj: address of the throw-temporary */
			aobj = mkunaryexpr(TBAND, value);
			aobj->type = pt;
			arg->next->next->next->next->next->next = aobj;
			call2->u.call.args = arg;
			call2->u.call.nargs = 7;
			return call2;
		}
		(void)fd; (void)call; (void)a1; (void)a2;
		return cpp_exc_throw_call_scalar(t, value);
	} else {
		a2 = value ? mkexpr(EXPRCAST, &typeullong, value)
		           : mkconstexpr(&typeullong, 0);
	}
	a2->next = NULL;
	a1->next = a2;
	call->u.call.args = a1;
	call->u.call.nargs = 2;
	return call;
}

/* Build a call `_meuos_exc_throw(typecode_expr, value_expr)` with already
 * computed operands (used by bare rethrow `throw;`). */
static struct expr *
cpp_exc_throw_call2(struct expr *tcode, struct expr *value)
{
	struct decl *fd = cpp_ensure_exc_fn("_meuos_exc_throw");
	struct expr *fn, *call;

	if (!fd)
		return mkexpr(EXPRCONST, &typevoid, NULL);
	fn = mkexpr(EXPRIDENT, fd->type, NULL);
	fn->u.ident.decl = fd;
	fn = decay(fn);
	call = mkexpr(EXPRCALL, &typevoid, fn);
	value = exprconvert(value, &typeullong);
	value->next = NULL;
	tcode->next = value;
	call->u.call.args = tcode;
	call->u.call.nargs = 2;
	return call;
}

/* C++ `try`/`catch` statement.  Parsed and recognised (so it is no longer
 * reported as an undeclared identifier), but the landingpad / .eh_frame
 * unwinder backend that routes a thrown exception to a catch block is not
 * yet implemented — so we emit a clear diagnostic rather than silent
 * miscompilation. */
/* Build a call to a MeuOS exception-runtime helper by name (all from
 * <meuos_exc.h>: _meuos_exc_try_begin/try_end/caught_type/caught_value),
 * returning its value or void as a funcexpr'd statement.  Falls back to
 * cpp_ensure_exc_fn for the throw helper. */
static struct expr *
cpp_exc_helper_call(const char *name, struct type *ret,
                    struct expr *arg1, struct expr *arg2)
{
	extern struct scope filescope;
	struct decl *fd = cpp_ensure_exc_fn(name);
	struct expr *fn, *call;
	if (!fd)
		return mkexpr(EXPRCONST, &typevoid, NULL);
	fn = mkexpr(EXPRIDENT, fd->type, NULL);
	fn->u.ident.decl = fd;
	fn = decay(fn);
	call = mkexpr(EXPRCALL, ret, fn);
	call->u.call.args = arg1;
	call->u.call.nargs = arg1 ? 1 : 0;
	if (arg2) {
		arg1->next = arg2;
		call->u.call.nargs = 2;
	}
	return call;
}

/* Build a `T*` slice-view pointer for the class catch parameter of type
 * `ctype`, re-aiming the runtime-carried object at the base sub-object that
 * actually matched the catch.  The libc carries the full thrown object (the
 * registered derived type D); catch(Base) matches that D via the typecode
 * skew used by cpp_is_derived, so the Base sub-object sits at a non-zero
 * offset in D (mcc-side slice; independent of libc's offset argument).  A
 * local `__exc_slice` offset is selected by branching on
 * _meuos_exc_caught_type() over every registered type derived from ctype
 * (0 when the catch type itself matched / the base is at offset 0), then we
 * build `(T*)((char*)_meuos_exc_caught_obj() + __exc_slice)`.  All the
 * statements (decl + the if/else emitters) are emitted into `f` first; the
 * returned expression is the final slice pointer. */
static struct expr *
exc_slice_ptr(struct func *f, struct type *ctype)
{
	extern struct block *mkblock(char *);
	extern void funclabel(struct func *, struct block *);
	extern struct value *funcbranch(struct func *, struct expr *,
	    struct block *, struct block *);
	extern void funcjmp(struct func *, struct block *);
	extern void funcinit(struct func *, struct decl *, struct init *, bool);
	struct expr *ct, *se, *co, *cvs;
	struct decl *sd;
	int i;

	/* local unsigned long long __exc_slice = 0; */
	sd = mkdecl("__exc_slice", DECLOBJECT,
	    &typeulong, QUALNONE, LINKNONE);
	sd->u.obj.storage = SDAUTO;
	funcinit(f, sd, NULL, false);
	se = mkexpr(EXPRIDENT, &typeulong, NULL);
	se->lvalue = true;
	se->u.ident.decl = sd;
	/* funcinit() with hasinit=false only allocates storage; auto objects
	 * are uninitialised in C.  Without this zero store, the variable
	 * carries stack garbage on the trivial case (no derived types match
	 * the catch type, so the for-loop never writes __exc_slice), and
	 * the slice pointer ends up at an arbitrary address — segfault on
	 * any trivial class throw/catch pair. */
	funcexpr(f, mkassignexpr(se, mkconstexpr(&typeulong, 0)));
	ct = NULL;
	for (i = 0; i < exc_tc_n; i++) {
		struct block *bsy, *bsn;
		unsigned long long soff;
		if (exc_tc_regs[i].t == ctype)
			continue;
		if (!cpp_is_derived(exc_tc_regs[i].t, ctype))
			continue;
		soff = exc_base_slice_offset(exc_tc_regs[i].t, ctype);
		if (soff == (unsigned long long)-1)
			continue;
		if (!ct)
			ct = cpp_exc_helper_call("_meuos_exc_caught_type",
			    &typeint, NULL, NULL);
		bsy = mkblock("exc_slice_y");
		bsn = mkblock("exc_slice_n");
		funcbranch(f,
		    mkbinaryexpr(&tok.loc, TEQL, ct,
		        mkconstexpr(&typeint, exc_tc_regs[i].code)),
		    bsy, bsn);
		funclabel(f, bsy);
		funcexpr(f, mkassignexpr(se,
		    mkconstexpr(&typeulong, soff)));
		funcjmp(f, bsn);
		funclabel(f, bsn);
	}
	co = cpp_exc_helper_call("_meuos_exc_caught_obj",
	    mkpointertype(&typevoid, QUALCONST), NULL, NULL);
	/* (T*)((char*)co + __exc_slice) */
	cvs = mkexpr(EXPRBINARY,
	    mkpointertype(&typechar, QUALNONE), NULL);
	cvs->op = TADD;
	cvs->u.binary.l = mkexpr(EXPRCAST,
	    mkpointertype(&typechar, QUALNONE), co);
	cvs->u.binary.r = se;
	cvs = mkexpr(EXPRCAST,
	    mkpointertype(ctype, QUALNONE), cvs);
	return cvs;
}

/* C++ `try { ... } catch (T e) { ... }` — lowered onto the libc
 * setjmp/longjmp exception runtime (meuos_exc.h): declare a local
 * `_meuos_exc_frame`, setjmp into it, register with try_begin, branch on
 * the setjmp return: normal path runs the try body then try_end (pop); the
 * longjmp path (r != 0) matches the caught type and, on hit, copy-inits the
 * catch parameter from caught_value.  Uncaught type falls to a rethrow for
 * now (phases 3-4 make this the precise multi-catch dispatch).
 *
 * The program must `#include <meuos_exc.h>` so `_meuos_exc_frame` and the
 * helper declarations are in scope.
 */
void
cpp_exc_stmt(struct func *f, struct scope *s)
{
	extern struct scope filescope;
	extern void stmt(struct func *, struct scope *);
	extern struct scope *mkscope(struct scope *);
	extern struct scope *delscope(struct scope *);
	extern void next(void);
	extern struct block *mkblock(char *);
	extern void funclabel(struct func *, struct block *);
	extern struct value *funcbranch(struct func *, struct expr *,
	    struct block *, struct block *);
	extern void funcjmp(struct func *, struct block *);
	extern struct type *typename(struct scope *, enum typequal *,
	    struct expr **);
	struct type *frame_t;
	struct expr *frame_e, *addr, *st, *r_e, *cond, *cv, *casted;
	struct decl *frame_d, *r_d, *fd_setjmp, *ed;
	struct block *bcaught, *bnormal, *bjoin;
	enum typequal tq = QUALNONE;
	struct type *ctype = NULL;
	int catch_is_ref = 0;
	char name[256];
	int catch_tc;

	if (cpp_tok_kind() != CPP_TTRY)
		return;
	/* The frame struct comes from <meuos_exc.h> (declared in filescope). */
	frame_t = scopegettag(&filescope, "_meuos_exc_frame", true);
	if (!frame_t || (frame_t->kind != TYPESTRUCT &&
	                 frame_t->kind != TYPEUNION))
		error_tok_code(E_TEMPLATE, &tok,
		    "try/catch requires '#include <meuos_exc.h>' (defines _meuos_exc_frame)");
	next(); /* consume 'try' */

	s = mkscope(s);

	/* local `_meuos_exc_frame frame;` */
	frame_d = mkdecl("frame", DECLOBJECT, frame_t, QUALNONE, LINKNONE);
	frame_d->u.obj.storage = SDAUTO;
	funcinit(f, frame_d, NULL, false);
	frame_e = mkexpr(EXPRIDENT, frame_t, NULL);
	frame_e->lvalue = true;
	frame_e->u.ident.decl = frame_d;

	/* local `int __exc_r;` */
	r_d = mkdecl("__exc_r", DECLOBJECT, &typeint, QUALNONE, LINKNONE);
	r_d->u.obj.storage = SDAUTO;
	funcinit(f, r_d, NULL, false);
	r_e = mkexpr(EXPRIDENT, &typeint, NULL);
	r_e->lvalue = true;
	r_e->u.ident.decl = r_d;

	/* __exc_r = setjmp(frame.env)   (env is the first member, offset 0) */
	addr = mkunaryexpr(TBAND, frame_e);
	fd_setjmp = scopegetdecl(&filescope, "setjmp", true);
	if (fd_setjmp && fd_setjmp->kind == DECLFUNC) {
		struct expr *fn, *call;
		fn = mkexpr(EXPRIDENT, fd_setjmp->type, NULL);
		fn->u.ident.decl = fd_setjmp;
		fn = decay(fn);
		call = mkexpr(EXPRCALL, &typeint, fn);
		call->u.call.args = addr;
		call->u.call.nargs = 1;
		funcexpr(f, mkassignexpr(r_e, call));
	} else {
		error_tok_code(E_TEMPLATE, &tok,
		    "setjmp not declared (meuos_exc.h requires <setjmp.h>)");
	}

	/* if (__exc_r != 0) -> caught branch, else try body */
	cond = mkbinaryexpr(&tok.loc, TNEQ, r_e,
	                    mkconstexpr(&typeint, 0));
	bcaught = mkblock("exc_caught");
	bnormal = mkblock("exc_normal");
	bjoin = mkblock("exc_join");
	funcbranch(f, cond, bcaught, bnormal);

	/* normal (r == 0): register the handler, run the try body, then pop.
	 * try_begin is only called on the first setjmp pass (r == 0); an
	 * exceptional longjmp returns with r != 0 straight into the caught
	 * branch WITHOUT re-registering — re-registering would re-push this
	 * frame and make a `throw;` rethrow re-enter this same catch (loop). */
	funclabel(f, bnormal);
	st = cpp_exc_helper_call("_meuos_exc_try_begin", &typevoid,
	                          frame_e, NULL);
	funcexpr(f, st);
	stmt(f, s);
	funcexpr(f, cpp_exc_helper_call("_meuos_exc_try_end", &typevoid,
	                                NULL, NULL));
	funcjmp(f, bjoin);

	/* caught (r != 0): dispatch over the catch sequence by type code */
	funclabel(f, bcaught);
	{
		struct block *bnext = NULL;
for (;;) {
		struct block *bhit, *bmiss;
		struct expr *ctx, *cnd;
		if (cpp_tok_kind() != CPP_TCATCH) {
			error_tok_code(E_TEMPLATE, &tok,
			    "expected 'catch' after 'try'");
			break;
		}
		catch_is_ref = 0;  /* reset per-catch */
		if (bnext)
			funclabel(f, bnext); /* previous catch's miss lands here */
			next(); /* consume 'catch' */
			expect(TLPAREN, "after 'catch'");
			ctype = NULL;
			if (strcmp(tokenstr(tok.kind), "...") == 0) {
				next(); /* consume '...' */
				expect(TRPAREN, "after catch(...)");
				/* catch-all: no param, matches anything, runs the body,
				 * then terminates the sequence (C++ requires it last).
				 * bnext is cleared so the rethrow-after-loop does not
				 * re-label the previous catch's miss onto the rethrow. */
				stmt(f, s);
				funcjmp(f, bjoin);
				bnext = NULL;
				break;
			} else {
				const char *ts;
				struct type *tt;
				/* Skip leading const/volatile on the catch type:
				 * catch(const T) / catch(const T&)  binds the same
				 * parameter as the unqualified form — C++ ignores the
				 * top-level cv-qualifier when matching the catch type,
				 * and our T&→T* downgrade makes const transparent to
				 * the pointer cast in the binding.  `const`/`volatile`
				 * are C keywords (TCONST/TVOLATILE, below TIDENT), so
				 * check tok.kind directly — cpp_tok_kind() maps these
				 * to CPP_TNONE. */
				while (tok.kind == TCONST || tok.kind == TVOLATILE)
					next();
				ts = tokenstr(tok.kind);
				if (strcmp(ts, "int") == 0)
					ctype = &typeint;
				else if (strcmp(ts, "char") == 0)
					ctype = &typechar;
				else if (strcmp(ts, "long") == 0)
					ctype = &typelong;
				else if (strcmp(ts, "short") == 0)
					ctype = &typeshort;
				else if (strcmp(ts, "double") == 0)
					ctype = &typedouble;
				else {
					tt = scopegettag(s, ts, true);
					if (!tt)
						tt = scopegettag(&filescope, ts, true);
					if (!tt)
						error_tok_code(E_TEMPLATE, &tok,
						    "unknown catch parameter type");
					ctype = tt;
				}
				next(); /* consume the type name */
				/* Catch(T&) is supported as a binding to the carried
				 * runtime object (no copy).  mcc has no real reference
				 * type yet; we model it with a T* local so the catch body
				 * reads via (*e).x.  Type matching keeps using `ctype`
				 * so the type-code/derived match stays unchanged. */
				if (tok.kind == TBAND) {
					next(); /* consume '&' */
					catch_is_ref = 1;
					name[0] = '\0';
					if (tok.kind != TRPAREN) {
						snprintf(name, sizeof name,
						    "%s", tokenstr(tok.kind));
						next();
					}
					expect(TRPAREN,
					    "after catch parameter (&)");
				} else if (tok.kind != TRPAREN) {
					snprintf(name, sizeof name, "%s", tokenstr(tok.kind));
					next();
					expect(TRPAREN, "after catch parameter");
				} else {
					name[0] = '\0';
				}
				if (ctype) {
					/* branch: if (caught_type() == tc(ctype) OR any
					 * registered derived type) hit else miss.  A
					 * catch(Base) matches a throw(Derived) because we
					 * widen the condition over every type already
					 * registered in the typecode table that is derived
					 * from the catch type (cpp_is_derived; base appears
					 * as an anonymous member).  This gives single-inheri
					 * tance base-catch without a full RTTI/typeinfo.
					 *
					 * Scalar-rank widening (integer same-size): for an
					 * integer catch type, also match any registered
					 * integer type of the same size (throw int can be
					 * caught by catch short and vice versa; the cast is
					 * done by exprconvert at the assignment). */
					int i;
					ctx = cpp_exc_helper_call("_meuos_exc_caught_type",
					    &typeint, NULL, NULL);
					cnd = mkbinaryexpr(&tok.loc, TEQL, ctx,
					    mkconstexpr(&typeint, cpp_exc_typecode(ctype)));
					for (i = 0; i < exc_tc_n; i++) {
						if (exc_tc_regs[i].t == ctype)
							continue;
						if (cpp_is_derived(exc_tc_regs[i].t, ctype))
							cnd = mkbinaryexpr(&tok.loc, TLOR, cnd,
							    mkbinaryexpr(&tok.loc, TEQL,
							        ctx,
							        mkconstexpr(&typeint,
							            exc_tc_regs[i].code)));
						}
						/* integer widening is NOT implicit in C++ catch type
					 * matching (the standard says catch is exact-type +
					 * derived-class only).  The assignment of the caught
					 * value to the catch parameter is still handled by
					 * exprconvert below, which does standard integer
					 * conversions on the value.  So throw(int) will NOT
					 * be caught by catch(long) — the type-code mismatch
					 * causes the catch to miss and fall through to the
					 * next handler.  This matches C++ standard semantics
					 * (no implicit promotion/truncation in catch).
					 * Throwing a short and catching an int is rejected
					 * at the type-code level; the value, if the exact
					 * type matched, is widened by exprconvert. */
					bhit = mkblock("exc_catch_hit");
					bmiss = mkblock("exc_catch_miss");
					funcbranch(f, cnd, bhit, bmiss);
					funclabel(f, bhit);
					/* declare the catch param.  By-value: T e, then
					 * initialise from caught_value / caught_obj.  By-ref:
					 * T *e, then bind to the runtime heap object
					 * (no copy).  catch_is_ref is set by the parser
					 * above when '&' was consumed. */
					if (catch_is_ref)
						ed = mkdecl(name, DECLOBJECT,
						    mkpointertype(ctype, QUALNONE),
						    QUALNONE, LINKNONE);
					else
						ed = mkdecl(name, DECLOBJECT, ctype,
						    QUALNONE, LINKNONE);
					ed->u.obj.storage = SDAUTO;
					funcinit(f, ed, NULL, false);
					scopeputdecl(s, ed);
					if (catch_is_ref) {
						/* T *e = (T *)_meuos_exc_caught_obj();
						 * No copy; access via (*e).x.  Only when the
						 * active exception IS an object (scalar-throw
						 * with the same typecode would set caught_obj
						 * to NULL via the scalar path; guard against
						 * deref).  For class types caught_is_obj is
						 * true; for the (rare) scalar-throw with the
						 * same typecode caught_obj stays NULL.
						 *
						 * Base-subobject slicing: when this catch is a
						 * base type (Base&) matching a derived throw
						 * (D), the runtime carries the D object; we
						 * adjust the pointer to the Base sub-object
						 * (mcc-side slice, does not rely on the libc
						 * offset argument).  A local __exc_slice offset
						 * selects the base offset for whatever derived
						 * type actually matched, defaulting to 0 (the
						 * catch type itself, or an unambiguous base at
						 * offset 0). */
						struct expr *ep, *cvs;
						struct expr *isobj;
						struct block *bp1, *bp0, *bpjoin;
						ep = mkexpr(EXPRIDENT,
						    mkpointertype(ctype, QUALNONE),
						    NULL);
						ep->lvalue = true;
						ep->u.ident.decl = ed;
						isobj = cpp_exc_helper_call(
						    "_meuos_exc_caught_is_obj",
						    &typeint, NULL, NULL);
						bp1 = mkblock("exc_ref_obj");
						bp0 = mkblock("exc_ref_no");
						bpjoin = mkblock("exc_ref_join");
						funcbranch(f, isobj, bp1, bp0);
						funclabel(f, bp1);
						/* T *e = slice-view pointer into the carried object
						 * (mcc-side base-subobject slice: selects __exc_slice
						 * per matching derived type, then
						 * (T*)((char*)caught_obj + __exc_slice)). */
						cvs = exc_slice_ptr(f, ctype);
						funcexpr(f, mkassignexpr(ep, cvs));
						funcjmp(f, bpjoin);
						funclabel(f, bp0);
						funcjmp(f, bpjoin);
						funclabel(f, bpjoin);
					} else if (ctype->kind != TYPESTRUCT &&
					    ctype->kind != TYPEUNION) {
						/* scalar catch: copy caught_value into the param */
						struct expr *ep = mkexpr(EXPRIDENT, ctype, NULL);
						struct expr *val;
						ep->lvalue = true;
						ep->u.ident.decl = ed;
						val = cpp_exc_helper_call("_meuos_exc_caught_value",
						    &typeullong, NULL, NULL);
						funcexpr(f, mkassignexpr(ep, exprconvert(val, ctype)));
					} else {
						/* class catch param: rebuild the object from the
						 * runtime's carried heap object (phase 4).  The
						 * catch parameter is copy-initialized from
						 * `*(T *)_meuos_exc_caught_obj()`.  Only when the
						 * active exception IS an object (caught_is_obj) is
						 * a pointer carried; a scalar-caught class (older
						 * type-code-only path) leaves the param
						 * default/uninitialized (no crash on NULL).  The
						 * carried heap object is then released
						 * (dtor+free) via _meuos_exc_caught_free.  For
						 * byte-copyable classes struct assignment copies;
						 * user copy ctors are a later increment (4b). */
						struct expr *ep, *cvs, *deref;
						struct expr *isobj;
						struct block *bp1, *bp0, *bpjoin;
						ep = mkexpr(EXPRIDENT, ctype, NULL);
						ep->lvalue = true;
						ep->u.ident.decl = ed;
						isobj = cpp_exc_helper_call("_meuos_exc_caught_is_obj",
						    &typeint, NULL, NULL);
						bp1 = mkblock("exc_param_obj");
						bp0 = mkblock("exc_param_scalar");
						bpjoin = mkblock("exc_param_join");
						funcbranch(f, isobj, bp1, bp0);
						funclabel(f, bp1);
						/* copy the catch parameter from the slice-view of the
						 * carried object: *(T*)((char*)caught_obj + __exc_slice).
						 * The slice re-aims at the matching base sub-object so a
						 * catch(Base) by value copies Base's bytes, not the head of
						 * the derived object (same slice logic as the by-ref path). */
						cvs = exc_slice_ptr(f, ctype);
						deref = mkunaryexpr(TMUL, cvs);
						funcexpr(f, mkassignexpr(ep, deref));
						funcjmp(f, bpjoin);
						funclabel(f, bp0);
						/* scalar-or-none payload: leave param uninitialised */
						funcjmp(f, bpjoin);
						funclabel(f, bpjoin);
					}
					stmt(f, s); /* the catch body */
					if (ctype && ctype->kind == TYPESTRUCT ||
					    ctype && ctype->kind == TYPEUNION) {
						/* release the runtime-carried heap object after the
						 * catch consumed it (dtor + free; idempotent for a
						 * scalar exception) */
						funcexpr(f, cpp_exc_helper_call(
						    "_meuos_exc_caught_free", &typevoid, NULL, NULL));
					}
					funcjmp(f, bjoin);
				} else {
					/* catch(...) — catch-all: matches every thrown
					 * value, no parameter, runs the body.  C++ requires
					 * it last, so no further catch follows.  Clear bnext
					 * so the rethrow-after-loop doesn't re-label the
					 * previous catch's miss onto the rethrow: the
					 * catch-all already swallowed everything. */
					stmt(f, s);
					funcjmp(f, bjoin);
					bnext = NULL;
					break;
				}
				bnext = bmiss; /* next catch (or rethrow) lands on the miss */
			}
			if (cpp_tok_kind() != CPP_TCATCH)
				break;
		}
		/* no catch matched: rethrow the current exception (stage 3-4
		 * will make this precise; rethrow targets an outer handler or
		 * aborts if none, which matches uncaught semantics). */
		if (bnext)
			funclabel(f, bnext);
		{
			struct expr *ct, *cv, *rt;
			ct = cpp_exc_helper_call("_meuos_exc_caught_type", &typeint,
			    NULL, NULL);
			cv = cpp_exc_helper_call("_meuos_exc_caught_value", &typeullong,
			    NULL, NULL);
			rt = cpp_exc_helper_call("_meuos_exc_throw", &typevoid, ct, cv);
			funcexpr(f, rt);
		}
	}
	funclabel(f, bjoin);
	s = delscope(s);
}


/* C++ `throw` expression (`throw expr;` or bare `throw;` rethrow).
 *
 * `throw expr` lowers to `_meuos_exc_throw(typecode(expr), value)`.  A bare
 * `throw;` (no operand) rethrows the currently-handled exception — in the
 * value-passing ABI that is simply re-raising (caught_type, caught_value),
 * which the runtime routes to the next outer handler or aborts if none.
 */
struct expr *
cpp_parse_throw_expr(struct scope *s)
{
	extern struct expr *assignexpr(struct scope *);
	struct expr *e = NULL;

	next(); /* consume 'throw' */
	if (tok.kind != TSEMICOLON && tok.kind != TEOF)
		e = assignexpr(s);
	if (e)
		return cpp_exc_throw_call(e->type, e);
	/* bare `throw;` — rethrow the current exception */
	{
		struct expr *ct, *cv;
		ct = cpp_exc_helper_call("_meuos_exc_caught_type", &typeint,
		    NULL, NULL);
		cv = cpp_exc_helper_call("_meuos_exc_caught_value", &typeullong,
		    NULL, NULL);
		return cpp_exc_throw_call2(ct, cv);
	}
}
