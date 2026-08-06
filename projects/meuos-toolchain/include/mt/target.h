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
	/* Optional pending fixups (when operand references a symbol).
	 * fixup2 is used by pseudo-instructions that expand to two
	 * instructions (e.g. riscv la: auipc + addi). */
	int         fixup_section;   /* section index or -1 if none */
	size_t      fixup_offset;    /* byte offset within bytes[] */
	unsigned    fixup_width;     /* 1/2/4 bytes */
	unsigned    reloc_type;      /* relocation type constant */
	const char *fixup_symbol;    /* symbol name (must stay valid) */
	int64_t     fixup_addend;
	int         fixup2_present;  /* non-zero when fixup2 is active */
	size_t      fixup2_offset;
	unsigned    fixup2_width;
	unsigned    reloc_type2;
	const char *fixup2_symbol;
	int64_t     fixup2_addend;
	/* ISA feature bits (MT_FEATURE_*) this instruction requires beyond
	 * the baseline for its architecture.  The assembler compares these
	 * against the active feature set and rejects the instruction when
	 * a required bit is disabled (ISA gating).  Encoders set this to 0
	 * for baseline instructions. */
	uint64_t    required_features;
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
	/* DWARF .eh_frame parameters (CIE/FDE generation).
	 * These fields are populated per-architecture so that the assembler
	 * emits correct CIE and FDE entries without hardcoded x86_64 values. */
	uint8_t  dwarf_ra_reg;       /* CIE return-address register number */
	uint8_t  dwarf_code_align;   /* CIE code alignment factor (ULEB128, >=1) */
	int8_t   dwarf_data_align;   /* CIE data alignment factor (SLEB128, negative) */
	uint8_t  dwarf_fde_encoding; /* FDE address encoding (DW_EH_PE_*) */
	unsigned dwarf_fde_reloc;    /* relocation type for FDE initial_loc */

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

/* Map a -march= value to an ISA feature bitmask for the given architecture
 * name.  Returns 0 when the value is not recognised (caller keeps baseline).
 * Examples (x86_64): "x86-64-v2"/"v2", "x86-64-v3"/"v3", "x86-64-v4"/"v4".
 * Other architectures currently return 0 (baseline only). */
uint64_t mt_target_features_for_march(const char *arch, const char *march);

/* Return a stack-allocated copy of the base target with its ISA feature
 * bitmask replaced by `features`.  Used by the assembler to apply -march=
 * without mutating the shared static descriptor. */
struct mt_target mt_target_clone_with_features(const struct mt_target *base,
                                                uint64_t features);

/* Human-readable name of the lowest ISA feature bit set in `mask` that is
 * missing from `have`.  Returns NULL when all required bits are present.
 * Used to build precise ISA-gating diagnostics. */
const char *mt_feature_name_missing(uint64_t have, uint64_t required);

#endif /* MT_TARGET_H */
