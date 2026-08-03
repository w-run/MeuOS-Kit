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
	uint32_t *blkbase;     /* first position of each block (by id) */
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
		ctx->blkbase[b->id] = pos;
		for (uint32_t j = 0; j < b->nins; j++)
			b->ins[j].pos = pos++;
		b->term.pos = pos++;
	}
	ctx->npos = pos;
}

/* Extend intervals across back edges.  A value defined before a loop and
 * used anywhere inside it (header or body) must keep its register for the
 * whole loop, otherwise a loop-internal temp may reuse it and clobber the
 * value on a later iteration. */
static void
mreg_loop_extend(MFnM *fm, MRegCtx *ctx)
{
	/* for each back edge src -> dst, find the loop extent [dst, src] */
	for (uint32_t i = 0; i < ctx->nblk; i++) {
		MBlkM *src = ctx->order[i];
		MBlkM *succ[2] = { src->s1, src->s2 };
		for (int k = 0; k < 2; k++) {
			if (!succ[k] || succ[k]->id >= ctx->nblk)
				continue;
			uint32_t dstbase = ctx->blkbase[succ[k]->id];
			if (dstbase >= ctx->blkbase[src->id])
				continue;   /* not a back edge */
			uint32_t srclast = src->term.pos;
			/* every block between the header and the back-edge source */
			for (uint32_t j = 0; j < ctx->nblk; j++) {
				MBlkM *b = ctx->order[j];
				uint32_t bbase = ctx->blkbase[b->id];
				if (bbase < dstbase || bbase > ctx->blkbase[src->id])
					continue;
				for (uint32_t m = 0; m < b->nins; m++) {
					MInsM *in = &b->ins[m];
					MVal *ops[5] = { in->src[0], in->src[1], in->src[2],
					                 in->addr.base, in->addr.index };
					for (int x = 0; x < 5; x++) {
						MRegInterval *iv =
							ops[x] ? mreg_intv(ctx, ops[x]) : 0;
						if (iv && iv->start < dstbase &&
						    iv->end < srclast)
							iv->end = srclast;
					}
				}
			}
		}
	}
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
				/* a value may be defined more than once in the machine
				 * layer (e.g. mabi_selcall points the aggregate-return
				 * call dst at a pad before the call, then the call
				 * re-defines it).  The interval must start at the FIRST
				 * def so the value stays live from its earliest write
				 * through every use — otherwise the linear scan may hand
				 * that register to a neighbour before the last def. */
				if (in->pos < iv->start)
					iv->start = in->pos;
				if (in->extra == 1)
					iv->phislot = true;   /* phi-edge copy dest */
				/* varargs: keep va_list / stack-pad addresses in slots so
				 * vastart/va_arg never depend on a register that an
				 * emitted temporary could clobber */
				if ((fm->host && fm->host->vararg) &&
				    in->op == MMOP_ALLOCA16)
					iv->phislot = true;
				/* 32-bit targets (i386/arm, kl_in_reg==0): 64-bit
				 * integer values cannot live in a single register.
				 * Force them to stack slots so the emitter can
				 * access low/high halves via EAX:EDX register pairs
				 * or ARM's equivalent. */
				if (in->dst->type == MT_I64 && !fm->mt->kl_in_reg)
					iv->phislot = true;
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
		if (iv->v && iv->start != UINT32_MAX && iv->end <= iv->start)
			iv->end = iv->start + 1;
	}
}

/* ---- Phase B: spill slot packing (slot4/slot8) ---------------------------- */

/* MRegSlots is declared in mir.h. */

/* Allocate a stack slot, returning the negative rbp offset.  4-byte values
 * (i32/f32) get slot4 slots accessed with movl/movss; everything else gets
 * 8-byte slot8 slots.  The slot8 cursor absorbs slot4 so an 8-byte value
 * never straddles a 4-byte slot's alignment (QBE spill.c invariant). */
int32_t
mreg_slot_alloc(MRegSlots *s, MType t)
{
	if (t == MT_I32 || t == MT_F32) {
		/* 4-byte slot: never inside an 8-byte slot (8-byte slots occupy
		 * two units); place it strictly below the deepest 8-byte slot */
		s->slot4 += 1;
		if (s->slot4 <= s->slot8)
			s->slot4 = s->slot8 + 1;
		return -s->slot4 * 4;
	}
	/* 8-byte slot: two units, aligned even, always deeper than the
	 * 4-byte cursor */
	if (s->slot8 < s->slot4)
		s->slot8 = s->slot4;
	if (s->slot8 & 1)
		s->slot8 += 1;
	s->slot8 += 2;
	return -s->slot8 * 4;
}

int32_t
mreg_slot_total(const MRegSlots *s)
{
	int32_t d = s->slot8 > s->slot4 ? s->slot8 : s->slot4;
	return d * 4;   /* bytes (caller aligns to 16) */
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

/* Build the caller-saved (px) and callee-saved (pc) pools per class.
 * Excludes rglob (frame regs), reserved and scratch (emitter temps). */
static void
mreg_pool_build(const MTargetM *mt, MRegPool *pc, MRegPool *px)
{
	uint64_t no = mt->rglob | mt->reserved | mt->scratch;
	for (int r = mt->gpr0; r < mt->gpr0 + mt->ngpr; r++) {
		if ((no >> r) & 1)
			continue;
		if (mt->regs[r].callee_saved)
			pc[0].regs[pc[0].nreg++] = r;
		else
			px[0].regs[px[0].nreg++] = r;
	}
	for (int r = mt->fpr0; r < mt->fpr0 + mt->nfpr; r++) {
		if ((no >> r) & 1)
			continue;
		if (mt->regs[r].callee_saved)
			pc[1].regs[pc[1].nreg++] = r;
		else
			px[1].regs[px[1].nreg++] = r;
	}
}

/* Is register r a callee-saved register of the given class? */
static bool
mreg_in_callee(const MTargetM *mt, int r, int cls)
{
	if (cls == 0) {
		for (int c = mt->gpr0; c < mt->gpr0 + mt->ngpr; c++)
			if (c == r)
				return mt->regs[c].callee_saved;
	} else {
		for (int c = mt->fpr0; c < mt->fpr0 + mt->nfpr; c++)
			if (c == r)
				return mt->regs[c].callee_saved;
	}
	return false;
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
	bool vararg = fm->host && fm->host->vararg;
	MRegPool pc[2] = { 0 }, px[2] = { 0 };
	mreg_pool_build(mt, pc, px);

	/* fixed occupations (MV_REG operands) */
	MFixed fixed[64];
	memset(fixed, 0, sizeof fixed);
	if (fm->has_sret && mt->sret_reg >= 0) {
		/* the hidden sret buffer lives in the target's sret register
		 * (x86_64: RDI; riscv64/arm64: A0) for the whole function */
		for (uint32_t p = 0; p < ctx->npos; p++)
			fixed_add(&fixed[mt->sret_reg], p);
	}
	{
		uint32_t *calls = NULL;
		uint32_t ncalls = 0, ccalls = 0;
		for (MBlkM *b = fm->link; b; b = b->link) {
			for (uint32_t j = 0; j < b->nins; j++) {
				MInsM *in = &b->ins[j];
				MVal *ops[5] = { in->dst, in->src[0], in->src[1],
				                 in->addr.base, in->addr.index };
				for (int k = 0; k < 5; k++)
					if (ops[k] && ops[k]->kind == MV_REG && ops[k]->reg >= 0) {
						fixed_add(&fixed[ops[k]->reg], in->pos);
						/* Incoming argument registers hold their values from
						 * function entry until the entry-block instruction
						 * that consumes them (selpar's param moves).  Any
						 * interval starting before that consumption would
						 * clobber the argument — e.g. the empty-class pad
						 * alloca grabbed RSI and overwrote a following
						 * scalar parameter still living in RSI (verified
						 * 2026-08-03 under MCC_MIR_BACKEND=1).  Reserve the
						 * register for the whole [0, pos) prefix. */
						if (b == fm->start)
							for (uint32_t p = 0; p < in->pos; p++)
								fixed_add(&fixed[ops[k]->reg], p);
					}
				if (in->op == MMOP_CALL) {
					/* keep every call: a fixed 64-entry array silently
					 * dropped calls in huge functions, misclassifying
					 * call-crossing intervals as non-crossing */
					if (ncalls == ccalls) {
						ccalls = ccalls ? ccalls * 2 : 64;
						calls = realloc(calls, ccalls * sizeof *calls);
					}
					calls[ncalls++] = in->pos;
				}
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
			if (ctx->intv[i].v && ctx->intv[i].start < ctx->intv[i].end)
				cand[ncand++] = ctx->intv[i];
		qsort(cand, ncand, sizeof *cand, intv_cmp_start);

		typedef struct { uint32_t end; int reg; MVal *v; uint32_t candidx; } MActive;
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
				/* hinted register first (MVal.hint, e.g. ABI boundaries) */
				int hint = iv->v->hint;
				if (hint >= 0 && !busy[hint] &&
				    !fixed_conflict(&fixed[hint], s, e)) {
					bool incaller = false, incallee = false;
					for (uint32_t p = 0; p < px[cls].nreg; p++)
						if (px[cls].regs[p] == hint)
							incaller = true;
					for (uint32_t p = 0; p < pc[cls].nreg; p++)
						if (pc[cls].regs[p] == hint)
							incallee = true;
					/* a call-crossing value must never take a
					 * caller-saved hint: the call clobbers it */
					if (incallee || (!cross && incaller))
						chosen = hint;
				}
				/* caller-saved pool first for non-call-crossing values */
				if (chosen < 0 && !cross)
					for (uint32_t p = 0; p < px[cls].nreg && chosen < 0; p++) {
						int r = px[cls].regs[p];
						if (busy[r] || fixed_conflict(&fixed[r], s, e))
							continue;
						chosen = r;
					}
				/* varargs keep the rbp-176 reg_save_area clean: no
				 * callee-saved registers, call-crossing values spill */
				if (chosen < 0 && !vararg)
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
				fm->regsused |= 1ull << chosen;   /* remember for prologue */
				busy[chosen] = true;
				act[nact].end = e;
				act[nact].reg = chosen;
				act[nact].v = iv->v;
				act[nact].candidx = i;
				nact++;
			} else if (!iv->phislot && nact < 256) {
				/* spill-either heuristic: no free register — reuse the
				 * register of an active interval (same class) whose live
				 * range extends PAST the new interval AND whose register
				 * suits the new interval (no fixed conflict; callee-saved
				 * when the new value crosses a call).  The spilled value
				 * keeps its slot: the emitter stores every def and loads
				 * every use of a slot-resident temp, so a mid-scan spill
				 * stays correct (the earlier def still writes the slot).
				 * Keeping short-lived values in registers over long-lived
				 * ones cuts total spills on pressure-heavy functions. */
				int victim = -1;
				uint32_t victim_end = iv->end;
				for (uint32_t k = 0; k < nact; k++) {
					if (act[k].end <= victim_end ||
					    mreg_class(act[k].v->type) != cls)
						continue;
					int r = act[k].reg;
					if (fixed_conflict(&fixed[r], s, e))
						continue;
					if (cross && mreg_in_callee(mt, r, cls) == false)
						continue;
					victim_end = act[k].end;
					victim = (int)k;
				}
				if (victim >= 0) {
					MVal *vv = act[victim].v;
					vv->slot = mreg_slot_alloc(&slots, vv->type) -
					           (vararg ? 176 : 0);
					vv->reg = -1;
					cand[act[victim].candidx].reg = -1;
					iv->reg = act[victim].reg;
					fm->regsused |= 1ull << act[victim].reg;
					act[victim].end = e;      /* the register now holds iv */
					act[victim].v = iv->v;
					act[victim].candidx = i;
				} else {
					/* spill the new interval */
					iv->v->slot = mreg_slot_alloc(&slots, iv->v->type) -
					              (vararg ? 176 : 0);
					iv->v->reg = -1;
				}
			} else {
				/* spill to a stack slot below the reg_save_area */
				iv->v->slot = mreg_slot_alloc(&slots, iv->v->type) -
				              (vararg ? 176 : 0);
				iv->v->reg = -1;
			}
		}
		/* write back register assignments */
		for (uint32_t i = 0; i < ncand; i++)
			if (cand[i].reg >= 0)
				cand[i].v->reg = cand[i].reg;
		free(calls);

		/* Shift spill slots below the callee-saved save area so the
		 * emitter's prologue pushes do not overlap them. */
		int savesz = 0;
		for (int r = mt->gpr0; r < mt->gpr0 + mt->ngpr; r++)
			if (mt->regs[r].callee_saved && ((fm->regsused >> r) & 1))
				savesz += 8;
		if (savesz)
			for (uint32_t i = 0; i < ctx->nval; i++)
				if (ctx->intv[i].v && ctx->intv[i].v->slot != -1)
					ctx->intv[i].v->slot -= savesz;

		fm->slot = mreg_slot_total(&slots) + savesz;
		free(cand);
	}
	for (uint32_t r = 0; r < 64; r++)
		free(fixed[r].pos);
}

/* ---- postra: redundant-move elimination ---------------------------------- */

static bool
mval_is_src(MVal *v, MInsM *in)
{
	return v && (in->src[0] == v || in->src[1] == v || in->src[2] == v ||
	             in->addr.base == v || in->addr.index == v);
}

/* Is v used by anything other than the skipped positions pi/ci of curb? */
static bool
mval_used_anywhere(MFnM *fm, MBlkM *curb, uint32_t pi, uint32_t ci, MVal *v)
{
	for (MBlkM *b = fm->link; b; b = b->link) {
		for (uint32_t j = 0; j < b->nins; j++) {
			if (b == curb && (j == pi || j == ci))
				continue;
			if (mval_is_src(v, &b->ins[j]))
				return true;
		}
		if (b->term.src[0] == v || b->term.addr.base == v ||
		    b->term.addr.index == v)
			return true;
	}
	return false;
}

static bool
same_mov_loc(MVal *a, MVal *b)
{
	if (!a || !b)
		return false;
	int reg_a = -1, reg_b = -1, slot_a = -1, slot_b = -1;
	if ((a->kind == MV_TEMP && a->reg >= 0) || a->kind == MV_REG)
		reg_a = a->reg;
	else if (a->kind == MV_TEMP && a->slot != -1)
		slot_a = a->slot;
	if ((b->kind == MV_TEMP && b->reg >= 0) || b->kind == MV_REG)
		reg_b = b->reg;
	else if (b->kind == MV_TEMP && b->slot != -1)
		slot_b = b->slot;
	if (reg_a >= 0 && reg_b >= 0)
		return reg_a == reg_b;
	if (slot_a != -1 && slot_b != -1)
		return slot_a == slot_b;
	return false;
}

/* Drop redundant MOVs after allocation: same-location moves, dead
 * destinations, and a -> b ; c -> b chains (b used only by the second). */
static bool
mreg_postra(MFnM *fm)
{
	bool changed = false;
	for (MBlkM *b = fm->link; b; b = b->link) {
		uint32_t w = 0;
		for (uint32_t i = 0; i < b->nins; i++) {
			MInsM *in = &b->ins[i];
			bool drop = false;
			if (in->op == MMOP_MOV && in->dst && in->src[0]) {
				MVal *d = in->dst, *s = in->src[0];
				if (same_mov_loc(d, s)) {
					drop = true;   /* mov %r, %r */
				} else if (w > 0 && b->ins[w - 1].op == MMOP_MOV &&
				           b->ins[w - 1].dst == s &&
				           b->ins[w - 1].src[0] != d &&
				           !mval_used_anywhere(fm, b, w - 1, i, s)) {
					/* chain: b = a; c = b  ->  c = a */
					in->src[0] = b->ins[w - 1].src[0];
					w--;
					changed = true;
					if (same_mov_loc(d, in->src[0]))
						drop = true;
				} else if (d->kind == MV_TEMP && d->reg >= 0 &&
				           !mval_used_anywhere(fm, b, i, i, d)) {
					/* destination never read again */
					drop = true;
				}
			}
			if (drop) {
				changed = true;
				continue;
			}
			b->ins[w++] = *in;
		}
		b->nins = w;
	}
	return changed;
}

/* ---- allocation entry ------------------------------------------------------ */

void
mfnm_regalloc(MFnM *fm)
{
	MRegCtx ctx = { 0 };
	ctx.nblk = fm->nblk;
	ctx.nval = fm->host ? fm->host->nval : 0;
	ctx.intv = calloc(ctx.nval ? ctx.nval : 1, sizeof *ctx.intv);
	ctx.blkbase = calloc(ctx.nblk ? ctx.nblk : 1, sizeof *ctx.blkbase);
	for (uint32_t i = 0; i < ctx.nval; i++) {
		MVal *v = fm->host->val[i];
		if (v && v->kind == MV_TEMP) {
			ctx.intv[i].v = v;
			ctx.intv[i].reg = -1;
			ctx.intv[i].start = UINT32_MAX;   /* sentinel: no def yet */
		}
	}

	mreg_pos(fm, &ctx);
	mreg_intervals(fm, &ctx);
	mreg_loop_extend(fm, &ctx);
	mreg_scan(fm, &ctx);
	mreg_postra(fm);

	if (getenv("MCC_DEBUG_MBE")) {
		fprintf(stderr, "> regalloc %s:\n", fm->name ? fm->name : "?");
		for (uint32_t i = 0; i < ctx.nval; i++) {
			MRegInterval *iv = &ctx.intv[i];
			if (iv->v)
				fprintf(stderr, "  %s [%u,%u) t=%d reg=%d slot=%d\n",
				        iv->v->name ? iv->v->name : "?", iv->start, iv->end,
				        (int)iv->v->type, iv->v->reg, iv->v->slot);
		}
	}

	free(ctx.order);
	free(ctx.blkbase);
	free(ctx.intv);
}
