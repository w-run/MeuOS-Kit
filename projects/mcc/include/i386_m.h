/* i386_m.h — i386 (32-bit) machine-layer register definitions
 * (MIR-native backend).
 *
 * i386 SysV cdecl ABI: all arguments on the stack.  Return in EAX
 * (32-bit) or EDX:EAX (64-bit).  Callee-saved: EBX, ESI, EDI.
 * Caller-saved: EAX, ECX, EDX.  FP via SSE2 (xmm0-xmm7).
 *
 * Register numbering: GPRs 0..7 (eax, ecx, edx, ebx, esp, ebp, esi,
 * edi), SSE registers 8..15 (xmm0-xmm7).
 */
#ifndef MCC_I386_M_H
#define MCC_I386_M_H

#include "mir.h"

enum I386MREG {
	I386MREG_NONE = -1,
	I386MREG_EAX = 0,  /* return value, caller-save */
	I386MREG_ECX,       /* caller-save */
	I386MREG_EDX,       /* caller-save, return high half */
	I386MREG_EBX,       /* callee-save */
	I386MREG_ESP,       /* stack pointer */
	I386MREG_EBP,       /* frame pointer */
	I386MREG_ESI,       /* callee-save */
	I386MREG_EDI,       /* callee-save */
	I386MREG_XMM0,      /* FP return value */
	I386MREG_XMM1,
	I386MREG_XMM2,
	I386MREG_XMM3,
	I386MREG_XMM4,
	I386MREG_XMM5,
	I386MREG_XMM6,
	I386MREG_XMM7,
	I386MREG_NREG,
};

extern const MTargetM mtarget_i386;

#endif /* MCC_I386_M_H */