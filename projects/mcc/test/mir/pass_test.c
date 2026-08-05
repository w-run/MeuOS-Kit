/* pass_test.c — MIR optimization pass unit tests (B.2).
 *
 * Builds small MFn programs and verifies fold/simpl/dce transform them
 * as expected.  Pass outputs are inspected structurally.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mir.h"

static int failures;

#define CHECK(cond, msg) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); \
		failures++; \
	} \
} while (0)

/* Count instructions in a block matching an op. */
static uint32_t
count_op(MBlk *b, MOP op)
{
	uint32_t n = 0;
	for (uint32_t i = 0; i < b->nins; i++)
		if (b->ins[i].op == op)
			n++;
	return n;
}

static MVal *
find_def(MFn *fn, const char *name)
{
	for (uint32_t i = 0; i < fn->nval; i++)
		if (fn->val[i]->name && strcmp(fn->val[i]->name, name) == 0)
			return fn->val[i];
	return 0;
}

/* Test 1: constant folding — `r = a + 40 + 2` where a is a param.
 * Expects: fold(40+2) -> 42 constant folded into `add`.
 * We construct `a + 42` directly and expect the ADD to remain but if both
 * operands are constant it folds away. */
static void
test_fold_const(void)
{
	MFn *fn = mfn_new("fold_const", 2);
	MBlk *b = mblk_new(fn, "entry");
	mfn_addblk(fn, b);

	MVal *a = mval_new(fn, MV_TEMP, MT_I32, 0, "a");
	MVal *r = mval_new(fn, MV_TEMP, MT_I32, 0, "r");
	madd1(fn, b, MOP_PAR, MT_I32, a, MREF_CON(0));
	MConst *c42 = mconst_int(fn, MT_I32, 42);
	MConst *c1 = mconst_int(fn, MT_I32, 1);
	madd(fn, b, MOP_ADD, MT_I32, r, MREF_CON(c42), MREF_CON(c1));
	mret(fn, b, MREF_VAL(r));

	/* both operands constant: add folds to a constant 43 */
	run_mir_pass(fn, MIR_PASS_FOLD);
	CHECK(b->nins == 1, "add folded away, only PAR remains");
	/* the return must reference the new constant value */
	CHECK(b->term.src[0].con != 0, "term references a constant");
	if (b->term.src[0].con)
		CHECK(b->term.src[0].con->u.i == 43, "folded value is 43");
	mfn_free(fn);
}

/* Test 2: algebraic simplification — x+0 -> x, x*1 -> x. */
static void
test_simpl_id(void)
{
	MFn *fn = mfn_new("simpl_id", 2);
	MBlk *b = mblk_new(fn, "entry");
	mfn_addblk(fn, b);

	MVal *x = mval_new(fn, MV_TEMP, MT_I32, 0, "x");
	MVal *r1 = mval_new(fn, MV_TEMP, MT_I32, 0, "r1");
	MVal *r2 = mval_new(fn, MV_TEMP, MT_I32, 0, "r2");
	MVal *r3 = mval_new(fn, MV_TEMP, MT_I32, 0, "r3");
	madd1(fn, b, MOP_PAR, MT_I32, x, MREF_CON(0));
	MConst *c0 = mconst_int(fn, MT_I32, 0);
	MConst *c1 = mconst_int(fn, MT_I32, 1);
	madd(fn, b, MOP_ADD, MT_I32, r1, MREF_VAL(x), MREF_CON(c0));
	madd(fn, b, MOP_MUL, MT_I32, r2, MREF_VAL(x), MREF_CON(c1));
	madd(fn, b, MOP_XOR, MT_I32, r3, MREF_VAL(x), MREF_VAL(x));
	mret(fn, b, MREF_VAL(r1));

	run_mir_pass(fn, MIR_PASS_FOLD);
	/* x+0 and x*1 and x^x all vanish -> only PAR remains */
	CHECK(b->nins == 1, "all identity ops simplified away");
	/* r1 maps to x */
	CHECK(b->term.src[0].val == x, "ret r1 maps to x");
	mfn_free(fn);
}

/* Test 3: udiv by power of two -> shr. */
static void
test_simpl_udiv(void)
{
	MFn *fn = mfn_new("simpl_udiv", 2);
	MBlk *b = mblk_new(fn, "entry");
	mfn_addblk(fn, b);

	MVal *x = mval_new(fn, MV_TEMP, MT_I32, 0, "x");
	MVal *r = mval_new(fn, MV_TEMP, MT_I32, 0, "r");
	madd1(fn, b, MOP_PAR, MT_I32, x, MREF_CON(0));
	MConst *c8 = mconst_int(fn, MT_I32, 8);
	madd(fn, b, MOP_UDIV, MT_I32, r, MREF_VAL(x), MREF_CON(c8));
	mret(fn, b, MREF_VAL(r));

	run_mir_pass(fn, MIR_PASS_FOLD);
	CHECK(count_op(b, MOP_SHR) == 1, "udiv by 8 becomes shr");
	CHECK(count_op(b, MOP_UDIV) == 0, "no udiv remains");
	mfn_free(fn);
}

/* Test 3b: signed div/rem by power of two must NOT be strength-reduced.
 * C division rounds toward zero, but SAR rounds toward -inf (and `x & m`
 * is not a valid srem), so folding MOP_DIV/MOP_REM miscompiled negatives
 * (e.g. -7/2 -> -4 instead of -3, verified 2026-08-03).  Both ops stay. */
static void
test_simpl_sdiv_not_reduced(void)
{
	MFn *fn = mfn_new("simpl_sdiv", 2);
	MBlk *b = mblk_new(fn, "entry");
	mfn_addblk(fn, b);

	MVal *x = mval_new(fn, MV_TEMP, MT_I32, 0, "x");
	MVal *rd = mval_new(fn, MV_TEMP, MT_I32, 0, "rd");
	MVal *rr = mval_new(fn, MV_TEMP, MT_I32, 0, "rr");
	madd1(fn, b, MOP_PAR, MT_I32, x, MREF_CON(0));
	MConst *c8 = mconst_int(fn, MT_I32, 8);
	madd(fn, b, MOP_DIV, MT_I32, rd, MREF_VAL(x), MREF_CON(c8));
	madd(fn, b, MOP_REM, MT_I32, rr, MREF_VAL(x), MREF_CON(c8));
	mret(fn, b, MREF_VAL(rd));

	run_mir_pass(fn, MIR_PASS_FOLD);
	CHECK(count_op(b, MOP_DIV) == 1, "signed div by 8 kept (not shr)");
	CHECK(count_op(b, MOP_REM) == 1, "signed rem by 8 kept (not and)");
	CHECK(count_op(b, MOP_SAR) == 0, "no sar introduced");
	CHECK(count_op(b, MOP_SHR) == 0, "no shr introduced");
	mfn_free(fn);
}

/* Test 3c: exact defect-V regression cases at the MIR pass level —
 * signed div by 2 (`-7/2`) and signed rem by 4 (`-7%4`) must NOT be
 * strength-reduced to SAR/AND (C truncates toward zero, SAR toward -inf).
 * Mirrors the four runtime cases from the canary signed_div_pow2.c. */
static void
test_simpl_sdiv_pow2_exact(void)
{
	MFn *fn = mfn_new("simpl_sdiv_exact", 2);
	MBlk *b = mblk_new(fn, "entry");
	mfn_addblk(fn, b);

	MVal *x = mval_new(fn, MV_TEMP, MT_I32, 0, "x");
	MVal *rd2 = mval_new(fn, MV_TEMP, MT_I32, 0, "rd2");
	MVal *rr4 = mval_new(fn, MV_TEMP, MT_I32, 0, "rr4");
	madd1(fn, b, MOP_PAR, MT_I32, x, MREF_CON(0));
	MConst *c2 = mconst_int(fn, MT_I32, 2);
	MConst *c4 = mconst_int(fn, MT_I32, 4);
	madd(fn, b, MOP_DIV, MT_I32, rd2, MREF_VAL(x), MREF_CON(c2));
	madd(fn, b, MOP_REM, MT_I32, rr4, MREF_VAL(x), MREF_CON(c4));
	mret(fn, b, MREF_VAL(rd2));

	run_mir_pass(fn, MIR_PASS_FOLD);
	CHECK(count_op(b, MOP_DIV) == 1, "signed div by 2 kept (not sar)");
	CHECK(count_op(b, MOP_REM) == 1, "signed rem by 4 kept (not and)");
	CHECK(count_op(b, MOP_SAR) == 0, "no sar introduced for div by 2");
	CHECK(count_op(b, MOP_SHR) == 0, "no shr introduced for div by 2");
	CHECK(count_op(b, MOP_AND) == 0, "no and introduced for rem by 4");
	mfn_free(fn);
}

/* Fold a single binary op on two integer constants and return the folded
 * result.  `ok` reports whether the op folded to a constant at all. */
static int64_t
fold_binop_const(MOP op, int64_t lhs, int64_t rhs, int *ok)
{
	MFn *fn = mfn_new("fold_binop", 2);
	MBlk *b = mblk_new(fn, "entry");
	mfn_addblk(fn, b);

	MVal *a = mval_new(fn, MV_TEMP, MT_I32, 0, "a");
	MVal *r = mval_new(fn, MV_TEMP, MT_I32, 0, "r");
	madd1(fn, b, MOP_PAR, MT_I32, a, MREF_CON(0));
	madd(fn, b, op, MT_I32, r, MREF_CON(mconst_int(fn, MT_I32, lhs)),
	     MREF_CON(mconst_int(fn, MT_I32, rhs)));
	mret(fn, b, MREF_VAL(r));

	run_mir_pass(fn, MIR_PASS_FOLD);
	int64_t got = 0;
	*ok = (b->term.src[0].con != 0);
	if (*ok)
		got = b->term.src[0].con->u.i;
	mfn_free(fn);
	return got;
}

/* Test 3d: the exact miscompile cases from defect V, pinned by value.
 *
 * A power-of-two divisor is the case that used to be strength-reduced into
 * SAR/AND; with a negative dividend SAR rounds toward -inf while C rounds
 * toward zero, so `-7/2` came out as -4 instead of -3.  These assertions
 * pin the C semantics for the constant-fold path (the constexpr/consteval
 * route, where a wrong value would be baked in at compile time); Tests 3b
 * and 3c pin the non-constant path structurally. */
static void
test_fold_signed_pow2_values(void)
{
	struct { MOP op; int64_t l, r, want; const char *msg; } cases[] = {
		{ MOP_DIV, -7,   2,  -3, "div(-7,2) == -3 (not -4)" },
		{ MOP_REM, -7,   4,  -3, "rem(-7,4) == -3 (not 1)"  },
		{ MOP_DIV, -17,  8,  -2, "div(-17,8) == -2 (not -3)" },
		{ MOP_REM, -17,  8,  -1, "rem(-17,8) == -1 (not 7)"  },
		/* positive dividends must stay correct too */
		{ MOP_DIV,  7,   2,   3, "div(7,2) == 3"  },
		{ MOP_REM,  7,   4,   3, "rem(7,4) == 3"  },
		/* -8 is exactly divisible: no rounding ambiguity */
		{ MOP_DIV, -8,   2,  -4, "div(-8,2) == -4" },
		{ MOP_REM, -8,   2,   0, "rem(-8,2) == 0"  },
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
		int ok = 0;
		int64_t got = fold_binop_const(cases[i].op, cases[i].l,
		                               cases[i].r, &ok);
		CHECK(ok, cases[i].msg);
		if (ok)
			CHECK(got == cases[i].want, cases[i].msg);
	}
}

/* Test 3e: an unused MOP_PAR is ABI-significant and must survive DCE.
 * Removing an unused parameter's PAR shifted a following scalar
 * parameter's argument register (RSI->RDI) under MCC_MIR_BACKEND=1,
 * breaking the caller/callee agreement (verified 2026-08-03). */
static void
test_dce_param_kept(void)
{
	MFn *fn = mfn_new("dce_param", 2);
	MBlk *b = mblk_new(fn, "entry");
	mfn_addblk(fn, b);

	MVal *a = mval_new(fn, MV_TEMP, MT_AGG, 0, "a");  /* unused agg param */
	MVal *n = mval_new(fn, MV_TEMP, MT_I32, 0, "n");
	MVal *r = mval_new(fn, MV_TEMP, MT_I32, 0, "r");
	MVal *dead = mval_new(fn, MV_TEMP, MT_I32, 0, "dead");
	madd1(fn, b, MOP_PAR, MT_AGG, a, MREF_CON(0));
	madd1(fn, b, MOP_PAR, MT_I32, n, MREF_CON(0));
	madd(fn, b, MOP_ADD, MT_I32, dead, MREF_VAL(n), MREF_CON(mconst_int(fn, MT_I32, 1)));
	madd1(fn, b, MOP_COPY, MT_I32, r, MREF_VAL(n));
	mret(fn, b, MREF_VAL(r));

	run_mir_pass(fn, MIR_PASS_DCE);
	/* both PARs survive (the unused agg param is ABI-significant); the
	 * genuinely dead ADD is removed */
	CHECK(count_op(b, MOP_PAR) == 2, "unused agg param's PAR kept");
	CHECK(count_op(b, MOP_ADD) == 0, "dead add removed");
	mfn_free(fn);
}

/* Test 4: dead code elimination. */
static void
test_dce(void)
{
	MFn *fn = mfn_new("dce", 2);
	MBlk *b = mblk_new(fn, "entry");
	mfn_addblk(fn, b);

	MVal *x = mval_new(fn, MV_TEMP, MT_I32, 0, "x");
	MVal *dead = mval_new(fn, MV_TEMP, MT_I32, 0, "dead");
	MVal *live = mval_new(fn, MV_TEMP, MT_I32, 0, "live");
	madd1(fn, b, MOP_PAR, MT_I32, x, MREF_CON(0));
	MConst *c1 = mconst_int(fn, MT_I32, 1);
	madd(fn, b, MOP_ADD, MT_I32, dead, MREF_VAL(x), MREF_CON(c1));
	madd1(fn, b, MOP_COPY, MT_I32, live, MREF_VAL(x));
	mret(fn, b, MREF_VAL(live));

	run_mir_pass(fn, MIR_PASS_DCE);
	/* dead add has no uses and is pure -> removed; PAR + COPY remain */
	CHECK(b->nins == 2, "dead add removed, PAR + COPY remain");
	CHECK(count_op(b, MOP_COPY) == 1, "COPY kept");
	mfn_free(fn);
}

/* Test 5: store has side effect, never DCE'd. */
static void
test_dce_sideeffect(void)
{
	MFn *fn = mfn_new("dce_side", 2);
	MBlk *b = mblk_new(fn, "entry");
	mfn_addblk(fn, b);

	MVal *p = mval_new(fn, MV_TEMP, MT_PTR, 0, "p");
	MVal *v = mval_new(fn, MV_TEMP, MT_I32, 0, "v");
	madd1(fn, b, MOP_PAR, MT_PTR, p, MREF_CON(0));
	madd1(fn, b, MOP_PAR, MT_I32, v, MREF_CON(0));
	madd(fn, b, MOP_STORE, MT_I32, 0, MREF_VAL(p), MREF_VAL(v));
	mretvoid(fn, b);

	run_mir_pass(fn, MIR_PASS_DCE);
	CHECK(count_op(b, MOP_STORE) == 1, "store kept (side effect)");
	mfn_free(fn);
}

/* Test 6: copy propagation — `d = copy x` used within the block. */
static void
test_copy_prop(void)
{
	MFn *fn = mfn_new("copy_prop", 2);
	MBlk *b = mblk_new(fn, "entry");
	mfn_addblk(fn, b);

	MVal *x = mval_new(fn, MV_TEMP, MT_I32, 0, "x");
	MVal *d = mval_new(fn, MV_TEMP, MT_I32, 0, "d");
	madd1(fn, b, MOP_PAR, MT_I32, x, MREF_CON(0));
	madd1(fn, b, MOP_COPY, MT_I32, d, MREF_VAL(x));
	mret(fn, b, MREF_VAL(d));

	run_mir_pass(fn, MIR_PASS_COPY);
	/* the copy is gone; ret references x directly */
	CHECK(count_op(b, MOP_COPY) == 0, "copy eliminated");
	CHECK(b->term.src[0].val == x, "ret references source directly");
	mfn_free(fn);
}

/* Test 7: GVN — `a+b` computed twice in one block, second is redundant. */
static void
test_gvn(void)
{
	MFn *fn = mfn_new("gvn", 2);
	MBlk *b = mblk_new(fn, "entry");
	mfn_addblk(fn, b);

	MVal *a = mval_new(fn, MV_TEMP, MT_I32, 0, "a");
	MVal *bb = mval_new(fn, MV_TEMP, MT_I32, 0, "b");
	MVal *r1 = mval_new(fn, MV_TEMP, MT_I32, 0, "r1");
	MVal *r2 = mval_new(fn, MV_TEMP, MT_I32, 0, "r2");
	madd1(fn, b, MOP_PAR, MT_I32, a, MREF_CON(0));
	madd1(fn, b, MOP_PAR, MT_I32, bb, MREF_CON(0));
	madd(fn, b, MOP_ADD, MT_I32, r1, MREF_VAL(a), MREF_VAL(bb));
	madd(fn, b, MOP_ADD, MT_I32, r2, MREF_VAL(a), MREF_VAL(bb));
	mret(fn, b, MREF_VAL(r2));

	run_mir_pass(fn, MIR_PASS_GVN);
	/* only one add remains; ret uses r1 */
	CHECK(count_op(b, MOP_ADD) == 1, "redundant add eliminated");
	CHECK(b->term.src[0].val == r1, "ret references first result");
	mfn_free(fn);
}

/* Test 8: GVN with differing operands — no spurious elimination. */
static void
test_gvn_nomatch(void)
{
	MFn *fn = mfn_new("gvn_nomatch", 2);
	MBlk *b = mblk_new(fn, "entry");
	mfn_addblk(fn, b);

	MVal *a = mval_new(fn, MV_TEMP, MT_I32, 0, "a");
	MVal *bb = mval_new(fn, MV_TEMP, MT_I32, 0, "b");
	MVal *c = mval_new(fn, MV_TEMP, MT_I32, 0, "c");
	MVal *r1 = mval_new(fn, MV_TEMP, MT_I32, 0, "r1");
	MVal *r2 = mval_new(fn, MV_TEMP, MT_I32, 0, "r2");
	madd1(fn, b, MOP_PAR, MT_I32, a, MREF_CON(0));
	madd1(fn, b, MOP_PAR, MT_I32, bb, MREF_CON(0));
	madd1(fn, b, MOP_PAR, MT_I32, c, MREF_CON(0));
	madd(fn, b, MOP_ADD, MT_I32, r1, MREF_VAL(a), MREF_VAL(bb));
	madd(fn, b, MOP_ADD, MT_I32, r2, MREF_VAL(a), MREF_VAL(c));
	mret(fn, b, MREF_VAL(r2));

	run_mir_pass(fn, MIR_PASS_GVN);
	CHECK(count_op(b, MOP_ADD) == 2, "different adds both kept");
	mfn_free(fn);
}

/* Test 9: phi copy propagation — phi(v, v) collapses to v. */
static void
test_phi_copy(void)
{
	MFn *fn = mfn_new("phi_copy", 2);
	MBlk *e = mblk_new(fn, "entry");
	MBlk *j = mblk_new(fn, "join");
	mfn_addblk(fn, e);
	mfn_addblk(fn, j);

	MVal *v = mval_new(fn, MV_TEMP, MT_I32, 0, "v");
	MVal *p = mval_new(fn, MV_TEMP, MT_I32, 0, "p");
	madd1(fn, e, MOP_PAR, MT_I32, v, MREF_CON(0));
	mterm(fn, e, MOP_JMP, MREF_CON(0), j, 0);

	/* phi p = phi(v, v) */
	mphi_add(fn, j, MT_I32, p);
	MPhi *phi = j->phi;
	phi->narg = 2;
	phi->carg = 2;
	phi->arg = malloc(2 * sizeof *phi->arg);
	phi->blk = malloc(2 * sizeof *phi->blk);
	phi->arg[0] = v;
	phi->arg[1] = v;
	phi->blk[0] = e;
	phi->blk[1] = e;

	mret(fn, j, MREF_VAL(p));

	run_mir_pass(fn, MIR_PASS_COPY);
	CHECK(j->phi == NULL, "phi collapsed");
	CHECK(j->term.src[0].val == v, "ret references v directly");
	mfn_free(fn);
}

/* Test 10: signed division / remainder folding with a negative operand.
 * div(-7, 3) -> -2 (truncation toward zero), rem(-7, 3) -> -1. */
static void
test_fold_sdiv_neg(void)
{
	MFn *fn = mfn_new("fold_sdiv_neg", 2);
	MBlk *b = mblk_new(fn, "entry");
	mfn_addblk(fn, b);

	MVal *a = mval_new(fn, MV_TEMP, MT_I32, 0, "a");
	MVal *r = mval_new(fn, MV_TEMP, MT_I32, 0, "r");
	MConst *cneg7 = mconst_int(fn, MT_I32, -7);
	MConst *c3 = mconst_int(fn, MT_I32, 3);
	madd1(fn, b, MOP_PAR, MT_I32, a, MREF_CON(0));
	madd(fn, b, MOP_DIV, MT_I32, r, MREF_CON(cneg7), MREF_CON(c3));
	mret(fn, b, MREF_VAL(r));

	run_mir_pass(fn, MIR_PASS_FOLD);
	CHECK(b->nins == 1, "sdiv folded away");
	if (b->term.src[0].con)
		CHECK(b->term.src[0].con->u.i == -2, "div(-7,3) folds to -2");
	mfn_free(fn);
}

/* Test 11: signed remainder folding with a negative operand. */
static void
test_fold_rem_neg(void)
{
	MFn *fn = mfn_new("fold_rem_neg", 2);
	MBlk *b = mblk_new(fn, "entry");
	mfn_addblk(fn, b);

	MVal *a = mval_new(fn, MV_TEMP, MT_I32, 0, "a");
	MVal *r = mval_new(fn, MV_TEMP, MT_I32, 0, "r");
	MConst *cneg7 = mconst_int(fn, MT_I32, -7);
	MConst *c3 = mconst_int(fn, MT_I32, 3);
	madd1(fn, b, MOP_PAR, MT_I32, a, MREF_CON(0));
	madd(fn, b, MOP_REM, MT_I32, r, MREF_CON(cneg7), MREF_CON(c3));
	mret(fn, b, MREF_VAL(r));

	run_mir_pass(fn, MIR_PASS_FOLD);
	CHECK(b->nins == 1, "srem folded away");
	if (b->term.src[0].con)
		CHECK(b->term.src[0].con->u.i == -1, "rem(-7,3) folds to -1");
	mfn_free(fn);
}

/* Test 12: shift-by-zero is the identity — shl(x,0)/sar(x,0)/shr(x,0) -> x. */
static void
test_fold_shift_zero(void)
{
	MFn *fn = mfn_new("fold_shift_zero", 2);
	MBlk *b = mblk_new(fn, "entry");
	mfn_addblk(fn, b);

	MVal *x = mval_new(fn, MV_TEMP, MT_I32, 0, "x");
	MVal *r1 = mval_new(fn, MV_TEMP, MT_I32, 0, "r1");
	MVal *r2 = mval_new(fn, MV_TEMP, MT_I32, 0, "r2");
	MVal *r3 = mval_new(fn, MV_TEMP, MT_I32, 0, "r3");
	madd1(fn, b, MOP_PAR, MT_I32, x, MREF_CON(0));
	MConst *c0 = mconst_int(fn, MT_I32, 0);
	madd(fn, b, MOP_SHL, MT_I32, r1, MREF_VAL(x), MREF_CON(c0));
	madd(fn, b, MOP_SAR, MT_I32, r2, MREF_VAL(x), MREF_CON(c0));
	madd(fn, b, MOP_SHR, MT_I32, r3, MREF_VAL(x), MREF_CON(c0));
	mret(fn, b, MREF_VAL(r1));

	run_mir_pass(fn, MIR_PASS_FOLD);
	/* all three shift-by-zero ops fold to x: only PAR + RET remain */
	CHECK(count_op(b, MOP_SHL) == 0, "shl(x,0) folded away");
	CHECK(count_op(b, MOP_SAR) == 0, "sar(x,0) folded away");
	CHECK(count_op(b, MOP_SHR) == 0, "shr(x,0) folded away");
	CHECK(b->nins == 1, "only PAR remains (ret is the terminator)");
	CHECK(b->term.src[0].val == x, "ret r1 maps to x");
	mfn_free(fn);
}

/* Test 13: floating-point constant comparison folding.
 * cflt(1.5, 2.5) -> 1, cfgt(2.5, 1.5) -> 1. */
static void
test_fold_fcmp(void)
{
	MFn *fn = mfn_new("fold_fcmp", 2);
	MBlk *b = mblk_new(fn, "entry");
	mfn_addblk(fn, b);

	MVal *a = mval_new(fn, MV_TEMP, MT_I32, 0, "a");
	MVal *r1 = mval_new(fn, MV_TEMP, MT_I32, 0, "r1");
	MVal *r2 = mval_new(fn, MV_TEMP, MT_I32, 0, "r2");
	MConst *c15 = mconst_flt(fn, MT_F64, 1.5);
	MConst *c25 = mconst_flt(fn, MT_F64, 2.5);
	madd1(fn, b, MOP_PAR, MT_I32, a, MREF_CON(0));
	madd(fn, b, MOP_CFLT, MT_I32, r1, MREF_CON(c15), MREF_CON(c25));
	madd(fn, b, MOP_CFGT, MT_I32, r2, MREF_CON(c25), MREF_CON(c15));
	mret(fn, b, MREF_VAL(r1));

	run_mir_pass(fn, MIR_PASS_FOLD);
	CHECK(b->nins == 1, "both float compares folded away");
	if (b->term.src[0].con)
		CHECK(b->term.src[0].con->u.i == 1, "1.5 < 2.5 folds to 1");
	mfn_free(fn);
}

/* Test 14: a call is a side effect and survives DCE even when its result
 * is unused. */
static void
test_dce_call_sideeffect(void)
{
	MFn *fn = mfn_new("dce_call", 2);
	MBlk *b = mblk_new(fn, "entry");
	mfn_addblk(fn, b);

	MVal *r = mval_new(fn, MV_TEMP, MT_I32, 0, "r");
	MVal *sym = mval_global(fn, "side_effect", true, false);
	MConst *c1 = mconst_int(fn, MT_I32, 1);
	madd1(fn, b, MOP_ARG, MT_I32, 0, MREF_CON(c1));
	madd1(fn, b, MOP_CALL, MT_I32, r, MREF_VAL(sym));
	mretvoid(fn, b);

	run_mir_pass(fn, MIR_PASS_DCE);
	CHECK(count_op(b, MOP_CALL) == 1, "call kept (side effect)");
	mfn_free(fn);
}

/* Test 15: an unused load is treated as side-effect-free and removed by
 * DCE (passes.c marks only STORE/CALL/ALLOCA/VASTART/SALLOC as effects).
 * NOTE: this pins current behavior; revisit if volatile/aliasing awareness
 * is added. */
static void
test_dce_load_removed(void)
{
	MFn *fn = mfn_new("dce_load", 2);
	MBlk *b = mblk_new(fn, "entry");
	mfn_addblk(fn, b);

	MVal *p = mval_new(fn, MV_TEMP, MT_PTR, 0, "p");
	MVal *v = mval_new(fn, MV_TEMP, MT_I32, 0, "v");
	madd1(fn, b, MOP_PAR, MT_PTR, p, MREF_CON(0));
	madd1(fn, b, MOP_LOAD, MT_I32, v, MREF_VAL(p));
	mretvoid(fn, b);

	run_mir_pass(fn, MIR_PASS_DCE);
	CHECK(count_op(b, MOP_LOAD) == 0, "unused load removed");
	CHECK(count_op(b, MOP_PAR) == 1, "param kept");
	mfn_free(fn);
}

int main(void)
{
	test_fold_const();
	test_simpl_id();
	test_simpl_udiv();
	test_simpl_sdiv_not_reduced();
	test_simpl_sdiv_pow2_exact();
	test_fold_signed_pow2_values();
	test_dce_param_kept();
	test_dce();
	test_dce_sideeffect();
	test_copy_prop();
	test_gvn();
	test_gvn_nomatch();
	test_phi_copy();
	test_fold_sdiv_neg();
	test_fold_rem_neg();
	test_fold_shift_zero();
	test_fold_fcmp();
	test_dce_call_sideeffect();
	test_dce_load_removed();

	if (failures) {
		fprintf(stderr, "%d failures\n", failures);
		return 1;
	}
	printf("pass_test: all checks passed\n");
	return 0;
}
