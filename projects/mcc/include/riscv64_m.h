/* riscv64_m.h — riscv64 machine-layer register definitions (MIR-native).
 *
 * The MIR-native backend is target-parameterized through MTargetM
 * (include/mir.h).  This header defines the riscv64 register ids used by
 * the machine target description (machine.c) and the per-arch backend
 * files (riscv64_mabi.c / riscv64_memit.c).
 *
 * Register numbering: GPRs 0..31 (x0-x31 by ABI name), FPRs 32..63
 * (f0-f31).  ABI names: zero/ra/sp/gp/tp/t0-2/s0(fp)/s1/a0-7/s2-11/t3-6.
 */
#ifndef MCC_RISCV64_M_H
#define MCC_RISCV64_M_H

typedef enum RV64MREG {
	RV64MREG_ZERO = 0,   /* x0: hardwired zero */
	RV64MREG_RA,         /* x1: return address */
	RV64MREG_SP,         /* x2: stack pointer */
	RV64MREG_GP,         /* x3: global pointer */
	RV64MREG_TP,         /* x4: thread pointer */
	RV64MREG_T0,         /* x5 */
	RV64MREG_T1,         /* x6 */
	RV64MREG_T2,         /* x7 */
	RV64MREG_FP,         /* x8: frame pointer (s0) */
	RV64MREG_S1,         /* x9 */
	RV64MREG_A0,         /* x10: 1st int arg / return value */
	RV64MREG_A1,         /* x11 */
	RV64MREG_A2,         /* x12 */
	RV64MREG_A3,         /* x13 */
	RV64MREG_A4,         /* x14 */
	RV64MREG_A5,         /* x15 */
	RV64MREG_A6,         /* x16 */
	RV64MREG_A7,         /* x17 */
	RV64MREG_S2,         /* x18 */
	RV64MREG_S3,         /* x19 */
	RV64MREG_S4,         /* x20 */
	RV64MREG_S5,         /* x21 */
	RV64MREG_S6,         /* x22 */
	RV64MREG_S7,         /* x23 */
	RV64MREG_S8,         /* x24 */
	RV64MREG_S9,         /* x25 */
	RV64MREG_S10,        /* x26 */
	RV64MREG_S11,        /* x27 */
	RV64MREG_T3,         /* x28 */
	RV64MREG_T4,         /* x29 */
	RV64MREG_T5,         /* x30 */
	RV64MREG_T6,         /* x31 */
	/* FPRs */
	RV64MREG_F0 = 32,
	RV64MREG_F1, RV64MREG_F2, RV64MREG_F3,
	RV64MREG_F4, RV64MREG_F5, RV64MREG_F6, RV64MREG_F7,
	RV64MREG_F8, RV64MREG_F9,
	RV64MREG_FA0,        /* f10: 1st FP arg / return */
	RV64MREG_FA1, RV64MREG_FA2, RV64MREG_FA3,
	RV64MREG_FA4, RV64MREG_FA5, RV64MREG_FA6, RV64MREG_FA7,
	RV64MREG_F18, RV64MREG_F19, RV64MREG_F20, RV64MREG_F21,
	RV64MREG_F22, RV64MREG_F23, RV64MREG_F24, RV64MREG_F25,
	RV64MREG_F26, RV64MREG_F27,
	RV64MREG_F28, RV64MREG_F29, RV64MREG_F30, RV64MREG_F31,
	RV64MREG_NREG,
} RV64MREG;

/* argreg table: a0-a7 (ints), then fa0-fa7 (floats); -1 terminates.
 * The mabi's rarg() walks this table with an int/float cursor. */
extern const int rv64_argreg[17];

#endif /* MCC_RISCV64_M_H */
