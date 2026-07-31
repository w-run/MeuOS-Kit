#include "arm.h"

int armv7_op[NOp] = {
#define O(op, t, x) [O##op] =
#define V(imm) { imm },
#include "ir_ops.h"
};

/* Caller-saved pool: the 4 AAPCS argument GPRs (R0-R3) + R12, and the
 * 8 FP argument/return registers (D0-D7).  D1 is NOT excluded: AAPCS
 * marks it caller-saved, so a value living in D1 (a 2nd double
 * argument, or a temp rega allocated there) must be saved across calls.
 * The int<->float conversion scratch is s16 (the low half of D8),
 * which rega never allocates, so conversions never clobber a live
 * value. */
int arm32_rsave[] = {
	R0, R1, R2, R3, R12, D0, D1, D2, D3, D4, D5, D6, D7, -1
};
int arm32_rclob[] = {
	R4, R5, R6, R7, R8, R9, R11,
	D8, D9, D10, D11, D12, D13, D14, D15, -1
};

#define RGLOB 0

/* D8 is reserved as the FP conversion scratch (s16): the emitter
 * lowers int<->float casts to `vmov s16, rN; vcvt.f64.s32 dN, s16`
 * (and the reverse), and s16 aliases D8.  rega never allocates D8
 * (T.reserved), so a conversion cannot clobber a live value. */
#define RFPSCRATCH BIT(D8)

static int
arm32_memargs(int op)
{
	(void)op;
	return 0;
}

Target T_arm32 = {
	.name = "armv7",
	.gpr0 = R0,
	.ngpr = NGPR,
	.fpr0 = D0,
	.nfpr = NFPR,
	.rglob = RGLOB,
	.nrglob = 0,
	.reserved = RFPSCRATCH,
	.kl_in_reg = 0,
	.rsave = arm32_rsave,
	.nrsave = {NGPS, NFPS},
	.retregs = arm32_retregs,
	.argregs = arm32_argregs,
	.memargs = arm32_memargs,
	.abi0 = elimsb,
	.abi1 = arm32_abi,
	.isel = arm32_isel,
	.emitfn = arm32_emitfn,
	.emitfin = elf_emitfin,
	.asloc = ".L",
	.cansel = 0,
};

MAKESURE(rsave_size_ok, sizeof arm32_rsave == (NGPS+NFPS+1) * sizeof(int));
MAKESURE(rclob_size_ok, sizeof arm32_rclob == (NCLR+1) * sizeof(int));
MAKESURE(rglob_not_stacked, (RGLOB & (BIT(R0) - 1)) == 0);
