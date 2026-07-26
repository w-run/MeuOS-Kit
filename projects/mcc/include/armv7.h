#include "ir.h"

typedef struct Arm32Op Arm32Op;

enum Arm32Reg {
	R0 = RXX + 1, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12,
	SP, LR,
	D0, D1, D2, D3, D4, D5, D6, D7,
	D8, D9, D10, D11, D12, D13, D14, D15,
	NFPR = D15 - D0 + 1,
	NGPR = LR - R0 + 1,
	NGPS = 5,
	NFPS = D7 - D0 + 1,
	NCLR = 15,
};
MAKESURE(reg_not_tmp, D15 < (int)Tmp0);

struct Arm32Op { char imm; };

extern int arm32_op[];
extern int arm32_rsave[];
extern int arm32_rclob[];
bits arm32_retregs(Ref, int[2]);
bits arm32_argregs(Ref, int[2]);
void arm32_abi(Fn *);
void arm32_isel(Fn *);
void arm32_emitfn(Fn *, FILE *);
