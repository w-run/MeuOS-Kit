/* machine.c — MIR machine layer (P1).
 *
 * The MIR-native backend foundation: physical registers as MVal (MV_REG),
 * addressing modes (MAddr), machine opcodes (MMOP), condition codes (MCC),
 * and machine functions/blocks/instructions (MFnM/MBlkM/MInsM).
 *
 * Purity rule (team decision): these are NEW MIR-native types.  No QBE
 * Fn/Ins/Ref, no Ref bitfield packing, no fill* pass names.  The existing
 * MIR pipeline (MFn -> lir_bridge -> LIR) is untouched; the machine layer
 * lives in its own MMOP/MREG/MFnM namespace and only consumes the shared
 * MVal/MConst pool through the owning MFn.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mir.h"
#include "x86_64_m.h"
#include "riscv64_m.h"

/* ---- riscv64 machine target (register descriptions) -------------------- */
/* 32 GPRs (x0-x31, ABI names) + 32 FPRs (f0-f31).  Caller-saved: t0-6,
 * a0-7, f0-7, fa0-7, f28-31; callee-saved: s1-s11, f8-9, f18-27.  The
 * emitter uses t0-t2 (5-7) and t3-t6 (28-31) as scratch. */

static const MRegInfo rv64_regs[RV64MREG_NREG] = {
	[RV64MREG_ZERO] = { "zero", MRC_GPR, true,  false, false },
	[RV64MREG_RA]   = { "ra",   MRC_GPR, true,  false, false },
	[RV64MREG_SP]   = { "sp",   MRC_GPR, true,  false, false },
	[RV64MREG_GP]   = { "gp",   MRC_GPR, true,  false, false },
	[RV64MREG_TP]   = { "tp",   MRC_GPR, true,  false, false },
	[RV64MREG_T0]   = { "t0",   MRC_GPR, true,  false, false },
	[RV64MREG_T1]   = { "t1",   MRC_GPR, true,  false, false },
	[RV64MREG_T2]   = { "t2",   MRC_GPR, true,  false, false },
	[RV64MREG_FP]   = { "fp",   MRC_GPR, true,  false, false },
	[RV64MREG_S1]   = { "s1",   MRC_GPR, false, true,  false },
	[RV64MREG_A0]   = { "a0",   MRC_GPR, true,  false, true  },
	[RV64MREG_A1]   = { "a1",   MRC_GPR, true,  false, true  },
	[RV64MREG_A2]   = { "a2",   MRC_GPR, true,  false, true  },
	[RV64MREG_A3]   = { "a3",   MRC_GPR, true,  false, true  },
	[RV64MREG_A4]   = { "a4",   MRC_GPR, true,  false, true  },
	[RV64MREG_A5]   = { "a5",   MRC_GPR, true,  false, true  },
	[RV64MREG_A6]   = { "a6",   MRC_GPR, true,  false, true  },
	[RV64MREG_A7]   = { "a7",   MRC_GPR, true,  false, true  },
	[RV64MREG_S2]   = { "s2",   MRC_GPR, false, true,  false },
	[RV64MREG_S3]   = { "s3",   MRC_GPR, false, true,  false },
	[RV64MREG_S4]   = { "s4",   MRC_GPR, false, true,  false },
	[RV64MREG_S5]   = { "s5",   MRC_GPR, false, true,  false },
	[RV64MREG_S6]   = { "s6",   MRC_GPR, false, true,  false },
	[RV64MREG_S7]   = { "s7",   MRC_GPR, false, true,  false },
	[RV64MREG_S8]   = { "s8",   MRC_GPR, false, true,  false },
	[RV64MREG_S9]   = { "s9",   MRC_GPR, false, true,  false },
	[RV64MREG_S10]  = { "s10",  MRC_GPR, false, true,  false },
	[RV64MREG_S11]  = { "s11",  MRC_GPR, false, true,  false },
	[RV64MREG_T3]   = { "t3",   MRC_GPR, true,  false, false },
	[RV64MREG_T4]   = { "t4",   MRC_GPR, true,  false, false },
	[RV64MREG_T5]   = { "t5",   MRC_GPR, true,  false, false },
	[RV64MREG_T6]   = { "t6",   MRC_GPR, true,  false, false },
	[RV64MREG_F0]   = { "f0",   MRC_FPR, true,  false, false },
	[RV64MREG_F1]   = { "f1",   MRC_FPR, true,  false, false },
	[RV64MREG_F2]   = { "f2",   MRC_FPR, true,  false, false },
	[RV64MREG_F3]   = { "f3",   MRC_FPR, true,  false, false },
	[RV64MREG_F4]   = { "f4",   MRC_FPR, true,  false, false },
	[RV64MREG_F5]   = { "f5",   MRC_FPR, true,  false, false },
	[RV64MREG_F6]   = { "f6",   MRC_FPR, true,  false, false },
	[RV64MREG_F7]   = { "f7",   MRC_FPR, true,  false, false },
	[RV64MREG_F8]   = { "f8",   MRC_FPR, false, true,  false },
	[RV64MREG_F9]   = { "f9",   MRC_FPR, false, true,  false },
	[RV64MREG_FA0]  = { "fa0",  MRC_FPR, true,  false, true  },
	[RV64MREG_FA1]  = { "fa1",  MRC_FPR, true,  false, true  },
	[RV64MREG_FA2]  = { "fa2",  MRC_FPR, true,  false, true  },
	[RV64MREG_FA3]  = { "fa3",  MRC_FPR, true,  false, true  },
	[RV64MREG_FA4]  = { "fa4",  MRC_FPR, true,  false, true  },
	[RV64MREG_FA5]  = { "fa5",  MRC_FPR, true,  false, true  },
	[RV64MREG_FA6]  = { "fa6",  MRC_FPR, true,  false, true  },
	[RV64MREG_FA7]  = { "fa7",  MRC_FPR, true,  false, true  },
	[RV64MREG_F18]  = { "f18",  MRC_FPR, false, true,  false },
	[RV64MREG_F19]  = { "f19",  MRC_FPR, false, true,  false },
	[RV64MREG_F20]  = { "f20",  MRC_FPR, false, true,  false },
	[RV64MREG_F21]  = { "f21",  MRC_FPR, false, true,  false },
	[RV64MREG_F22]  = { "f22",  MRC_FPR, false, true,  false },
	[RV64MREG_F23]  = { "f23",  MRC_FPR, false, true,  false },
	[RV64MREG_F24]  = { "f24",  MRC_FPR, false, true,  false },
	[RV64MREG_F25]  = { "f25",  MRC_FPR, false, true,  false },
	[RV64MREG_F26]  = { "f26",  MRC_FPR, false, true,  false },
	[RV64MREG_F27]  = { "f27",  MRC_FPR, false, true,  false },
	[RV64MREG_F28]  = { "f28",  MRC_FPR, true,  false, false },
	[RV64MREG_F29]  = { "f29",  MRC_FPR, true,  false, false },
	[RV64MREG_F30]  = { "f30",  MRC_FPR, true,  false, false },
	[RV64MREG_F31]  = { "f31",  MRC_FPR, true,  false, false },
};

/* RISC-V LP64D argument order: 8 integer (a0-a7), 8 FP (fa0-fa7). */
const int rv64_argreg[17] = {
	RV64MREG_A0, RV64MREG_A1, RV64MREG_A2, RV64MREG_A3,
	RV64MREG_A4, RV64MREG_A5, RV64MREG_A6, RV64MREG_A7,
	RV64MREG_FA0, RV64MREG_FA1, RV64MREG_FA2, RV64MREG_FA3,
	RV64MREG_FA4, RV64MREG_FA5, RV64MREG_FA6, RV64MREG_FA7,
	-1
};
static const int rv64_rsave[] = {
	RV64MREG_T0, RV64MREG_T1, RV64MREG_T2,
	RV64MREG_A0, RV64MREG_A1, RV64MREG_A2, RV64MREG_A3,
	RV64MREG_A4, RV64MREG_A5, RV64MREG_A6, RV64MREG_A7,
	RV64MREG_T3, RV64MREG_T4, RV64MREG_T5, RV64MREG_T6,
	RV64MREG_F0, RV64MREG_F1, RV64MREG_F2, RV64MREG_F3,
	RV64MREG_F4, RV64MREG_F5, RV64MREG_F6, RV64MREG_F7,
	RV64MREG_FA0, RV64MREG_FA1, RV64MREG_FA2, RV64MREG_FA3,
	RV64MREG_FA4, RV64MREG_FA5, RV64MREG_FA6, RV64MREG_FA7,
	RV64MREG_F28, RV64MREG_F29, RV64MREG_F30, RV64MREG_F31,
	-1
};
static const int rv64_rclob[] = {
	RV64MREG_S1, RV64MREG_S2, RV64MREG_S3, RV64MREG_S4, RV64MREG_S5,
	RV64MREG_S6, RV64MREG_S7, RV64MREG_S8, RV64MREG_S9, RV64MREG_S10,
	RV64MREG_S11,
	RV64MREG_F8, RV64MREG_F9,
	RV64MREG_F18, RV64MREG_F19, RV64MREG_F20, RV64MREG_F21,
	RV64MREG_F22, RV64MREG_F23, RV64MREG_F24, RV64MREG_F25,
	RV64MREG_F26, RV64MREG_F27,
	-1
};

/* P3a ABI lowering for riscv64 LP64D (riscv64_mabi.c). */
extern void mfnm_abi_riscv64(MFnM *fm);
const MTargetM mtarget_riscv64 = {
	.name = "riscv64",
	.nreg = RV64MREG_NREG,
	.regs = rv64_regs,
	.gpr0 = RV64MREG_ZERO,
	.ngpr = 32,
	.fpr0 = RV64MREG_F0,
	.nfpr = 32,
	/* never allocated: zero/ra/sp/gp/tp/fp (frame) */
	.rglob = (1ull << RV64MREG_ZERO) | (1ull << RV64MREG_RA) |
	         (1ull << RV64MREG_SP) | (1ull << RV64MREG_GP) |
	         (1ull << RV64MREG_TP) | (1ull << RV64MREG_FP),
	.reserved = 0,
	.argreg = rv64_argreg,
	.rsave = rv64_rsave,
	.rclob = rv64_rclob,
	.ptrsize = 8,
	.stackalign = 16,
	.kl_in_reg = true,
	.feat = 0,               /* no cmov, no scale-index addressing */
	.sret_reg = RV64MREG_A0,
	.abi = mfnm_abi_riscv64,
	/* emitter temporaries: t0/t1 (addressing scratch) + t6 (large
	 * offsets / dynamic alloca) + f28/f29 (float scratch) — never
	 * handed to the allocator. */
	.scratch = (1ull << RV64MREG_T0) | (1ull << RV64MREG_T1) |
	           (1ull << RV64MREG_T2) | (1ull << RV64MREG_T6) |
	           (1ull << RV64MREG_F28) | (1ull << RV64MREG_F29),
};

/* ---- x86-64 machine target (register descriptions) --------------------- */

static const MRegInfo x64_regs[X64MREG_NREG] = {
	/* GPR, caller-saved */
	[X64MREG_RAX] = { "rax", MRC_GPR, true,  false, false },
	[X64MREG_RCX] = { "rcx", MRC_GPR, true,  false, true  }, /* 4th int arg */
	[X64MREG_RDX] = { "rdx", MRC_GPR, true,  false, true  }, /* 3rd int arg */
	[X64MREG_RSI] = { "rsi", MRC_GPR, true,  false, true  }, /* 2nd int arg */
	[X64MREG_RDI] = { "rdi", MRC_GPR, true,  false, true  }, /* 1st int arg */
	[X64MREG_R8]  = { "r8",  MRC_GPR, true,  false, true  }, /* 5th int arg */
	[X64MREG_R9]  = { "r9",  MRC_GPR, true,  false, true  }, /* 6th int arg */
	[X64MREG_R10] = { "r10", MRC_GPR, true,  false, false },
	[X64MREG_R11] = { "r11", MRC_GPR, true,  false, false },
	/* GPR, callee-saved */
	[X64MREG_RBX] = { "rbx", MRC_GPR, false, true,  false },
	[X64MREG_R12] = { "r12", MRC_GPR, false, true,  false },
	[X64MREG_R13] = { "r13", MRC_GPR, false, true,  false },
	[X64MREG_R14] = { "r14", MRC_GPR, false, true,  false },
	[X64MREG_R15] = { "r15", MRC_GPR, false, true,  false },
	/* frame / stack */
	[X64MREG_RBP] = { "rbp", MRC_GPR, false, false, false },
	[X64MREG_RSP] = { "rsp", MRC_GPR, false, false, false },
	/* XMM, caller-saved under SysV */
	[X64MREG_XMM0]  = { "xmm0",  MRC_FPR, true, false, true  },
	[X64MREG_XMM1]  = { "xmm1",  MRC_FPR, true, false, true  },
	[X64MREG_XMM2]  = { "xmm2",  MRC_FPR, true, false, true  },
	[X64MREG_XMM3]  = { "xmm3",  MRC_FPR, true, false, true  },
	[X64MREG_XMM4]  = { "xmm4",  MRC_FPR, true, false, true  },
	[X64MREG_XMM5]  = { "xmm5",  MRC_FPR, true, false, true  },
	[X64MREG_XMM6]  = { "xmm6",  MRC_FPR, true, false, true  },
	[X64MREG_XMM7]  = { "xmm7",  MRC_FPR, true, false, true  },
	[X64MREG_XMM8]  = { "xmm8",  MRC_FPR, true, false, false },
	[X64MREG_XMM9]  = { "xmm9",  MRC_FPR, true, false, false },
	[X64MREG_XMM10] = { "xmm10", MRC_FPR, true, false, false },
	[X64MREG_XMM11] = { "xmm11", MRC_FPR, true, false, false },
	[X64MREG_XMM12] = { "xmm12", MRC_FPR, true, false, false },
	[X64MREG_XMM13] = { "xmm13", MRC_FPR, true, false, false },
	[X64MREG_XMM14] = { "xmm14", MRC_FPR, true, false, false },
	[X64MREG_XMM15] = { "xmm15", MRC_FPR, true, false, false },
};

/* SysV argument order: 6 integer (rdi rsi rdx rcx r8 r9), 8 SSE (xmm0-7) */
static const int x64_argreg[] = {
	X64MREG_RDI, X64MREG_RSI, X64MREG_RDX, X64MREG_RCX,
	X64MREG_R8, X64MREG_R9,
	X64MREG_XMM0, X64MREG_XMM1, X64MREG_XMM2, X64MREG_XMM3,
	X64MREG_XMM4, X64MREG_XMM5, X64MREG_XMM6, X64MREG_XMM7,
	-1
};

/* P2 ABI lowering for x86_64 SysV (src/target/x86_64/x86_64_mabi.c). */
extern void mfnm_abi_x86_64(MFnM *fm);
static const int x64_rsave[] = {
	X64MREG_RAX, X64MREG_RCX, X64MREG_RDX, X64MREG_RSI, X64MREG_RDI,
	X64MREG_R8, X64MREG_R9, X64MREG_R10, X64MREG_R11,
	X64MREG_XMM0, X64MREG_XMM1, X64MREG_XMM2, X64MREG_XMM3,
	X64MREG_XMM4, X64MREG_XMM5, X64MREG_XMM6, X64MREG_XMM7,
	X64MREG_XMM8, X64MREG_XMM9, X64MREG_XMM10, X64MREG_XMM11,
	X64MREG_XMM12, X64MREG_XMM13, X64MREG_XMM14, X64MREG_XMM15,
	-1
};
static const int x64_rclob[] = {
	X64MREG_RBX, X64MREG_R12, X64MREG_R13, X64MREG_R14, X64MREG_R15, -1
};

const MTargetM mtarget_x86_64 = {
	.name = "x86_64",
	.nreg = X64MREG_NREG,
	.regs = x64_regs,
	.gpr0 = X64MREG_RAX,
	.ngpr = X64MREG_RSP - X64MREG_RAX + 1,   /* 16 GPR incl. rbp/rsp */
	.fpr0 = X64MREG_XMM0,
	.nfpr = 16,
	.rglob = (1ull << X64MREG_RBP) | (1ull << X64MREG_RSP),
	.reserved = 0,
	.argreg = x64_argreg,
	.rsave = x64_rsave,
	.rclob = x64_rclob,
	.ptrsize = 8,
	.stackalign = 16,
	.kl_in_reg = true,
	.feat = MTF_SCALE_INDEX | MTF_CMOV,
	.sret_reg = X64MREG_RDI,
	.abi = mfnm_abi_x86_64,
	/* emitter temporaries: rax (accumulator), rcx (divisor/shift), rdx
	 * (division remainder), r9 (large immediates), r10/r11 (addressing),
	 * xmm0 (float accumulator) — never handed to the allocator. */
	.scratch = (1ull << X64MREG_RAX) | (1ull << X64MREG_RCX) |
	           (1ull << X64MREG_RDX) | (1ull << X64MREG_R9) |
	           (1ull << X64MREG_R10) | (1ull << X64MREG_R11) |
	           (1ull << X64MREG_XMM0),
};

const char *
mreg_name(const MTargetM *mt, MReg r)
{
	if (!mt || r < 0 || r >= mt->nreg)
		return "?";
	return mt->regs[r].name;
}

int
mreg_id(const MTargetM *mt, const char *name)
{
	if (!mt || !name)
		return -1;
	for (uint32_t i = 0; i < mt->nreg; i++)
		if (mt->regs[i].name && strcmp(mt->regs[i].name, name) == 0)
			return (int)i;
	return -1;
}

/* Return the MVal for a physical register of `mt`, creating it on first
 * use.  Register values live in fn->reg[] and stay OUT of the SSA val
 * table (they carry no def/use chains).  id == MReg so dumps stay
 * readable. */
MVal *
mfn_reg(MFn *fn, const MTargetM *mt, MReg r)
{
	if (!mt || r < 0 || r >= mt->nreg)
		return 0;
	if (!fn->reg) {
		fn->reg = calloc(mt->nreg, sizeof *fn->reg);
		fn->nreg = mt->nreg;
	}
	if (!fn->reg[r]) {
		MVal *v = calloc(1, sizeof *v);
		v->id = (uint32_t)r;
		v->kind = MV_REG;
		/* machine word type follows the target pointer width: i386/arm
		 * (ILP32) registers are 32-bit, LP64 targets are 64-bit. */
		v->type = mt->regs[r].cls == MRC_FPR ? MT_F64 :
		          (mt->ptrsize == 4 ? MT_I32 : MT_I64);
		v->reg = r;
		v->slot = -1;
		v->hint = -1;
		v->lirtmp = -1;
		v->name = mx_strdup(mt->regs[r].name);
		fn->reg[r] = v;
	}
	return fn->reg[r];
}

/* ---- addressing modes -------------------------------------------------- */

MAddr
maddr(MVal *base, MVal *index, uint8_t scale, int64_t off)
{
	MAddr a = {0};
	a.base = base;
	a.index = index;
	a.scale = scale == 0 ? 1 : scale;
	a.off = off;
	return a;
}

MAddr
maddr_sym(MVal *base, MConst *offcon, int64_t off)
{
	MAddr a = {0};
	a.base = base;
	a.offcon = offcon;
	a.off = off;
	return a;
}

/* ---- opcode / condition names ----------------------------------------- */

const char *
mmop_name(MMOP op)
{
	static const char *names[MMOP_NOP] = {
		[MMOP_NONE]     = "none",
		[MMOP_MOV]      = "mov",
		[MMOP_MOVSX]    = "movsx",
		[MMOP_MOVZX]    = "movzx",
		[MMOP_LEA]      = "lea",
		[MMOP_PUSH]     = "push",
		[MMOP_POP]      = "pop",
		[MMOP_ADD]      = "add",
		[MMOP_SUB]      = "sub",
		[MMOP_MUL]      = "mul",
		[MMOP_AND]      = "and",
		[MMOP_OR]       = "or",
		[MMOP_XOR]      = "xor",
		[MMOP_SHL]      = "shl",
		[MMOP_SHR]      = "shr",
		[MMOP_SAR]      = "sar",
		[MMOP_NEG]      = "neg",
		[MMOP_NOT]      = "not",
		[MMOP_DIV]      = "div",
		[MMOP_UDIV]     = "udiv",
		[MMOP_REM]      = "rem",
		[MMOP_UREM]     = "urem",
		[MMOP_FADD]     = "fadd",
		[MMOP_FSUB]     = "fsub",
		[MMOP_FMUL]     = "fmul",
		[MMOP_FDIV]     = "fdiv",
		[MMOP_FNEG]     = "fneg",
		[MMOP_FSQRT]    = "fsqrt",
		[MMOP_CVTSI2SS] = "cvtsi2ss",
		[MMOP_CVTSI2SD] = "cvtsi2sd",
		[MMOP_CVTSI2SS_U] = "cvtsi2ss_u",
		[MMOP_CVTSI2SD_U] = "cvtsi2sd_u",
		[MMOP_CVTSS2SD] = "cvtss2sd",
		[MMOP_CVTSD2SS] = "cvtsd2ss",
		[MMOP_CVTTSS2SI]= "cvttss2si",
		[MMOP_CVTTSD2SI]= "cvttsd2si",
		[MMOP_LOAD]     = "load",
		[MMOP_LOAD_S8]  = "load_s8",
		[MMOP_LOAD_S16] = "load_s16",
		[MMOP_LOAD_S32] = "load_s32",
		[MMOP_LOAD_Z8]  = "load_z8",
		[MMOP_LOAD_Z16] = "load_z16",
		[MMOP_LOAD_Z32] = "load_z32",
		[MMOP_STORE]    = "store",
		[MMOP_BLIT]     = "blit",
		[MMOP_ALLOCA4]  = "alloca4",
		[MMOP_ALLOCA8]  = "alloca8",
		[MMOP_ALLOCA16] = "alloca16",
		[MMOP_SALLOC]   = "salloc",
		[MMOP_VASTART]  = "vastart",
		[MMOP_VAARG]    = "vaarg",
		[MMOP_CMP]      = "cmp",
		[MMOP_TEST]     = "test",
		[MMOP_SETCC]    = "setcc",
		[MMOP_CMOV]     = "cmov",
		[MMOP_SETCCR]   = "setccr",
		[MMOP_PARM]     = "parm",
		[MMOP_ARG]      = "arg",
		[MMOP_JMP]      = "jmp",
		[MMOP_JCC]      = "jcc",
		[MMOP_CALL]     = "call",
		[MMOP_RET]      = "ret",
	};
	return (unsigned)op < MMOP_NOP ? names[op] : "?";
}

const char *
mcc_name(MCC cc)
{
	static const char *names[MCC_NCC] = {
		[MCC_NONE] = "none",
		[MCC_EQ]  = "eq",  [MCC_NE] = "ne",
		[MCC_CS]  = "cs",  [MCC_CC] = "cc",
		[MCC_MI]  = "mi",  [MCC_PL] = "pl",
		[MCC_VS]  = "vs",  [MCC_VC] = "vc",
		[MCC_HI]  = "hi",  [MCC_LS] = "ls",
		[MCC_GE]  = "ge",  [MCC_LT] = "lt",
		[MCC_GT]  = "gt",  [MCC_LE] = "le",
		[MCC_AL]  = "al",
	};
	return (unsigned)cc < MCC_NCC ? names[cc] : "?";
}

/* ---- machine function / block construction ----------------------------- */

MFnM *
mfnm_new(MFn *host, const MTargetM *mt, const char *name)
{
	MFnM *fm = calloc(1, sizeof *fm);
	fm->name = name ? mx_strdup(name) : 0;
	fm->mt = mt ? mt : &mtarget_x86_64;
	fm->host = host;
	return fm;
}

MBlkM *
mblkm_new(MFnM *fm, const char *name)
{
	MBlkM *b = calloc(1, sizeof *b);
	b->id = fm->nblk;
	b->name = name ? mx_strdup(name) : 0;
	b->term.op = MMOP_NONE;
	return b;
}

void
mfnm_addblk(MFnM *fm, MBlkM *b)
{
	b->id = fm->nblk;
	if (fm->nblk == 0)
		fm->start = b;
	b->link = fm->link;
	fm->link = b;
	fm->nblk++;
}

/* ---- machine instruction builders -------------------------------------- */

static MInsM *
minsm_alloc(MFnM *fm, MBlkM *b, MMOP op, MType dtype, MVal *dst)
{
	(void)fm;
	if (b->nins == b->cins) {
		b->cins = b->cins ? b->cins * 2 : 16;
		b->ins = realloc(b->ins, b->cins * sizeof *b->ins);
	}
	MInsM *in = &b->ins[b->nins++];
	memset(in, 0, sizeof *in);
	in->id = b->nins - 1;
	in->op = op;
	in->dtype = dtype;
	in->dst = dst;
	in->blk = b;
	return in;
}

MInsM *
maddm(MFnM *fm, MBlkM *b, MMOP op, MType dtype, MVal *dst,
      MVal *s0, MVal *s1)
{
	MInsM *in = minsm_alloc(fm, b, op, dtype, dst);
	in->src[0] = s0;
	in->src[1] = s1;
	return in;
}

MInsM *
maddm3(MFnM *fm, MBlkM *b, MMOP op, MType dtype, MVal *dst,
       MVal *s0, MVal *s1, MVal *s2)
{
	MInsM *in = minsm_alloc(fm, b, op, dtype, dst);
	in->src[0] = s0;
	in->src[1] = s1;
	in->src[2] = s2;
	return in;
}

MInsM *
maddm_addr(MFnM *fm, MBlkM *b, MMOP op, MType dtype, MVal *dst,
           MAddr a, MVal *s0)
{
	MInsM *in = minsm_alloc(fm, b, op, dtype, dst);
	in->addr = a;
	in->src[0] = s0;
	return in;
}

MInsM *
maddm_cst(MFnM *fm, MBlkM *b, MMOP op, MType dtype, MVal *dst,
          MVal *s0, MConst *cst)
{
	MInsM *in = minsm_alloc(fm, b, op, dtype, dst);
	in->src[0] = s0;
	in->cst = cst;
	return in;
}

MInsM *
maddm_cc(MFnM *fm, MBlkM *b, MMOP op, MType dtype, MVal *dst,
         MVal *s0, MVal *s1, MCC cc)
{
	MInsM *in = minsm_alloc(fm, b, op, dtype, dst);
	in->src[0] = s0;
	in->src[1] = s1;
	in->cc = cc;
	return in;
}

MInsM *
maddm_blit(MFnM *fm, MBlkM *b, MVal *dstptr, MVal *srcptr, MConst *size)
{
	MInsM *in = minsm_alloc(fm, b, MMOP_BLIT, MT_NONE, 0);
	in->src[0] = dstptr;
	in->src[1] = srcptr;
	in->cst = size;
	return in;
}

void
mfnm_term(MFnM *fm, MBlkM *b, MMOP op, MVal *s0, MBlkM *s1, MBlkM *s2,
          MCC cc)
{
	(void)fm;
	b->term.op = op;
	b->term.src[0] = s0;
	b->term.cc = cc;
	b->term.blk = b;
	b->s1 = s1;
	b->s2 = s2;
}

/* ---- dump -------------------------------------------------------------- */

static const char *
mtname(MType t)
{
	static const char *names[MT_NTYPE] = {
		[MT_NONE] = "none", [MT_VOID] = "void",
		[MT_I8] = "i8",  [MT_I16] = "i16", [MT_I32] = "i32", [MT_I64] = "i64",
		[MT_F32] = "f32", [MT_F64] = "f64",
		[MT_PTR] = "ptr", [MT_AGG] = "agg",
	};
	return (unsigned)t < MT_NTYPE ? names[t] : "?";
}

static void
print_mval(FILE *f, MVal *v)
{
	if (!v) {
		fputs("_", f);
		return;
	}
	switch (v->kind) {
	case MV_REG:
		fprintf(f, "%%%s", v->name ? v->name : "reg");
		break;
	case MV_TEMP:
		fprintf(f, "%%v%u", v->id);
		break;
	case MV_GLOBAL:
		fprintf(f, "@%s", v->sym ? v->sym : "?");
		break;
	case MV_CONST:
		fprintf(f, "$c%u", v->id);
		break;
	case MV_TYPE:
		fprintf(f, "!t%u", v->td ? v->td->id : 0);
		break;
	case MV_LABEL:
		fprintf(f, "&%s", v->defblk && v->defblk->name ? v->defblk->name : "?");
		break;
	default:
		fputs("?", f);
		break;
	}
}

static void
print_mconst(FILE *f, MConst *c)
{
	if (!c) {
		fputs("_", f);
		return;
	}
	switch (c->kind) {
	case MC_INT:
		fprintf(f, "$%lld", (long long)c->u.i);
		break;
	case MC_FLT:
		fprintf(f, "$%f", c->type == MT_F32 ? (double)c->u.s : c->u.d);
		break;
	case MC_ADDR:
		fprintf(f, "&%s%+lld", c->u.addr.sym ? c->u.addr.sym : "?",
		        (long long)c->u.addr.off);
		break;
	default:
		fputs("$?", f);
		break;
	}
}

static void
print_maddr(FILE *f, MAddr a)
{
	fputs("[", f);
	if (a.offcon) {
		print_mconst(f, a.offcon);
	} else if (a.off) {
		fprintf(f, "%lld", (long long)a.off);
	}
	if (a.base) {
		fputs(a.offcon || a.off ? "+" : "", f);
		print_mval(f, a.base);
	}
	if (a.index) {
		fprintf(f, "+%s*%u", a.index->name ? a.index->name : "idx", a.scale);
	}
	fputs("]", f);
}

static void
dump_mblk(FILE *f, MBlkM *b)
{
	fprintf(f, "\nblock %s (id %u)\n", b->name ? b->name : "?", b->id);
	for (uint32_t i = 0; i < b->nins; i++) {
		MInsM *in = &b->ins[i];
		fputs("  ", f);
		if (in->dst) {
			print_mval(f, in->dst);
			fputs(" = ", f);
		} else {
			fputs("      ", f);
		}
		fprintf(f, "%s (%s)", mmop_name(in->op), mtname(in->dtype));
		for (int k = 0; k < 3 && in->src[k]; k++) {
			fputs(k ? ", " : " ", f);
			print_mval(f, in->src[k]);
		}
		if (in->cst) {
			fputs(" ", f);
			print_mconst(f, in->cst);
		}
		if (in->op == MMOP_LOAD || in->op == MMOP_STORE ||
		    in->op == MMOP_LEA || in->op == MMOP_BLIT) {
			fputs(" @", f);
			print_maddr(f, in->addr);
		}
		if (in->op == MMOP_SETCC || in->op == MMOP_JCC)
			fprintf(f, " cc=%s", mcc_name(in->cc));
		fputs("\n", f);
	}
	fputs("  term ", f);
	switch (b->term.op) {
	case MMOP_JMP:
		fprintf(f, "jmp %s\n", b->s1 && b->s1->name ? b->s1->name : "?");
		break;
	case MMOP_JCC:
		fprintf(f, "jcc %s -> %s / %s\n", mcc_name(b->term.cc),
		        b->s1 && b->s1->name ? b->s1->name : "?",
		        b->s2 && b->s2->name ? b->s2->name : "?");
		break;
	case MMOP_CALL:
		fputs("call ", f);
		print_mval(f, b->term.src[0]);
		fputs("\n", f);
		break;
	case MMOP_RET:
		fputs("ret ", f);
		print_mval(f, b->term.src[0]);
		fputs("\n", f);
		break;
	default:
		fputs("(none)\n", f);
		break;
	}
}

void
mfnm_dump(MFnM *fm, FILE *out)
{
	fprintf(out, "machine function %s (host %s, nblk %u)\n",
	        fm->name ? fm->name : "?", fm->host && fm->host->name ? fm->host->name : "?",
	        fm->nblk);
	fprintf(out, "  slot %d salign %d nspill %u regsused %#llx\n",
	        fm->slot, fm->salign, fm->nspill, (unsigned long long)fm->regsused);
	for (MBlkM *b = fm->link; b; b = b->link)
		dump_mblk(out, b);
}

void
mfnm_free(MFnM *fm)
{
	if (!fm)
		return;
	for (MBlkM *b = fm->link; b;) {
		MBlkM *next = b->link;
		free(b->name);
		free(b->ins);
		free(b);
		b = next;
	}
	free((char *)fm->name);
	free(fm);
}
