#include "ir.h"

typedef struct I386Op I386Op;

enum I386Reg {
	/* caller-save */
	EAX = RXX + 1, /* return value, div dividend */
	ECX,           /* caller-save, shift uses CL */
	EDX,           /* caller-save, div uses edx:eax */

	/* callee-save */
	EBX,
	ESI,
	EDI,

	/* globally live */
	EBP, /* frame pointer */
	ESP, /* stack pointer */

	NFPR = 0, /* no floating-point registers */
	NGPR = ESP - EAX + 1,
	NFPS = 0,

	NGPS = 3, /* EAX, ECX, EDX are caller-save */
	NCLR = 3, /* EBX, ESI, EDI are callee-save */
};
MAKESURE(reg_not_tmp, ESP < (int)Tmp0);

struct I386Op {
	char nmem;
	char zflag;
	char lflag;
};

/* targ.c */
extern I386Op i386_op[];

/* sysv.c (abi) */
extern int i386_sysv_rsave[];
extern int i386_sysv_rclob[];
bits i386_sysv_retregs(Ref, int[2]);
bits i386_sysv_argregs(Ref, int[2]);
void i386_sysv_abi(Fn *);

/* isel.c */
void i386_isel(Fn *);

/* emit.c */
void i386_sysv_emitfn(Fn *, FILE *);
