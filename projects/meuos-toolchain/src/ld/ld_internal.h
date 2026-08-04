/* ld_internal.h - internal cross-file declarations for the mt/ld linker.
 *
 * Split out of link.c so the large linker file could be layered into
 * per-stage submodules (dynamic / rela / layout / elfout).  All linker
 * state lives in `struct ld_context` (defined here); submodules operate
 * on a context passed by pointer, so there is no file-level global state
 * to share.
 */
#ifndef MEUOS_TOOLCHAIN_LD_INTERNAL_H
#define MEUOS_TOOLCHAIN_LD_INTERNAL_H

#include "mt/ld.h"
#include "mt/archive.h"
#include "mt/elf.h"
#include "mt/elf32.h"
#include "mt/target.h"
#include "mt/msys.h"

#include <stdbool.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Per-architecture relocation application (link.c / reloc.c) */
extern int mt_apply_aarch64_reloc(unsigned type, unsigned char *loc,
                                  uint64_t S, int64_t A, uint64_t P);
extern int la64_apply_reloc(unsigned type, unsigned char *loc,
                            uint64_t S, int64_t A, uint64_t P);
extern int riscv64_apply_reloc(unsigned reloc_type, unsigned char *place,
                               uint64_t S, int64_t A, uint64_t P);
extern int i386_apply_reloc(unsigned reloc_type, unsigned char *place,
                            uint64_t S, int64_t A, uint64_t P);
extern int mt_apply_arm_reloc(unsigned type, unsigned char *loc,
                              uint64_t S, int64_t A, uint64_t P);

/* msys VFS support: globals from main.c */
extern struct msys *ld_msys;
extern int msys_vfs_load(const char *path, void **buf, size_t *size);

#define LD_SHF_WRITE 0x1ULL
#define LD_SHF_ALLOC 0x2ULL
#define LD_SHF_EXECINSTR 0x4ULL
#define LD_PT_LOAD 1
#define LD_PF_X 1
#define LD_PF_W 2
#define LD_PF_R 4
#define LD_PAGE 0x1000ULL
#define LD_BASE 0x400000ULL
#define LD_SHN_COMMON 0xfff2
#define LD_STB_LOCAL 0
#define LD_STB_GLOBAL 1
#define LD_STB_WEAK 2
#define LD_STB_SHIFT 4
#define LD_R_X86_64_64 1
#define LD_R_X86_64_PC32 2
#define LD_R_X86_64_PLT32 4
#define LD_R_X86_64_GOTPCREL 9
#define LD_R_X86_64_REX_GOTPCRELX 42
#define LD_R_X86_64_32 10
#define LD_R_X86_64_32S 11
#define LD_R_X86_64_TPOFF32 23
/* Dynamic TLS relocations (GD/LD model).  When linking a shared library
 * (ET_DYN) these are preserved as RELATIVE/GLOB_DAT-style DTPMOD/DTPOFF
 * entries in .rela.dyn so ld.so can resolve them at load time via the
 * per-module TLS blocks and __tls_get_addr().  When linking a static
 * executable (ET_EXEC) they are relaxed to the Local-Exec TPOFF32 model. */
#define LD_R_X86_64_TLSGD   19  /* General Dynamic: lea sym@tlsgd(%rip), %rdi */
#define LD_R_X86_64_TLSLD   20  /* Local Dynamic:  lea sym@tlsld(%rip), %rdi */
#define LD_R_X86_64_DTPOFF  21  /* DTP-relative offset: sym@dtpoff */
#define LD_R_X86_64_DTPMOD  16  /* Module id: R_X86_64_DTPMOD64 (GOT pair DTPMOD slot) */
#define LD_R_X86_64_TPOFF64 18  /* TP-relative 64-bit (IE GOT slot content) */
#define LD_R_X86_64_GOTTPOFF 22 /* Initial Exec: movq sym@gottpoff(%rip), %r */

/* i386 TLS relocation types (used by the i386 dispatch) */
#define LD_R_386_TLS_GD   18  /* General Dynamic */
#define LD_R_386_TLS_LDM  19  /* Local Dynamic */

struct ld_group {
	char *name;
	uint32_t type;
	uint64_t flags;
	uint64_t align;
	unsigned char *data;
	size_t size;
	size_t capacity;
	uint64_t file_offset;
	uint64_t address;
	int rank;
	int kept;      /* 1 = reachable from roots (for --gc-sections) */
};

struct ld_section_map {
	int group;
	uint64_t offset;
};

struct ld_object {
	char *name;
	unsigned char *data;
	size_t size;
	int elf_class;
	int is_shared;   /* 1 = this is a shared library (ET_DYN) input */
	union {
		struct mt_elf64_view v64;
		struct mt_elf32_view v32;
	} view;
	struct ld_section_map *maps;
	uint16_t symtab_index;
	union {
		struct mt_elf64_section v64;
		struct mt_elf32_section v32;
	} symtab;
	union {
		struct mt_elf64_section v64;
		struct mt_elf32_section v32;
	} strtab;
	union {
		struct mt_elf64_section v64;
		struct mt_elf32_section v32;
	} section_names;
	int has_symtab;
};

struct ld_objects {
	struct ld_object *items;
	size_t count;
	size_t capacity;
};

struct ld_archives {
	char **paths;
	size_t count;
	size_t capacity;
};

struct ld_global {
	char *name;
	struct ld_object *object;
	uint64_t symbol_index;
	int group;
	uint64_t offset;
	uint64_t size;
	uint64_t align;
	int defined;
	int weak;
	int common;
	int absolute;  /* 1 = value is an absolute address (from --defsym) */
	struct ld_global *alias; /* non-NULL: this global redirects to alias target
	                         * (used by --wrap). symbol_value() and other
	                         * resolvers follow the alias chain automatically. */
};

struct ld_globals {
	struct ld_global *items;
	size_t count;
	size_t capacity;
};

struct ld_got_entry {
	char *name;
	uint64_t offset;
	int reloc_type;  /* 0=RELATIVE(default), MT_R_X86_64_JUMP_SLOT for PLT imports */
	int tls;         /* 1=TLS GOT entry (GD/LD DTPMOD/DTPOFF pair, or IE TPOFF) */
	int slots;       /* number of 8-byte slots this entry occupies (TLS GD/LD=2) */
	uint64_t plt_offset; /* byte offset of this entry's PLT stub within .plt (JUMP_SLOT only) */
};

struct ld_got {
	struct ld_got_entry *items;
	size_t count;
	size_t capacity;
	int group;
};

/* One exported symbol recorded for the dynamic symbol table (.dynsym).
 * The actual .dynsym entry bytes are filled in after layout (when the final
 * virtual addresses are known), so we keep the metadata here. */
struct ld_dynsym_entry {
	struct ld_global *global;  /* back-pointer into ctx->globals */
	uint32_t dynstr_offset;    /* offset of the name within .dynstr */
	int stt;                   /* STT_FUNC / STT_OBJECT / STT_TLS */
};

/* A static-executable TLS GD/LD descriptor: points into .data where a
 * 16-byte `tls_index{ti_module=1, ti_offset}` lives for one TLS symbol. */
struct ld_tls_desc {
	char *name;
	int group;        /* .data group index */
	uint64_t offset;  /* offset of the descriptor within the group */
};

struct ld_context {
	const struct mt_target *target;
	struct ld_objects objects;
	struct ld_archives archives;
	struct ld_group *groups;
	size_t group_count;
	size_t group_capacity;
	struct ld_globals globals;
	struct ld_got got;
	int tls_tdata_group;
	int tls_tbss_group;
	uint64_t tls_tdata_size;
	uint64_t tls_size;
	uint64_t tls_align;
	/* Static-executable GD/LD TLS descriptor table (方案B): one 16-byte
	 * `tls_index{ti_module=1, ti_offset}` per GD/LD symbol in .data, letting
	 * the retained `call __tls_get_addr` resolve tp + ti->ti_offset. */
	struct ld_tls_desc *tls_descs;
	size_t tls_desc_count;
	size_t tls_desc_capacity;
	int shared;          /* 1 = ET_DYN (shared library), 0 = ET_EXEC */
	int pie;             /* 1 = PIE (ET_DYN + PT_INTERP) */
	int build_id;        /* 1 = generate .note.gnu.build-id */
	int eh_frame_hdr;    /* 1 = generate .eh_frame_hdr */
	int whole_archive;   /* 1 = force-extract all archive members */
	int as_needed;        /* 1 = --as-needed, 0 = --no-as-needed */
	int no_undefined;    /* 1 = error on undefined symbols */
	int gc_sections;     /* 1 = garbage-collect unused sections */
	int print_map;       /* 1 = output link map */
	int cref;            /* 1 = output cross-reference table */
	const char *link_script; /* path to section layout script */
	const char *soname;  /* DT_SONAME for shared lib (may be NULL) */
	const char *dynamic_linker; /* PT_INTERP path (may be NULL) */
	/* Dynamic symbol table bookkeeping (shared libs only) */
	struct ld_dynsym_entry *dynsym_entries;
	size_t dynsym_count;
	size_t dynsym_capacity;
	uint64_t dynsym_data_offset;   /* offset of first entry within .dynsym group */
	uint32_t soname_dynstr_offset; /* .dynstr offset of DT_SONAME string (0 if none) */
	/* --add-needed sonames: stored as (.dynstr_offset, .dynstr_offset, ...) */
	const char *const *add_needed;
	size_t add_needed_count;
	uint32_t *needed_dynstr_offsets; /* malloc'd array of .dynstr offsets */
	/* --version-script symbol export list */
	const char *const *version_script;
	size_t version_script_count;
	/* In-memory archive data (for VFS .msys archives) */
	unsigned char **archive_mem_data;
	size_t *archive_mem_size;
	size_t archive_mem_count;
	size_t archive_mem_capacity;
	/* .rela.dyn in-place fill: rela_count is the next slot index; the
	 * section buffer is pre-reserved in build_dynamic_tables (so layout
	 * assigns the correct file offset / size) and filled incrementally by
	 * build_rela_dyn().  Indexing by slot is required so that growing the
	 * dynamic relocation list can never overflow the section and clobber
	 * the adjacent .dynamic section in the output file. */
	size_t rela_count;         /* number of RELA entries written so far */
	size_t rela_capacity_entries; /* reserved slot count in .rela.dyn */
	/* Linker options (copied from struct mt_ld_options) */
	const char *output;     /* output file path */
	const char *entry;      /* entry symbol (default "_start") */
	char error[512];
};
/* Low-level helpers defined in link.c, shared across the ld submodules. */
void *ld_malloc(size_t size);
void *ld_realloc(void *old, size_t size);
char *ld_strdup(const char *text);
int ld_error(struct ld_context *ctx, const char *message);
int ld_errorf(struct ld_context *ctx, const char *prefix, const char *name);
uint16_t read16(const unsigned char *p);
uint32_t read32(const unsigned char *p);
uint64_t read64(const unsigned char *p);
void write16(unsigned char *p, uint16_t value);
void write32(unsigned char *p, uint32_t value);
void write64(unsigned char *p, uint64_t value);
uint64_t align_up(uint64_t v, uint64_t a);
int find_group(struct ld_context *ctx, const char *name);
int get_group(struct ld_context *ctx, const char *name, uint32_t type,
              uint64_t flags, uint64_t align);
int append_group_data(struct ld_context *ctx, struct ld_group *group,
                      const unsigned char *data, size_t size, uint64_t align,
                      uint64_t *section_offset);
struct ld_global *find_global(struct ld_context *ctx, const char *name);
struct ld_global *get_global(struct ld_context *ctx, const char *name);

/* Dynamic-symbol/.dynamic construction (defined in dynamic.c). */
int build_dynamic_tables(struct ld_context *ctx);
int fill_dynamic_addresses(struct ld_context *ctx);
int ensure_dynamic_section(struct ld_context *ctx);
int ensure_pie_section(struct ld_context *ctx);
int build_rela_dyn(struct ld_context *ctx);
int rela_dyn_add(struct ld_context *ctx, uint64_t offset, uint64_t info,
                 int64_t addend);

#endif /* MEUOS_TOOLCHAIN_LD_INTERNAL_H */
