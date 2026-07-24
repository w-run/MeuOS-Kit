/* reloc.c — i386 relocation type definitions.
 *
 * Relocation types supported by the i386 ELF psABI, v1.1. */
#include <stdint.h>

/* Relocation types */
#define R_386_NONE     0
#define R_386_32       1   /* S + A */
#define R_386_PC32     2   /* S + A - P */
#define R_386_GOT32    3   /* G + A - P (unimplemented) */
#define R_386_PLT32    4   /* L + A - P */
#define R_386_COPY     5   /* (runtime only) */
#define R_386_GLOB_DAT 6   /* (runtime only) */
#define R_386_JUMP_SLOT 7  /* (runtime only) */
#define R_386_RELATIVE 8   /* (runtime only) */
#define R_386_GOTOFF   9   /* S + A - GOT */
#define R_386_GOTPC    10  /* GOT + A - P */
#define R_386_32S      11  /* S + A (sign-extended, same as R_386_32) */
#define R_386_TLS_TPOFF 14
#define R_386_TLS_IE   15
#define R_386_TLS_GOTIE 16
#define R_386_TLS_LE   17
#define R_386_TLS_GD   18
#define R_386_TLS_LDM  19
#define R_386_16       20
#define R_386_PC16     21
#define R_386_8        22
#define R_386_PC8      23

/* Returns the name of a relocation type, or NULL for unknown. */
const char *
i386_reloc_name(unsigned type)
{
	switch (type) {
	case R_386_NONE: return "R_386_NONE";
	case R_386_32:   return "R_386_32";
	case R_386_PC32: return "R_386_PC32";
	case R_386_GOT32: return "R_386_GOT32";
	case R_386_PLT32: return "R_386_PLT32";
	case R_386_COPY:  return "R_386_COPY";
	case R_386_GOTOFF: return "R_386_GOTOFF";
	case R_386_GOTPC:  return "R_386_GOTPC";
	case R_386_TLS_TPOFF: return "R_386_TLS_TPOFF";
	case R_386_TLS_GD:   return "R_386_TLS_GD";
	default: return "R_386_??";
	}
}
