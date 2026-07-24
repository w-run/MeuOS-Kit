#ifndef MT_TARGET_H
#define MT_TARGET_H

#include <stddef.h>
#include <stdint.h>

/*
 * Architecture target descriptor.
 *
 * Each supported target architecture has a static instance describing its
 * ELF-level properties.  The descriptor is used by as, ld, and other tools
 * to set the correct e_machine, e_ident class, and section header sizes
 * without hardcoding per-arch values in every output path.
 */

struct mt_insn {
	int    ok;               /* 0 = parse/encode failed, 1 = ok */
	int    fixed;            /* 1 = instruction is fully resolved */
	unsigned char bytes[16]; /* encoded instruction bytes */
	size_t size;             /* number of valid bytes */
	/* Optional pending fixup (when operand references a symbol) */
	int         fixup_section;   /* section index or -1 if none */
	size_t      fixup_offset;    /* byte offset within bytes[] */
	unsigned    fixup_width;     /* 1/2/4 bytes */
	unsigned    reloc_type;      /* relocation type constant */
	const char *fixup_symbol;    /* symbol name (must stay valid) */
	int64_t     fixup_addend;
};

struct mt_target {
	const char *name;       /* canonical name: "x86_64", "aarch64", …   */
	uint16_t emachine;      /* ELF e_machine (EM_X86_64 = 62, …)       */
	uint8_t  elf_class;     /* ELFCLASS64 (2) or ELFCLASS32 (1)        */
	uint8_t  elf_endian;    /* ELFDATA2LSB (1)                         */
	uint32_t e_flags;       /* arch-specific ELF e_flags               */
	uint16_t ehdr_size;     /* sizeof(ElfNN_Ehdr): 64 for ELF64, 52…   */
	uint16_t shdr_size;     /* sizeof(ElfNN_Shdr): 64 for ELF64, 40…   */
	/* Instruction encoder: parse mnemonic + operands, fill mt_insn.
	 * Returns 0 on success, -1 on unsupported instruction.  When an
	 * operand references a symbol, the encoder sets mt_insn.fixup_*
	 * fields and the caller creates a relocation entry. */
	int (*encode_insn)(const struct mt_target *target,
	                   const char *mnemonic, const char *operands,
	                   struct mt_insn *out);
};

/* Default x86_64 target descriptor, for backward compatibility. */
extern const struct mt_target mt_target_x86_64;

/* Look up a target by name (e.g. "x86_64", "aarch64").  Returns NULL when
 * the target is not recognised. */
const struct mt_target *mt_target_lookup(const char *name);

#endif /* MT_TARGET_H */
