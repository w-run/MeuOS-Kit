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
	/* i386 has no *allocatable* floating-point register class.  x87 is a
	 * stack machine (ST(0)..ST(7) form a push/pop evaluation stack, not a
	 * flat register file), so it cannot be modelled as a QBE register
	 * class the way SSE/AdvSIMD/FP are on the 64-bit targets.  The
	 * deliberate, correct design is therefore:
	 *   - nfpr == 0  (no FPR class at all), and
	 *   - every Ks/Kd temporary is stack-slot resident; x87 is used only
	 *     as a transient evaluation stack (fld/fop/fstp) between slots.
	 * rega.c/ralloctry() short-circuit KBASE==1 temps to SLOT() when
	 * nfpr==0, and i386_isel.c passes Ks/Kd slot temps through untouched,
	 * so floats never reach register allocation.  fpr0 is thus 0 (an
	 * index below the GPR range, never a valid machine register): the
	 * FPR interval [fpr0, fpr0+nfpr) is empty and collides with nothing.
	 * Do NOT "declare ST(0..7) as an FPR class" — that would require a
	 * register-to-stack allocation pass QBE does not have. */ \
	.fpr0 = 0, \
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
