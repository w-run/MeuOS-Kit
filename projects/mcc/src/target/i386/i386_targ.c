#include "i386.h"

I386Op i386_op[NOp] = {
#define O(op, t, x) [O##op] =
#define X(nm, zf, lf) { nm, zf, lf, },
	#include "ir_ops.h"
};

static int
i386_memargs(int op)
{
	return i386_op[op].nmem;
}

#define I386_COMMON \
	.gpr0 = EAX, \
	.ngpr = NGPR, \
	.fpr0 = EAX, /* no FPR, reuse EAX as placeholder */ \
	.nfpr = NFPR, \
	.rglob = BIT(EBP) | BIT(ESP), \
	.nrglob = 2, \
	.kl_in_reg = 0, /* i386 has no 64-bit GPRs; Kl values always live in slots.
			 * spill.c must never keep Kl temps in v (the live register
			 * set) so that Kl operand/result handling is done through
			 * slot-based Kw decompositions emitted by i386_isel. */ \
	.memargs = i386_memargs, \
	.abi0 = elimsb, \
	.isel = i386_isel, \
	.cansel = 1,

Target T_i386_sysv = {
	.name = "i386",
	.emitfin = elf_emitfin,
	.asloc = ".L",
	.abi1 = i386_sysv_abi,
	.rsave = i386_sysv_rsave,
	.nrsave = {NGPS, NFPS},
	.retregs = i386_sysv_retregs,
	.argregs = i386_sysv_argregs,
	.emitfn = i386_sysv_emitfn,
	I386_COMMON
};
