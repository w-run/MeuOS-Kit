/* aarch64/reloc.c — AArch64 ELF relocation type constants. */

/* The relocation constants are provided for reference/documentation.
 * Only R_AARCH64_CALL26 (283), R_AARCH64_ADR_PREL_PG_HI21 (275),
 * R_AARCH64_CONDBR19 (279), and R_AARCH64_ABS64 (257) are actively
 * used by the assembler and linker at this time. */

enum {
	R_AARCH64_NONE           = 0,
	R_AARCH64_ABS64         = 257,
	R_AARCH64_ABS32         = 258,
	R_AARCH64_PREL64        = 260,
	R_AARCH64_PREL32        = 261,
	R_AARCH64_MOVW_UABS_G0_NC = 262,
	R_AARCH64_ADR_PREL_LO21      = 274,
	R_AARCH64_ADR_PREL_PG_HI21   = 275,
	R_AARCH64_ADD_ABS_LO12_NC    = 277,
	R_AARCH64_LDST8_ABS_LO12_NC  = 278,
	R_AARCH64_CONDBR19           = 279,
	R_AARCH64_JUMP26             = 282,
	R_AARCH64_CALL26             = 283,
	R_AARCH64_LDST64_ABS_LO12_NC = 286,
	R_AARCH64_ADR_GOT_PAGE       = 311,
	R_AARCH64_LD64_GOT_LO12_NC   = 312,
	R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21 = 289,
	R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC = 290,
};
