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

/* ISA feature bit definitions (for struct mt_target.features).
 * Architecture-specific bits use the upper 32 bits (32-63). */
#define MT_FEATURE_SSE       (1ULL << 0)  /* x86 SSE */
#define MT_FEATURE_SSE2      (1ULL << 1)  /* x86 SSE2 */
#define MT_FEATURE_SSE3      (1ULL << 2)  /* x86 SSE3 */
#define MT_FEATURE_SSSE3     (1ULL << 3)  /* x86 SSSE3 */
#define MT_FEATURE_SSE4_1    (1ULL << 4)  /* x86 SSE4.1 */
#define MT_FEATURE_SSE4_2    (1ULL << 5)  /* x86 SSE4.2 */
#define MT_FEATURE_AVX       (1ULL << 6)  /* x86 AVX */
#define MT_FEATURE_AVX2      (1ULL << 7)  /* x86 AVX2 */
#define MT_FEATURE_POPCNT    (1ULL << 8)  /* x86 POPCNT */
#define MT_FEATURE_BMI       (1ULL << 9)  /* x86 BMI1/BMI2 */
#define MT_FEATURE_SSE4A     (1ULL << 10) /* x86 SSE4a */
#define MT_FEATURE_FMA       (1ULL << 11) /* x86 FMA3 */
#define MT_FEATURE_AVX512F   (1ULL << 12) /* x86 AVX-512 F */
#define MT_FEATURE_FP16      (1ULL << 13) /* aarch64 FEAT_FP16 */
#define MT_FEATURE_SVE       (1ULL << 14) /* aarch64 SVE */
#define MT_FEATURE_RV_F      (1ULL << 32) /* riscv F extension */
#define MT_FEATURE_RV_D      (1ULL << 33) /* riscv D extension */
#define MT_FEATURE_RV_C      (1ULL << 34) /* riscv C (compressed) */
#define MT_FEATURE_RV_V      (1ULL << 35) /* riscv V (vector) */
#define MT_FEATURE_VFP       (1ULL << 32) /* arm VFP */
#define MT_FEATURE_NEON      (1ULL << 33) /* arm NEON */
#define MT_FEATURE_THUMB     (1ULL << 34) /* arm Thumb */
#define MT_FEATURE_LSX       (1ULL << 32) /* loongarch LSX */
#define MT_FEATURE_LASX      (1ULL << 33) /* loongarch LASX */

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
	uint64_t features;      /* ISA feature bitmask (0 = baseline only) */
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
