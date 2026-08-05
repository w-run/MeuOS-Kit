/* aarch64_m.h — aarch64 machine-layer register definitions (MIR-native).
 *
 * The MIR-native backend is target-parameterized through MTargetM
 * (include/mir.h).  This header defines the aarch64 register ids used by
 * the machine target description (machine.c) and the per-arch backend
 * files (aarch64_mabi.c / aarch64_memit.c).
 *
 * Register numbering: GPRs 0..31 (x0-x31 by ABI name), V regs 32..63
 * (v0-v31).  ABI names: x0-x7 args/return, x8 indirect result, x9-x15
 * caller-saved temp, x16/x17 (ip0/ip1) intra-procedure scratch, x18
 * platform, x19-x28 callee-saved, x29 (fp), x30 (lr), x31 (sp).
 */
#ifndef MCC_AARCH64_M_H
#define MCC_AARCH64_M_H

typedef enum A64MREG {
	A64MREG_X0 = 0,        /* 1st int arg / return value */
	A64MREG_X1,            /* 2nd int arg */
	A64MREG_X2,            /* 3rd int arg */
	A64MREG_X3,            /* 4th int arg */
	A64MREG_X4,            /* 5th int arg */
	A64MREG_X5,            /* 6th int arg */
	A64MREG_X6,            /* 7th int arg */
	A64MREG_X7,            /* 8th int arg */
	A64MREG_X8,            /* indirect result (sret) */
	A64MREG_X9,            /* caller-saved temp */
	A64MREG_X10,           /* caller-saved temp */
	A64MREG_X11,           /* caller-saved temp */
	A64MREG_X12,           /* caller-saved temp */
	A64MREG_X13,           /* caller-saved temp */
	A64MREG_X14,           /* caller-saved temp */
	A64MREG_X15,           /* caller-saved temp */
	A64MREG_IP0,           /* x16: intra-procedure scratch */
	A64MREG_IP1,           /* x17: intra-procedure scratch */
	A64MREG_X18,           /* platform register (unused) */
	A64MREG_X19,           /* callee-saved */
	A64MREG_X20,           /* callee-saved */
	A64MREG_X21,           /* callee-saved */
	A64MREG_X22,           /* callee-saved */
	A64MREG_X23,           /* callee-saved */
	A64MREG_X24,           /* callee-saved */
	A64MREG_X25,           /* callee-saved */
	A64MREG_X26,           /* callee-saved */
	A64MREG_X27,           /* callee-saved */
	A64MREG_X28,           /* callee-saved */
	A64MREG_X29,           /* frame pointer (fp) */
	A64MREG_X30,           /* link register (lr) */
	A64MREG_X31,           /* stack pointer (sp) */
	/* V (SIMD/FP) registers */
	A64MREG_V0 = 32,       /* 1st FP arg / return */
	A64MREG_V1, A64MREG_V2, A64MREG_V3,
	A64MREG_V4, A64MREG_V5, A64MREG_V6, A64MREG_V7,
	A64MREG_V8,            /* callee-saved (low 64 bits) */
	A64MREG_V9, A64MREG_V10, A64MREG_V11, A64MREG_V12,
	A64MREG_V13, A64MREG_V14, A64MREG_V15,
	A64MREG_V16,           /* caller-saved */
	A64MREG_V17, A64MREG_V18, A64MREG_V19, A64MREG_V20,
	A64MREG_V21, A64MREG_V22, A64MREG_V23, A64MREG_V24,
	A64MREG_V25, A64MREG_V26, A64MREG_V27, A64MREG_V28,
	A64MREG_V29, A64MREG_V30, A64MREG_V31,
	A64MREG_NREG,
} A64MREG;

/* argreg table: x0-x7 (ints), then v0-v7 (floats); -1 terminates.
 * The mabi's rarg() walks this table with an int/float cursor. */
extern const int a64_argreg[17];

#endif /* MCC_AARCH64_M_H */
