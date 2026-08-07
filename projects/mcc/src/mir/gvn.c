/* gvn.c — MIR global value numbering (B.2).
 *
 * Assigns every pure instruction a value number derived from its opcode,
 * destination type (MType) and its operands' value numbers.  When a later
 * instruction computes a value whose defining instruction dominates it,
 * its uses are rewritten to the earlier result and the redundant
 * computation is eliminated (DCE removes the now-dead instruction).
 * Operates function-wide: blocks are processed in RPO and a value may be
 * reused only when its definition dominates the use, which preserves the
 * SSA invariants the machine backend and bridge rely on.
 *
 * Only pure instructions are numbered: arithmetic, comparisons and
 * conversions.  Loads/stores/calls/allocas are excluded (memory side
 * effects).  Mirrors the LIR gvn pass but operates on MFn so the MIR
 * machine backend benefits directly.
 */
#include <stdlib.h>

#include "mir.h"

/* ---- CFG / dominators --------------------------------------------------- */

static void
fill_preds(MFn *fn)
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

/* Collect blocks in postorder from start; returns count. */
static uint32_t
blk_post(MFn *fn, MBlk **post)
{
	uint32_t n = fn->nblk;
	MBlk **st = malloc((n ? n : 1) * sizeof *st);
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
		MBlk *b = st[sp-1];
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

static uint32_t
rpo_idx(MBlk **rpo, uint32_t nrpo, MBlk *b)
{
	for (uint32_t i = 0; i < nrpo; i++)
		if (rpo[i] == b)
			return i;
	return nrpo;
}

static MBlk *
dom_intersect(MBlk *a, MBlk *b, MBlk **rpo, uint32_t nrpo)
{
	while (a != b) {
		while (rpo_idx(rpo, nrpo, a) > rpo_idx(rpo, nrpo, b))
			a = a->idom;
		while (rpo_idx(rpo, nrpo, b) > rpo_idx(rpo, nrpo, a))
			b = b->idom;
	}
	return a;
}

/* Standard iterative immediate-dominator computation. */
static void
fill_idom(MFn *fn, MBlk **rpo, uint32_t nrpo)
{
	bool changed;

	if (nrpo == 0)
		return;
	for (MBlk *b = fn->link; b; b = b->link) {
		b->idom = 0;
		b->depth = 0;
	}
	rpo[0]->idom = rpo[0];
	changed = true;
	while (changed) {
		changed = false;
		for (uint32_t i = 1; i < nrpo; i++) {
			MBlk *b = rpo[i];
			MBlk *ni = 0;
			for (uint32_t j = 0; j < b->npred; j++) {
				MBlk *p = b->pred[j];
				if (!p->idom)
					continue;
				ni = ni ? dom_intersect(p, ni, rpo, nrpo) : p;
			}
			if (ni && ni != b->idom) {
				b->idom = ni;
				changed = true;
			}
		}
	}
	for (uint32_t i = 1; i < nrpo; i++)
		if (rpo[i]->idom)
			rpo[i]->depth = rpo[i]->idom->depth + 1;
}

/* Does block `a` dominate block `b` (a == b allowed)? */
static bool
mdom(MBlk *a, MBlk *b)
{
	if (!a || !b)
		return false;
	while (b && b->depth > a->depth)
		b = b->idom;
	return b == a;
}

/* ---- value numbering ---------------------------------------------------- */

typedef struct MGVNEnt MGVNEnt;
struct MGVNEnt {
	uint32_t op, dtype;
	uint32_t vn0, vn1;       /* operand value numbers */
	uint32_t vnc;            /* value number of this computation */
	MVal *canon;             /* canonical result value */
	MBlk *defblk;            /* block defining `canon` */
	MGVNEnt *next;
};

#define GVN_NBKT 4096

typedef struct MGVN {
	MGVNEnt *bkt[GVN_NBKT];
	uint32_t *vn;            /* vn per MVal id */
	uint32_t nvn;            /* next fresh computation vn */
} MGVN;

/* Value number of a value reference (operand).  Temps carry their
 * computation vn; constants/globals/types get unique id-space-tagged vns
 * so they never spuriously match one another.  Constants are pooled per
 * function, so the pooled id distinguishes e.g. `a+1` from `a+2`. */
static uint32_t
mvn(MGVN *g, MRef r)
{
	MVal *v = r.val;
	MConst *c = r.con;

	if (c)
		return 0x80000000u + c->id;
	if (!v)
		return 0;
	switch (v->kind) {
	case MV_CONST:
		return 0x80000000u + (v->con ? v->con->id : 0);
	case MV_GLOBAL:
		return 0x40000000u + v->id;
	case MV_TYPE:
		return 0x20000000u + v->id;
	case MV_TEMP:
		return v->id < g->nvn ? g->vn[v->id] : 0;
	default:
		return 0;
	}
}

static bool
is_pure(MOP op)
{
	switch (op) {
	case MOP_ADD: case MOP_SUB: case MOP_MUL:
	case MOP_DIV: case MOP_UDIV: case MOP_REM: case MOP_UREM:
	case MOP_NEG: case MOP_AND: case MOP_OR: case MOP_XOR:
	case MOP_SHL: case MOP_SHR: case MOP_SAR:
	case MOP_CEQ: case MOP_CNE:
	case MOP_CSLT: case MOP_CSLE: case MOP_CSGT: case MOP_CSGE:
	case MOP_CULT: case MOP_CULE: case MOP_CUGT: case MOP_CUGE:
	case MOP_CFEQ: case MOP_CFNE: case MOP_CFLT: case MOP_CFLE:
	case MOP_CFGT: case MOP_CFGE:
	case MOP_SEXT: case MOP_ZEXT: case MOP_TRUNC:
	case MOP_CAST: case MOP_F2I: case MOP_UF2I: case MOP_I2F: case MOP_UI2F:
	case MOP_FEXT: case MOP_FTRUNC:
		return true;
	default:
		return false;
	}
}

static uint32_t
bkt_idx(uint32_t op, uint32_t dtype, uint32_t vn0, uint32_t vn1)
{
	uint32_t h = op * 131 + dtype * 17;
	h = h * 33 + vn0;
	h = h * 33 + vn1;
	return h & (GVN_NBKT-1);
}

/* Find an entry with the same signature whose definition dominates `b`. */
static MGVNEnt *
gvn_find(MGVN *g, uint32_t op, uint32_t dtype, uint32_t vn0, uint32_t vn1,
         MBlk *b)
{
	uint32_t idx = bkt_idx(op, dtype, vn0, vn1);
	for (MGVNEnt *e = g->bkt[idx]; e; e = e->next)
		if (e->op == op && e->dtype == dtype &&
		    e->vn0 == vn0 && e->vn1 == vn1 &&
		    mdom(e->defblk, b))
			return e;
	return 0;
}

static void
gvn_put(MGVN *g, uint32_t op, uint32_t dtype, uint32_t vn0, uint32_t vn1,
        uint32_t vnc, MVal *canon, MBlk *defblk)
{
	uint32_t idx = bkt_idx(op, dtype, vn0, vn1);
	MGVNEnt *e = malloc(sizeof *e);
	e->op = op;
	e->dtype = dtype;
	e->vn0 = vn0;
	e->vn1 = vn1;
	e->vnc = vnc;
	e->canon = canon;
	e->defblk = defblk;
	e->next = g->bkt[idx];
	g->bkt[idx] = e;
}

/* ---- pass driver -------------------------------------------------------- */

uint32_t
mgvn(MFn *fn)
{
	MGVN g = {0};
	uint32_t npost = 0, nrpo = 0;
	MBlk **post, **rpo;
	uint32_t removed = 0;

	build_uses(fn);
	g.nvn = fn->nval;
	g.vn = calloc(g.nvn ? g.nvn : 1, sizeof *g.vn);
	post = malloc((fn->nblk ? fn->nblk : 1) * sizeof *post);
	rpo = malloc((fn->nblk ? fn->nblk : 1) * sizeof *rpo);

	/* each temp starts with a unique vn (params, phi results, loads, etc.) */
	for (uint32_t i = 0; i < fn->nval; i++)
		if (fn->val[i] && fn->val[i]->kind == MV_TEMP)
			g.vn[fn->val[i]->id] = fn->val[i]->id + 1;
	g.nvn = fn->nval + 1;

	fill_preds(fn);
	npost = blk_post(fn, post);
	for (uint32_t i = 0; i < npost; i++)
		rpo[nrpo++] = post[npost-1-i];   /* reverse postorder */
	fill_idom(fn, rpo, nrpo);

	for (uint32_t bi = 0; bi < nrpo; bi++) {
		MBlk *b = rpo[bi];

		for (uint32_t i = 0; i < b->nins; i++) {
			MIns *in = &b->ins[i];

			if (!in->dst || in->dst->kind != MV_TEMP || !is_pure(in->op))
				continue;

			uint32_t vn0 = mvn(&g, in->src[0]);
			uint32_t vn1 = mvn(&g, in->src[1]);
			uint32_t op = in->op, dt = in->dtype;

			MGVNEnt *e = gvn_find(&g, op, dt, vn0, vn1, b);
			if (e) {
				/* redundant: forward uses to the canonical value */
				mref_replace(fn, in->dst, MREF_VAL(e->canon));
				g.vn[in->dst->id] = e->vnc;
				/* neutralize so a later DCE pass removes it (keeps dst:
				 * mdce drops instructions whose dst has zero uses) */
				in->op = MOP_NONE;
				in->src[0] = (MRef){0};
				in->src[1] = (MRef){0};
				removed++;
			} else {
				/* register this computation; a fresh vn for its result */
				uint32_t vnc = g.nvn++;
				g.vn[in->dst->id] = vnc;
				gvn_put(&g, op, dt, vn0, vn1, vnc, in->dst, b);
			}
		}
	}

	build_uses(fn);
	for (uint32_t i = 0; i < GVN_NBKT; i++) {
		MGVNEnt *e = g.bkt[i];
		while (e) {
			MGVNEnt *nx = e->next;
			free(e);
			e = nx;
		}
	}
	free(g.vn);
	free(post);
	free(rpo);
	return removed;
}
