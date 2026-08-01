/* regalloc.c — MIR machine-layer register allocation (P4).
 *
 * Linear-scan allocator over explicit-SSA live intervals, per the design
 * in docs/mir-backend/regalloc-design.md.  The machine layer has no
 * reverse use chains (maddm stores plain MVal* operands), so intervals
 * are built by scanning the instruction arrays.  SSA phis were already
 * lowered to pred-edge copies by the isel pass (x86_64_mbe.c), so this
 * pass only sees straight-line instructions plus the terminator.
 *
 * Phases (A→E):
 *   A. mreg_intervals:  assign global positions, build [start,end) per
 *      MV_TEMP from def/use scan.
 *   B. mreg_slots:      slot4/slot8 packing for forced/selected spills.
 *   C. mreg_scan:       linear scan over caller/callee-saved pools with
 *      CALLs clearing the caller-saved pool.
 *   E. emit adaptation: MVal.reg filled (emit uses %reg), fm->slot/
 *      regsused filled (emit saves callee-saved in prologue/epilogue).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mir.h"

/* ---- interval bookkeeping ------------------------------------------------- */

typedef struct MRegInterval {
	uint32_t start, end;   /* [start, end) in global positions */
	MVal *v;
	int reg;               /* assigned physical reg, -1 = slot-resident */
	int cost;
	bool phislot;          /* defined by a phi-edge copy: force spill */
} MRegInterval;

typedef struct MRegCtx {
	MRegInterval *intv;    /* indexed by MVal.id (host->val) */
	uint32_t nval;
	uint32_t npos;
	MBlkM **order;         /* forward block order */
	uint32_t nblk;
} MRegCtx;

static MRegInterval *
mreg_intv(MRegCtx *ctx, MVal *v)
{
	return v && v->kind == MV_TEMP ? &ctx->intv[v->id] : 0;
}

static void
mreg_scan_use(MRegCtx *ctx, MVal *v, uint32_t pos)
{
	MRegInterval *iv = mreg_intv(ctx, v);
	if (iv && pos + 1 > iv->end)
		iv->end = pos + 1;
}

/* ---- Phase A: global positions + live intervals -------------------------- */

static void
mreg_pos(MFnM *fm, MRegCtx *ctx)
{
	/* forward order: start block gets the lowest positions */
	ctx->order = calloc(ctx->nblk, sizeof *ctx->order);
	uint32_t i = ctx->nblk;
	for (MBlkM *b = fm->link; b; b = b->link)
		ctx->order[--i] = b;

	uint32_t pos = 0;
	for (i = 0; i < ctx->nblk; i++) {
		MBlkM *b = ctx->order[i];
		for (uint32_t j = 0; j < b->nins; j++)
			b->ins[j].pos = pos++;
		b->term.pos = pos++;
	}
	ctx->npos = pos;
}

/* Build the live interval of every MV_TEMP used by the function.  Machine
 * instructions reference MVal directly, so we scan all operands. */
static void
mreg_intervals(MFnM *fm, MRegCtx *ctx)
{
	for (MBlkM *b = fm->link; b; b = b->link) {
		for (uint32_t j = 0; j < b->nins; j++) {
			MInsM *in = &b->ins[j];
			if (in->dst && in->dst->kind == MV_TEMP) {
				MRegInterval *iv = mreg_intv(ctx, in->dst);
				iv->start = in->pos;
				if (in->extra == 1)
					iv->phislot = true;   /* phi-edge copy dest */
			}
			for (int k = 0; k < 3; k++)
				mreg_scan_use(ctx, in->src[k], in->pos);
			mreg_scan_use(ctx, in->addr.base, in->pos);
			mreg_scan_use(ctx, in->addr.index, in->pos);
		}
		/* terminator operands */
		mreg_scan_use(ctx, b->term.src[0], b->term.pos);
		mreg_scan_use(ctx, b->term.addr.base, b->term.pos);
		mreg_scan_use(ctx, b->term.addr.index, b->term.pos);
	}
	/* dead values: interval collapses to the def point */
	for (uint32_t i = 0; i < ctx->nval; i++) {
		MRegInterval *iv = &ctx->intv[i];
		if (iv->v && iv->end <= iv->start)
			iv->end = iv->start + 1;
	}
}

/* ---- Phase B: spill slot packing (slot4/slot8) ---------------------------- */

/* MRegSlots is declared in mir.h. */

/* Allocate a stack slot, returning the negative rbp offset.  Inherits the
 * QBE spill.c double-cursor invariant (slot8 absorbs slot4 so an 8-byte
 * value never straddles a 4-byte slot's alignment). */
int32_t
mreg_slot_alloc(MRegSlots *s, MType t)
{
	bool is8 = t == MT_I64 || t == MT_PTR || t == MT_F64;
	int32_t u;
	if (is8)
		u = s->slot8 += 2;
	else
		u = s->slot4 += 1;
	if (s->slot4 > s->slot8)
		s->slot8 = s->slot4;
	return -u * 4;
}

int32_t
mreg_slot_total(const MRegSlots *s)
{
	return s->slot8 * 4;   /* 4-byte units -> bytes (caller aligns to 16) */
}

/* ---- Phase C: linear scan ------------------------------------------------ */

typedef struct MRegPool {
	int regs[64];
	uint32_t nreg;
} MRegPool;

/* Fixed (ABI) register occupations: a physical register used by MV_REG at
 * a given position must not be handed to an interval live there. */
typedef struct {
	uint32_t *pos;
	uint32_t n, c;
} MFixed;

static int
mreg_class(MType t)
{
	return (t == MT_F32 || t == MT_F64) ? 1 : 0;
}

static void
fixed_add(MFixed *f, uint32_t pos)
{
	if (f->n == f->c) {
		f->c = f->c ? f->c * 2 : 8;
		f->pos = realloc(f->pos, f->c * sizeof *f->pos);
	}
	f->pos[f->n++] = pos;
}

static bool
fixed_conflict(MFixed *f, uint32_t s, uint32_t e)
{
	for (uint32_t i = 0; i < f->n; i++)
		if (f->pos[i] >= s && f->pos[i] < e)
			return true;
	return false;
}

static int
intv_cmp_start(const void *a, const void *b)
{
	const MRegInterval *x = a, *y = b;
	if (x->start != y->start)
		return x->start < y->start ? -1 : 1;
	return x->v->id < y->v->id ? -1 : 1;
}

/* Build the caller-saved (px) and callee-saved (pc) pools per class. */
static void
mreg_pool_build(const MTargetM *mt, MRegPool *pc, MRegPool *px)
{
	for (int r = mt->gpr0; r < mt->gpr0 + mt->ngpr; r++) {
		if ((mt->rglob >> r) & 1)
			continue;   /* rbp/rsp */
		if (mt->regs[r].callee_saved)
			pc[0].regs[pc[0].nreg++] = r;
		else
			px[0].regs[px[0].nreg++] = r;
	}
	for (int r = mt->fpr0; r < mt->fpr0 + mt->nfpr; r++) {
		if (mt->regs[r].callee_saved)
			pc[1].regs[pc[1].nreg++] = r;
		else
			px[1].regs[px[1].nreg++] = r;
	}
}

static bool
interval_crosses_call(uint32_t s, uint32_t e, uint32_t *calls, uint32_t nc)
{
	for (uint32_t i = 0; i < nc; i++)
		if (calls[i] >= s && calls[i] < e)
			return true;
	return false;
}

static void
mreg_scan(MFnM *fm, MRegCtx *ctx)
{
	const MTargetM *mt = fm->mt;
	MRegSlots slots = { 0 };
	MRegPool pc[2] = { 0 }, px[2] = { 0 };
	mreg_pool_build(mt, pc, px);

	/* fixed occupations (MV_REG operands) */
	MFixed fixed[64];
	memset(fixed, 0, sizeof fixed);
	{
		uint32_t calls[64], ncalls = 0;
		for (MBlkM *b = fm->link; b; b = b->link) {
			for (uint32_t j = 0; j < b->nins; j++) {
				MInsM *in = &b->ins[j];
				MVal *ops[5] = { in->dst, in->src[0], in->src[1],
				                 in->addr.base, in->addr.index };
				for (int k = 0; k < 5; k++)
					if (ops[k] && ops[k]->kind == MV_REG && ops[k]->reg >= 0)
						fixed_add(&fixed[ops[k]->reg], in->pos);
				if (in->op == MMOP_CALL && ncalls < 64)
					calls[ncalls++] = in->pos;
			}
			MInsM *t = &b->term;
			MVal *tops[3] = { t->src[0], t->addr.base, t->addr.index };
			for (int k = 0; k < 3; k++)
				if (tops[k] && tops[k]->kind == MV_REG && tops[k]->reg >= 0)
					fixed_add(&fixed[tops[k]->reg], t->pos);
		}

		/* collect candidate intervals (MV_TEMP), sort by start */
		MRegInterval *cand = malloc(ctx->nval * sizeof *cand);
		uint32_t ncand = 0;
		for (uint32_t i = 0; i < ctx->nval; i++)
			if (ctx->intv[i].v)
				cand[ncand++] = ctx->intv[i];
		qsort(cand, ncand, sizeof *cand, intv_cmp_start);

		typedef struct { uint32_t end; int reg; MVal *v; } MActive;
		MActive act[256];
		uint32_t nact = 0;
		bool busy[64] = { 0 };

		for (uint32_t i = 0; i < ncand; i++) {
			MRegInterval *iv = &cand[i];
			uint32_t s = iv->start, e = iv->end;
			/* expire */
			for (uint32_t k = nact; k-- > 0;)
				if (act[k].end <= s) {
					busy[act[k].reg] = false;
					act[k] = act[--nact];
				}
			int cls = mreg_class(iv->v->type);
			bool cross = interval_crosses_call(s, e, calls, ncalls);
			int chosen = -1;
			if (!iv->phislot) {
				/* caller-saved pool first for non-call-crossing values */
				if (!cross)
					for (uint32_t p = 0; p < px[cls].nreg && chosen < 0; p++) {
						int r = px[cls].regs[p];
						if (busy[r] || fixed_conflict(&fixed[r], s, e))
							continue;
						chosen = r;
					}
				if (chosen < 0)
					for (uint32_t p = 0; p < pc[cls].nreg && chosen < 0; p++) {
						int r = pc[cls].regs[p];
						if (busy[r] || fixed_conflict(&fixed[r], s, e))
							continue;
						chosen = r;
					}
			}
			/* phi-edge destinations stay in slots (parallel-move safety) */
			if (chosen >= 0 && !iv->phislot && nact < 256) {
				iv->reg = chosen;
				busy[chosen] = true;
				act[nact].end = e;
				act[nact].reg = chosen;
				act[nact].v = iv->v;
				nact++;
			} else {
				/* spill to a stack slot */
				iv->v->slot = mreg_slot_alloc(&slots, iv->v->type);
				iv->v->reg = -1;
			}
		}
		/* write back register assignments */
		for (uint32_t i = 0; i < ncand; i++)
			if (cand[i].reg >= 0)
				cand[i].v->reg = cand[i].reg;
		for (uint32_t r = 0; r < 64; r++)
			if (busy[r])
				fm->regsused |= 1ull << r;
		fm->slot = mreg_slot_total(&slots);
		free(cand);
	}
	for (uint32_t r = 0; r < 64; r++)
		free(fixed[r].pos);
}

/* ---- allocation entry ------------------------------------------------------ */

void
mfnm_regalloc(MFnM *fm)
{
	MRegCtx ctx = { 0 };
	ctx.nblk = fm->nblk;
	ctx.nval = fm->host ? fm->host->nval : 0;
	ctx.intv = calloc(ctx.nval ? ctx.nval : 1, sizeof *ctx.intv);
	for (uint32_t i = 0; i < ctx.nval; i++) {
		MVal *v = fm->host->val[i];
		if (v && v->kind == MV_TEMP) {
			ctx.intv[i].v = v;
			ctx.intv[i].reg = -1;
		}
	}

	mreg_pos(fm, &ctx);
	mreg_intervals(fm, &ctx);
	mreg_scan(fm, &ctx);

	if (getenv("MCC_DEBUG_MBE")) {
		fprintf(stderr, "> regalloc %s:\n", fm->name ? fm->name : "?");
		for (uint32_t i = 0; i < ctx.nval; i++) {
			MRegInterval *iv = &ctx.intv[i];
			if (iv->v)
				fprintf(stderr, "  %s [%u,%u) reg=%d slot=%d\n",
				        iv->v->name ? iv->v->name : "?", iv->start, iv->end,
				        iv->v->reg, iv->v->slot);
		}
	}

	free(ctx.order);
	free(ctx.intv);
}
