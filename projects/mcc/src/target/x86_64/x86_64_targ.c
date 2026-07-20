#include "x86_64.h"

Amd64Op amd64_op[NOp] = {
#define O(op, t, x) [O##op] =
#define X(nm, zf, lf) { nm, zf, lf, },
	#include "ir_ops.h"
};

static int
amd64_memargs(int op)
{
	return amd64_op[op].nmem;
}

#define AMD64_COMMON \
	.gpr0 = RAX, \
	.ngpr = NGPR, \
	.fpr0 = XMM0, \
	.nfpr = NFPR, \
	.rglob = BIT(RBP) | BIT(RSP), \
	.nrglob = 2, \
	.kl_in_reg = 1, \
	.memargs = amd64_memargs, \
	.abi0 = elimsb, \
	.isel = amd64_isel, \
	.cansel = 1,

Target T_amd64_sysv = {
	.name = "x86_64",
	.emitfin = elf_emitfin,
	.asloc = ".L",
	.abi1 = amd64_sysv_abi,
	.rsave = amd64_sysv_rsave,
	.nrsave = {NGPS_SYSV, NFPS},
	.retregs = amd64_sysv_retregs,
	.argregs = amd64_sysv_argregs,
	.emitfn = amd64_sysv_emitfn,
	AMD64_COMMON
};
