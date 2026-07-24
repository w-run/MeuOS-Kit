/* reloc.c — LoongArch relocation type definitions (psABI v1.0). */
#include <stdint.h>

#define R_LARCH_NONE       0
#define R_LARCH_32         1
#define R_LARCH_64         2
#define R_LARCH_RELATIVE   3
#define R_LARCH_COPY       4
#define R_LARCH_JUMP_SLOT  5
#define R_LARCH_B16        6
#define R_LARCH_B21        7
#define R_LARCH_B26        8
#define R_LARCH_ABS_HI20   9
#define R_LARCH_ABS_LO12   10
#define R_LARCH_ABS64_LO20 11
#define R_LARCH_ABS64_HI12 12
#define R_LARCH_PCALA_HI20 20  /* auipc-like: Page(S+A-P)>>12 */
#define R_LARCH_PCALA_LO12 21  /* (S+A) & 0xFFF -> 12-bit immediate */
#define R_LARCH_GOTPC_HI20 24  /* GOT page address */
#define R_LARCH_GOT64_PC_LO20 28
#define R_LARCH_GOT64_HI12 29
#define R_LARCH_GOT64_LO20 30
#define R_LARCH_TLS_LE_HI20  47
#define R_LARCH_TLS_LE_ADD_LO12 49
#define R_LARCH_TLS_GD_PC_HI20 60
#define R_LARCH_TLS_GD_PC_LO12 61

const char *
la64_reloc_name(unsigned type)
{
	switch (type) {
	case R_LARCH_NONE: return "R_LARCH_NONE";
	case R_LARCH_32:   return "R_LARCH_32";
	case R_LARCH_64:   return "R_LARCH_64";
	case R_LARCH_B16:  return "R_LARCH_B16";
	case R_LARCH_B21:  return "R_LARCH_B21";
	case R_LARCH_B26:  return "R_LARCH_B26";
	case R_LARCH_ABS_HI20: return "R_LARCH_ABS_HI20";
	case R_LARCH_ABS_LO12: return "R_LARCH_ABS_LO12";
	case R_LARCH_PCALA_HI20: return "R_LARCH_PCALA_HI20";
	case R_LARCH_PCALA_LO12: return "R_LARCH_PCALA_LO12";
	case R_LARCH_GOTPC_HI20: return "R_LARCH_GOTPC_HI20";
	case R_LARCH_TLS_LE_HI20: return "R_LARCH_TLS_LE_HI20";
	case R_LARCH_TLS_LE_ADD_LO12: return "R_LARCH_TLS_LE_ADD_LO12";
	case R_LARCH_TLS_GD_PC_HI20: return "R_LARCH_TLS_GD_PC_HI20";
	case R_LARCH_TLS_GD_PC_LO12: return "R_LARCH_TLS_GD_PC_LO12";
	default: return "R_LARCH_??";
	}
}
