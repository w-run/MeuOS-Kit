/* reloc.c — ARM (armv7) relocation type definitions. */
#include <stdint.h>

#define R_ARM_NONE        0
#define R_ARM_PC24        1
#define R_ARM_ABS32       2
#define R_ARM_REL32       3
#define R_ARM_LDR_PC_G0   4
#define R_ARM_ABS16       5
#define R_ARM_ABS12       6
#define R_ARM_THM_ABS5    7
#define R_ARM_ABS8        8
#define R_ARM_SBREL32     9
#define R_ARM_THM_CALL    10
#define R_ARM_THM_PC8     11
#define R_ARM_BREL_ADJ    12
#define R_ARM_TLS_DESC    13
#define R_ARM_PREL31      16
#define R_ARM_COPY        20
#define R_ARM_GLOB_DAT    21
#define R_ARM_JUMP_SLOT   22
#define R_ARM_RELATIVE    23
#define R_ARM_GOTOFF      24
#define R_ARM_GOTPC       25
#define R_ARM_GOT32       26
#define R_ARM_PLT32       27
#define R_ARM_CALL        28
#define R_ARM_JUMP24      29
#define R_ARM_THM_JUMP24  30
#define R_ARM_BASE_ABS    31
#define R_ARM_ALU_PCREL_0 32
#define R_ARM_MOVW_ABS_NC 43
#define R_ARM_MOVT_ABS    44
#define R_ARM_MOVW_PREL_NC 45
#define R_ARM_MOVT_PREL   46
#define R_ARM_THM_MOVW_ABS_NC 47
#define R_ARM_THM_MOVT_ABS 48
#define R_ARM_THM_MOVW_PREL_NC 49
#define R_ARM_THM_MOVT_PREL 50
#define R_ARM_TLS_GD32    104
#define R_ARM_TLS_LDM32   105
#define R_ARM_TLS_LDO32   106
#define R_ARM_TLS_LE32    107
#define R_ARM_TLS_IE32    107
#define R_ARM_TLS_TPOFF32 108
#define R_ARM_GOT_PREL    96

const char *
arm_reloc_name(unsigned type)
{
	switch (type) {
	case R_ARM_NONE: return "R_ARM_NONE";
	case R_ARM_PC24: return "R_ARM_PC24";
	case R_ARM_ABS32: return "R_ARM_ABS32";
	case R_ARM_REL32: return "R_ARM_REL32";
	case R_ARM_LDR_PC_G0: return "R_ARM_LDR_PC_G0";
	case R_ARM_THM_CALL: return "R_ARM_THM_CALL";
	case R_ARM_GOT32: return "R_ARM_GOT32";
	case R_ARM_PLT32: return "R_ARM_PLT32";
	case R_ARM_CALL: return "R_ARM_CALL";
	case R_ARM_JUMP24: return "R_ARM_JUMP24";
	case R_ARM_ALU_PCREL_0: return "R_ARM_ALU_PCREL_0";
	case R_ARM_MOVW_ABS_NC: return "R_ARM_MOVW_ABS_NC";
	case R_ARM_MOVT_ABS: return "R_ARM_MOVT_ABS";
	case R_ARM_MOVW_PREL_NC: return "R_ARM_MOVW_PREL_NC";
	case R_ARM_MOVT_PREL: return "R_ARM_MOVT_PREL";
	case R_ARM_TLS_GD32: return "R_ARM_TLS_GD32";
	case R_ARM_TLS_LDO32: return "R_ARM_TLS_LDO32";
	case R_ARM_TLS_LE32: return "R_ARM_TLS_LE32";
	case R_ARM_GOT_PREL: return "R_ARM_GOT_PREL";
	default: return "R_ARM_??";
	}
}