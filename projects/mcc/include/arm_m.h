/* arm_m.h — ARM (armv7-a, 32-bit) machine-layer register definitions
 * (MIR-native).
 *
 * The MIR-native backend is target-parameterized through MTargetM
 * (include/mir.h).  This header defines the ARM register ids used by the
 * machine target description (machine.c) and the per-arch backend files
 * (arm_mabi.c / arm_memit.c).
 *
 * Register numbering: GPRs 0..15 (r0-r15 by ABI name), VFP D regs 16..47
 * (d0-d31).  AAPCS32: r0-r3 argument/return, r4-r9 callee-saved, r10
 * (ip) caller-saved scratch, r11 (fp) frame pointer, r12 (ip) scratch,
 * r13 (sp), r14 (lr), r15 (pc).  VFP: d0-d7 FP args/return, d8-d15
 * callee-saved, d16-d31 caller-saved.
 */
#ifndef MCC_ARM_M_H
#define MCC_ARM_M_H

typedef enum ARMMREG {
	ARM_R0 = 0,        /* 1st int arg / return value */
	ARM_R1,            /* 2nd int arg */
	ARM_R2,            /* 3rd int arg */
	ARM_R3,            /* 4th int arg */
	ARM_R4,            /* callee-saved */
	ARM_R5,            /* callee-saved */
	ARM_R6,            /* callee-saved */
	ARM_R7,            /* callee-saved */
	ARM_R8,            /* callee-saved */
	ARM_R9,            /* callee-saved */
	ARM_R10,           /* ip: caller-saved scratch */
	ARM_R11,           /* fp: frame pointer */
	ARM_R12,           /* ip: caller-saved scratch */
	ARM_SP,            /* r13: stack pointer */
	ARM_LR,            /* r14: link register */
	ARM_PC,            /* r15: program counter */
	/* VFP double registers (d0-d31); d2n aliases s4n..s4n+3 */
	ARM_D0 = 16,       /* 1st FP arg / return */
	ARM_D1, ARM_D2, ARM_D3, ARM_D4, ARM_D5, ARM_D6, ARM_D7,
	ARM_D8,            /* callee-saved */
	ARM_D9, ARM_D10, ARM_D11, ARM_D12, ARM_D13, ARM_D14, ARM_D15,
	ARM_D16,           /* caller-saved */
	ARM_D17, ARM_D18, ARM_D19, ARM_D20, ARM_D21, ARM_D22, ARM_D23,
	ARM_D24, ARM_D25, ARM_D26, ARM_D27, ARM_D28, ARM_D29, ARM_D30,
	ARM_D31,
	ARM_MREG_NREG,
} ARMMREG;

/* argreg table: r0-r3 (ints), then d0-d7 (floats); -1 terminates.
 * The mabi's rarg() walks this table with an int/float cursor. */
extern const int arm_argreg[13];

#endif /* MCC_ARM_M_H */
