/* ifconv.c — MIR-native if-conversion (branch -> cmov, x86-64).
 *
 * The frontend lowers `x = cond ? a : b` and short if-else assignments
 * into a branch diamond:
 *
 *   body:                          body (converted):
 *     cmp a, b                       cmp a, b
 *     %c = setcc cc                  cmov<cc>  a_val -> dst
 *     cmp %c, $0                     cmov<!cc> b_val -> dst
 *     jcc ne -> T / F                store dst ...  (store form)
 *   T: [load a] ; write dst <- a     jmp J
 *   F: [load b] ; write dst <- b
 *   J: use(dst)
 *
 * When both arms write the SAME integer destination with no side effects,
 * the branch is replaced by straight-line conditional moves.  Detection
 * is deliberately conservative:
 *   - the two arms must be exactly `[optional single load/mov]` + one
 *     write (store to a slot, or a phi-edge mov to a temp), both to the
 *     SAME destination;
 *   - the selected value must be integer (x86 has no FP cmov);
 *   - the destination block must be reached only from this branch (a
 *     clean diamond), so converting cannot change other paths.
 * Anything else keeps its branches.
 */
#include <stdlib.h>
#include <string.h>

#include "mir.h"

/* Simple addressing equality (slot / symbol). */
static bool
maddr_eq(const MAddr *x, const MAddr *y)
{
	return x->base == y->base && x->index == y->index &&
	       x->scale == y->scale && x->off == y->off &&
	       x->offcon == y->offcon;
}

/* The machine instruction in block b that defines v, if any. */
static MInsM *
mblk_def(MBlkM *b, MVal *v)
{
	if (!v)
		return 0;
	for (uint32_t i = 0; i < b->nins; i++)
		if (b->ins[i].dst == v)
			return &b->ins[i];
	return 0;
}

/* True if v is referenced by any instruction other than `skip` anywhere
 * in the function (safety: removing the setcc must not dangle a use). */
static bool
mval_used_elsewhere(MFnM *fm, MVal *v, MInsM *skip)
{
	if (!v)
		return false;
	for (MBlkM *b = fm->link; b; b = b->link) {
		for (uint32_t i = 0; i < b->nins; i++) {
			MInsM *in = &b->ins[i];
			if (in == skip)
				continue;
			for (int k = 0; k < 3; k++)
				if (in->src[k] == v)
					return true;
			if (in->addr.base == v || in->addr.index == v)
				return true;
		}
		MInsM *t = &b->term;
		for (int k = 0; k < 3; k++)
			if (t->src[k] == v)
				return true;
		if (t->addr.base == v || t->addr.index == v)
			return true;
	}
	return false;
}

/* True if `target` is branched to only from `only` (clean diamond). */
static bool
single_pred(MFnM *fm, MBlkM *target, MBlkM *only)
{
	for (MBlkM *b = fm->link; b; b = b->link) {
		if (b == target || b == only)
			continue;
		if (b->term.op == MMOP_JMP && b->s1 == target)
			return false;
		if (b->term.op == MMOP_JCC &&
		    (b->s1 == target || b->s2 == target))
			return false;
	}
	return true;
}

/* One arm of the convertible diamond. */
typedef struct {
	MBlkM *b;
	MInsM *write;      /* MMOP_STORE or phi-edge MMOP_MOV (extra=1) */
	MVal *val;         /* value written (write->src[0]) */
	bool has_load;     /* val defined by a single LOAD in the arm */
	MAddr load_addr;   /* that load's address */
	bool has_mov;      /* val defined by a single MOV in the arm */
	MVal *mov_src;     /* that MOV's source */
} Arm;

static bool
arm_parse(MBlkM *blk, Arm *arm)
{
	memset(arm, 0, sizeof *arm);
	arm->b = blk;
	if (blk->term.op != MMOP_JMP || blk->nins < 1)
		return false;
	MInsM *w = &blk->ins[blk->nins - 1];
	if (w->op != MMOP_STORE &&
	    !(w->op == MMOP_MOV && w->extra == 1))
		return false;
	arm->write = w;
	arm->val = w->src[0];
	if (!arm->val)
		return false;
	if (blk->nins == 2) {
		MInsM *d = &blk->ins[0];
		if (d->dst != arm->val)
			return false;
		if (d->op == MMOP_LOAD) {
			arm->has_load = true;
			arm->load_addr = d->addr;
		} else if (d->op == MMOP_MOV) {
			arm->has_mov = true;
			arm->mov_src = d->src[0];
		} else {
			return false;
		}
	} else if (blk->nins > 2) {
		return false;
	}
	return true;
}

static bool
arm_same_dst(const Arm *x, const Arm *y)
{
	if (x->write->op == MMOP_STORE && y->write->op == MMOP_STORE)
		return maddr_eq(&x->write->addr, &y->write->addr);
	if (x->write->op == MMOP_MOV && y->write->op == MMOP_MOV)
		return x->write->dst == y->write->dst;
	return false;
}

static bool
arm_integer(const Arm *a)
{
	MType t = a->write->dtype;
	return t == MT_I32 || t == MT_I64 || t == MT_PTR;
}

/* Resolve the value to cmov from for one arm, appending replicated loads
 * to B when needed.  Prefers reusing a comparison operand already loaded
 * from the same address. */
static MVal *
arm_source(MFnM *fm, MBlkM *b, const Arm *arm, MVal *cmpa, MVal *cmpb)
{
	MType t = arm->val->type;
	if (arm->has_mov)
		return arm->mov_src;
	if (arm->has_load) {
		MInsM *da = mblk_def(b, cmpa);
		MInsM *db = mblk_def(b, cmpb);
		if (da && da->op == MMOP_LOAD && maddr_eq(&da->addr, &arm->load_addr))
			return cmpa;
		if (db && db->op == MMOP_LOAD && maddr_eq(&db->addr, &arm->load_addr))
			return cmpb;
		MVal *fresh = mval_new(fm->host, MV_TEMP, t, 0, "cmov");
		maddm_addr(fm, b, MMOP_LOAD, t, fresh, arm->load_addr, 0);
		return fresh;
	}
	/* no preceding def: val must be live in B already (defined outside
	 * the arm); consts/globals are legal cmov sources too */
	return arm->val;
}

void
mfnm_ifconv(MFnM *fm)
{
	/* -O1 keeps branches (level-difference semantics, check-olevel);
	 * cmov if-conversion starts at -O2. */
	if (!fm->host || fm->host->optlevel < 2)
		return;
	for (MBlkM *b = fm->link; b; b = b->link) {
		if (b->term.op != MMOP_JCC || b->term.cc != MCC_NE) {
			continue;
		}
		MBlkM *T = b->s1, *F = b->s2;
		if (!T || !F || T == F)
			continue;

		/* --- condition: last ins cmp <bool>,$0 ; jcc ne ------------ */
		if (b->nins < 1)
			continue;
		MInsM *recm = &b->ins[b->nins - 1];
		if (recm->op != MMOP_CMP || !recm->src[0] ||
		    recm->src[0]->kind != MV_TEMP ||
		    !recm->cst || recm->cst->kind != MC_INT || recm->cst->u.i != 0) {
			continue;
		}
		MVal *boolv = recm->src[0];

		MInsM *flagcmp = 0;    /* cmp that stays (provides flags) */
		MInsM *setcc = 0;      /* setcc to remove (pattern A) */
		MCC tcc;               /* condition selecting the T arm */
		if (b->nins >= 3) {
			MInsM *s2 = &b->ins[b->nins - 2];
			MInsM *c1 = &b->ins[b->nins - 3];
			if (s2->op == MMOP_SETCC && s2->dst == boolv &&
			    (c1->op == MMOP_CMP || c1->op == MMOP_TEST)) {
				/* pattern A: cmp a,b ; setcc cc ; cmp bool,$0 */
				flagcmp = c1;
				setcc = s2;
				tcc = s2->cc;
			}
		}
		if (!flagcmp && boolv) {
			/* pattern B: cmp bool,$0 ; jcc ne — flags already right */
			flagcmp = recm;
			tcc = MCC_NE;
		}
		if (!flagcmp) {
			continue;
		}

		/* the boolean must be dead after removing the setcc (A) */
		if (setcc && mval_used_elsewhere(fm, boolv, recm)) {
			continue;
		}

		/* --- arms ------------------------------------------------ */
		Arm at, af;
		if (!arm_parse(T, &at) || !arm_parse(F, &af)) {
			continue;
		}
		if (!arm_same_dst(&at, &af)) {
			continue;
		}
		if (!arm_integer(&at) || !arm_integer(&af))
			continue;
		/* clean diamond: T/F reached only from this branch */
		if (!single_pred(fm, T, b) || !single_pred(fm, F, b))
			continue;

		MVal *cmpa = flagcmp->src[0];
		MVal *cmpb = flagcmp->op == MMOP_CMP ? flagcmp->src[1] : 0;

		/* drop setcc + cmp bool,$0 (pattern A), keeping flagcmp */
		if (setcc)
			b->nins -= 2;

		MVal *tsrc = arm_source(fm, b, &at, cmpa, cmpb);
		MVal *fsrc = arm_source(fm, b, &af, cmpa, cmpb);
		if (!tsrc || !fsrc)
			continue;

		/* emit cmovs + result write in B */
		if (at.write->op == MMOP_STORE) {
			MVal *sel = mval_new(fm->host, MV_TEMP, at.write->dtype,
			                     0, "cmov");
			MInsM *c1 = maddm(fm, b, MMOP_CMOV, at.write->dtype,
			                   sel, tsrc, 0);
			c1->cc = tcc;
			MInsM *c2 = maddm(fm, b, MMOP_CMOV, at.write->dtype,
			                   sel, fsrc, 0);
			c2->cc = mcc_neg(tcc);
			maddm_addr(fm, b, MMOP_STORE, at.write->dtype, 0,
			           at.write->addr, sel);
		} else {
			/* phi temp destination: cmov straight into the phi dst */
			MVal *pd = at.write->dst;
			MInsM *c1 = maddm(fm, b, MMOP_CMOV, at.write->dtype, pd,
			                   tsrc, 0);
			c1->cc = tcc;
			MInsM *c2 = maddm(fm, b, MMOP_CMOV, at.write->dtype, pd,
			                   fsrc, 0);
			c2->cc = mcc_neg(tcc);
		}

		/* straight-line: branch the arms to the common join */
		b->term.op = MMOP_JMP;
		b->term.cc = MCC_NONE;
		b->s1 = T->s1;
		b->s2 = 0;

		/* the arms are now unreachable; leave them in the block list
		 * (dead code) — the emitter/regalloc tolerate dead blocks, and
		 * removing them would require predecessor re-analysis */
	}
}
