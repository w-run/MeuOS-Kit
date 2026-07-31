#include "arm.h"

int armv7_op[NOp] = {
#define O(op, t, x) [O##op] =
#define V(imm) { imm },
#include "ir_ops.h"
};

/* D1 is reserved as the FP conversion scratch (see arm.h NFPS): rega's
 * caller-saved FP pool is {D0, D2..D7}, keeping D1 free for the
 * vmov s2/vcvt sequences emitted for int<->float casts. */
int arm32_rsave[] = {
	R0, R1, R2, R3, R12, D0, D2, D3, D4, D5, D6, D7, -1
};
int arm32_rclob[] = {
	R4, R5, R6, R7, R8, R9, R11,
	D8, D9, D10, D11, D12, D13, D14, D15, -1
};

#define RGLOB 0

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
