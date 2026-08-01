/* machine_test.c — P1 machine-layer unit test (check-mir-machine).
 *
 * Verifies the MIR-native machine layer (target-parameterized, covering
 * all 6 architectures; x86_64 is the first instantiation):
 *   1. x86-64 machine target (mtarget_x86_64): register table names,
 *      classes, save semantics, SysV argument register order.
 *   2. mfn_reg(): registers are MV_REG MVal in fn->reg[], created on
 *      demand, identical instance on repeat lookup.
 *   3. addressing modes (MAddr): base/index/scale/offset and
 *      symbol-relative forms.
 *   4. machine opcodes (MMOP) and semantic condition codes (MCC).
 *   5. machine instruction construction (maddm*) incl. BLIT, LOAD/STORE
 *      with addressing mode, SETCC with condition code, terminators.
 *   6. mfnm_dump produces readable output.
 *
 * Links against build.c/mir_util.c/machine.c/print.c (mir.h only, no
 * LIR/QBE structures).
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "mir.h"

static int npass, nfail;

#define CHECK(cond) do { \
	if (cond) { npass++; } \
	else { nfail++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static void
test_registers(void)
{
	const MTargetM *mt = &mtarget_x86_64;
	int rax = mreg_id(mt, "rax");
	int rbx = mreg_id(mt, "rbx");
	int rsp = mreg_id(mt, "rsp");
	int xmm0 = mreg_id(mt, "xmm0");
	int xmm15 = mreg_id(mt, "xmm15");
	CHECK(rax >= 0 && rbx >= 0 && rsp >= 0 && xmm0 >= 0 && xmm15 >= 0);
	CHECK(mreg_id(mt, "nosuchreg") == -1);

	/* table integrity */
	CHECK(mt->nreg == 32);            /* 16 GPR (incl. rbp/rsp) + 16 XMM */
	CHECK(strcmp(mreg_name(mt, rax), "rax") == 0);
	CHECK(strcmp(mreg_name(mt, xmm15), "xmm15") == 0);
	CHECK(mt->regs[rax].cls == MRC_GPR);
	CHECK(mt->regs[rax].caller_saved);
	CHECK(!mt->regs[rax].callee_saved);
	CHECK(mt->regs[rbx].callee_saved);
	CHECK(!mt->regs[rbx].caller_saved);
	CHECK(!mt->regs[rsp].caller_saved && !mt->regs[rsp].callee_saved);
	CHECK(mt->regs[xmm0].cls == MRC_FPR);
	CHECK(mt->regs[xmm0].arg);
	CHECK(!mt->regs[xmm15].arg);      /* xmm15 not an arg reg */
	/* SysV integer argument order: rdi rsi rdx rcx r8 r9 */
	CHECK(mt->argreg[0] == mreg_id(mt, "rdi") && mt->argreg[1] == mreg_id(mt, "rsi") &&
	      mt->argreg[2] == mreg_id(mt, "rdx") && mt->argreg[3] == mreg_id(mt, "rcx") &&
	      mt->argreg[4] == mreg_id(mt, "r8") && mt->argreg[5] == mreg_id(mt, "r9"));
	/* machine target facts */
	CHECK(mt->ptrsize == 8);
	CHECK(mt->stackalign == 16);
	CHECK(mt->kl_in_reg);
	CHECK(mt->feat & MTF_SCALE_INDEX);
	CHECK(!(mt->feat & MTF_COND_EXEC));   /* arm feature, absent here */
	CHECK(strcmp(mreg_name(mt, (MReg)mt->nreg), "?") == 0);
	CHECK(strcmp(mreg_name(0, rax), "?") == 0);

	MFn *fn = mfn_new("regtest", 2);

	/* mfn_reg: created on demand, MV_REG, id==MReg, cached */
	MVal *rrax = mfn_reg(fn, mt, rax);
	CHECK(rrax != 0);
	CHECK(rrax->kind == MV_REG);
	CHECK(rrax->reg == rax);
	CHECK(rrax->id == rax);
	CHECK(rrax->type == MT_I64);           /* GPR is 64-bit int class */
	CHECK(fn->reg != 0 && fn->nreg == mt->nreg);
	CHECK(fn->reg[rax] == rrax);
	CHECK(mfn_reg(fn, mt, rax) == rrax);   /* same instance */
	CHECK(mfn_reg(fn, mt, xmm0) != 0);
	CHECK(mfn_reg(fn, mt, xmm0)->type == MT_F64);  /* FPR is f64 class */
	CHECK(mfn_reg(fn, mt, xmm0) != rrax);
	/* boundary: out-of-range registers are rejected */
	CHECK(mfn_reg(fn, mt, (MReg)-1) == 0);
	CHECK(mfn_reg(fn, mt, (MReg)mt->nreg) == 0);
	CHECK(mfn_reg(fn, 0, rax) == 0);

	mfn_free(fn);
}

static void
test_addressing(void)
{
	const MTargetM *mt = &mtarget_x86_64;
	MFn *fn = mfn_new("addrtest", 2);
	MVal *base = mfn_reg(fn, mt, mreg_id(mt, "rdi"));
	MVal *idx = mfn_reg(fn, mt, mreg_id(mt, "rax"));
	MConst *sym = mconst_addr(fn, "symbol", 0, false, true);

	MAddr a = maddr(base, idx, 4, 8);
	CHECK(a.base == base);
	CHECK(a.index == idx);
	CHECK(a.scale == 4);
	CHECK(a.off == 8);
	CHECK(a.offcon == 0);

	MAddr b = maddr(base, 0, 0, -16);     /* scale 0 -> default 1 */
	CHECK(b.scale == 1);
	CHECK(b.off == -16);
	CHECK(b.index == 0);

	MAddr c = maddr_sym(base, sym, 32);
	CHECK(c.offcon == sym);
	CHECK(c.off == 32);
	CHECK(c.base == base);

	mfn_free(fn);
}

static void
test_opcodes(void)
{
	CHECK(strcmp(mmop_name(MMOP_BLIT), "blit") == 0);
	CHECK(strcmp(mmop_name(MMOP_ALLOCA16), "alloca16") == 0);
	CHECK(strcmp(mmop_name(MMOP_CMP), "cmp") == 0);
	CHECK(strcmp(mmop_name(MMOP_SETCC), "setcc") == 0);
	CHECK(strcmp(mmop_name(MMOP_NONE), "none") == 0);
	/* semantic condition codes */
	CHECK(strcmp(mcc_name(MCC_EQ), "eq") == 0);
	CHECK(strcmp(mcc_name(MCC_CC), "cc") == 0);   /* unsigned less */
	CHECK(strcmp(mcc_name(MCC_HI), "hi") == 0);   /* unsigned greater */
	CHECK(strcmp(mcc_name(MCC_GE), "ge") == 0);   /* signed >= */
	CHECK(strcmp(mcc_name(MCC_AL), "al") == 0);   /* always */
}

static void
test_machine_fn(void)
{
	const MTargetM *mt = &mtarget_x86_64;
	MFn *fn = mfn_new("machine", 2);
	MFnM *fm = mfnm_new(fn, mt, "mmachine");
	CHECK(fm->host == fn);
	CHECK(fm->mt == mt);

	MVal *rax = mfn_reg(fn, mt, mreg_id(mt, "rax"));
	MVal *rcx = mfn_reg(fn, mt, mreg_id(mt, "rcx"));
	MVal *rdx = mfn_reg(fn, mt, mreg_id(mt, "rdx"));
	MVal *rdi = mfn_reg(fn, mt, mreg_id(mt, "rdi"));
	MVal *rsi = mfn_reg(fn, mt, mreg_id(mt, "rsi"));
	MVal *tmp = mval_new(fn, MV_TEMP, MT_I64, 0, "t0");
	MConst *c64 = mconst_int(fn, MT_I64, 64);

	MBlkM *b0 = mblkm_new(fm, "entry");
	MBlkM *b1 = mblkm_new(fm, "loop");
	MBlkM *b2 = mblkm_new(fm, "exit");
	mfnm_addblk(fm, b0);
	mfnm_addblk(fm, b1);
	mfnm_addblk(fm, b2);
	CHECK(fm->nblk == 3);
	CHECK(fm->start == b0);

	/* arithmetic: rax = add(rcx, rdx) */
	MInsM *in = maddm(fm, b0, MMOP_ADD, MT_I64, rax, rcx, rdx);
	CHECK(in->op == MMOP_ADD && in->dst == rax);
	CHECK(in->src[0] == rcx && in->src[1] == rdx);
	CHECK(in->blk == b0);

	/* immediate: rcx = add(rcx, 64) */
	MInsM *in2 = maddm_cst(fm, b0, MMOP_ADD, MT_I64, rcx, rcx, c64);
	CHECK(in2->cst == c64 && in2->src[0] == rcx);

	/* load with addressing mode: tmp = load[rdi + rcx*8 + 16] */
	MAddr a = maddr(rdi, rcx, 8, 16);
	MInsM *ld = maddm_addr(fm, b0, MMOP_LOAD, MT_I64, tmp, a, 0);
	CHECK(ld->op == MMOP_LOAD && ld->dst == tmp);
	CHECK(ld->addr.base == rdi && ld->addr.index == rcx);
	CHECK(ld->addr.scale == 8 && ld->addr.off == 16);

	/* store: store[rdi + 0] <- rax */
	MAddr sa = maddr(rdi, 0, 1, 0);
	MInsM *st = maddm_addr(fm, b0, MMOP_STORE, MT_I64, 0, sa, rax);
	CHECK(st->op == MMOP_STORE && st->src[0] == rax);
	CHECK(st->addr.base == rdi);

	/* setcc: tmp = setcc(eq) */
	MInsM *sc = maddm_cc(fm, b0, MMOP_SETCC, MT_I8, tmp, 0, 0, MCC_EQ);
	CHECK(sc->op == MMOP_SETCC && sc->cc == MCC_EQ);

	/* blit: aggregate copy dst=rdi, src=rsi, size=32 */
	MConst *c32 = mconst_int(fn, MT_I32, 32);
	MInsM *bl = maddm_blit(fm, b0, rdi, rsi, c32);
	CHECK(bl->op == MMOP_BLIT);
	CHECK(bl->src[0] == rdi && bl->src[1] == rsi);
	CHECK(bl->cst == c32);

	/* terminator: jcc ne b0 -> b1 / b2 */
	mfnm_term(fm, b0, MMOP_JCC, 0, b1, b2, MCC_NE);
	CHECK(b0->term.op == MMOP_JCC && b0->term.cc == MCC_NE);
	CHECK(b0->s1 == b1 && b0->s2 == b2);

	/* ret value */
	mfnm_term(fm, b2, MMOP_RET, rax, 0, 0, MCC_NONE);
	CHECK(b2->term.op == MMOP_RET && b2->term.src[0] == rax);

	/* dump must be non-empty and mention the machine constructs */
	FILE *snap = tmpfile();
	CHECK(snap != 0);
	mfnm_dump(fm, snap);
	rewind(snap);
	char buf[4096];
	size_t got = fread(buf, 1, sizeof buf - 1, snap);
	buf[got] = 0;
	fclose(snap);
	CHECK(got > 0);
	CHECK(strstr(buf, "machine function mmachine") != 0);
	CHECK(strstr(buf, "blit") != 0);
	CHECK(strstr(buf, "setcc") != 0);
	CHECK(strstr(buf, "cc=eq") != 0);       /* setcc condition code */
	CHECK(strstr(buf, "jcc ne -> loop / exit") != 0);

	/* machine instruction id sequence within a block */
	CHECK(b0->nins == 6);
	CHECK(b0->ins[0].id == 0 && b0->ins[5].id == 5);

	mfnm_free(fm);
	mfn_free(fn);
}

int
main(void)
{
	test_registers();
	test_addressing();
	test_opcodes();
	test_machine_fn();

	printf("machine_test: %d passed, %d failed\n", npass, nfail);
	return nfail ? 1 : 0;
}
