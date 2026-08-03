/* mem2reg.c — MIR alloca promotion (scalar replacement of stack slots).
 *
 * The C/C++ frontends lower every named object — including parameters —
 * into an MOP_ALLOCA slot and route all reads/writes through MOP_LOAD /
 * MOP_STORE.  For scalars whose address never escapes, that memory traffic
 * is pure overhead: the machine backend has to materialise the slot address
 * into a register and dereference it on every access, producing the
 * characteristic
 *
 *      movq  %r12, %r10
 *      movl  (%r10), %eax
 *
 * pair inside loops.  passes.c's mloadfwd() already forwards store→load
 * pairs, but only *within* a single basic block, so a loop body reloads its
 * induction variable and parameters from memory on every iteration.
 *
 * This pass performs the classic mem2reg transform: promote qualifying
 * alloca slots to SSA values function-wide, inserting phi nodes at the
 * iterated dominance frontier of the slot's stores.  After promotion the
 * loads/stores (and the alloca itself) are dead and DCE removes them.
 *
 * Promotion is deliberately conservative — a slot qualifies only when
 *
 *   1. it is a DIRECT MOP_ALLOCA result (not a computed base+offset, which
 *      could alias another object),
 *   2. its type is a scalar the backend can keep in one register (no MT_AGG,
 *      no aggregate MTypeDesc),
 *   3. every use of the address value is either the address operand of a
 *      MOP_LOAD (src[0]) or of a MOP_STORE (src[1]) — any other appearance
 *      (ARG to a call, stored into another location, used by a phi, taken by
 *      an MOP_EXTRA op, …) means the address escapes and the slot is skipped,
 *   4. all loads/stores agree on one access type, so we never promote a slot
 *      that is type-punned through differently sized accesses,
 *   5. the alloca has a statically known scalar size matching that type.
 *
 * The rename step maintains the explicit-SSA invariants mssa_check() (ssa.c)
 * enforces: every value created here gets exactly one definition — either an
 * MIns (a load rewritten in place into a MOP_COPY) or an MPhi — never both.
 */
#include <stdlib.h>
#include <string.h>

#include "mir.h"

/* ---- CFG: predecessors, reverse postorder, dominators ------------------ */
/* Mirrors the (file-static) helpers in gvn.c; kept local so this pass owns
 * its own analysis and stays independent of gvn's pass ordering. */

static void
m2r_fill_preds(MFn *fn)
{
	for (MBlk *b = fn->link; b; b = b->link) {
		free(b->pred);
		b->pred = 0;
		b->npred = 0;
	}
	for (MBlk *b = fn->link; b; b = b->link) {
		if (b->s1) {
			MBlk *s = b->s1;
			s->pred = realloc(s->pred, (s->npred + 1) * sizeof *s->pred);
			s->pred[s->npred++] = b;
		}
		if (b->s2) {
			MBlk *s = b->s2;
			s->pred = realloc(s->pred, (s->npred + 1) * sizeof *s->pred);
			s->pred[s->npred++] = b;
		}
	}
}

/* Iterative DFS postorder from the entry block. */
static uint32_t
m2r_postorder(MFn *fn, MBlk **post)
{
	uint32_t n = fn->nblk ? fn->nblk : 1;
	MBlk **st = malloc(n * sizeof *st);
	uint32_t sp = 0, np = 0;

	for (MBlk *b = fn->link; b; b = b->link)
		b->visit = 0;
	if (!fn->start) {
		free(st);
		return 0;
	}
	st[sp++] = fn->start;
	fn->start->visit = 1;
	while (sp) {
		MBlk *b = st[sp - 1];
		MBlk *c = 0;
		if (b->s1 && !b->s1->visit)
			c = b->s1;
		else if (b->s2 && !b->s2->visit)
			c = b->s2;
		if (c) {
			c->visit = 1;
			st[sp++] = c;
		} else {
			post[np++] = b;
			sp--;
		}
	}
	free(st);
	return np;
}

/* rpo index per block id, filled by m2r_cfg (UINT32_MAX = unreachable) */
#define M2R_NORPO ((uint32_t)-1)

static MBlk *
m2r_dom_intersect(MBlk *a, MBlk *b, const uint32_t *ridx)
{
	while (a != b) {
		while (ridx[a->id] > ridx[b->id])
			a = a->idom;
		while (ridx[b->id] > ridx[a->id])
			b = b->idom;
	}
	return a;
}

/* Cooper/Harvey/Kennedy iterative dominators over the RPO. */
static void
m2r_fill_idom(MFn *fn, MBlk **rpo, uint32_t nrpo, const uint32_t *ridx)
{
	bool changed;

	if (!nrpo)
		return;
	for (MBlk *b = fn->link; b; b = b->link) {
		b->idom = 0;
		b->depth = 0;
	}
	rpo[0]->idom = rpo[0];
	do {
		changed = false;
		for (uint32_t i = 1; i < nrpo; i++) {
			MBlk *b = rpo[i];
			MBlk *ni = 0;
			for (uint32_t j = 0; j < b->npred; j++) {
				MBlk *p = b->pred[j];
				/* skip unreachable / not-yet-processed preds */
				if (!p->idom || ridx[p->id] == M2R_NORPO)
					continue;
				ni = ni ? m2r_dom_intersect(p, ni, ridx) : p;
			}
			if (ni && ni != b->idom) {
				b->idom = ni;
				changed = true;
			}
		}
	} while (changed);
	for (uint32_t i = 1; i < nrpo; i++)
		if (rpo[i]->idom)
			rpo[i]->depth = rpo[i]->idom->depth + 1;
}

/* Dominance frontiers (Cytron et al.): for every join point, walk each
 * predecessor up the dominator tree until the block's idom is reached. */
static void
m2r_fill_frontiers(MFn *fn, const uint32_t *ridx)
{
	for (MBlk *b = fn->link; b; b = b->link) {
		free(b->fron);
		b->fron = 0;
		b->nfron = 0;
	}
	for (MBlk *b = fn->link; b; b = b->link) {
		if (b->npred < 2 || ridx[b->id] == M2R_NORPO)
			continue;
		for (uint32_t j = 0; j < b->npred; j++) {
			MBlk *r = b->pred[j];
			if (ridx[r->id] == M2R_NORPO)
				continue;
			while (r && r != b->idom) {
				bool dup = false;
				for (uint32_t k = 0; k < r->nfron; k++)
					if (r->fron[k] == b) {
						dup = true;
						break;
					}
				if (!dup) {
					r->fron = realloc(r->fron,
					    (r->nfron + 1) * sizeof *r->fron);
					r->fron[r->nfron++] = b;
				}
				if (r->idom == r)   /* entry: stop */
					break;
				r = r->idom;
			}
		}
	}
}

/* ---- candidate selection ----------------------------------------------- */

typedef struct M2RSlot {
	MVal *addr;        /* the alloca result value */
	MType type;        /* uniform access type of all loads/stores */
	uint32_t idx;      /* dense slot index */
} M2RSlot;

/* Scalar types we are willing to hold in an SSA value.  Aggregates and
 * anything carrying an MTypeDesc are rejected: the backend keeps those in
 * memory and a phi over them has no register representation. */
static bool
m2r_scalar(MType t)
{
	switch (t) {
	case MT_I8: case MT_I16: case MT_I32: case MT_I64:
	case MT_F32: case MT_F64:
	case MT_PTR:
		return true;
	default:
		return false;
	}
}

static uint32_t
m2r_tysize(MType t)
{
	switch (t) {
	case MT_I8:  return 1;
	case MT_I16: return 2;
	case MT_I32: case MT_F32: return 4;
	case MT_I64: case MT_F64: return 8;
	case MT_PTR: return 8;
	default: return 0;
	}
}

/* Decide whether `v` (a direct alloca result) can be promoted, and if so
 * report the common access type in *out.
 *
 * Conservative escape test: the address may appear ONLY as the address
 * operand of a load (src[0]) or a store (src[1]).  Appearing as a store's
 * *value* operand (src[0]) means the address itself is written somewhere
 * and escapes; appearing in a phi, an ARG, or any other opcode likewise
 * disqualifies the slot. */
static bool
m2r_promotable(MVal *v, MType *out)
{
	MType ty = MT_NONE;
	bool seen = false;

	if (!v || v->kind != MV_TEMP || v->td)
		return false;
	if (v->nuse == 0)
		return false;    /* nothing to gain; let DCE drop it */

	for (uint32_t i = 0; i < v->nuse; i++) {
		MUse *u = &v->use[i];
		MIns *in = u->ins;
		MType at;

		if (u->phi || !in)
			return false;
		if (in->op == MOP_LOAD && u->argn == 0) {
			at = in->dtype;
		} else if (in->op == MOP_STORE && u->argn == 1) {
			at = in->dtype;
		} else {
			return false;    /* escapes */
		}
		/* A load/store with a constant displacement addresses a
		 * sub-object of the slot, not the whole scalar. */
		if (in->cst)
			return false;
		if (!m2r_scalar(at))
			return false;
		if (!seen) {
			ty = at;
			seen = true;
		} else if (ty != at) {
			return false;    /* type-punned access */
		}
	}
	if (!seen)
		return false;
	*out = ty;
	return true;
}

/* The alloca's byte size must be a known constant equal to the access type
 * size, otherwise the slot is larger than the scalar we would promote (an
 * array or a padded object) and promotion would drop storage. */
static bool
m2r_size_ok(MIns *alloca_in, MType ty)
{
	MConst *c = alloca_in->src[0].con;
	uint32_t want = m2r_tysize(ty);

	if (!want || !c || c->kind != MC_INT)
		return false;
	return (uint64_t)c->u.i == want;
}

/* ---- renaming ----------------------------------------------------------- */

typedef struct M2RCtx {
	MFn *fn;
	M2RSlot *slot;        /* dense array of promoted slots */
	uint32_t nslot;
	int32_t *slotof;      /* MVal id -> slot index, -1 none */
	MVal ***stack;        /* per-slot value stack (renaming) */
	uint32_t *nstack, *cstack;
	MPhi ***phi;          /* per-slot phi inserted per block id */
	uint32_t nblkid;
} M2RCtx;

static void
m2r_push(M2RCtx *c, uint32_t s, MVal *v)
{
	if (c->nstack[s] == c->cstack[s]) {
		c->cstack[s] = c->cstack[s] ? c->cstack[s] * 2 : 8;
		c->stack[s] = realloc(c->stack[s],
		    c->cstack[s] * sizeof *c->stack[s]);
	}
	c->stack[s][c->nstack[s]++] = v;
}

static MVal *
m2r_top(M2RCtx *c, uint32_t s)
{
	return c->nstack[s] ? c->stack[s][c->nstack[s] - 1] : 0;
}

/* An undefined read (load before any store on this path).  C makes this UB;
 * we materialise a zero constant so the value still has a definition and
 * mssa_check's single-def invariant holds. */
static MVal *
m2r_undef(M2RCtx *c, uint32_t s, MBlk *b)
{
	MFn *fn = c->fn;
	MType ty = c->slot[s].type;
	MVal *v = mval_new(fn, MV_TEMP, ty, 0, "m2r.undef");
	MConst *zero = (ty == MT_F32 || ty == MT_F64)
	    ? mconst_flt(fn, ty, 0.0)
	    : mconst_int(fn, ty, 0);
	MBlk *e = fn->start ? fn->start : b;

	/* Define it at the top of the entry block so it dominates every use. */
	if (e->nins == e->cins) {
		e->cins = e->cins ? e->cins * 2 : 8;
		e->ins = realloc(e->ins, e->cins * sizeof *e->ins);
	}
	memmove(&e->ins[1], &e->ins[0], e->nins * sizeof *e->ins);
	e->nins++;
	MIns *in = &e->ins[0];
	memset(in, 0, sizeof *in);
	in->op = MOP_COPY;
	in->dtype = ty;
	in->dst = v;
	in->src[0] = MREF_CON(zero);
	in->blk = e;
	v->def = in;
	v->defblk = e;
	/* moving the array invalidates every def back-pointer in this block */
	for (uint32_t i = 0; i < e->nins; i++)
		if (e->ins[i].dst && e->ins[i].dst->kind == MV_TEMP &&
		    e->ins[i].dst->def)
			e->ins[i].dst->def = &e->ins[i];
	return v;
}

/* Rewrite one block's loads/stores against the current renaming stacks,
 * then recurse over the dominator-tree children.  Iterative worklist with
 * an explicit "pop to this depth" marker, so deep CFGs cannot blow the C
 * stack. */
static void
m2r_rename(M2RCtx *c, MBlk **rpo, uint32_t nrpo, const uint32_t *ridx)
{
	MFn *fn = c->fn;
	uint32_t ns = c->nslot;

	/* dominator-tree children, built from idom */
	MBlk ***kids = calloc(c->nblkid, sizeof *kids);
	uint32_t *nkid = calloc(c->nblkid, sizeof *nkid);
	for (uint32_t i = 1; i < nrpo; i++) {
		MBlk *b = rpo[i];
		MBlk *d = b->idom;
		if (!d || d == b)
			continue;
		kids[d->id] = realloc(kids[d->id],
		    (nkid[d->id] + 1) * sizeof **kids);
		kids[d->id][nkid[d->id]++] = b;
	}

	/* work item: enter block, or unwind stacks to a recorded depth */
	typedef struct { MBlk *b; uint32_t *mark; } Item;
	Item *work = malloc((nrpo + 1) * sizeof *work);
	uint32_t nwork = 0;

	if (nrpo)
		work[nwork++] = (Item){ rpo[0], 0 };
	while (nwork) {
		Item it = work[--nwork];
		if (!it.b) {                       /* unwind marker */
			for (uint32_t s = 0; s < ns; s++)
				c->nstack[s] = it.mark[s];
			free(it.mark);
			continue;
		}
		MBlk *b = it.b;

		/* record stack depths so siblings start from our parent's state */
		uint32_t *mark = malloc((ns ? ns : 1) * sizeof *mark);
		for (uint32_t s = 0; s < ns; s++)
			mark[s] = c->nstack[s];

		/* 1. phis defined here become the current value */
		for (uint32_t s = 0; s < ns; s++)
			if (c->phi[s][b->id])
				m2r_push(c, s, c->phi[s][b->id]->dst);

		/* 2. rewrite the block body */
		for (uint32_t i = 0; i < b->nins; i++) {
			MIns *in = &b->ins[i];
			if (in->op == MOP_LOAD && in->src[0].val) {
				int32_t s = c->slotof[in->src[0].val->id];
				if (s < 0)
					continue;
				MVal *cur = m2r_top(c, (uint32_t)s);
				if (!cur)
					cur = m2r_undef(c, (uint32_t)s, b);
				/* the entry-block insert above may have moved
				 * this block's array; re-resolve */
				in = &b->ins[i];
				/* %dst = load %slot  ->  %dst = copy %cur */
				in->op = MOP_COPY;
				in->src[0] = MREF_VAL(cur);
				in->src[1] = (MRef){ 0 };
				in->cst = 0;
			} else if (in->op == MOP_STORE && in->src[1].val) {
				int32_t s = c->slotof[in->src[1].val->id];
				if (s < 0)
					continue;
				MRef v = in->src[0];
				MVal *nv;
				if (v.val) {
					nv = v.val;
				} else {
					/* store of a constant: materialise it so
					 * the slot's SSA value is always an MVal
					 * (phi args are MVal*, not MRef) */
					nv = mval_new(fn, MV_TEMP,
					    c->slot[s].type, 0, "m2r.c");
					in->op = MOP_COPY;
					in->dst = nv;
					in->src[0] = v;
					in->src[1] = (MRef){ 0 };
					in->cst = 0;
					nv->def = in;
					nv->defblk = b;
					m2r_push(c, (uint32_t)s, nv);
					continue;
				}
				/* the store itself becomes dead; turn it into a
				 * no-op so index/def bookkeeping stays stable
				 * (DCE drops MOP_NOP) */
				in->op = MOP_NOP;
				in->dst = 0;
				in->src[0] = (MRef){ 0 };
				in->src[1] = (MRef){ 0 };
				in->cst = 0;
				m2r_push(c, (uint32_t)s, nv);
			}
		}

		/* 3. fill successors' phi arguments for the edge b -> succ */
		MBlk *succ[2] = { b->s1, b->s2 };
		for (int k = 0; k < 2; k++) {
			MBlk *sb = succ[k];
			if (!sb)
				continue;
			for (uint32_t s = 0; s < ns; s++) {
				MPhi *p = c->phi[s][sb->id];
				if (!p)
					continue;
				MVal *cur = m2r_top(c, s);
				if (!cur)
					cur = m2r_undef(c, s, b);
				if (p->narg == p->carg) {
					p->carg = p->carg ? p->carg * 2 : 4;
					p->arg = realloc(p->arg,
					    p->carg * sizeof *p->arg);
					p->blk = realloc(p->blk,
					    p->carg * sizeof *p->blk);
				}
				p->arg[p->narg] = cur;
				p->blk[p->narg] = b;
				p->narg++;
			}
		}

		/* 4. schedule unwind, then children (LIFO -> children first) */
		work[nwork++] = (Item){ 0, mark };
		if (nwork + nkid[b->id] + 1 > nrpo + 1) {
			work = realloc(work,
			    (nwork + nkid[b->id] + 1) * sizeof *work);
		}
		for (uint32_t i = 0; i < nkid[b->id]; i++)
			work[nwork++] = (Item){ kids[b->id][i], 0 };
	}
	(void)ridx;
	for (uint32_t i = 0; i < c->nblkid; i++)
		free(kids[i]);
	free(kids);
	free(nkid);
	free(work);
}

/* ---- driver ------------------------------------------------------------- */

uint32_t
mmem2reg(MFn *fn)
{
	uint32_t promoted = 0;

	if (!fn || !fn->start || !fn->nblk)
		return 0;

	/* block ids are dense per function, but be defensive */
	uint32_t nblkid = 0;
	for (MBlk *b = fn->link; b; b = b->link)
		if (b->id + 1 > nblkid)
			nblkid = b->id + 1;
	if (!nblkid)
		return 0;

	m2r_fill_preds(fn);

	MBlk **post = malloc(fn->nblk * sizeof *post);
	uint32_t npost = m2r_postorder(fn, post);
	if (!npost) {
		free(post);
		return 0;
	}
	/* reverse the postorder in place -> RPO */
	MBlk **rpo = post;
	for (uint32_t i = 0, j = npost - 1; i < j; i++, j--) {
		MBlk *t = rpo[i];
		rpo[i] = rpo[j];
		rpo[j] = t;
	}
	uint32_t *ridx = malloc(nblkid * sizeof *ridx);
	for (uint32_t i = 0; i < nblkid; i++)
		ridx[i] = M2R_NORPO;
	for (uint32_t i = 0; i < npost; i++)
		ridx[rpo[i]->id] = i;

	m2r_fill_idom(fn, rpo, npost, ridx);
	m2r_fill_frontiers(fn, ridx);

	/* --- collect candidates ------------------------------------------- */
	build_uses(fn);
	M2RSlot *slot = 0;
	uint32_t nslot = 0;
	int32_t *slotof = malloc((fn->nval ? fn->nval : 1) * sizeof *slotof);
	for (uint32_t i = 0; i < fn->nval; i++)
		slotof[i] = -1;

	for (uint32_t i = 0; i < npost; i++) {
		MBlk *b = rpo[i];
		for (uint32_t n = 0; n < b->nins; n++) {
			MIns *in = &b->ins[n];
			MType ty;
			if (in->op != MOP_ALLOCA || !in->dst)
				continue;
			if (!m2r_promotable(in->dst, &ty)) {
				if (getenv("M2R_DIAG"))
					fprintf(stderr, "m2r: %s: slot v%u REJECT promotable\n",
					    fn->name, in->dst->id);
				continue;
			}
			if (!m2r_size_ok(in, ty)) {
				if (getenv("M2R_DIAG"))
					fprintf(stderr, "m2r: %s: slot v%u REJECT size ty=%d\n",
					    fn->name, in->dst->id, (int)ty);
				continue;
			}
			if (getenv("M2R_DIAG"))
				fprintf(stderr, "m2r: %s: slot v%u ACCEPT ty=%d\n",
				    fn->name, in->dst->id, (int)ty);
			slot = realloc(slot, (nslot + 1) * sizeof *slot);
			slot[nslot].addr = in->dst;
			slot[nslot].type = ty;
			slot[nslot].idx = nslot;
			slotof[in->dst->id] = (int32_t)nslot;
			nslot++;
		}
	}
	if (!nslot) {
		free(slot);
		free(slotof);
		free(ridx);
		free(post);
		return 0;
	}

	/* --- place phis at the iterated dominance frontier of the stores --- */
	MPhi ***phi = malloc(nslot * sizeof *phi);
	bool *has = malloc(nblkid * sizeof *has);      /* store in block */
	bool *queued = malloc(nblkid * sizeof *queued);
	MBlk **wl = malloc(nblkid * sizeof *wl);

	for (uint32_t s = 0; s < nslot; s++) {
		phi[s] = calloc(nblkid, sizeof **phi);
		memset(has, 0, nblkid * sizeof *has);
		memset(queued, 0, nblkid * sizeof *queued);
		uint32_t nwl = 0;

		/* blocks containing a store to this slot seed the worklist */
		for (uint32_t u = 0; u < slot[s].addr->nuse; u++) {
			MUse *use = &slot[s].addr->use[u];
			if (!use->ins || use->ins->op != MOP_STORE)
				continue;
			MBlk *b = use->ins->blk;
			if (!b || ridx[b->id] == M2R_NORPO || has[b->id])
				continue;
			has[b->id] = true;
			wl[nwl++] = b;
		}
		while (nwl) {
			MBlk *b = wl[--nwl];
			for (uint32_t k = 0; k < b->nfron; k++) {
				MBlk *d = b->fron[k];
				if (phi[s][d->id])
					continue;
				MVal *dst = mval_new(fn, MV_TEMP, slot[s].type,
				    0, "m2r.phi");
				mphi_add(fn, d, slot[s].type, dst);
				dst->defblk = d;
				phi[s][d->id] = d->phi;   /* just prepended */
				if (!queued[d->id]) {
					queued[d->id] = true;
					wl[nwl++] = d;
				}
			}
		}
		promoted++;
	}
	free(has);
	free(queued);
	free(wl);

	/* --- rename ------------------------------------------------------- */
	M2RCtx ctx;
	memset(&ctx, 0, sizeof ctx);
	ctx.fn = fn;
	ctx.slot = slot;
	ctx.nslot = nslot;
	ctx.slotof = slotof;
	ctx.phi = phi;
	ctx.nblkid = nblkid;
	ctx.stack = calloc(nslot, sizeof *ctx.stack);
	ctx.nstack = calloc(nslot, sizeof *ctx.nstack);
	ctx.cstack = calloc(nslot, sizeof *ctx.cstack);

	m2r_rename(&ctx, rpo, npost, ridx);

	/* --- drop the now-dead allocas ------------------------------------ */
	for (MBlk *b = fn->link; b; b = b->link) {
		uint32_t nout = 0;
		for (uint32_t i = 0; i < b->nins; i++) {
			MIns *in = &b->ins[i];
			if (in->op == MOP_ALLOCA && in->dst &&
			    in->dst->id < fn->nval && slotof[in->dst->id] >= 0)
				continue;
			b->ins[nout++] = *in;
		}
		b->nins = nout;
		/* the compaction moved instructions: refresh def back-pointers */
		for (uint32_t i = 0; i < b->nins; i++) {
			MIns *in = &b->ins[i];
			if (in->dst && in->dst->kind == MV_TEMP && in->dst->def)
				in->dst->def = in;
		}
	}
	/* promoted slot addresses no longer have a definition; retire them so
	 * mssa_check's "value has no definition" rule does not fire */
	for (uint32_t s = 0; s < nslot; s++) {
		slot[s].addr->kind = MV_NONE;
		slot[s].addr->def = 0;
		slot[s].addr->defphi = 0;
		slot[s].addr->nuse = 0;
	}

	for (uint32_t s = 0; s < nslot; s++) {
		free(phi[s]);
		free(ctx.stack[s]);
	}
	free(phi);
	free(ctx.stack);
	free(ctx.nstack);
	free(ctx.cstack);
	free(slot);
	free(slotof);
	free(ridx);
	free(post);

	build_uses(fn);
	return promoted;
}
