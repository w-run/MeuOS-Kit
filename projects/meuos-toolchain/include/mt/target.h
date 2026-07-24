#ifndef MT_TARGET_H
#define MT_TARGET_H

#include <stdint.h>

/*
 * Architecture target descriptor.
 *
 * Each supported target architecture has a static instance describing its
 * ELF-level properties.  The descriptor is used by as, ld, and other tools
 * to set the correct e_machine, e_ident class, and section header sizes
 * without hardcoding per-arch values in every output path.
 */

struct mt_target {
	const char *name;       /* canonical name: "x86_64", "aarch64", …   */
	uint16_t emachine;      /* ELF e_machine (EM_X86_64 = 62, …)       */
	uint8_t  elf_class;     /* ELFCLASS64 (2) or ELFCLASS32 (1)        */
	uint8_t  elf_endian;    /* ELFDATA2LSB (1)                         */
	uint32_t e_flags;       /* arch-specific ELF e_flags               */
	uint16_t ehdr_size;     /* sizeof(ElfNN_Ehdr): 64 for ELF64, 52…   */
	uint16_t shdr_size;     /* sizeof(ElfNN_Shdr): 64 for ELF64, 40…   */
};

/* Default x86_64 target descriptor, for backward compatibility. */
extern const struct mt_target mt_target_x86_64;

/* Look up a target by name (e.g. "x86_64", "aarch64").  Returns NULL when
 * the target is not recognised. */
const struct mt_target *mt_target_lookup(const char *name);

#endif /* MT_TARGET_H */
