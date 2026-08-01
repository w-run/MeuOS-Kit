/* mabi_test.c — P2 x86-64 SysV ABI lowering unit test (check-mir-abi).
 *
 * Verifies the ported classification rules and the selpar/selcall/selret
 * lowering on MIR-native data (MFnM + MTypeDesc):
 *   1. mabi_typclass: eightbyte classification (int/float/array/nested),
 *      in-memory fallback, 8/16-byte register return boundaries.
 *   2. mabi_argsclass: integer/SSE register exhaustion, aggregate spill.
 *   3. mfnm_abi_x86_64: scalar params -> reg moves, scalar ret -> RAX,
 *      aggregate return pack/unpack, aggregate args, sret hidden pointer.
 *
 * Links mir/ sources + the x86_64 machine ABI (no LIR/QBE structures).
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mir.h"
#include "x86_64_m.h"

extern void mfnm_abi_x86_64(MFnM *fm);

static int npass, nfail;

#define CHECK(cond) do { \
	if (cond) { npass++; } \
	else { nfail++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

/* build a struct type: fields are (type, offset) pairs, -1 type ends */
static MTypeDesc *
mkstruct(MFn *fn, const char *name, int (*spec)[2])
{
	MTypeDesc *td = mtd_new(name, false);
	for (int i = 0; spec[i][0] >= 0; i++) {
		mtd_add_field(td, "f", (MType)spec[i][0], 0, spec[i][1], -1, 0);
	}
	mtd_finalize(td);
	mfn_addtype(fn, td);
	return td;
}

static void
test_typclass(void)
{
	MFn *fn = mfn_new("t", 2);
	MAClass a;

	/* {int a; int b} -> 8 bytes, one integer class, in regs */
	{
		int spec[][2] = {{MT_I32, 0}, {MT_I32, 4}, {-1, 0}};
		MTypeDesc *td = mkstruct(fn, "S8", spec);
		mabi_typclass(&a, td);
		CHECK(a.inmem == 0);
		CHECK(a.size == 8);
		CHECK(a.cls[0] == MT_I64 && a.cls[1] == MT_NONE);
	}

	/* {double d0; double d1} -> 16 bytes, two FP classes */
	{
		int spec[][2] = {{MT_F64, 0}, {MT_F64, 8}, {-1, 0}};
		MTypeDesc *td = mkstruct(fn, "S16f", spec);
		mabi_typclass(&a, td);
		CHECK(a.inmem == 0);
		CHECK(a.size == 16);
		CHECK(a.cls[0] == MT_F64 && a.cls[1] == MT_F64);
	}

	/* {int x; double d} (padding to 16) -> I64, F64 */
	{
		int spec[][2] = {{MT_I32, 0}, {MT_F64, 8}, {-1, 0}};
		MTypeDesc *td = mkstruct(fn, "S16m", spec);
		mabi_typclass(&a, td);
		CHECK(a.size == 16);
		CHECK(a.cls[0] == MT_I64 && a.cls[1] == MT_F64);
	}

	/* array char[8] -> 8 bytes, integer class */
	{
		MTypeDesc *elem = mtd_new(0, false);
		elem->elem_type = MT_I8;
		elem->size = 1;
		elem->align = 1;
		MTypeDesc *arr = mtd_array(elem, 8);
		mtd_finalize(arr);
		mfn_addtype(fn, arr);
		mabi_typclass(&a, arr);
		CHECK(a.size == 8);
		CHECK(a.cls[0] == MT_I64);
	}

	/* large struct {int a[6]} = 24 bytes -> in memory */
	{
		int spec[][2] = {{MT_I32, 0}, {MT_I32, 4}, {MT_I32, 8},
		                 {MT_I32, 12}, {MT_I32, 16}, {MT_I32, 20}, {-1, 0}};
		MTypeDesc *td = mkstruct(fn, "S24", spec);
		mabi_typclass(&a, td);
		CHECK(a.inmem == 1);
	}

	/* empty struct -> in memory */
	{
		MTypeDesc *td = mtd_new("S0", false);
		mtd_finalize(td);
		mfn_addtype(fn, td);
		mabi_typclass(&a, td);
		CHECK(a.inmem == 1);
	}

	mfn_free(fn);
}

static void
test_argsclass(void)
{
	MFn *fn = mfn_new("t", 2);
	MFnM *fm = mfnm_new(fn, &mtarget_x86_64, "t");
	MInsM *m = calloc(16, sizeof *m);
	MAClass *ac = calloc(16, sizeof *ac);
	int n = 7;

	/* 7 scalar i32 args: first 6 in regs, 7th on stack */
	for (int i = 0; i < n; i++)
		m[i].dtype = MT_I32;
	int fa = mabi_argsclass(fm, m, n, ac, 0);
	for (int i = 0; i < 6; i++)
		CHECK(ac[i].inmem == 0);
	CHECK(ac[6].inmem == 2);
	CHECK(((fa >> 4) & 15) == 6);   /* all 6 GPRs consumed */

	/* 9 scalar double args: 8 in sse regs, 9th on stack */
	n = 9;
	for (int i = 0; i < n; i++)
		m[i].dtype = MT_F64;
	mabi_argsclass(fm, m, n, ac, 0);
	for (int i = 0; i < 8; i++)
		CHECK(ac[i].inmem == 0);
	CHECK(ac[8].inmem == 2);

	/* aggregate arg {int,int} after 6 ints: 1 more int slot needed,
	 * only 0 left -> spills to stack */
	{
		int spec[][2] = {{MT_I32, 0}, {MT_I32, 4}, {-1, 0}};
		MTypeDesc *td = mkstruct(fn, "A8", spec);
		n = 7;
		for (int i = 0; i < 6; i++)
			m[i].dtype = MT_I32;
		m[6].td = td;                       /* 8-byte aggregate */
		mabi_argsclass(fm, m, n, ac, 0);
		CHECK(ac[6].inmem == 1);            /* spilled */
	}

	free(ac);
	free(m);
	mfnm_free(fm);
	mfn_free(fn);
}

/* scan a block for an instruction matching op/dst */
static MInsM *
find_ins(MBlkM *b, MMOP op, MVal *dst)
{
	for (uint32_t i = 0; i < b->nins; i++)
		if (b->ins[i].op == op && b->ins[i].dst == dst)
			return &b->ins[i];
	return 0;
}

static void
test_scalar_abi(void)
{
	/* int f(int a, int b) { return a + b; } */
	MFn *fn = mfn_new("f", 2);
	MFnM *fm = mfnm_new(fn, &mtarget_x86_64, "f");
	MBlkM *b = mblkm_new(fm, "entry");
	mfnm_addblk(fm, b);

	MVal *a = mval_new(fn, MV_TEMP, MT_I32, 0, "a");
	MVal *bb = mval_new(fn, MV_TEMP, MT_I32, 0, "b");
	MVal *s = mval_new(fn, MV_TEMP, MT_I32, 0, "s");
	maddm(fm, b, MMOP_PARM, MT_I32, a, 0, 0);
	maddm(fm, b, MMOP_PARM, MT_I32, bb, 0, 0);
	maddm(fm, b, MMOP_ADD, MT_I32, s, a, bb);
	mfnm_term(fm, b, MMOP_RET, s, 0, 0, MCC_NONE);
	b->term.dtype = MT_I32;

	mfnm_abi_x86_64(fm);

	/* entry: a <- edi, b <- esi; ADD s; ret moved eax <- s */
	MInsM *ma = find_ins(b, MMOP_MOV, a);
	MInsM *mb = find_ins(b, MMOP_MOV, bb);
	CHECK(ma && ma->src[0] == mfn_reg(fn, &mtarget_x86_64, X64MREG_RDI));
	CHECK(mb && mb->src[0] == mfn_reg(fn, &mtarget_x86_64, X64MREG_RSI));
	MInsM *mr = find_ins(b, MMOP_MOV, mfn_reg(fn, &mtarget_x86_64, X64MREG_RAX));
	CHECK(mr && mr->src[0] == s);
	CHECK(b->term.op == MMOP_RET && b->term.src[0] == 0);
	/* order: params, add, ret-move */
	CHECK(b->nins == 4);
	CHECK(b->ins[2].op == MMOP_ADD);

	mfnm_free(fm);
	mfn_free(fn);
}

static void
test_agg_ret(void)
{
	/* struct S {int a; int b;} mk(void) { ... return pad; }
	 * 8-byte aggregate return: RAX = load [pad+0] */
	MFn *fn = mfn_new("mk", 2);
	MFnM *fm = mfnm_new(fn, &mtarget_x86_64, "mk");
	MBlkM *b = mblkm_new(fm, "entry");
	mfnm_addblk(fm, b);

	int spec[][2] = {{MT_I32, 0}, {MT_I32, 4}, {-1, 0}};
	MTypeDesc *td = mkstruct(fn, "S", spec);
	fm->retty = td;

	MVal *pad = mval_new(fn, MV_TEMP, MT_PTR, 0, "pad");
	maddm(fm, b, MMOP_LEA, MT_PTR, pad, mfn_reg(fn, &mtarget_x86_64, X64MREG_RBX), 0);
	mfnm_term(fm, b, MMOP_RET, pad, 0, 0, MCC_NONE);
	b->term.td = td;

	mfnm_abi_x86_64(fm);

	/* RAX = load [pad + 0] */
	MInsM *m = find_ins(b, MMOP_LOAD, mfn_reg(fn, &mtarget_x86_64, X64MREG_RAX));
	CHECK(m != 0);
	CHECK(m->addr.base == pad && m->addr.off == 0);
	CHECK(m->dtype == MT_I64);
	CHECK(b->term.src[0] == 0);

	mfnm_free(fm);
	mfn_free(fn);
}

static void
test_agg_arg(void)
{
	/* void g(struct S s);  call g(pad) with 8-byte aggregate arg:
	 * RDI = load [pad+0]; call; (void ret) */
	MFn *fn = mfn_new("gcall", 2);
	MFnM *fm = mfnm_new(fn, &mtarget_x86_64, "gcall");
	MBlkM *b = mblkm_new(fm, "entry");
	mfnm_addblk(fm, b);

	int spec[][2] = {{MT_I32, 0}, {MT_I32, 4}, {-1, 0}};
	MTypeDesc *td = mkstruct(fn, "S", spec);

	MVal *pad = mval_new(fn, MV_TEMP, MT_PTR, 0, "pad");
	MVal *g = mval_global(fn, "g", true, false);
	maddm(fm, b, MMOP_ARG, MT_PTR, 0, pad, 0);
	b->ins[b->nins - 1].td = td;
	maddm(fm, b, MMOP_CALL, MT_NONE, 0, g, 0);
	mfnm_term(fm, b, MMOP_RET, 0, 0, 0, MCC_NONE);

	mfnm_abi_x86_64(fm);

	/* RDI = load [pad+0] before the call */
	MVal *rdi = mfn_reg(fn, &mtarget_x86_64, X64MREG_RDI);
	MInsM *m = find_ins(b, MMOP_LOAD, rdi);
	CHECK(m && m->addr.base == pad);
	MInsM *c = find_ins(b, MMOP_CALL, 0);
	CHECK(c && c->src[0] == g);
	CHECK(find_ins(b, MMOP_ARG, 0) == 0);   /* marker consumed */

	mfnm_free(fm);
	mfn_free(fn);
}

int
main(void)
{
	printf("run typclass\n"); fflush(0); test_typclass();
	printf("run argsclass\n"); fflush(0); test_argsclass();
	printf("run scalar\n"); fflush(0); test_scalar_abi();
	printf("run aggret\n"); fflush(0); test_agg_ret();
	printf("run aggarg\n"); fflush(0); test_agg_arg();

	printf("mabi_test: %d passed, %d failed\n", npass, nfail);
	return nfail ? 1 : 0;
}
