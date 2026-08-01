/* bridge_test.c — MIR → LIR bridge end-to-end test (B.4 bridge).
 *
 * Builds a small MFn (add: a+b) and a fib(n) function, bridges to the LIR
 * Fn, runs the full pass pipeline, and emits x86-64 assembly.  The asm is
 * assembled+linked+run to verify the emitted code is executable and
 * produces the expected result.
 *
 * Links against libmcc.a (the shared backend archive).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mir.h"
#include "ir.h"
#include "x86_64.h"

extern Target T_amd64_sysv;
extern void run_passes(Fn *);
extern void printfn(Fn *, FILE *);

/* globals normally provided by src/driver/main.c (not in libmcc.a) */
Target T;
char debug['Z' + 1];
int opt_level = 2;
int warn_level = WARN_ALL;
enum tls_model tls_model = TLSM_DEFAULT;

/* Build fib(n): n<2 -> n else fib(n-1)+fib(n-2).  A real control-flow
 * function exercising phi nodes and multiple blocks. */
static MFn *
build_fib(void)
{
	MFn *fn = mfn_new("fib", 2);
	fn->export = true; /* test driver links against the emitted symbol */
	MBlk *entry = mblk_new(fn, "entry");
	MBlk *base = mblk_new(fn, "base");
	MBlk *rec = mblk_new(fn, "rec");
	MBlk *merge = mblk_new(fn, "merge");
	MBlk *retb = mblk_new(fn, "ret");
	mfn_addblk(fn, entry);
	mfn_addblk(fn, base);
	mfn_addblk(fn, rec);
	mfn_addblk(fn, merge);
	mfn_addblk(fn, retb);

	MVal *n = mval_new(fn, MV_TEMP, MT_I32, 0, "n");
	MVal *c2 = mval_new(fn, MV_TEMP, MT_I32, 0, "c2");
	MVal *cmp = mval_new(fn, MV_TEMP, MT_I32, 0, "cmp");
	MVal *fn1 = mval_new(fn, MV_TEMP, MT_I32, 0, "fn1");
	MVal *fn2 = mval_new(fn, MV_TEMP, MT_I32, 0, "fn2");
	MVal *nm1 = mval_new(fn, MV_TEMP, MT_I32, 0, "nm1");
	MVal *nm2 = mval_new(fn, MV_TEMP, MT_I32, 0, "nm2");
	MVal *sum = mval_new(fn, MV_TEMP, MT_I32, 0, "sum");
	MVal *r = mval_new(fn, MV_TEMP, MT_I32, 0, "r");
	MVal *fibsym = mval_global(fn, "fib", true, false);

	fn->nparam = 1;
	fn->param = malloc(sizeof *fn->param);
	fn->param[0] = n;
	fn->rettype = MT_I32;

	MConst *c0 = mconst_int(fn, MT_I32, 0);
	MConst *c1 = mconst_int(fn, MT_I32, 1);
	MConst *c2c = mconst_int(fn, MT_I32, 2);

	/* entry: n = par */
	madd1(fn, entry, MOP_PAR, MT_I32, n, MREF_CON(0));
	/* cmp = n < 2 */
	madd(fn, entry, MOP_CSLT, MT_I32, cmp, MREF_VAL(n), MREF_CON(c2c));
	mterm(fn, entry, MOP_JNZ, MREF_VAL(cmp), base, rec);

	/* base: r = n */
	madd1(fn, base, MOP_COPY, MT_I32, r, MREF_VAL(n));
	mterm(fn, base, MOP_JMP, MREF_CON(0), retb, 0);

	/* rec: nm1 = n-1; fn1 = fib(nm1); nm2 = n-2; fn2 = fib(nm2) */
	madd(fn, rec, MOP_SUB, MT_I32, nm1, MREF_VAL(n), MREF_CON(c1));
	madd1(fn, rec, MOP_ARG, MT_I32, 0, MREF_VAL(nm1));
	madd1(fn, rec, MOP_CALL, MT_I32, fn1, MREF_VAL(fibsym));
	madd(fn, rec, MOP_SUB, MT_I32, nm2, MREF_VAL(n), MREF_CON(c2c));
	madd1(fn, rec, MOP_ARG, MT_I32, 0, MREF_VAL(nm2));
	madd1(fn, rec, MOP_CALL, MT_I32, fn2, MREF_VAL(fibsym));
	/* sum = fn1 + fn2 */
	madd(fn, rec, MOP_ADD, MT_I32, sum, MREF_VAL(fn1), MREF_VAL(fn2));
	madd1(fn, rec, MOP_COPY, MT_I32, r, MREF_VAL(sum));
	mterm(fn, rec, MOP_JMP, MREF_CON(0), retb, 0);

	/* ret: return r */
	mret(fn, retb, MREF_VAL(r));

	return fn;
}

int
main(int argc, char **argv)
{
	MFn *fn;
	Fn *lir;
	int use_gv = 0;
	(void)use_gv;

	/* init backend target */
	T = T_amd64_sysv;

	fn = build_fib();

	if (argc > 1 && strcmp(argv[1], "-d") == 0)
		mfn_dump(fn, stderr);

	/* run MIR passes, then bridge */
	run_mir_passes(fn, 2);
	lir = lir_bridge(fn);

	if (argc > 1 && strcmp(argv[1], "-d") == 0)
		printfn(lir, stderr);

	run_passes(lir);
	T.emitfn(lir, stdout);

	mfn_free(fn);
	return 0;
}
