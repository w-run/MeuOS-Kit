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

int main(void)
{
	test_fold_const();
	test_simpl_id();
	test_simpl_udiv();
	test_dce();
	test_dce_sideeffect();

	if (failures) {
		fprintf(stderr, "%d failures\n", failures);
		return 1;
	}
	printf("pass_test: all checks passed\n");
	return 0;
}
