#include "ir.h"

typedef struct La64Op La64Op;

enum La64Reg {
	T0 = RXX + 1, T1, T2, T3, T4, T5, T6, T7, T8,
	A0, A1, A2, A3, A4, A5, A6, A7,
	S0, S1, S2, S3, S4, S5, S6, S7, S8,
	FP, S9 = FP, SP, TP, RA,
	FT0, FT1, FT2, FT3, FT4, FT5, FT6, FT7, FT8, FT9, FT10,
	FT11, FT12, FT13, FT14, FT15,
	FA0, FA1, FA2, FA3, FA4, FA5, FA6, FA7,
	FS0, FS1, FS2, FS3, FS4, FS5, FS6, FS7,
	NFPR = FS7 - FT0 + 1,
	NGPR = RA - T0 + 1,
	NGPS = A7 - T0 + 1,
	NFPS = FA7 - FT0 + 1,
	NCLR = (S8 - S0 + 1) + (FS7 - FS0 + 1),
};
MAKESURE(reg_not_tmp, FS7 < (int)Tmp0);

struct La64Op {
	char imm;
};

/* targ.c */
extern int la64_rsave[];
extern int la64_rclob[];
extern La64Op la64_op[];

/* abi.c */
bits la64_retregs(Ref, int[2]);
bits la64_argregs(Ref, int[2]);
void la64_abi(Fn *);

/* isel.c */
void la64_isel(Fn *);

/* emit.c */
void la64_emitfn(Fn *, FILE *);
