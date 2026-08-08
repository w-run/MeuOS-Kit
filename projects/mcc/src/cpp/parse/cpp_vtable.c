/* cpp_vtable.c — m++ (C++) virtual functions / vtable construction.
 *
 * Computes a class's vtable layout (slot ordering across the base
 * hierarchy), emits the ``__vptr`` member and ``.vtable`` objects, and
 * lowers virtual calls to vtable dispatch.  Entry points cpp_emit_vtables
 * / cpp_is_virtual / cpp_vslot_index / cpp_method_owner / cpp_base_offset
 * / cpp_make_vcall are exported to the rest of the C++ front-end.
 * cpp_init_vptrs is called by the member/ctor paths in cpp_parse.c.
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

/* --- virtual functions / vtable (C.2.5) ------------------------------ */

/* Signature key of a virtual method: `name` + trailing-const marker + the
 * encoded parameter types, independent of the declaring class so an
 * override in a derived class reuses the base slot.  Mirrors the mangled
 * name encoding in cpp_define_method (`Class_methodK<params>`). */
void
cpp_vkey(const char *mname, struct type *funct, bool is_const,
         char *buf, size_t bufsz)
{
	struct decl *cur;
	size_t n;

	snprintf(buf, bufsz, "%s%s", mname ? mname : "?", is_const ? "K" : "");
	n = strlen(buf);
	if (funct && funct->kind == TYPEFUNC) {
		for (cur = funct->u.func.params; cur; cur = cur->next) {
			char code[64];
			size_t cl;
			cpp_mangle_type(cur->type, code, sizeof code);
			cl = strlen(code);
			if (n + cl < bufsz) {
				memcpy(buf + n, code, cl + 1);
				n += cl;
			}
		}
	}
}

/* Symbol name of the implementation of virtual member `m` of `owner`
 * (`Class_methodK<params>`), matching cpp_define_method's mangling. */
static void
cpp_slot_mangled(struct type *owner, struct member *m,
                 char *buf, size_t bufsz)
{
	struct decl *cur;
	size_t n;

	snprintf(buf, bufsz, "%s_%s",
	         (owner && owner->u.structunion.tag) ? owner->u.structunion.tag
	                                            : "anon",
	         m->name[0] == '~' ? "dtor" : m->name);
	n = strlen(buf);
	if (m->is_const && n + 1 < bufsz) {
		strcpy(buf + n, "K");
		++n;
	}
	if (m->type && m->type->kind == TYPEFUNC) {
		for (cur = m->type->u.func.params; cur; cur = cur->next) {
			char code[64];
			size_t cl;
			cpp_mangle_type(cur->type, code, sizeof code);
			cl = strlen(code);
			if (n + cl < bufsz) {
				memcpy(buf + n, code, cl + 1);
				n += cl;
			}
		}
	}
}

/* Find the most-derived implementation of the virtual method with `key`
 * in the class hierarchy rooted at `d` (the class itself first, then its
 * direct bases in declaration order).  Returns true and sets *owner/*outm
 * on success. */
bool
cpp_find_final(struct type *d, const char *key, struct type **owner,
               struct member **outm)
{
	struct member *m;
	char k[256];

	if (!d || (d->kind != TYPESTRUCT && d->kind != TYPEUNION))
		return false;
	for (m = d->u.structunion.members; m; m = m->next) {
		if (m->is_virtual && m->name) {
			cpp_vkey(m->name[0] == '~' ? "dtor" : m->name, m->type, m->is_const, k, sizeof k);
			if (strcmp(k, key) == 0) {
				if (owner)
					*owner = d;
				if (outm)
					*outm = m;
				return true;
			}
		}
	}
	for (m = d->u.structunion.members; m; m = m->next)
		if (!m->name && cpp_find_final(m->type, key, owner, outm))
			return true;
	return false;
}

/* A class is abstract (cannot be instantiated) when it has a pure virtual
 * member (`= 0`) that is not overridden by a concrete implementation.  A
 * derived class that fills every inherited pure-virtual slot is concrete. */
bool
cpp_is_abstract(struct type *t)
{
	struct member *m, *impl;
	char k[256];

	if (!t || (t->kind != TYPESTRUCT && t->kind != TYPEUNION))
		return false;
	for (m = t->u.structunion.members; m; m = m->next) {
		if (m->is_virtual && m->name) {
			cpp_vkey(m->name, m->type, m->is_const, k, sizeof k);
			impl = NULL;
			if (cpp_find_final(t, k, NULL, &impl) && impl && impl->is_pure)
				return true;
		}
	}
	return false;
}

/* Class that declares the member function `name` of `t` (the defining
 * base for inherited methods), or NULL. */
struct type *
cpp_method_owner(struct type *t, const char *name)
{
	struct type *owner = NULL;

	if (cpp_method_member(t, name, &owner))
		return owner;
	return NULL;
}

bool
cpp_is_virtual(struct type *t, const char *name)
{
	struct member *m = cpp_method_member(t, name, NULL);

	return m && m->is_virtual;
}

/* Recursive helper for cpp_base_offset. */
static bool
cpp_base_offset_r(struct type *d, struct type *base, unsigned long long *off)
{
	struct member *m;
	unsigned long long sub;

	if (d == base) {
		*off = 0;
		return true;
	}
	if (!d || (d->kind != TYPESTRUCT && d->kind != TYPEUNION))
		return false;
	/* Search direct base subobjects (anonymous members) first. */
	for (m = d->u.structunion.members; m; m = m->next) {
		if (!m->name && m->type &&
		    (m->type->kind == TYPESTRUCT || m->type->kind == TYPEUNION)) {
			if (m->type == base) {
				*off = m->offset;
				return true;
			}
			if (cpp_base_offset_r(m->type, base, &sub)) {
				*off = m->offset + sub;
				return true;
			}
		}
	}
	/* Not found in the non-virtual hierarchy: check virtual bases. */
	if (d->u.structunion.virtual_bases) {
		struct cpp_vbase *vb;
		for (vb = d->u.structunion.virtual_bases; vb; vb = vb->next) {
			if (vb->type == base) {
				*off = vb->offset;
				return true;
			}
			/* recurse into the virtual base's own virtual bases */
			if (cpp_base_offset_r(vb->type, base, &sub)) {
				*off = vb->offset + sub;
				return true;
			}
		}
	}
	/* Also try each base's virtual_bases if this type has none directly */
	for (m = d->u.structunion.members; m; m = m->next) {
		if (!m->name && m->type &&
		    (m->type->kind == TYPESTRUCT || m->type->kind == TYPEUNION)) {
			if (cpp_base_offset_r(m->type, base, &sub)) {
				*off = sub;
				return true;
			}
		}
	}
	return false;
}

unsigned long long
cpp_base_offset(struct type *derived, struct type *base)
{
	unsigned long long off = 0;

	cpp_base_offset_r(derived, base, &off);
	return off;
}

int
cpp_vslot_index(struct type *t, struct member *m)
{
	(void)t;
	return m ? m->vslot : -1;
}

/* Insert the hidden vptr member at offset 0, shifting every existing
 * member (including base subobjects) up by one pointer.  Only called for
 * classes that need their own vptr (polymorphic with no polymorphic
 * primary base). */
static void
cpp_insert_vptr(struct type *t)
{
	struct member *v, *m;

	v = xmalloc(sizeof *v);
	v->name = xmalloc(7);
	strcpy(v->name, "__vptr");
	v->type = mkpointertype(&typevoid, QUALNONE);
	v->qual = QUALNONE;
	v->offset = 0;
	v->bits.before = v->bits.after = 0;
	v->access = ACC_PRIVATE;
	v->is_mutable = false;
	v->is_virtual = false;
	v->is_const = false;
	v->vslot = -1;
	v->next = t->u.structunion.members;
	for (m = v->next; m; m = m->next)
		/* function members occupy no layout space (their offset is the
		 * defining subobject's offset, not a byte position), so only
		 * data members and base subobjects shift up */
		if (!(m->type && m->type->kind == TYPEFUNC))
			m->offset += 8;
	t->u.structunion.members = v;
	t->size += 8;
	if (t->align < 8)
		t->align = 8;
}

/* --- virtual base DAG layout (simplified fixed-offset ABI) ---------- */

/* Collect every unique virtual base reachable from `t`, including those
 * inherited indirectly through another virtual or non-virtual base.
 * Returns the count of unique virtual bases found (written to *out
 * as a linked list, each at offset 0 — the DAG pass assigns offsets). */
static struct cpp_vbase *
collect_virtual_bases(struct type *t, struct cpp_vbase *seen)
{
	struct member *m;
	struct cpp_vbase *vb, *cur;
	struct cpp_vbase *tail = NULL, *result = NULL;

	if (!t || (t->kind != TYPESTRUCT && t->kind != TYPEUNION))
		return NULL;

	/* check direct bases for virtual ones, then recurse into every
	 * base (virtual and non-virtual alike) for indirect virtuals */
	for (m = t->u.structunion.members; m; m = m->next) {
		if (m->name || !m->type ||
		    (m->type->kind != TYPESTRUCT && m->type->kind != TYPEUNION))
			continue;
		if (m->is_virtual_base) {
			/* deduplicate: skip if already in `seen` */
			for (cur = seen; cur; cur = cur->next)
				if (cur->type == m->type)
					break;
			if (cur)
				continue;
			/* mark as seen, add to result */
			for (cur = result; cur; cur = cur->next)
				if (cur->type == m->type)
					break;
			if (cur)
				continue;
			vb = xmalloc(sizeof *vb);
			vb->type = m->type;
			vb->offset = 0;
			vb->next = NULL;
			if (tail)
				tail->next = vb;
			else
				result = vb;
			tail = vb;
			/* add to seen list too */
			vb = xmalloc(sizeof *vb);
			vb->type = m->type;
			vb->offset = 0;
			vb->next = seen;
			seen = vb;
			/* recurse into the virtual base class itself */
			seen = collect_virtual_bases(m->type, seen);
		}
		/* also recurse into non-virtual bases (they may inherit
		 * virtual bases of their own) */
		seen = collect_virtual_bases(m->type, seen);
	}
	return result;
}

/* Assign fixed offsets to the virtual bases of `t` and finalise the
 * class's size.  Virtual bases are placed after the non-virtual portion
 * (which is already finalised by this point), each aligned to its own
 * alignment, and each appearing only once (shared across diamonds). */
static void
assign_vbase_offsets(struct type *t, struct cpp_vbase *list)
{
	struct cpp_vbase *vb;
	unsigned long long off = ALIGNUP(t->size, 8); /* start after non-virtual tail */

	for (vb = list; vb; vb = vb->next) {
		int align = vb->type->align > 8 ? 8 : vb->type->align;
		off = ALIGNUP(off, align);
		vb->offset = off;
		off += vb->type->size;
	}
	/* update class size to cover the virtual bases */
	if (off > t->size)
		t->size = off;
	t->align = 8; /* virtual bases may require pointer alignment */
}

/* Compute the flattened virtual-base list for a complete-object class
 * `t`.  Called from cpp_compute_vtable after the sequential layout is
 * fixed.  Results are stored in t->u.structunion.virtual_bases. */
void
cpp_compute_virtual_bases(struct type *t)
{
	struct cpp_vbase *list, *vb;
	struct member *m;

	if (!t || (t->kind != TYPESTRUCT && t->kind != TYPEUNION))
		return;

	list = collect_virtual_bases(t, NULL);
	if (!list)
		return;

	assign_vbase_offsets(t, list);
	t->u.structunion.virtual_bases = list;
	t->u.structunion.has_virtual_base = true;

	/* back-patch the anonymous member offsets so existing recursive
	 * helpers (cpp_base_offset_r, cpp_is_derived) see the correct
	 * DAG-assigned position rather than the stale 0 from the
	 * sequential layout skip */
	for (m = t->u.structunion.members; m; m = m->next) {
		if (!m->name && m->is_virtual_base) {
			for (vb = list; vb; vb = vb->next)
				if (vb->type == m->type)
					m->offset = vb->offset;
		}
	}
}

/* --- virtual call through a virtual base ---------------------------- */

/* Find the offset of `base` within `derived` when `base` is a virtual
 * base.  Returns true and sets *off on success. */
bool
cpp_vbase_offset(struct type *derived, struct type *base,
                 unsigned long long *off)
{
	struct cpp_vbase *vb;

	if (!derived || (derived->kind != TYPESTRUCT && derived->kind != TYPEUNION))
		return false;
	/* check direct virtual_bases */
	for (vb = derived->u.structunion.virtual_bases; vb; vb = vb->next) {
		if (vb->type == base) {
			*off = vb->offset;
			return true;
		}
	}
	/* if derived doesn't have the list, try searching the member
	 * hierarchy recursively for a class that does */
	{
		struct member *m;
		for (m = derived->u.structunion.members; m; m = m->next) {
			if (!m->name && m->type &&
			    (m->type->kind == TYPESTRUCT || m->type->kind == TYPEUNION)) {
				if (cpp_vbase_offset(m->type, base, off))
					return true;
			}
		}
	}
	return false;
}

/* Polyorphic classes needing vtable emission, in definition order. */
struct cpp_vclass {
	struct type *t;
	struct cpp_vclass *next;
};
static struct cpp_vclass *g_cpp_vclasses;
static struct cpp_vclass **g_cpp_vclasses_end = &g_cpp_vclasses;

/* Compute the vtable slot layout for `t` (called once the class body and
 * all base classes are complete): primary-base slots first, then this
 * class's own new virtuals; overrides reuse the base slot index.  Also
 * inserts the hidden vptr member when needed and records the class for
 * vtable emission. */
void
cpp_compute_vtable(struct type *t)
{
	struct member *m;
	struct type *P = NULL;
	struct cpp_vslot *vs, *bvs, *nv, **ve;
	bool has_poly_base = false;

	if (!t || (t->kind != TYPESTRUCT && t->kind != TYPEUNION))
		return;

	/* primary base: first polymorphic direct base (anonymous member).
	 * Virtual bases are excluded from primary base consideration —
	 * they live at the tail of the object layout and never serve as
	 * the primary base (which must be at offset 0). */
	for (m = t->u.structunion.members; m; m = m->next) {
		if (!m->name && !m->is_virtual_base && m->type &&
		    (m->type->kind == TYPESTRUCT || m->type->kind == TYPEUNION) &&
		    m->type->u.structunion.poly) {
			P = m->type;
			break;
		}
	}
	t->u.structunion.primary_base = P;

	/* layout: primary base's slots, then own virtuals (overrides reuse
	 * the base slot instead of appending) */
	t->u.structunion.vslots = NULL;
	t->u.structunion.nvslots = 0;
	for (bvs = P ? P->u.structunion.vslots : NULL; bvs; bvs = bvs->next) {
		nv = xmalloc(sizeof *nv);
		nv->name = bvs->name;
		memcpy(nv->key, bvs->key, sizeof nv->key);
		nv->m = NULL;
		nv->owner = bvs->owner;
		nv->index = t->u.structunion.nvslots++;
		nv->next = NULL;
		ve = &t->u.structunion.vslots;
		while (*ve)
			ve = &(*ve)->next;
		*ve = nv;
	}
	for (vs = t->u.structunion.own_virtuals; vs; vs = vs->next) {
		int idx = -1;
		for (bvs = t->u.structunion.vslots; bvs; bvs = bvs->next)
			if (strcmp(bvs->key, vs->key) == 0) {
				idx = bvs->index;
				break;
			}
		vs->index = idx;
		if (idx < 0) {
			/* genuinely new virtual: append a slot */
			vs->index = t->u.structunion.nvslots++;
			nv = xmalloc(sizeof *nv);
			nv->name = vs->name;
			memcpy(nv->key, vs->key, sizeof nv->key);
			nv->m = vs->m;
			nv->owner = t;
			nv->index = vs->index;
			nv->next = NULL;
			ve = &t->u.structunion.vslots;
			while (*ve)
				ve = &(*ve)->next;
			*ve = nv;
		}
	}

	/* record the slot index on each virtual member (the call lowering
	 * reads it via m->vslot) */
	for (m = t->u.structunion.members; m; m = m->next) {
		if (m->is_virtual && m->name) {
			char key[256];
			cpp_vkey(m->name[0] == '~' ? "dtor" : m->name, m->type, m->is_const, key, sizeof key);
			for (vs = t->u.structunion.own_virtuals; vs; vs = vs->next)
				if (strcmp(vs->key, key) == 0) {
					m->vslot = vs->index;
					break;
				}
		}
	}

	for (m = t->u.structunion.members; m; m = m->next)
		if (!m->name && !m->is_virtual_base && m->type &&
		    (m->type->kind == TYPESTRUCT || m->type->kind == TYPEUNION) &&
		    m->type->u.structunion.poly)
			has_poly_base = true;
	t->u.structunion.poly = t->u.structunion.own_poly || has_poly_base;

	if (t->u.structunion.poly && !P && t->u.structunion.own_poly)
		cpp_insert_vptr(t);

	/* virtual base DAG layout: assign fixed offsets for every shared
	 * virtual base in this hierarchy (after the sequential layout). */
	cpp_compute_virtual_bases(t);

	/* record for vtable emission (once) */
	if (t->u.structunion.poly) {
		struct cpp_vclass *vc;
		for (vc = g_cpp_vclasses; vc; vc = vc->next)
			if (vc->t == t)
				return;
		vc = xmalloc(sizeof *vc);
		vc->t = t;
		vc->next = NULL;
		*g_cpp_vclasses_end = vc;
		g_cpp_vclasses_end = &vc->next;
	}
}

/* Vtable symbol name for class `most`, viewing the slot layout of
 * `owner`: `__mxx_vtable_<most>` (primary) or
 * `__mxx_vtable_<most>_<owner>` (secondary subobject). */
static void
vt_symbol_name(struct type *most, struct type *owner, char *buf, size_t bufsz)
{
	const char *mtag, *otag;

	mtag = most->u.structunion.tag ? most->u.structunion.tag : "anon";
	if (owner == most) {
		snprintf(buf, bufsz, "__mxx_vtable_%s", mtag);
		return;
	}
	otag = owner->u.structunion.tag ? owner->u.structunion.tag : "anon";
	snprintf(buf, bufsz, "__mxx_vtable_%s_%s", mtag, otag);
}

/* A decl whose address is the named vtable symbol (used by the vptr
 * initialization stores). */
static struct decl *
vt_decl(struct type *most, struct type *owner)
{
	extern struct decl *mkdecl(char *, enum declkind, struct type *,
	    enum typequal, enum linkage);
	char name[256];
	char *pname;
	struct decl *vd;

	vt_symbol_name(most, owner, name, sizeof name);
	pname = xmalloc(strlen(name) + 1);
	strcpy(pname, name);
	vd = mkdecl(pname, DECLOBJECT, mkpointertype(&typevoid, QUALNONE),
	            QUALNONE, LINKEXTERN);
	vd->value = mkglobal(vd);
	return vd;
}

/* Emit `*(void **)((char *)thisp + off) = &vtable` — install one vptr.
 * Built directly at the IR level (IADD + ISTOREL) to keep the frontend
 * function body simple. */
/* IR opcode numbers used by the vptr initialization (enum instkind in
 * irgen.h; values verified against include/ops.h). */
enum {
	CPP_IR_IADD = 1,
	CPP_IR_ISTOREL = 17,
};

/* Emit `*(void **)((char *)thisp + off) = &vtable` — install one vptr.
 * Built directly at the IR level (IADD + ISTOREL) to keep the frontend
 * function body simple. */
static void
emit_vptr_store(struct func *f, struct expr *thisp, unsigned long long off,
                struct decl *vd)
{
	extern struct value *funcexpr(struct func *, struct expr *);
	extern struct value *funcinst(struct func *, int, int,
	    struct value *, struct value *);
	extern struct value *mkintconst(unsigned long long);
	struct value *base;
	int pcls = typelong.size == 4 ? 'w' : 'l';

	base = funcexpr(f, thisp);
	if (off)
		base = funcinst(f, CPP_IR_IADD, pcls, base, mkintconst(off));
	/* *base = &__mxx_vtable_... (VALUE_GLOBAL address) */
	funcinst(f, CPP_IR_ISTOREL, 0, vd->value, base);
}

/* Install every vptr of a complete object of type `t` at `thisp`:
 * the primary vptr (offset 0) plus one per secondary polymorphic base
 * subobject, and one per polymorphic virtual base subobject.
 * Called at the start of a constructor body (after the base constructors
 * ran) and when an object with no user constructor is defined. */
void
cpp_init_vptrs(struct func *f, struct type *t, struct expr *thisp)
{
	struct member *m;
	struct cpp_vbase *vb;

	if (!f || !t || (t->kind != TYPESTRUCT && t->kind != TYPEUNION))
		return;
	if (!t->u.structunion.poly)
		return;
	/* primary vptr */
	if (t->u.structunion.primary_base || t->u.structunion.own_poly)
		emit_vptr_store(f, thisp, 0, vt_decl(t, t));
	/* secondary non-virtual polymorphic bases */
	for (m = t->u.structunion.members; m; m = m->next) {
		if (!m->name && !m->is_virtual_base && m->type &&
		    (m->type->kind == TYPESTRUCT || m->type->kind == TYPEUNION) &&
		    m->type->u.structunion.poly &&
		    m->type != t->u.structunion.primary_base)
			emit_vptr_store(f, thisp, m->offset, vt_decl(t, m->type));
	}
	/* polymorphic virtual bases: each gets its own vptr */
	for (vb = t->u.structunion.virtual_bases; vb; vb = vb->next) {
		if (vb->type->u.structunion.poly)
			emit_vptr_store(f, thisp, vb->offset, vt_decl(t, vb->type));
	}
}

/* Emit one vtable: the slot layout of `owner` filled with the most-derived
 * implementations found in `most`.  The primary table (owner == most) is
 * exported; secondary tables stay local. */
static void
emit_vtable_one(struct type *most, struct type *owner)
{
	char name[256];
	struct cpp_vslot *vs;

	vt_symbol_name(most, owner, name, sizeof name);
	printf(".section .data,\"aw\",@progbits\n");
	printf(".balign 8\n");
	if (owner == most)
		printf(".globl %s\n", name);
	printf("%s:\n", name);
	for (vs = owner->u.structunion.vslots; vs; vs = vs->next) {
		struct type *impl_owner = NULL;
		struct member *impl = NULL;
		if (cpp_find_final(most, vs->key, &impl_owner, &impl) && !impl->is_pure) {
			char mangled[256];
			cpp_slot_mangled(impl_owner, impl, mangled, sizeof mangled);
			printf("    .quad %s\n", mangled);
		} else {
			/* a pure virtual (`= 0`) has a null vtable slot */
			printf("    .quad 0\n");
		}
	}
}

/* Emit the vtables of every polymorphic class defined in this translation
 * unit: the primary table plus one secondary table per polymorphic
 * secondary base subobject, and one per polymorphic virtual base. */
void
cpp_emit_vtables(void)
{
	struct cpp_vclass *vc;
	struct member *m;
	struct cpp_vbase *vb;

	for (vc = g_cpp_vclasses; vc; vc = vc->next) {
		struct type *t = vc->t;
		if (t->u.structunion.primary_base || t->u.structunion.own_poly)
			emit_vtable_one(t, t);
		for (m = t->u.structunion.members; m; m = m->next) {
			if (!m->name && !m->is_virtual_base && m->type &&
			    (m->type->kind == TYPESTRUCT || m->type->kind == TYPEUNION) &&
			    m->type->u.structunion.poly &&
			    m->type != t->u.structunion.primary_base)
				emit_vtable_one(t, m->type);
		}
		/* polymorphic virtual bases: each gets its own vtable
		 * (emitted as a secondary vtable of the most-derived class) */
		for (vb = t->u.structunion.virtual_bases; vb; vb = vb->next) {
			if (vb->type->u.structunion.poly)
				emit_vtable_one(t, vb->type);
		}
	}
}

/* Shallow-deep clone of a simple expression tree.  Used to build the
 * virtual-call indirect callee expression without sharing nodes with the
 * call's this argument (the call tree is freed with delexpr, which
 * assumes a tree, not a DAG). */
static struct expr *
cpp_expr_clone(struct expr *e)
{
	struct expr *n;

	if (!e)
		return NULL;
	n = mkexpr(e->kind, e->type, e->base ? cpp_expr_clone(e->base) : NULL);
	n->qual = e->qual;
	n->lvalue = e->lvalue;
	n->decayed = e->decayed;
	n->op = e->op;
	n->toeval = NULL;
	n->next = NULL;
	switch (e->kind) {
	case EXPRIDENT:
		n->u.ident.decl = e->u.ident.decl;
		break;
	case EXPRCONST:
		n->u.constant = e->u.constant;
		break;
	case EXPRCAST:
		n->toeval = cpp_expr_clone(e->toeval);
		break;
	case EXPRBINARY:
		n->u.binary.l = cpp_expr_clone(e->u.binary.l);
		n->u.binary.r = cpp_expr_clone(e->u.binary.r);
		break;
	default:
		/* the this object is never anything more complex */
		break;
	}
	return n;
}

/* Build the indirect callable for a virtual member call `obj.vmeth(args)`:
 * `*(fn_type **)((*(void **)thisp) + slot * 8)`.  The call lowering
 * (TLPAREN) prepends the this argument and matches the explicit
 * parameters against fn_type. */
struct expr *
cpp_make_vcall(struct expr *thisp, struct type *owner, struct member *m,
               int slot)
{
	extern struct decl *mkdecl(char *, enum declkind, struct type *,
	    enum typequal, enum linkage);
	struct type *mtype, *fn_type;
	struct decl *td, *cur, *nd, **end;
	struct expr *addr, *vt, *fn;

	if (!owner || !m || !m->type || m->type->kind != TYPEFUNC)
		return NULL;

	mtype = m->type;
	/* signature with the implicit `this` prepended */
	fn_type = mktype(TYPEFUNC, 0);
	fn_type->base = mtype->base;
	fn_type->u.func.isvararg = mtype->u.func.isvararg;
	fn_type->u.func.params = NULL;
	fn_type->u.func.nparam = 0;
	td = mkdecl("this", DECLOBJECT,
	            mkpointertype(owner, m->is_const ? QUALCONST : QUALNONE),
	            QUALNONE, LINKNONE);
	td->u.obj.storage = SDAUTO;
	fn_type->u.func.params = td;
	fn_type->u.func.nparam = 1;
	end = &td->next;
	for (cur = mtype->u.func.params; cur; cur = cur->next) {
		nd = mkdecl(cur->name, DECLOBJECT, cur->type, cur->qual, LINKNONE);
		nd->u.obj.storage = SDAUTO;
		*end = nd;
		end = &nd->next;
		++fn_type->u.func.nparam;
	}

	/* vt = *(void **)thisp  (thisp cloned: the call's this argument is a
	 * separate node; delexpr would otherwise free it twice) */
	addr = mkbinaryexpr(&tok.loc, TADD,
	                    exprconvert(cpp_expr_clone(thisp), &typeulong),
	                    mkconstexpr(&typeulong, 0));
	addr->type = mkpointertype(mkpointertype(&typevoid, QUALNONE), QUALNONE);
	vt = mkunaryexpr(TMUL, addr);
	/* slot = (fn_type **)((unsigned long)vt + slot * 8)
	 * fn    = *slot   (the slot holds the implementation's address) */
	addr = mkbinaryexpr(&tok.loc, TADD, exprconvert(vt, &typeulong),
	                    mkconstexpr(&typeulong, (unsigned long long)slot * 8));
	addr->type = mkpointertype(mkpointertype(fn_type, QUALNONE), QUALNONE);
	fn = mkunaryexpr(TMUL, addr);
	return fn;
}

/* Resolve the address of a member function taken without a call
 * (`&obj.meth`).  The member symbols are argument-encoded
 * (`Class_meth_ii`), so a single overload is found by scanning the
 * class scope for the `Class_meth` prefix.  Returns the unique DECLFUNC
 * decl and copies its exact mangled name into `mname`; returns NULL when
 * there is no such method or more than one candidate (ambiguous). */
struct decl *
cpp_find_unique_member(struct type *t, const char *name,
                       char *mname, size_t mname_sz)
{
	extern struct scope filescope;
	struct scope *sc = t->scope ? t->scope : &filescope;
	const char *tag = t->u.structunion.tag ? t->u.structunion.tag : "anon";
	size_t plen = strlen(tag) + 1 + strlen(name);
	struct decl *found = NULL;
	const char *found_name = NULL;
	int n = 0;
	size_t i;

	snprintf(mname, mname_sz, "%s_%s", tag, name);
	for (i = 0; i < sc->decls.cap; i++) {
		const struct mapkey *k = &sc->decls.keys[i];
		if (!k->str)
			continue;
		if (k->len >= plen && memcmp(k->str, mname, plen) == 0) {
			struct decl *d = sc->decls.vals[i].p;
			if (d && d->kind == DECLFUNC) {
				found = d;
				found_name = k->str;
				++n;
			}
		}
	}
	if (n == 1) {
		snprintf(mname, mname_sz, "%s", found_name);
		return found;
	}
	return NULL;
}
