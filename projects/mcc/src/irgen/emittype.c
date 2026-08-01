/* emittype.c - Register a frontend struct/union type into IR's `typ[]` table.
 *
 * IR describes aggregate (struct/union) layout with `Typ` records stored in
 * the global `typ[]` array. Each Typ holds a list of `Field` descriptors that
 * the ABI passes (amd64/sysv.c `classify`/`typclass`) walk to decide whether a
 * value fits in registers (<=16B, well-aligned) or must be passed in memory.
 *
 * The legacy frontend emitter printed `type :tag = { w, w, }` text IL and let
 * IR's parser (`parsetyp`/`parsefields`) build the in-memory Typ. In direct-IR
 * mode we construct the Typ ourselves. This mirrors parsefields' field model:
 *   - Fb/Fh/Fw/Fl/Fs/Fd : a primitive of `len` bytes (1/2/4/8)
 *   - FPad               : `len` bytes of padding
 *   - FTyp               : a nested aggregate; `len` is its typ[] index
 *   - FEnd               : list terminator
 * Arrays expand to one field per element (as parsefields does); a struct has a
 * single field list (`nunion=1`), a union has one list per member.
 *
 * `emittype` is idempotent (`if (t->value) return`) so it is safe to call from
 * every site that might encounter an aggregate (mkfunc, funcexpr). */
#include "irgen.h"

/* Current number of Typ entries in `typ[]`.  IR keeps `ntyp` as a file-local
 * static in parse.c because only the parser grows the table; here emittype is
 * the sole grower, so a file-local static is equivalent.  ABI passes only read
 * `typ[idx]`, never `ntyp`, so a private counter is fine. */
static ulong ntyp;

/* Map a frontend `data` class char ('b','h','w','l','s','d') to a IR Field
 * type enum. These match parsefields' switch in qbe/parse.c L977-983. */
static int
data_to_fieldtype(char data)
{
	switch (data) {
	case 'b': return Fb;
	case 'h': return Fh;
	case 'w': return Fw;
	case 'l': return Fl;
	case 's': return Fs;
	case 'd': return Fd;
	default:  die("mcc: unsupported struct member data class '%c'", data);
	}
}

/* log2 of a byte alignment (1->0, 2->1, 4->2, 8->3, 16->4).
 * IR's Typ.align stores this exponent; typclass does `1u << t->align`. */
static int
align_log2(unsigned a)
{
	int l = 0;
	while (a > 1) {
		a >>= 1;
		l++;
	}
	return l;
}

/* Append the fields describing one member into f[] starting at *pn, expanding
 * arrays to one field per element (parsefields L1012-1015). Strips array
 * wrappers to reach the element type. Nested aggregates recurse via emittype
 * (a no-op if already emitted) and become FTyp fields carrying the typ[] index.
 * Does NOT write FEnd; the caller does. */
static void
emitmemberfields(Field *f, uint *pn, struct member *m)
{
	struct type *sub;
	uint n = *pn;
	uint elemsz, count, c;

	/* peel array types: e.g. int[3] -> base int */
	for (sub = m->type; sub->kind == TYPEARRAY; sub = sub->base)
		;
	elemsz = sub->size;
	count = elemsz ? (unsigned)(m->type->size / elemsz) : 1;
	if (sub->kind == TYPESTRUCT || sub->kind == TYPEUNION) {
		/* ensure the nested type has a typ[] slot; idempotent */
		emittype(sub);
		for (c = 0; c < count && n < NField; c++, n++) {
			f[n].type = FTyp;
			f[n].len = sub->value->id;
		}
	} else {
		int ft = data_to_fieldtype(irtype(sub).data);
		for (c = 0; c < count && n < NField; c++, n++) {
			f[n].type = ft;
			f[n].len = elemsz;
		}
	}
	*pn = n;
}

void
emittype(struct type *t)
{
	Typ *ty;
	struct member *m;
	struct type *sub;
	struct value *v;
	uint idx;

	/* idempotent: already assigned a typ[] slot */
	if (t->value)
		return;
	/* only aggregates need a Typ entry; scalars use class chars directly */
	if (t->kind != TYPESTRUCT && t->kind != TYPEUNION)
		return;

	/* allocate the VALUE_TYPE descriptor and reserve a typ[] slot.
	 * v->id is the 0-based typ[] index (TYPE(v->id) and fn->retty use it
	 * directly to index typ[]), matching qbe/parse.c parsetyp which does
	 * `ty = &typ[ntyp++]`. The index is stable across reallocations
	 * (vgrow preserves entries), but the Typ * pointer is not -- so we
	 * save the index and re-fetch the pointer after any recursive
	 * emittype (which may vgrow typ[]). */
	v = xmalloc(sizeof *v);
	v->kind = VALUE_TYPE;
	v->u.name = t->u.structunion.tag;
	v->id = ntyp;          /* 0-based index of the slot we're about to fill */
	vgrow(&typ, ntyp + 1);
	idx = ntyp;            /* save: typ[] may be realloc'd by recursion below */
	ntyp++;                /* reserve the slot */
	t->value = v;

	/* recursively register nested aggregate members BEFORE building this
	 * type's fields, so FTyp indices are valid. C disallows a struct
	 * containing itself by value, so this recursion is a finite DAG;
	 * pointer members are skipped (not struct/union). Mirrors reference
	 * cproc/qbe.c L1214-1218. */
	for (m = t->u.structunion.members; m; m = m->next) {
		for (sub = m->type; sub->kind == TYPEARRAY; sub = sub->base)
			;
		if (sub->kind == TYPESTRUCT || sub->kind == TYPEUNION)
			emittype(sub);
	}

	/* re-fetch Typ *: typ[] may have been realloc'd by the recursion above */
	ty = &typ[idx];
	memset(ty, 0, sizeof *ty);
	ty->name = v->u.name;
	ty->isunion = (t->kind == TYPEUNION);
	ty->isdark = 0;
	ty->align = t->align ? align_log2(t->align) : 0;
	ty->size = t->size;

	if (t->kind == TYPEUNION) {
		/* each union member is its own field list (parse.c L1073-1081);
		 * nunion = number of members */
		uint n = 0, i;
		for (m = t->u.structunion.members; m; m = m->next)
			n++;
		ty->fields = vnew(n ? n : 1, sizeof ty->fields[0], PHeap);
		i = 0;
		for (m = t->u.structunion.members; m; m = m->next, i++) {
			Field *f = ty->fields[i];
			uint fn = 0;
			emitmemberfields(f, &fn, m);
			f[fn].type = FEnd;
		}
		ty->nunion = i;
	} else {
		/* struct: a single field list, members laid out by offset */
		Field *f;
		uint n = 0;
		unsigned long long off = 0;
		ty->fields = vnew(1, sizeof ty->fields[0], PHeap);
		f = ty->fields[0];
		for (m = t->u.structunion.members; m; m = m->next) {
			/* C++ function members occupy no object storage and must not
			 * contribute to the IR layout. */
			if (m->type && m->type->kind == TYPEFUNC)
				continue;
			unsigned long long end = m->offset + m->type->size;
			int bitfield = m->bits.before || m->bits.after;

			/* A sequence of C bit-fields can share one allocation unit.
			 * The IR field list describes bytes, not source declarations, so
			 * emitting every declaration turns e.g. `unsigned a:30, b:2`
			 * into two words.  That made `struct Ins` claim 16 bytes while
			 * its ABI classification walked 20 bytes.  Emit the storage unit
			 * once and skip later fields wholly contained by it. */
			if (m->offset < off) {
				if (bitfield && end <= off)
					continue;
				die("mcc: overlapping aggregate members in IR layout");
			}
			/* insert padding for any gap before this member's offset */
			if (m->offset > off) {
				if (n < NField) {
					f[n].type = FPad;
					f[n].len = (uint)(m->offset - off);
					n++;
				}
				off = m->offset;
			}
			emitmemberfields(f, &n, m);
			off = end;
		}
		f[n].type = FEnd;
		ty->nunion = 1;
	}
}
