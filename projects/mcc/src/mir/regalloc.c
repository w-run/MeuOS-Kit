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

	/* P4a: intervals built; register/slot assignment lands in B/C. */

	if (getenv("MCC_DEBUG_MBE")) {
		fprintf(stderr, "> intervals %s:\n", fm->name ? fm->name : "?");
		for (uint32_t i = 0; i < ctx.nval; i++) {
			MRegInterval *iv = &ctx.intv[i];
			if (iv->v)
				fprintf(stderr, "  %s [%u,%u)\n",
				        iv->v->name ? iv->v->name : "?", iv->start, iv->end);
		}
	}

	free(ctx.order);
	free(ctx.intv);
}
