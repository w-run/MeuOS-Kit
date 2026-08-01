/* x86_64_m.h — x86-64 machine target for the MIR machine layer.
 *
 * Deliberately independent of the QBE-derived include/x86_64.h: this
 * header only pulls in mir.h, so the MIR-native backend (P2 ABI, P3
 * isel, ...) can be built without the LIR Fn/Ins/Ref structures.
 */
#ifndef MCC_X86_64_M_H
#define MCC_X86_64_M_H

#include "mir.h"

/* x86-64 physical register ids — indices into mtarget_x86_64.regs[]. */
enum X64MReg {
	X64MREG_NONE = -1,
	X64MREG_RAX = 0, X64MREG_RCX, X64MREG_RDX, X64MREG_RSI, X64MREG_RDI,
	X64MREG_R8, X64MREG_R9, X64MREG_R10, X64MREG_R11,
	X64MREG_RBX, X64MREG_R12, X64MREG_R13, X64MREG_R14, X64MREG_R15,
	X64MREG_RBP, X64MREG_RSP,
	X64MREG_XMM0, X64MREG_XMM1, X64MREG_XMM2, X64MREG_XMM3,
	X64MREG_XMM4, X64MREG_XMM5, X64MREG_XMM6, X64MREG_XMM7,
	X64MREG_XMM8, X64MREG_XMM9, X64MREG_XMM10, X64MREG_XMM11,
	X64MREG_XMM12, X64MREG_XMM13, X64MREG_XMM14, X64MREG_XMM15,
	X64MREG_NREG,
};

extern const MTargetM mtarget_x86_64;

/* ---- SysV aggregate classification (shared with the ABI lowering and
 * its unit tests) ------------------------------------------------------- */

typedef struct MAClass {
	MTypeDesc *td;
	int inmem;              /* 0 = in regs, 1 = stack param, 2 = stack arg */
	int align;              /* log2 alignment (3 = 8, 4 = 16) */
	uint32_t size;          /* size rounded up to alignment (min 8) */
	MType cls[2];           /* per-8-byte class: MT_NONE / MT_I64 / MT_F64 */
	MVal *ref[2];           /* argument/return registers assigned */
} MAClass;

/* SysV aggregate classification (port of x86_64_sysv.c typclass()).
 * Exported for unit tests and for the ABI lowering. */
void mabi_typclass(MAClass *a, MTypeDesc *td);

/* Classify one call site (port of argsclass()).  Returns the packed
 * register-usage code used for varargs bookkeeping. */
int mabi_argsclass(MFnM *fm, MInsM *m, int n, MAClass *ac, MAClass *aret);

/* P2 parallel-validation entry: lower a whole MFn to MFnM, run the SysV
 * ABI lowering, and dump the result (bridge still produces the asm). */
void mfnm_backend_x86_64(MFn *mf);

#endif /* MCC_X86_64_M_H */
