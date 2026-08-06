#ifndef MT_AS_INT_H
#define MT_AS_INT_H

#include "mt/elf.h"
#include "mt/target.h"

#include <stdint.h>
#include <stddef.h>

/* Shared internal structures used by the assembler and per-arch backends.
 * Defined in src/as/assemble.c, consumed by per-arch assembly files. */

struct as_section {
	char *name;
	uint32_t type;
	uint64_t flags;
	uint64_t align;
	unsigned char *data;
	size_t size;
	size_t capacity;
};

struct as_symbol {
	char *name;
	int section;
	uint64_t value;
	uint64_t size;
	unsigned bind;
	unsigned type;
	unsigned other;
	int defined;
	uint32_t output_index;
};

struct as_fixup {
	int section;
	size_t offset;
	unsigned type;
	unsigned width;
	int64_t addend;
	char *symbol;
	char *symbol2;   /* second operand of a symbol-difference fixup
	                    * (e.g. .4byte symA - symB); NULL for normal fixups */
};

struct as_operand {
	enum { OP_INVALID, OP_IMM, OP_REG, OP_MEM, OP_SYMBOL } kind;
	int reg;
	int width;
	int base;
	int index;
	int scale;
	int64_t displacement;
	char *symbol;
	char modifier[16];
	int64_t addend;
};

struct as_file {
	struct as_section *sections;
	size_t section_count;
	size_t section_capacity;
	struct as_symbol *symbols;
	size_t symbol_count;
	size_t symbol_capacity;
	struct as_fixup *fixups;
	size_t fixup_count;
	size_t fixup_capacity;
	int current;
	int section_stack[16];   /* for .pushsection/.popsection */
	int section_stack_depth;
	int cond_stack[16];     /* 0=active, 1=skipping (.if false) */
	int cond_depth;
	int rept_count;         /* remaining iterations for .rept (0 = not in rept) */
	long rept_pos;          /* ftell position at `rept line (to re-read) */
	const char *filename;
	unsigned line;
	char error[256];
	size_t numeric_counts[10];
	int block_comment;
	const struct mt_target *target;

	/* DWARF .file state */
	struct as_dwarf_file {
		int index;
		char *name;
	} *dwarf_files;
	size_t dwarf_file_count, dwarf_file_capacity;

	/* DWARF .loc state (buffered locs) */
	struct as_dwarf_loc {
		int section;
		uint64_t offset;
		int file;
		unsigned line;
		unsigned column;
	} *dwarf_locs;
	size_t dwarf_loc_count, dwarf_loc_capacity;

	/* CFI state */
	int cfi_active;
	unsigned char *cfi_prog;
	size_t cfi_prog_size, cfi_prog_capacity;
	uint64_t cfi_func_start;

	/* Personality routine (per-CIE, set by .cfi_personality) */
	int cfi_personality_set;
	uint8_t cfi_personality_encoding;
	char *cfi_personality_symbol;

	/* LSDA info (per-CIE encoding, per-FDE pointer) */
	int cfi_lsda_set;
	uint8_t cfi_lsda_encoding;
	char *cfi_lsda_current;   /* LSDA symbol for current FDE */
	char **cfi_lsda_pointers;  /* per-FDE LSDA symbol names */

	/* Completed FDE list */
	uint64_t *cfi_func_offsets;
	uint64_t *cfi_func_end;
	char **cfi_func_labels;
	unsigned char **cfi_fde_progs;
	size_t *cfi_fde_sizes;
	size_t cfi_fde_count, cfi_fde_capacity;

	/* Multi-CIE support: per-FDE personality tracking */
	int *cfi_fde_personality_set;
	uint8_t *cfi_fde_personality_encoding;
	char **cfi_fde_personality_symbol;
	int *cfi_fde_signal_frame;

	/* .cfi_signal_frame: mark current CIE as signal frame */
	int cfi_signal_frame;

	/* .cfi_sections: select output section (0=.eh_frame, 1=.debug_frame) */
	int cfi_section_type;
};

/* Functions shared between assemble.c and arch backends */
int as_append_bytes(struct as_file *as, struct as_section *section,
                    const void *data, size_t size);
int as_add_fixup(struct as_file *as, struct as_section *section,
                 size_t offset, unsigned width, unsigned type,
                 int64_t addend, const char *symbol);
int as_add_fixup_diff(struct as_file *as, struct as_section *section,
                      size_t offset, unsigned width, unsigned type,
                      int64_t addend, const char *symbol,
                      const char *symbol2);
int as_emit_le(struct as_file *as, struct as_section *section,
               uint64_t value, unsigned width);
int as_emit_u8(struct as_file *as, struct as_section *section,
               unsigned value);
int as_error(struct as_file *as, const char *format, ...);

/* Per-arch instruction emitter dispatcher */
int as_emit_instruction(struct as_file *as, char *mnemonic, char *operand_text);

/* aarch64 instruction emitter */
int as_emit_aarch64(struct as_file *as, char *mnemonic, char *operand_text);

/* Arena/allocation and section helpers defined in assemble.c, shared
 * with the extracted as_dwarf.c. */
void *mt_malloc(size_t size);
void *mt_realloc(void *old, size_t size);
char *mt_strdup(const char *text);
int get_section(struct as_file *as, const char *name);

/* DWARF debug-information emission (defined in as_dwarf.c). */
int emit_dwarf(struct as_file *as);

/* Operand / directive parsing helpers shared between assemble.c and
 * the extracted as_parse.c. */
char *trim(char *text);
int align_section(struct as_file *as, struct as_section *section,
                  uint64_t align);
int append_zeroes(struct as_file *as, struct as_section *section,
                  size_t count);
struct as_symbol *get_symbol(struct as_file *as, const char *name);
int parse_integer(const char *text, int64_t *value);
int parse_reference(const char *text, char **symbol, char *modifier,
                    size_t modifier_size, int64_t *addend, int *is_number);

/* Assembler directive parsing (defined in as_parse.c). */
int parse_directive(struct as_file *as, char *directive, char *rest);

/* ELF ET_REL output writing (defined in as_elfout.c). */
int write_object(struct as_file *as, const struct mt_target *target,
                 const char *output_path);

/* Symbol lookup (defined in assemble.c, shared with elfout.c). */
struct as_symbol *find_symbol(struct as_file *as, const char *name);

/* ELF symbol-table output record (used by as_elfout.c's build_symbols). */
struct elf_sym_out {
	uint32_t name;
	uint8_t info;
	uint8_t other;
	uint16_t section;
	uint64_t value;
	uint64_t size;
};

#define MT_ST_INFO(bind, type) MT_ELF64_ST_INFO(bind, type)

/* ELF ET_REL output structures (defined here, used by as_elfout.c). */
struct out_section {
	const char *name;
	uint32_t type;
	uint64_t flags;
	uint64_t align;
	unsigned char *data;
	size_t size;
	uint64_t file_offset;
	uint32_t link;
	uint32_t info;
	uint64_t entry_size;
	int nobits;
};
struct out_reloc {
	uint64_t offset;
	uint32_t type;
	uint32_t symbol;
	int64_t addend;
};
struct reloc_group {
	struct out_reloc *items;
	size_t count;
	size_t capacity;
};
struct string_table {
	unsigned char *data;
	size_t size;
	size_t capacity;
};

#endif /* MT_AS_INT_H */
