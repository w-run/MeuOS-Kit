/* mtarget.c — MIR machine target descriptions (register tables + constants).
 *
 * Extracted from machine.c during the MIR backend file split (2026-08-07).
 * Holds the 6 per-architecture register descriptions (MTargetM) and the
 * associated argument/save/clobber register arrays, plus the MIR-layer
 * global flags g_pic and g_tls_model.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mir.h"
#include "x86_64_m.h"
#include "riscv64_m.h"
#include "loongarch64_m.h"
#include "aarch64_m.h"
#include "arm_m.h"
#include "i386_m.h"

/* ---- loongarch64 machine target (register descriptions) ---------------- */

static const MRegInfo la64_regs[LA64MREG_NREG] = {
	[LA64MREG_ZERO]    = { "zero", MRC_GPR, true,  false, false },
	[LA64MREG_RA]      = { "ra",   MRC_GPR, true,  false, false },
	[LA64MREG_TP]      = { "tp",   MRC_GPR, true,  false, false },
	[LA64MREG_SP]      = { "sp",   MRC_GPR, true,  false, false },
	[LA64MREG_A0]      = { "a0",   MRC_GPR, true,  false, true  },
	[LA64MREG_A1]      = { "a1",   MRC_GPR, true,  false, true  },
	[LA64MREG_A2]      = { "a2",   MRC_GPR, true,  false, true  },
	[LA64MREG_A3]      = { "a3",   MRC_GPR, true,  false, true  },
	[LA64MREG_A4]      = { "a4",   MRC_GPR, true,  false, true  },
	[LA64MREG_A5]      = { "a5",   MRC_GPR, true,  false, true  },
	[LA64MREG_A6]      = { "a6",   MRC_GPR, true,  false, true  },
	[LA64MREG_A7]      = { "a7",   MRC_GPR, true,  false, true  },
	[LA64MREG_T0]      = { "t0",   MRC_GPR, true,  false, false },
	[LA64MREG_T1]      = { "t1",   MRC_GPR, true,  false, false },
	[LA64MREG_T2]      = { "t2",   MRC_GPR, true,  false, false },
	[LA64MREG_T3]      = { "t3",   MRC_GPR, true,  false, false },
	[LA64MREG_T4]      = { "t4",   MRC_GPR, true,  false, false },
	[LA64MREG_T5]      = { "t5",   MRC_GPR, true,  false, false },
	[LA64MREG_T6]      = { "t6",   MRC_GPR, true,  false, false },
	[LA64MREG_T7]      = { "t7",   MRC_GPR, true,  false, false },
	[LA64MREG_T8]      = { "t8",   MRC_GPR, true,  false, false },
	[LA64MREG_RESERVED] = { "r21", MRC_GPR, true, false, false },
	[LA64MREG_FP]      = { "fp",   MRC_GPR, true,  false, false },
	[LA64MREG_S0]      = { "s0",   MRC_GPR, false, true,  false },
	[LA64MREG_S1]      = { "s1",   MRC_GPR, false, true,  false },
	[LA64MREG_S2]      = { "s2",   MRC_GPR, false, true,  false },
	[LA64MREG_S3]      = { "s3",   MRC_GPR, false, true,  false },
	[LA64MREG_S4]      = { "s4",   MRC_GPR, false, true,  false },
	[LA64MREG_S5]      = { "s5",   MRC_GPR, false, true,  false },
	[LA64MREG_S6]      = { "s6",   MRC_GPR, false, true,  false },
	[LA64MREG_S7]      = { "s7",   MRC_GPR, false, true,  false },
	[LA64MREG_S8]      = { "s8",   MRC_GPR, false, true,  false },
	[LA64MREG_F0]      = { "f0",   MRC_FPR, true,  false, true  },
	[LA64MREG_F1]      = { "f1",   MRC_FPR, true,  false, true  },
	[LA64MREG_F2]      = { "f2",   MRC_FPR, true,  false, true  },
	[LA64MREG_F3]      = { "f3",   MRC_FPR, true,  false, true  },
	[LA64MREG_F4]      = { "f4",   MRC_FPR, true,  false, true  },
	[LA64MREG_F5]      = { "f5",   MRC_FPR, true,  false, true  },
	[LA64MREG_F6]      = { "f6",   MRC_FPR, true,  false, true  },
	[LA64MREG_F7]      = { "f7",   MRC_FPR, true,  false, true  },
	[LA64MREG_F8]      = { "f8",   MRC_FPR, true,  false, false },
	[LA64MREG_F9]      = { "f9",   MRC_FPR, true,  false, false },
	[LA64MREG_F10]     = { "f10",  MRC_FPR, true,  false, false },
	[LA64MREG_F11]     = { "f11",  MRC_FPR, true,  false, false },
	[LA64MREG_F12]     = { "f12",  MRC_FPR, true,  false, false },
	[LA64MREG_F13]     = { "f13",  MRC_FPR, true,  false, false },
	[LA64MREG_F14]     = { "f14",  MRC_FPR, true,  false, false },
	[LA64MREG_F15]     = { "f15",  MRC_FPR, true,  false, false },
	[LA64MREG_F16]     = { "f16",  MRC_FPR, true,  false, false },
	[LA64MREG_F17]     = { "f17",  MRC_FPR, true,  false, false },
	[LA64MREG_F18]     = { "f18",  MRC_FPR, true,  false, false },
	[LA64MREG_F19]     = { "f19",  MRC_FPR, true,  false, false },
	[LA64MREG_F20]     = { "f20",  MRC_FPR, true,  false, false },
	[LA64MREG_F21]     = { "f21",  MRC_FPR, true,  false, false },
	[LA64MREG_F22]     = { "f22",  MRC_FPR, true,  false, false },
	[LA64MREG_F23]     = { "f23",  MRC_FPR, true,  false, false },
	[LA64MREG_F24]     = { "f24",  MRC_FPR, false, true,  false },
	[LA64MREG_F25]     = { "f25",  MRC_FPR, false, true,  false },
	[LA64MREG_F26]     = { "f26",  MRC_FPR, false, true,  false },
	[LA64MREG_F27]     = { "f27",  MRC_FPR, false, true,  false },
	[LA64MREG_F28]     = { "f28",  MRC_FPR, false, true,  false },
	[LA64MREG_F29]     = { "f29",  MRC_FPR, false, true,  false },
	[LA64MREG_F30]     = { "f30",  MRC_FPR, false, true,  false },
	[LA64MREG_F31]     = { "f31",  MRC_FPR, false, true,  false },
};

const int la64_argreg[17] = {
	LA64MREG_A0, LA64MREG_A1, LA64MREG_A2, LA64MREG_A3,
	LA64MREG_A4, LA64MREG_A5, LA64MREG_A6, LA64MREG_A7,
	LA64MREG_F0, LA64MREG_F1, LA64MREG_F2, LA64MREG_F3,
	LA64MREG_F4, LA64MREG_F5, LA64MREG_F6, LA64MREG_F7,
	-1
};
static const int la64_rsave[] = {
	LA64MREG_T0, LA64MREG_T1, LA64MREG_T2, LA64MREG_T3, LA64MREG_T4,
	LA64MREG_T5, LA64MREG_T6, LA64MREG_T7, LA64MREG_T8,
	LA64MREG_A0, LA64MREG_A1, LA64MREG_A2, LA64MREG_A3,
	LA64MREG_A4, LA64MREG_A5, LA64MREG_A6, LA64MREG_A7,
	LA64MREG_F0, LA64MREG_F1, LA64MREG_F2, LA64MREG_F3,
	LA64MREG_F4, LA64MREG_F5, LA64MREG_F6, LA64MREG_F7,
	LA64MREG_F8, LA64MREG_F9, LA64MREG_F10, LA64MREG_F11,
	LA64MREG_F12, LA64MREG_F13, LA64MREG_F14, LA64MREG_F15,
	LA64MREG_F16, LA64MREG_F17, LA64MREG_F18, LA64MREG_F19,
	LA64MREG_F20, LA64MREG_F21, LA64MREG_F22, LA64MREG_F23,
	-1
};
static const int la64_rclob[] = {
	LA64MREG_S0, LA64MREG_S1, LA64MREG_S2, LA64MREG_S3, LA64MREG_S4,
	LA64MREG_S5, LA64MREG_S6, LA64MREG_S7, LA64MREG_S8,
	LA64MREG_F24, LA64MREG_F25, LA64MREG_F26, LA64MREG_F27,
	LA64MREG_F28, LA64MREG_F29, LA64MREG_F30, LA64MREG_F31,
	-1
};

extern void mfnm_abi_loongarch64(MFnM *fm);
const MTargetM mtarget_loongarch64 = {
	.name = "loongarch64",
	.nreg = LA64MREG_NREG,
	.regs = la64_regs,
	.gpr0 = LA64MREG_ZERO,
	.ngpr = 32,
	.fpr0 = LA64MREG_F0,
	.nfpr = 32,
	.rglob = (1ull << LA64MREG_ZERO) | (1ull << LA64MREG_RA) |
	         (1ull << LA64MREG_TP) | (1ull << LA64MREG_SP) |
	         (1ull << LA64MREG_FP) | (1ull << LA64MREG_RESERVED),
	.reserved = 0,
	.argreg = la64_argreg,
	.rsave = la64_rsave,
	.rclob = la64_rclob,
	.ptrsize = 8,
	.stackalign = 16,
	.kl_in_reg = true,
	.feat = 0,
	.sret_reg = LA64MREG_A0,
	.abi = mfnm_abi_loongarch64,
	.scratch = (1ull << LA64MREG_T0) | (1ull << LA64MREG_T1) |
	           (1ull << LA64MREG_T2) | (1ull << LA64MREG_T8),
};

/* ---- riscv64 machine target ------------------------------------------- */

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

extern void mfnm_abi_riscv64(MFnM *fm);
const MTargetM mtarget_riscv64 = {
	.name = "riscv64",
	.nreg = RV64MREG_NREG,
	.regs = rv64_regs,
	.gpr0 = RV64MREG_ZERO,
	.ngpr = 32,
	.fpr0 = RV64MREG_F0,
	.nfpr = 32,
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
	.feat = 0,
	.sret_reg = RV64MREG_A0,
	.abi = mfnm_abi_riscv64,
	.scratch = (1ull << RV64MREG_T0) | (1ull << RV64MREG_T1) |
	           (1ull << RV64MREG_T2) | (1ull << RV64MREG_T6) |
	           (1ull << RV64MREG_F28) | (1ull << RV64MREG_F29),
};

/* ---- aarch64 machine target -------------------------------------------- */

static const MRegInfo a64_regs[A64MREG_NREG] = {
	[A64MREG_X0]  = { "x0",  MRC_GPR, true,  false, true  },
	[A64MREG_X1]  = { "x1",  MRC_GPR, true,  false, true  },
	[A64MREG_X2]  = { "x2",  MRC_GPR, true,  false, true  },
	[A64MREG_X3]  = { "x3",  MRC_GPR, true,  false, true  },
	[A64MREG_X4]  = { "x4",  MRC_GPR, true,  false, true  },
	[A64MREG_X5]  = { "x5",  MRC_GPR, true,  false, true  },
	[A64MREG_X6]  = { "x6",  MRC_GPR, true,  false, true  },
	[A64MREG_X7]  = { "x7",  MRC_GPR, true,  false, true  },
	[A64MREG_X8]  = { "x8",  MRC_GPR, true,  false, false },
	[A64MREG_X9]  = { "x9",  MRC_GPR, true,  false, false },
	[A64MREG_X10] = { "x10", MRC_GPR, true,  false, false },
	[A64MREG_X11] = { "x11", MRC_GPR, true,  false, false },
	[A64MREG_X12] = { "x12", MRC_GPR, true,  false, false },
	[A64MREG_X13] = { "x13", MRC_GPR, true,  false, false },
	[A64MREG_X14] = { "x14", MRC_GPR, true,  false, false },
	[A64MREG_X15] = { "x15", MRC_GPR, true,  false, false },
	[A64MREG_IP0] = { "x16", MRC_GPR, true,  false, false },
	[A64MREG_IP1] = { "x17", MRC_GPR, true,  false, false },
	[A64MREG_X18] = { "x18", MRC_GPR, true,  false, false },
	[A64MREG_X19] = { "x19", MRC_GPR, false, true,  false },
	[A64MREG_X20] = { "x20", MRC_GPR, false, true,  false },
	[A64MREG_X21] = { "x21", MRC_GPR, false, true,  false },
	[A64MREG_X22] = { "x22", MRC_GPR, false, true,  false },
	[A64MREG_X23] = { "x23", MRC_GPR, false, true,  false },
	[A64MREG_X24] = { "x24", MRC_GPR, false, true,  false },
	[A64MREG_X25] = { "x25", MRC_GPR, false, true,  false },
	[A64MREG_X26] = { "x26", MRC_GPR, false, true,  false },
	[A64MREG_X27] = { "x27", MRC_GPR, false, true,  false },
	[A64MREG_X28] = { "x28", MRC_GPR, false, true,  false },
	[A64MREG_X29] = { "fp",  MRC_GPR, false, false, false },
	[A64MREG_X30] = { "lr",  MRC_GPR, false, false, false },
	[A64MREG_X31] = { "sp",  MRC_GPR, false, false, false },
	[A64MREG_V0]  = { "v0",  MRC_FPR, true,  false, true  },
	[A64MREG_V1]  = { "v1",  MRC_FPR, true,  false, true  },
	[A64MREG_V2]  = { "v2",  MRC_FPR, true,  false, true  },
	[A64MREG_V3]  = { "v3",  MRC_FPR, true,  false, true  },
	[A64MREG_V4]  = { "v4",  MRC_FPR, true,  false, true  },
	[A64MREG_V5]  = { "v5",  MRC_FPR, true,  false, true  },
	[A64MREG_V6]  = { "v6",  MRC_FPR, true,  false, true  },
	[A64MREG_V7]  = { "v7",  MRC_FPR, true,  false, true  },
	[A64MREG_V8]  = { "v8",  MRC_FPR, false, true,  false },
	[A64MREG_V9]  = { "v9",  MRC_FPR, false, true,  false },
	[A64MREG_V10] = { "v10", MRC_FPR, false, true,  false },
	[A64MREG_V11] = { "v11", MRC_FPR, false, true,  false },
	[A64MREG_V12] = { "v12", MRC_FPR, false, true,  false },
	[A64MREG_V13] = { "v13", MRC_FPR, false, true,  false },
	[A64MREG_V14] = { "v14", MRC_FPR, false, true,  false },
	[A64MREG_V15] = { "v15", MRC_FPR, false, true,  false },
	[A64MREG_V16] = { "v16", MRC_FPR, true,  false, false },
	[A64MREG_V17] = { "v17", MRC_FPR, true,  false, false },
	[A64MREG_V18] = { "v18", MRC_FPR, true,  false, false },
	[A64MREG_V19] = { "v19", MRC_FPR, true,  false, false },
	[A64MREG_V20] = { "v20", MRC_FPR, true,  false, false },
	[A64MREG_V21] = { "v21", MRC_FPR, true,  false, false },
	[A64MREG_V22] = { "v22", MRC_FPR, true,  false, false },
	[A64MREG_V23] = { "v23", MRC_FPR, true,  false, false },
	[A64MREG_V24] = { "v24", MRC_FPR, true,  false, false },
	[A64MREG_V25] = { "v25", MRC_FPR, true,  false, false },
	[A64MREG_V26] = { "v26", MRC_FPR, true,  false, false },
	[A64MREG_V27] = { "v27", MRC_FPR, true,  false, false },
	[A64MREG_V28] = { "v28", MRC_FPR, true,  false, false },
	[A64MREG_V29] = { "v29", MRC_FPR, true,  false, false },
	[A64MREG_V30] = { "v30", MRC_FPR, true,  false, false },
	[A64MREG_V31] = { "v31", MRC_FPR, true,  false, false },
};

const int a64_argreg[17] = {
	A64MREG_X0, A64MREG_X1, A64MREG_X2, A64MREG_X3,
	A64MREG_X4, A64MREG_X5, A64MREG_X6, A64MREG_X7,
	A64MREG_V0, A64MREG_V1, A64MREG_V2, A64MREG_V3,
	A64MREG_V4, A64MREG_V5, A64MREG_V6, A64MREG_V7,
	-1
};
static const int a64_rsave[] = {
	A64MREG_X0, A64MREG_X1, A64MREG_X2, A64MREG_X3,
	A64MREG_X4, A64MREG_X5, A64MREG_X6, A64MREG_X7,
	A64MREG_X8, A64MREG_X9, A64MREG_X10, A64MREG_X11,
	A64MREG_X12, A64MREG_X13, A64MREG_X14, A64MREG_X15,
	A64MREG_IP0, A64MREG_IP1, A64MREG_X18,
	A64MREG_V0, A64MREG_V1, A64MREG_V2, A64MREG_V3,
	A64MREG_V4, A64MREG_V5, A64MREG_V6, A64MREG_V7,
	A64MREG_V16, A64MREG_V17, A64MREG_V18, A64MREG_V19,
	A64MREG_V20, A64MREG_V21, A64MREG_V22, A64MREG_V23,
	A64MREG_V24, A64MREG_V25, A64MREG_V26, A64MREG_V27,
	A64MREG_V28, A64MREG_V29, A64MREG_V30, A64MREG_V31,
	-1
};
static const int a64_rclob[] = {
	A64MREG_X19, A64MREG_X20, A64MREG_X21, A64MREG_X22,
	A64MREG_X23, A64MREG_X24, A64MREG_X25, A64MREG_X26,
	A64MREG_X27, A64MREG_X28,
	A64MREG_V8, A64MREG_V9, A64MREG_V10, A64MREG_V11,
	A64MREG_V12, A64MREG_V13, A64MREG_V14, A64MREG_V15,
	-1
};

extern void mfnm_abi_aarch64(MFnM *fm);
const MTargetM mtarget_aarch64 = {
	.name = "aarch64",
	.nreg = A64MREG_NREG,
	.regs = a64_regs,
	.gpr0 = A64MREG_X0,
	.ngpr = 31,
	.fpr0 = A64MREG_V0,
	.nfpr = 32,
	.rglob = (1ull << A64MREG_X29) | (1ull << A64MREG_X30) |
	         (1ull << A64MREG_X31),
	.reserved = 0,
	.argreg = a64_argreg,
	.rsave = a64_rsave,
	.rclob = a64_rclob,
	.ptrsize = 8,
	.stackalign = 16,
	.kl_in_reg = true,
	.feat = 0,
	.sret_reg = A64MREG_X8,
	.abi = mfnm_abi_aarch64,
	.scratch = (1ull << A64MREG_X9) | (1ull << A64MREG_X10) |
	           (1ull << A64MREG_X11) | (1ull << A64MREG_IP0) |
	           (1ull << A64MREG_IP1) | (1ull << A64MREG_V16) |
	           (1ull << A64MREG_V17),
};

/* ---- ARM (armv7-a, 32-bit) machine target ------------------------------ */

static const MRegInfo arm_regs[ARM_MREG_NREG] = {
	[ARM_R0]  = { "r0",  MRC_GPR, true,  false, true  },
	[ARM_R1]  = { "r1",  MRC_GPR, true,  false, true  },
	[ARM_R2]  = { "r2",  MRC_GPR, true,  false, true  },
	[ARM_R3]  = { "r3",  MRC_GPR, true,  false, true  },
	[ARM_R4]  = { "r4",  MRC_GPR, false, true,  false },
	[ARM_R5]  = { "r5",  MRC_GPR, false, true,  false },
	[ARM_R6]  = { "r6",  MRC_GPR, false, true,  false },
	[ARM_R7]  = { "r7",  MRC_GPR, false, true,  false },
	[ARM_R8]  = { "r8",  MRC_GPR, false, true,  false },
	[ARM_R9]  = { "r9",  MRC_GPR, false, true,  false },
	[ARM_R10] = { "r10", MRC_GPR, true,  false, false },
	[ARM_R11] = { "fp",  MRC_GPR, false, false, false },
	[ARM_R12] = { "r12", MRC_GPR, true,  false, false },
	[ARM_SP]  = { "sp",  MRC_GPR, false, false, false },
	[ARM_LR]  = { "lr",  MRC_GPR, false, false, false },
	[ARM_PC]  = { "pc",  MRC_GPR, false, false, false },
	[ARM_D0]  = { "d0",  MRC_FPR, true,  false, true  },
	[ARM_D1]  = { "d1",  MRC_FPR, true,  false, true  },
	[ARM_D2]  = { "d2",  MRC_FPR, true,  false, true  },
	[ARM_D3]  = { "d3",  MRC_FPR, true,  false, true  },
	[ARM_D4]  = { "d4",  MRC_FPR, true,  false, true  },
	[ARM_D5]  = { "d5",  MRC_FPR, true,  false, true  },
	[ARM_D6]  = { "d6",  MRC_FPR, true,  false, true  },
	[ARM_D7]  = { "d7",  MRC_FPR, true,  false, true  },
	[ARM_D8]  = { "d8",  MRC_FPR, false, true,  false },
	[ARM_D9]  = { "d9",  MRC_FPR, false, true,  false },
	[ARM_D10] = { "d10", MRC_FPR, false, true,  false },
	[ARM_D11] = { "d11", MRC_FPR, false, true,  false },
	[ARM_D12] = { "d12", MRC_FPR, false, true,  false },
	[ARM_D13] = { "d13", MRC_FPR, false, true,  false },
	[ARM_D14] = { "d14", MRC_FPR, false, true,  false },
	[ARM_D15] = { "d15", MRC_FPR, false, true,  false },
	[ARM_D16] = { "d16", MRC_FPR, true,  false, false },
	[ARM_D17] = { "d17", MRC_FPR, true,  false, false },
	[ARM_D18] = { "d18", MRC_FPR, true,  false, false },
	[ARM_D19] = { "d19", MRC_FPR, true,  false, false },
	[ARM_D20] = { "d20", MRC_FPR, true,  false, false },
	[ARM_D21] = { "d21", MRC_FPR, true,  false, false },
	[ARM_D22] = { "d22", MRC_FPR, true,  false, false },
	[ARM_D23] = { "d23", MRC_FPR, true,  false, false },
	[ARM_D24] = { "d24", MRC_FPR, true,  false, false },
	[ARM_D25] = { "d25", MRC_FPR, true,  false, false },
	[ARM_D26] = { "d26", MRC_FPR, true,  false, false },
	[ARM_D27] = { "d27", MRC_FPR, true,  false, false },
	[ARM_D28] = { "d28", MRC_FPR, true,  false, false },
	[ARM_D29] = { "d29", MRC_FPR, true,  false, false },
	[ARM_D30] = { "d30", MRC_FPR, true,  false, false },
	[ARM_D31] = { "d31", MRC_FPR, true,  false, false },
};

const int arm_argreg[13] = {
	ARM_R0, ARM_R1, ARM_R2, ARM_R3,
	ARM_D0, ARM_D1, ARM_D2, ARM_D3,
	ARM_D4, ARM_D5, ARM_D6, ARM_D7,
	-1
};
static const int arm_rsave[] = {
	ARM_R0, ARM_R1, ARM_R2, ARM_R3, ARM_R12,
	ARM_D0, ARM_D1, ARM_D2, ARM_D3, ARM_D4, ARM_D5, ARM_D6, ARM_D7,
	ARM_D16, ARM_D17, ARM_D18, ARM_D19, ARM_D20, ARM_D21, ARM_D22,
	ARM_D23, ARM_D24, ARM_D25, ARM_D26, ARM_D27, ARM_D28, ARM_D29,
	ARM_D30, ARM_D31,
	-1
};
static const int arm_rclob[] = {
	ARM_R4, ARM_R5, ARM_R6, ARM_R7, ARM_R8, ARM_R9,
	ARM_D10, ARM_D11, ARM_D12, ARM_D13, ARM_D14, ARM_D15,
	-1
};

extern void mfnm_abi_arm(MFnM *fm);
const MTargetM mtarget_arm = {
	.name = "arm",
	.nreg = ARM_MREG_NREG,
	.regs = arm_regs,
	.gpr0 = ARM_R0,
	.ngpr = 16,
	.fpr0 = ARM_D0,
	.nfpr = 32,
	.rglob = (1ull << ARM_R11) | (1ull << ARM_SP) |
	         (1ull << ARM_LR) | (1ull << ARM_PC),
	.reserved = 0,
	.argreg = arm_argreg,
	.rsave = arm_rsave,
	.rclob = arm_rclob,
	.ptrsize = 4,
	.stackalign = 8,
	.kl_in_reg = false,
	.feat = 0,
	.sret_reg = ARM_R0,
	.abi = mfnm_abi_arm,
	.scratch = (1ull << ARM_R10) | (1ull << ARM_R12) |
	           (1ull << ARM_D8) | (1ull << ARM_D9),
};

/* ---- i386 (32-bit) machine target -------------------------------------- */

static const MRegInfo i386_regs[I386MREG_NREG] = {
	[I386MREG_EAX] = { "eax", MRC_GPR, true,  false, true  },
	[I386MREG_ECX] = { "ecx", MRC_GPR, true,  false, false },
	[I386MREG_EDX] = { "edx", MRC_GPR, true,  false, false },
	[I386MREG_EBX] = { "ebx", MRC_GPR, false, true,  false },
	[I386MREG_ESP] = { "esp", MRC_GPR, false, false, false },
	[I386MREG_EBP] = { "ebp", MRC_GPR, false, false, false },
	[I386MREG_ESI] = { "esi", MRC_GPR, false, true,  false },
	[I386MREG_EDI] = { "edi", MRC_GPR, false, true,  false },
	[I386MREG_XMM0] = { "xmm0", MRC_FPR, true,  false, false },
	[I386MREG_XMM1] = { "xmm1", MRC_FPR, true,  false, false },
	[I386MREG_XMM2] = { "xmm2", MRC_FPR, true,  false, false },
	[I386MREG_XMM3] = { "xmm3", MRC_FPR, true,  false, false },
	[I386MREG_XMM4] = { "xmm4", MRC_FPR, true,  false, false },
	[I386MREG_XMM5] = { "xmm5", MRC_FPR, true,  false, false },
	[I386MREG_XMM6] = { "xmm6", MRC_FPR, true,  false, false },
	[I386MREG_XMM7] = { "xmm7", MRC_FPR, true,  false, false },
};

static const int i386_argreg[] = {
	I386MREG_XMM0, I386MREG_XMM1, I386MREG_XMM2, I386MREG_XMM3,
	I386MREG_XMM4, I386MREG_XMM5, I386MREG_XMM6, I386MREG_XMM7,
	-1
};
static const int i386_rsave[] = {
	I386MREG_EAX, I386MREG_ECX, I386MREG_EDX,
	I386MREG_XMM0, I386MREG_XMM1, I386MREG_XMM2, I386MREG_XMM3,
	I386MREG_XMM4, I386MREG_XMM5, I386MREG_XMM6, I386MREG_XMM7,
	-1
};
static const int i386_rclob[] = {
	I386MREG_EBX, I386MREG_ESI, I386MREG_EDI,
	-1
};

extern void mfnm_abi_i386(MFnM *fm);
const MTargetM mtarget_i386 = {
	.name = "i386",
	.nreg = I386MREG_NREG,
	.regs = i386_regs,
	.gpr0 = I386MREG_EAX,
	.ngpr = 8,
	.fpr0 = I386MREG_XMM0,
	.nfpr = 8,
	.rglob = (1ull << I386MREG_EBP) | (1ull << I386MREG_ESP),
	.reserved = 0,
	.argreg = i386_argreg,
	.rsave = i386_rsave,
	.rclob = i386_rclob,
	.ptrsize = 4,
	.stackalign = 16,
	.kl_in_reg = false,
	.feat = 0,
	.sret_reg = I386MREG_EAX,
	.abi = mfnm_abi_i386,
	.scratch = (1ull << I386MREG_EAX) | (1ull << I386MREG_ECX) |
	           (1ull << I386MREG_EDX) | (1ull << I386MREG_XMM0),
};

/* ---- x86-64 machine target (register descriptions) --------------------- */

static const MRegInfo x64_regs[X64MREG_NREG] = {
	[X64MREG_RAX] = { "rax", MRC_GPR, true,  false, false },
	[X64MREG_RCX] = { "rcx", MRC_GPR, true,  false, true  },
	[X64MREG_RDX] = { "rdx", MRC_GPR, true,  false, true  },
	[X64MREG_RSI] = { "rsi", MRC_GPR, true,  false, true  },
	[X64MREG_RDI] = { "rdi", MRC_GPR, true,  false, true  },
	[X64MREG_R8]  = { "r8",  MRC_GPR, true,  false, true  },
	[X64MREG_R9]  = { "r9",  MRC_GPR, true,  false, true  },
	[X64MREG_R10] = { "r10", MRC_GPR, true,  false, false },
	[X64MREG_R11] = { "r11", MRC_GPR, true,  false, false },
	[X64MREG_RBX] = { "rbx", MRC_GPR, false, true,  false },
	[X64MREG_R12] = { "r12", MRC_GPR, false, true,  false },
	[X64MREG_R13] = { "r13", MRC_GPR, false, true,  false },
	[X64MREG_R14] = { "r14", MRC_GPR, false, true,  false },
	[X64MREG_R15] = { "r15", MRC_GPR, false, true,  false },
	[X64MREG_RBP] = { "rbp", MRC_GPR, false, false, false },
	[X64MREG_RSP] = { "rsp", MRC_GPR, false, false, false },
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

static const int x64_argreg[] = {
	X64MREG_RDI, X64MREG_RSI, X64MREG_RDX, X64MREG_RCX,
	X64MREG_R8, X64MREG_R9,
	X64MREG_XMM0, X64MREG_XMM1, X64MREG_XMM2, X64MREG_XMM3,
	X64MREG_XMM4, X64MREG_XMM5, X64MREG_XMM6, X64MREG_XMM7,
	-1
};
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

extern void mfnm_abi_x86_64(MFnM *fm);
const MTargetM mtarget_x86_64 = {
	.name = "x86_64",
	.nreg = X64MREG_NREG,
	.regs = x64_regs,
	.gpr0 = X64MREG_RAX,
	.ngpr = X64MREG_RSP - X64MREG_RAX + 1,
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
	.scratch = (1ull << X64MREG_RAX) | (1ull << X64MREG_RCX) |
	           (1ull << X64MREG_RDX) | (1ull << X64MREG_R9) |
	           (1ull << X64MREG_R10) | (1ull << X64MREG_R11) |
	           (1ull << X64MREG_XMM0),
};

/* ---- MIR-layer global flags (live in the machine layer) --------------- */

/* PIC/shared-code flag.  Defined here so standalone mir/ unit-test links
 * resolve it; emitters that read it declare it extern. */
int g_pic;

/* TLS access model mirror.  Same ownership pattern as g_pic: defined here
 * so check-mir-* links resolve it. */
int g_tls_model;