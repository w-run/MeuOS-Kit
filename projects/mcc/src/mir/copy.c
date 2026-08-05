/* copy.c — MIR copy propagation (B.2).
 *
 * Eliminates MOP_COPY instructions by forwarding the source value to the
 * copy's uses, and collapses phis whose incoming values are all the same
 * (phi copy propagation).
 *
 * Safety: MIR from func_to_mir is SSA-ish but not guaranteed to be strict
 * SSA (a temp may be redefined across blocks, e.g. in hand-built MFn).
 * So, mirroring msimp_block, a copy is only forwarded when every use of
 * its destination stays in the same block (used_outside) and the source
 * is also block-local (defined_outside): then the rewrite cannot change
 * the SSA dependency structure that promote/ssa later relies on.  Phi
 * collapse is inherently safe (the common value dominates every incoming
 * edge).
 */
#include <stdlib.h>

#include "mir.h"

/* Is `v` used outside block `b` (including by a phi)? */
static bool
used_outside(MFn *fn, MVal *v, MBlk *b)
{
	(void)fn;
	if (!v || v->kind != MV_TEMP)
		return false;
	for (uint32_t i = 0; i < v->nuse; i++) {
		MUse *u = &v->use[i];
		if (u->phi)
			return true;
		if (u->ins && u->ins->blk != b)
			return true;
	}
	return false;
}

/* Is `v` defined outside block `b`?  Conservative when no def block. */
static bool
defined_outside(MFn *fn, MVal *v, MBlk *b)
{
	(void)fn;
	if (!v || v->kind != MV_TEMP)
		return false;
	if (v->defblk)
		return v->defblk != b;
	return v->def != 0;
}

/* Forward `src` to every use of `dst`, then mark the copy dead. */
static void
kill_copy(MFn *fn, MBlk *b, MIns *in)
{
	MRef s = in->src[0];
	if (s.val || s.con)
		mref_replace(fn, in->dst, s);
	in->op = MOP_NONE;
	in->dst = NULL;
	(void)b;
}

static uint32_t
mcopy_block(MFn *fn, MBlk *b)
{
	uint32_t removed = 0;
	MIns *out = b->ins;
	uint32_t nout = 0;

	for (uint32_t i = 0; i < b->nins; i++) {
		MIns *in = &b->ins[i];
		bool dead = false;

		if (in->op == MOP_COPY && in->dst) {
			MRef s = in->src[0];
			bool src_ok = false;

			if (s.val && s.val->kind == MV_TEMP) {
				/* same-type (or untyped) temp source that is local to
				 * this block and whose uses are all in this block */
				if ((in->dst->type == MT_NONE ||
				     s.val->type == MT_NONE ||
				     in->dst->type == s.val->type) &&
				    !used_outside(fn, in->dst, b) &&
				    !defined_outside(fn, s.val, b))
					src_ok = true;
			} else if (s.con) {
				/* constant source: safe to forward anywhere local */
				if ((in->dst->type == MT_NONE ||
				     s.con->type == MT_NONE ||
				     in->dst->type == s.con->type) &&
				    !used_outside(fn, in->dst, b))
					src_ok = true;
			}
			if (src_ok) {
				kill_copy(fn, b, in);
				removed++;
				dead = true;
			}
		}
		if (!dead)
			out[nout++] = *in;
	}
	b->nins = nout;
	return removed;
}

/* Collapse a phi whose arguments are all the same value.  In SSA such a
 * phi is a no-op: the common value dominates every predecessor edge, so
 * replacing the phi result by it preserves dominance.  Self-edges (arg ==
 * dst, loop-carried) are excluded: collapsing those needs deeper loop
 * analysis that the LIR gvn performs after the bridge. */
static uint32_t
mcopy_phis(MFn *fn, MBlk *b)
{
	uint32_t removed = 0;
	MPhi **pp = &b->phi;

	while (*pp) {
		MPhi *p = *pp;
		MVal *common = NULL;
		bool same = true;
		bool self = false;

		for (uint32_t i = 0; i < p->narg; i++) {
			MVal *a = p->arg[i];
			if (!a)
				continue;
			if (a == p->dst) {
				self = true;
				continue;
			}
			if (!common)
				common = a;
			else if (a != common) {
				same = false;
				break;
			}
		}
		if (same && !self && common) {
			mref_replace(fn, p->dst, MREF_VAL(common));
			p->dst->defphi = NULL;
			*pp = p->link;
			free(p->arg);
			free(p->blk);
			free(p);
			removed++;
			/* rebuild so no use record dangles on the freed phi */
			build_uses(fn);
			continue;
		}
		pp = &p->link;
	}
	return removed;
}

uint32_t
mcopy(MFn *fn)
{
	uint32_t r = 0;

	/* Pass 1: copies.  Rebuild use chains per block so the rewrite never
	 * follows a use record whose instruction was already removed. */
	for (MBlk *b = fn->link; b; b = b->link) {
		build_uses(fn);
		r += mcopy_block(fn, b);
	}
	/* Pass 2: phi collapse. */
	for (MBlk *b = fn->link; b; b = b->link) {
		build_uses(fn);
		r += mcopy_phis(fn, b);
	}
	build_uses(fn);
	return r;
}
