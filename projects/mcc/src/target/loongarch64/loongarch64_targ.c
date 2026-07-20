#include "loongarch64.h"

La64Op la64_op[NOp] = {
#define O(op, t, x) [O##op] =
#define V(imm) { imm },
#include "ir_ops.h"
};

int la64_rsave[] = {
	T0, T1, T2, T3, T4, T5, T6, T7, T8,
	A0, A1, A2, A3, A4, A5, A6, A7,
	FA0, FA1, FA2,  FA3,  FA4, FA5, FA6, FA7,
	FT0, FT1, FT2, FT3, FT4, FT5, FT6, FT7,
	FT8, FT9, FT10, FT11, FT12, FT13, FT14, FT15,
	-1
};
int la64_rclob[] = {
	S0, S1, S2, S3, S4, S5, S6, S7, S8,
	FS0, FS1, FS2, FS3, FS4, FS5, FS6, FS7,
	-1
};

#define RGLOB (BIT(FP) | BIT(SP) | BIT(TP) | BIT(RA))

static int
la64_memargs(int op)
{
	(void)op;
	return 0;
}

Target T_la64 = {
	.name = "loongarch64",
	.gpr0 = T0,
	.ngpr = NGPR,
	.fpr0 = FT0,
	.nfpr = NFPR,
	.rglob = RGLOB,
	.nrglob = 4,
	.kl_in_reg = 1, /* la64 has 64-bit GPRs (LP64); Kl values may live in registers. */
	.rsave = la64_rsave,
	.nrsave = {NGPS, NFPS},
	.retregs = la64_retregs,
	.argregs = la64_argregs,
	.memargs = la64_memargs,
	.abi0 = elimsb,
	.abi1 = la64_abi,
	.isel = la64_isel,
	.emitfn = la64_emitfn,
	.emitfin = elf_emitfin,
	.asloc = ".L",
	.cansel = 0,
};

MAKESURE(rsave_size_ok, sizeof la64_rsave == (NGPS+NFPS+1) * sizeof(int));
MAKESURE(rclob_size_ok, sizeof la64_rclob == (NCLR+1) * sizeof(int));
