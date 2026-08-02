/* slotmerge.c — spill slot lifetime reuse (P2 performance).
 *
 * After spill+rega, every spilled temporary is referenced as RSlot(s)
 * and each slot is owned by exactly one temp (spill.c slot() never
 * reuses a slot, so hundreds of spilled temps => hundreds of slots =>
 * a multi-KB frame on BZ2_decompress).  This pass merges slots whose
 * lifetimes never overlap, shrinking fn->slot and therefore the stack
 * frame.
 *
 * Correctness model (conservative, per docs/p2-spill-slot-reuse.md):
 *  - A slot's live region is derived from its RSlot references in the
 *    final instruction stream — never from the spill-destroyed b->in/out
 *    liveness (that is what made the earlier P1 attempt segfault).
 *  - A slot referenced inside a loop is never merged: its value is live
 *    across iterations and the linear position model cannot prove that
 *    two loop refs never overlap.  (fillloop's b->loop > 1 marks loop
 *    blocks; rega keeps it up to date for new blocks.)
 *  - Merging requires the regions to be strictly disjoint; any doubt
 *    means no merge.  Single-access slots (one store and no reload, or
 *    a bare load) carry no usable lifetime info and are never merged.
 *  - Slots addressed through an RMem operand (arrays / structs: the base
 *    is a slot but the offset/index may reach far beyond the width of a
 *    single scalar temp) are never merged; their full extent is kept.
 *  - Merged slots are grouped by width (4 B / 8 B); an 8 B slot occupies
 *    two 4 B units, keeping the emit invariant s <= fn->slot.
 *
 * All temporaries are arena-allocated with alloc() (freed by freeall at
 * the end of function processing), matching the other backend passes.
 *
 * The pass is independent of rega/spill internals; it only rewrites
 * RSlot operands and fn->slot.  Disable by not invoking it (passes.c).
 */
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "ir.h"

/* [a, b) with 0 <= a; b == 0 means empty. */
typedef struct Range Range;
struct Range {
	int a, b;
};

static inline void
radd(Range *r, int n)
{
	if (!r->b)
		*r = (Range){n, n + 1};
	else if (n < r->a)
		r->a = n;
	else if (n >= r->b)
		r->b = n + 1;
}

void
slotmerge(Fn *fn)
{
	Blk *b;
	Ins *i;
	Phi *p;
	Range *br, *rng;
	int *width, *map, *newid, *vis, *mergeable;
	int *ext, *dyn;
	int nsl, nvis, ip, s, t, ng, gi, cur, ns, n;
	uint u;

	nsl = fn->slot;
	if (nsl <= 0)
		return;

	/* Slot width (in 4 B units): an 8 B temp owns a 2-unit slot.
	 * alloc() does not zero the arena memory, so clear it first. */
	width = alloc(nsl * sizeof *width);
	memset(width, 0, nsl * sizeof *width);
	for (t = 0; t < fn->ntmp; t++)
		if (fn->tmp[t].slot >= 0 && fn->tmp[t].slot < nsl)
			width[fn->tmp[t].slot] = KWIDE(fn->tmp[t].cls) ? 8 : 4;

	br = alloc(fn->nblk * sizeof *br);
	rng = alloc(nsl * sizeof *rng);
	memset(rng, 0, nsl * sizeof *rng);
	vis = alloc(nsl * sizeof *vis);
	map = alloc(nsl * sizeof *map);
	mergeable = alloc(nsl * sizeof *mergeable);
	ext = alloc(nsl * sizeof *ext);
	dyn = alloc(nsl * sizeof *dyn);
	for (s = 0; s < nsl; s++) {
		map[s] = -1;
		mergeable[s] = 1;
		ext[s] = 0;
		dyn[s] = 0;
	}

	/* Record one RSlot reference for slot `s` (access size in bytes for
	 * the extent model).  A slot referenced through RMem is never
	 * merged: its storage unit can be much larger than a scalar temp. */
#define ACC(s, sz) do { \
	if ((s) >= 0 && (s) < nsl) { \
		int _k, _seen = 0; \
		radd(&rng[s], ip); \
		for (_k = 0; _k < nvis; _k++) \
			if (vis[_k] == (s)) { _seen = 1; break; } \
		if (!_seen) \
			vis[nvis++] = (s); \
		if ((sz) > ext[s]) \
			ext[s] = (sz); \
	} \
} while (0)
#define TOUCH(rr) do { \
	Ref _r = (rr); \
	if (rtype(_r) == RSlot) \
		ACC(rsval(_r), 0); \
} while (0)
#define TOUCHMEM(rr, clssz) do { \
	Ref _r = (rr); \
	if (rtype(_r) == RMem) { \
		Mem *_mm = &fn->mem[_r.val]; \
		int _off = 0, _dyn = 0; \
		if (_mm->offset.type == CBits) \
			_off = (int)_mm->offset.bits.i; \
		if (rtype(_mm->index) == RCon && \
		    fn->con[_mm->index.val].type == CBits) \
			_off += (int)fn->con[_mm->index.val].bits.i * \
			    (_mm->scale ? _mm->scale : 1); \
		else if (rtype(_mm->index) == RTmp) \
			_dyn = 1; \
		if (rtype(_mm->base) == RSlot) { \
			int _s = rsval(_mm->base); \
			if (_s >= 0 && _s < nsl) { \
				mergeable[_s] = 0; \
				dyn[_s] |= _dyn; \
				ACC(_s, _dyn ? INT_MAX : (_off + (clssz))); \
			} \
		} \
		if (rtype(_mm->index) == RSlot) { \
			int _s = rsval(_mm->index); \
			if (_s >= 0 && _s < nsl) { \
				mergeable[_s] = 0; \
				dyn[_s] = 1; \
				ACC(_s, INT_MAX); \
			} \
		} \
	} else { \
		TOUCH(rr); \
	} \
} while (0)

	ip = INT_MAX - 1;
	for (n = fn->nblk - 1; n >= 0; n--) {
		b = fn->rpo[n];
		br[n].b = ip--;
		nvis = 0;
		for (p = b->phi; p; p = p->link) {
			TOUCH(p->to);
			for (u = 0; u < p->narg; u++)
				TOUCH(p->arg[u]);
			--ip;
		}
		for (i = &b->ins[b->nins]; i != b->ins;) {
			int sz = 8;
			--i;
			TOUCH(i->to);
			if (isload(i->op))
				sz = loadsz(i);
			else if (isstore(i->op))
				sz = storesz(i);
			else if (i->cls == Kw || i->cls == Ks)
				sz = 4;
			TOUCHMEM(i->arg[0], sz);
			TOUCHMEM(i->arg[1], sz);
			--ip;
		}
		TOUCH(b->jmp.arg);
		TOUCH(b->jmp.arg1);
		br[n].a = ip;
		/* Slots referenced inside a loop are live across iterations;
		 * the linear position model cannot prove non-overlap, so keep
		 * them unmerged. */
		if (b->loop > 1)
			for (t = 0; t < nvis; t++)
				mergeable[vis[t]] = 0;
	}
#undef ACC
#undef TOUCH
#undef TOUCHMEM

	/* Greedy merge per width: a slot joins a group when its region does
	 * not overlap the group's (regions sorted by start). */
	{
		int *slots = alloc(nsl * sizeof *slots);
		int *sa = alloc(nsl * sizeof *sa);
		int *sb = alloc(nsl * sizeof *sb);
		int *grep = alloc(nsl * sizeof *grep);
		int *gmaxb = alloc(nsl * sizeof *gmaxb);
		int wpass, w;

		for (wpass = 0; wpass < 2; wpass++) {
			w = wpass ? 8 : 4;
			ns = 0;
			for (s = 0; s < nsl; s++)
				if (rng[s].b && rng[s].b - rng[s].a > 1 &&
				    width[s] == w && mergeable[s]) {
					slots[ns] = s;
					sa[ns] = rng[s].a;
					sb[ns] = rng[s].b;
					ns++;
				}
			/* insertion sort by (a, slot id) — ns is small */
			for (t = 1; t < ns; t++) {
				int key = slots[t], ka = sa[t], kb = sb[t];
				gi = t;
				while (gi > 0 &&
				    (sa[gi-1] > ka ||
				     (sa[gi-1] == ka && slots[gi-1] > key))) {
					slots[gi] = slots[gi-1];
					sa[gi] = sa[gi-1];
					sb[gi] = sb[gi-1];
					gi--;
				}
				slots[gi] = key;
				sa[gi] = ka;
				sb[gi] = kb;
			}
			ng = 0;
			for (t = 0; t < ns; t++) {
				s = slots[t];
				for (gi = 0; gi < ng; gi++)
					if (gmaxb[gi] <= sa[t]) /* disjoint */
						break;
				if (gi < ng) {
					map[s] = grep[gi];
					if (sb[t] > gmaxb[gi])
						gmaxb[gi] = sb[t];
				} else {
					grep[ng] = s;
					gmaxb[ng] = sb[t];
					map[s] = s;
					ng++;
				}
			}
		}
	}

	/* Renumbering.  Unmergeable slots are frontend locals (slot <
	 * front_slot); they keep their original id and a measured extent —
	 * the rest of the frontend region when an index makes it unknown.
	 * Merged (spill) slots get dense ids after the frontend region,
	 * 8 B slots consuming two 4 B units so s <= fn->slot holds. */
	newid = alloc(nsl * sizeof *newid);
	for (s = 0; s < nsl; s++)
		newid[s] = -1;
	cur = 0;
	for (s = 0; s < nsl; s++)
		if (rng[s].b && !mergeable[s]) {
			int units;
			newid[s] = s;
			if (dyn[s]) {
				int rem = fn->front_slot > s ? fn->front_slot - s : 1;
				units = rem;
			} else {
				units = (ext[s] + 3) / 4;
				if (units < 1)
					units = 1;
			}
			if (s + units > cur)
				cur = s + units;
		}
	for (s = 0; s < nsl; s++)
		if (rng[s].b && mergeable[s] && (map[s] == s || map[s] == -1)) {
			newid[s] = cur;
			cur += (width[s] == 8) ? 2 : 1;
		}
	fn->slot = cur;

	/* Rewrite every RSlot operand to its merged slot. */
#define REWRITE(pp) do { \
	Ref *_pp = (pp); \
	if (rtype(*_pp) == RSlot) { \
		int _s = rsval(*_pp); \
		if (_s >= 0 && _s < nsl) { \
			int _d = newid[map[_s] >= 0 ? map[_s] : _s]; \
			assert(_d >= 0 && _d < fn->slot); \
			*_pp = SLOT(_d); \
		} \
	} \
} while (0)

	for (b = fn->start; b; b = b->link) {
		for (p = b->phi; p; p = p->link) {
			REWRITE(&p->to);
			for (u = 0; u < p->narg; u++)
				REWRITE(&p->arg[u]);
		}
		for (i = b->ins; i < &b->ins[b->nins]; i++) {
			REWRITE(&i->to);
			REWRITE(&i->arg[0]);
			REWRITE(&i->arg[1]);
			if (rtype(i->arg[0]) == RMem) {
				Mem *mm = &fn->mem[i->arg[0].val];
				REWRITE(&mm->base);
				REWRITE(&mm->index);
			}
			if (rtype(i->arg[1]) == RMem) {
				Mem *mm = &fn->mem[i->arg[1].val];
				REWRITE(&mm->base);
				REWRITE(&mm->index);
			}
		}
		REWRITE(&b->jmp.arg);
		REWRITE(&b->jmp.arg1);
	}
#undef REWRITE

	if (debug['P']) {
		int nu = 0, nlead8 = 0, nlead4 = 0;
		for (s = 0; s < nsl; s++)
			if (rng[s].b) {
				if (!mergeable[s])
					nu++;
				else if (map[s] == s) {
					if (width[s] == 8) nlead8++;
					else nlead4++;
				}
			}
		fprintf(stderr,
			"slotmerge: %s %d slots -> %d slots"
			" (unmergeable=%d lead8=%d lead4=%d)\n",
			fn->name, nsl, fn->slot, nu, nlead8, nlead4);
	}
}
