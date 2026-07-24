/* reloc.c — LoongArch relocation type definitions (ELF psABI v2.x).
 *
 * Values verified against /usr/include/elf.h (binutils 2.41).
 * NOTE: R_LARCH_B16/B21/B26 live at 64-66 (not 6-8) because LA64 uses
 * relocation type space differently from other architectures. */
#include <stdint.h>

#define R_LARCH_NONE        0
#define R_LARCH_32          1
#define R_LARCH_64          2
#define R_LARCH_RELATIVE    3
#define R_LARCH_COPY        4
#define R_LARCH_JUMP_SLOT   5

/* Branch relocations (offset in psABI) */
#define R_LARCH_B16        64
#define R_LARCH_B21        65
#define R_LARCH_B26        66

/* Absolute address relocations */
#define R_LARCH_ABS_HI20   67
#define R_LARCH_ABS_LO12   68
#define R_LARCH_ABS64_LO20 69
#define R_LARCH_ABS64_HI12 70

/* PC-relative address relocations */
#define R_LARCH_PCALA_HI20 71
#define R_LARCH_PCALA_LO12 72
#define R_LARCH_PCALA64_LO20 73
#define R_LARCH_PCALA64_HI12 74

/* GOT relocations */
#define R_LARCH_GOT_PC_HI20  75
#define R_LARCH_GOT_PC_LO12  76
#define R_LARCH_GOT64_PC_LO20 77
#define R_LARCH_GOT64_PC_HI12 78
#define R_LARCH_GOT_HI20     79
#define R_LARCH_GOT_LO12     80

/* TLS LE (local exec) relocations */
#define R_LARCH_TLS_LE_HI20   83
#define R_LARCH_TLS_LE_LO12   84
#define R_LARCH_TLS_LE64_LO20 85
#define R_LARCH_TLS_LE64_HI12 86

/* TLS IE (initial exec) relocations */
#define R_LARCH_TLS_IE_PC_HI20  87
#define R_LARCH_TLS_IE_PC_LO12  88

/* TLS GD (global dynamic) relocations */
#define R_LARCH_TLS_GD_PC_HI20 97
#define R_LARCH_TLS_GD_HI20    98

/* Marker / relaxation */
#define R_LARCH_RELAX     100
#define R_LARCH_DELETE    101
#define R_LARCH_ALIGN     102

const char *
la64_reloc_name(unsigned type)
{
	switch (type) {
	case R_LARCH_NONE:    return "R_LARCH_NONE";
	case R_LARCH_32:      return "R_LARCH_32";
	case R_LARCH_64:      return "R_LARCH_64";
	case R_LARCH_RELATIVE: return "R_LARCH_RELATIVE";
	case R_LARCH_COPY:    return "R_LARCH_COPY";
	case R_LARCH_JUMP_SLOT: return "R_LARCH_JUMP_SLOT";
	case R_LARCH_B16:     return "R_LARCH_B16";
	case R_LARCH_B21:     return "R_LARCH_B21";
	case R_LARCH_B26:     return "R_LARCH_B26";
	case R_LARCH_ABS_HI20: return "R_LARCH_ABS_HI20";
	case R_LARCH_ABS_LO12: return "R_LARCH_ABS_LO12";
	case R_LARCH_ABS64_LO20: return "R_LARCH_ABS64_LO20";
	case R_LARCH_ABS64_HI12: return "R_LARCH_ABS64_HI12";
	case R_LARCH_PCALA_HI20: return "R_LARCH_PCALA_HI20";
	case R_LARCH_PCALA_LO12: return "R_LARCH_PCALA_LO12";
	case R_LARCH_GOT_PC_HI20: return "R_LARCH_GOT_PC_HI20";
	case R_LARCH_GOT_PC_LO12: return "R_LARCH_GOT_PC_LO12";
	case R_LARCH_TLS_LE_HI20: return "R_LARCH_TLS_LE_HI20";
	case R_LARCH_TLS_LE_LO12: return "R_LARCH_TLS_LE_LO12";
	case R_LARCH_TLS_LE64_HI12: return "R_LARCH_TLS_LE64_HI12";
	case R_LARCH_TLS_GD_PC_HI20: return "R_LARCH_TLS_GD_PC_HI20";
	case R_LARCH_TLS_GD_HI20: return "R_LARCH_TLS_GD_HI20";
	case R_LARCH_RELAX:   return "R_LARCH_RELAX";
	case R_LARCH_DELETE:  return "R_LARCH_DELETE";
	case R_LARCH_ALIGN:   return "R_LARCH_ALIGN";
	default: return "R_LARCH_??";
	}
}
