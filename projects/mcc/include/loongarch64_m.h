/* loongarch64_m.h — loongarch64 machine-layer register definitions
 * (MIR-native).
 *
 * The MIR-native backend is target-parameterized through MTargetM
 * (include/mir.h).  This header defines the loongarch64 register ids used
 * by the machine target description (machine.c) and the per-arch backend
 * files (loongarch64_mabi.c / loongarch64_memit.c).
 *
 * Physical layout (LP64D): GPRs r0-r31 with ABI names
 *   r0=zero r1=ra r2=tp r3=sp r4-11=a0-a7 r12-20=t0-t8 r21=reserved
 *   r22=fp r23-31=s0-s8;
 * FPRs f0-f31 (fa0-fa7 = f0-f7, ft0-ft15 = f8-f23, fs0-fs7 = f24-f31).
 */
#ifndef MCC_LOONGARCH64_M_H
#define MCC_LOONGARCH64_M_H

typedef enum LA64MREG {
	LA64MREG_ZERO = 0,   /* r0: hardwired zero */
	LA64MREG_RA,         /* r1: return address */
	LA64MREG_TP,         /* r2: thread pointer */
	LA64MREG_SP,         /* r3: stack pointer */
	LA64MREG_A0,         /* r4: 1st int arg / return value */
	LA64MREG_A1,         /* r5 */
	LA64MREG_A2,         /* r6 */
	LA64MREG_A3,         /* r7 */
	LA64MREG_A4,         /* r8 */
	LA64MREG_A5,         /* r9 */
	LA64MREG_A6,         /* r10 */
	LA64MREG_A7,         /* r11 */
	LA64MREG_T0,         /* r12 */
	LA64MREG_T1,         /* r13 */
	LA64MREG_T2,         /* r14 */
	LA64MREG_T3,         /* r15 */
	LA64MREG_T4,         /* r16 */
	LA64MREG_T5,         /* r17 */
	LA64MREG_T6,         /* r18 */
	LA64MREG_T7,         /* r19 */
	LA64MREG_T8,         /* r20 */
	LA64MREG_RESERVED,   /* r21: reserved (not allocatable) */
	LA64MREG_FP,         /* r22: frame pointer */
	LA64MREG_S0,         /* r23 */
	LA64MREG_S1,         /* r24 */
	LA64MREG_S2,         /* r25 */
	LA64MREG_S3,         /* r26 */
	LA64MREG_S4,         /* r27 */
	LA64MREG_S5,         /* r28 */
	LA64MREG_S6,         /* r29 */
	LA64MREG_S7,         /* r30 */
	LA64MREG_S8,         /* r31 */
	/* FPRs: f0-f31 (fa0-fa7 = f0-f7, ft0-ft15 = f8-f23, fs0-fs7 = f24-31) */
	LA64MREG_F0 = 32,
	LA64MREG_F1, LA64MREG_F2, LA64MREG_F3,
	LA64MREG_F4, LA64MREG_F5, LA64MREG_F6, LA64MREG_F7,
	LA64MREG_F8, LA64MREG_F9, LA64MREG_F10, LA64MREG_F11,
	LA64MREG_F12, LA64MREG_F13, LA64MREG_F14, LA64MREG_F15,
	LA64MREG_F16, LA64MREG_F17, LA64MREG_F18, LA64MREG_F19,
	LA64MREG_F20, LA64MREG_F21, LA64MREG_F22, LA64MREG_F23,
	LA64MREG_F24, LA64MREG_F25, LA64MREG_F26, LA64MREG_F27,
	LA64MREG_F28, LA64MREG_F29, LA64MREG_F30, LA64MREG_F31,
	LA64MREG_NREG,
} LA64MREG;

/* argreg table: a0-a7 (ints), then fa0-fa7 (floats); -1 terminates. */
extern const int la64_argreg[17];

#endif /* MCC_LOONGARCH64_M_H */
