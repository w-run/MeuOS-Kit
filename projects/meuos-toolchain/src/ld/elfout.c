/* elfout.c - mt/ld ELF output writing.
 *
 * Writes the linked output executable / shared object (write_executable):
 * program headers, section headers, .eh_frame_hdr, symbol/string tables,
 * and the dynamic section layout.  Extracted from link.c (per-stage
 * module split; shared state in struct ld_context via ld_internal.h).
 */
#include "ld_internal.h"


static int
strings_init(struct ld_strings *strings)
{
	memset(strings, 0, sizeof(*strings));
	strings->data = (unsigned char *)ld_malloc(1);
	if (!strings->data)
		return -1;
	strings->data[0] = 0;
	strings->size = strings->capacity = 1;
	return 0;
}

static int
strings_add(struct ld_strings *strings, const char *name, uint32_t *offset)
{
	size_t length = strlen(name);
	size_t needed;
	unsigned char *data;
	size_t capacity;
	if (length > UINT32_MAX || strings->size > SIZE_MAX - length - 1)
		return -1;
	needed = strings->size + length + 1;
	if (needed > strings->capacity) {
		capacity = strings->capacity * 2;
		while (capacity < needed)
			capacity *= 2;
		data = (unsigned char *)ld_realloc(strings->data, capacity);
		if (!data)
			return -1;
		strings->data = data;
		strings->capacity = capacity;
	}
	*offset = (uint32_t)strings->size;
	memcpy(strings->data + strings->size, name, length + 1);
	strings->size = needed;
	return 0;
}

#define LD_PT_TLS 7

static int
write_program_header_type(FILE *file, uint32_t type, uint32_t flags,
                          uint64_t offset, uint64_t address,
                          uint64_t file_size, uint64_t memory_size,
                          uint64_t align, int is_elf32)
{
	if (is_elf32) {
		unsigned char p[32] = {0};
		write32(p + 0, type);
		write32(p + 4, (uint32_t)offset);
		write32(p + 8, (uint32_t)address);
		write32(p + 12, (uint32_t)address);
		write32(p + 16, (uint32_t)file_size);
		write32(p + 20, (uint32_t)memory_size);
		write32(p + 24, flags);
		write32(p + 28, (uint32_t)align);
		return fwrite(p, 1, sizeof(p), file) == sizeof(p) ? 0 : -1;
	} else {
		unsigned char p[56] = {0};
		write32(p + 0, type);
		write32(p + 4, flags);
		write64(p + 8, offset);
		write64(p + 16, address);
		write64(p + 24, address);
		write64(p + 32, file_size);
		write64(p + 40, memory_size);
		write64(p + 48, align);
		return fwrite(p, 1, sizeof(p), file) == sizeof(p) ? 0 : -1;
	}
}

static int
write_program_header(FILE *file, uint32_t flags, uint64_t offset,
                     uint64_t address, uint64_t file_size, uint64_t memory_size,
                     int is_elf32)
{
	return write_program_header_type(file, LD_PT_LOAD, flags, offset, address,
	                                file_size, memory_size, LD_PAGE, is_elf32);
}

/* ---- .eh_frame_hdr support (--eh-frame-hdr) ----

 * Parse .eh_frame, extract FDE PC ranges.
 * pc_abs_buf (if non-NULL) receives nfde pairs of {pc_start(8), fde_offset(8)}.
 * Returns number of FDEs found. */

/* DWARF EH pointer encoding constants */
#define DW_EH_PE_absptr  0x00
#define DW_EH_PE_uleb128 0x01
#define DW_EH_PE_udata2  0x02
#define DW_EH_PE_udata4  0x03
#define DW_EH_PE_udata8  0x04
#define DW_EH_PE_sleb128 0x09
#define DW_EH_PE_sdata2  0x0a
#define DW_EH_PE_sdata4  0x0b
#define DW_EH_PE_sdata8  0x0c
#define DW_EH_PE_pcrel   0x10
#define DW_EH_PE_textrel 0x20
#define DW_EH_PE_datarel 0x30
#define DW_EH_PE_funcrel 0x40
#define DW_EH_PE_aligned 0x50

/* Return the byte width of an EH pointer encoding (0 for ULEB/SLEB). */
static int
eh_pe_width(int encoding, int elf64)
{
	switch (encoding & 0x0f) {
	case DW_EH_PE_absptr:  return elf64 ? 8 : 4;
	case DW_EH_PE_udata2:
	case DW_EH_PE_sdata2:  return 2;
	case DW_EH_PE_udata4:
	case DW_EH_PE_sdata4:  return 4;
	case DW_EH_PE_udata8:
	case DW_EH_PE_sdata8:  return 8;
	default:               return 0; /* ULEB128 / SLEB128 / unknown */
	}
}

/* Read an encoded value at *pp, advancing *pp past it.  pe_base is the
 * section-relative base for DW_EH_PE_datarel.  Returns the absolute value. */
static uint64_t
eh_read_encoded(const unsigned char **pp, int encoding,
                uint64_t pe_base, uint64_t cur_addr, int elf64)
{
	const unsigned char *p = *pp;
	int w = eh_pe_width(encoding, elf64);
	uint64_t val;

	if (w == 0) {
		/* ULEB128 / SLEB128 — not commonly used for EH frame, skip */
		*pp = p;
		return 0;
	}

	switch (encoding & 0x0f) {
	case DW_EH_PE_absptr:
		val = elf64 ? read64(p) : read32(p);
		break;
	case DW_EH_PE_udata2: val = read16(p); break;
	case DW_EH_PE_sdata2: val = (uint64_t)(int64_t)(int16_t)read16(p); break;
	case DW_EH_PE_udata4: val = read32(p); break;
	case DW_EH_PE_sdata4: {
		uint32_t raw = read32(p);
		int32_t s32 = (int32_t)raw;
		val = (uint64_t)(int64_t)s32;
		break;
	}
	case DW_EH_PE_udata8: val = read64(p); break;
	case DW_EH_PE_sdata8: val = (uint64_t)(int64_t)read64(p); break;
	default: val = 0; break;
	}
	p += w;

	if (encoding & DW_EH_PE_pcrel) {
		val = cur_addr + val;
	}
	if ((encoding & 0x70) == DW_EH_PE_datarel)
		val = pe_base + val;

	*pp = p;
	return val;
}
/* Parse .eh_frame data and collect FDE entries.
 * fdes is a caller-owned buffer (can be NULL for first pass to count).
 * Returns number of FDEs found. */
static size_t
eh_parse_fdes(const unsigned char *data, size_t size,
              struct eh_frame_fde *fdes, size_t max_fdes,
              uint64_t eh_frame_addr, int elf64)
{
	size_t offset = 0;
	size_t nfde = 0;
	int fde_encoding = DW_EH_PE_absptr; /* default: native pointer */
	int addr_encoding = DW_EH_PE_udata4; /* default: 4-byte */

	while (offset + 8 <= size) {
		uint32_t len = read32(data + offset);
		if (len == 0) break; /* end marker */

		uint32_t end = offset + 4 + len;
		if (end > size) break;

		uint32_t cie_id = read32(data + offset + 4);

		if (cie_id == 0) {
			/* CIE record */
			if (len < 8) { offset = end; continue; }
			const char *aug = (const char *)data + offset + 9;
			size_t aug_len = strlen(aug);
			const unsigned char *p = (const unsigned char *)aug + aug_len + 1;

			/* code alignment factor (ULEB128) */
			uint64_t caf = 0; int shift = 0;
			while (*p & 0x80) { caf |= (uint64_t)(*p++ & 0x7f) << shift; shift += 7; }
			caf |= (uint64_t)(*p++ & 0x7f) << shift;

			/* data alignment factor (SLEB128) */
			int64_t daf = 0; shift = 0; int b;
			do { b = *p++; daf |= (int64_t)(b & 0x7f) << shift; shift += 7; }
			while (b & 0x80);
			if (shift > 7 && (b & 0x40))
				daf |= -(1LL << shift);

			/* return address register */
			p++; /* version 1: 1 byte */

			/* Check augmentation for 'R' (FDE encoding) and 'z' (length) */
			fde_encoding = DW_EH_PE_absptr;
			addr_encoding = DW_EH_PE_udata4;
			int has_z = 0;
			for (const char *ap = aug; *ap; ap++) {
				if (*ap == 'z') has_z = 1;
			}
			if (has_z) {
				/* augmentation length (ULEB128) */
				uint64_t augdata_len = 0; shift = 0;
				while (*p & 0x80) {
					augdata_len |= (uint64_t)(*p++ & 0x7f) << shift;
					shift += 7;
				}
				augdata_len |= (uint64_t)(*p++ & 0x7f) << shift;
				const unsigned char *augstart = p;
				for (const char *ap = aug; *ap; ap++) {
					if (*ap == 'R') {
						fde_encoding = *p++;
						/* Derive address_range encoding (plain width, no pcrel) */
						int w = eh_pe_width(fde_encoding, elf64);
						if (w == 8) addr_encoding = DW_EH_PE_udata8;
						else if (w == 4) addr_encoding = DW_EH_PE_udata4;
						else if (w == 2) addr_encoding = DW_EH_PE_udata2;
					} else if (*ap == 'L') {
						p++; /* LSDA encoding */
					} else if (*ap == 'P') {
						p++; /* personality encoding */
						int pw = eh_pe_width(*p, elf64);
						p++; p += pw;
					}
					/* 'z' is handled by has_z above — no per-loop work needed */
				}
				p = augstart + augdata_len;
			}
			offset = end;
			continue;
		}
		/* FDE record */
		const unsigned char *fp = data + offset + 8;
		uint64_t fde_addr = eh_frame_addr + offset + 8;
		uint64_t pc_start = eh_read_encoded(&fp, fde_encoding,
		                                     eh_frame_addr, fde_addr, elf64);
		eh_read_encoded(&fp, addr_encoding,
		                      eh_frame_addr, 0, elf64);

		if (nfde < max_fdes && fdes) {
			fdes[nfde].pc_start = pc_start;
			fdes[nfde].fde_offset = offset;
			fdes[nfde].has_pc = 1;
		}
		nfde++;

		offset = end;
	}

	return nfde;
}

/* Comparison function for qsort: sort FDEs by pc_start. */
static int
eh_fde_cmp(const void *a, const void *b)
{
	const struct eh_frame_fde *fa = (const struct eh_frame_fde *)a;
	const struct eh_frame_fde *fb = (const struct eh_frame_fde *)b;
	if (fa->pc_start < fb->pc_start) return -1;
	if (fa->pc_start > fb->pc_start) return 1;
	return 0;
}

/* Generate .eh_frame_hdr data with addresses already encoded.
 * hdr_addr is the VMA of the output .eh_frame_hdr section. */
static int
build_eh_frame_hdr(struct ld_context *ctx, unsigned char **out_data,
                   size_t *out_size, uint64_t hdr_addr)
{
	int ehg = find_group(ctx, ".eh_frame");
	if (ehg < 0) { *out_data = NULL; *out_size = 0; return 0; }
	struct ld_group *ehg_g = &ctx->groups[ehg];
	if (ehg_g->size == 0 || ehg_g->type == MT_SHT_NOBITS)
		{ *out_data = NULL; *out_size = 0; return 0; }

	int elf64 = (ctx->target->elf_class == MT_ELFCLASS64);
	size_t nfde = eh_parse_fdes(ehg_g->data, ehg_g->size, NULL, 0,
	                             ehg_g->address, elf64);
	if (nfde == 0) { *out_data = NULL; *out_size = 0; return 0; }

	struct eh_frame_fde *fdes = (struct eh_frame_fde *)
	    ld_malloc(nfde * sizeof(struct eh_frame_fde));
	if (!fdes) return ld_error(ctx, "out of memory");
	eh_parse_fdes(ehg_g->data, ehg_g->size, fdes, nfde,
	              ehg_g->address, elf64);
	qsort(fdes, nfde, sizeof(struct eh_frame_fde), eh_fde_cmp);

	size_t hdr_size = 12 + nfde * 8;
	unsigned char *hdr = (unsigned char *)ld_malloc(hdr_size ? hdr_size : 1);
	if (!hdr) { free(fdes); return ld_error(ctx, "out of memory"); }
	memset(hdr, 0, hdr_size);

	hdr[0] = 1;                       /* version */
	hdr[1] = 0x1b;                    /* .eh_frame ptr: pcrel | sdata4 */
	hdr[2] = 0x03;                    /* FDE count: udata4 */
	hdr[3] = 0x3b;                    /* table: datarel | sdata4 */

	/* .eh_frame pcrel from (hdr_addr + 4) */
	write32(hdr + 4, (uint32_t)(int32_t)((int64_t)ehg_g->address -
	                                      (int64_t)(hdr_addr + 4)));
	write32(hdr + 8, (uint32_t)nfde);

	/* Fill table: datarel from hdr_addr */
	int64_t base = (int64_t)hdr_addr;
	size_t ti;
	for (ti = 0; ti < nfde; ti++) {
		uint64_t fde_va = ehg_g->address + fdes[ti].fde_offset;
		int64_t loc_delta = (int64_t)fdes[ti].pc_start - base;
		int64_t fde_delta = (int64_t)fde_va - base;
		write32(hdr + 12 + ti * 8, (uint32_t)(int32_t)loc_delta);
		write32(hdr + 12 + ti * 8 + 4, (uint32_t)(int32_t)fde_delta);
	}
	free(fdes);
	*out_data = hdr;
	*out_size = hdr_size;
	return 0;
}

int
write_executable(struct ld_context *ctx, const char *path,
                 const char *entry, const struct mt_target *target)
{
	struct ld_group *entry_group = NULL;
	struct ld_global *entry_symbol;
	struct ld_strings shstr;
	struct ld_output_section *sections = NULL;
	uint32_t *name_offsets = NULL;
	unsigned char *eh_frame_hdr_data = NULL; /* generated eh_frame_hdr content */
	size_t eh_frame_hdr_size = 0;
	FILE *file = NULL;
	uint64_t entry_address;
	uint64_t rx_end = LD_PAGE;
	uint64_t rw_start = UINT64_MAX;
	uint64_t file_end = LD_PAGE;
	uint64_t memory_end = LD_PAGE;
	uint64_t alloc_file_end;
	uint64_t section_offset;
	uint32_t shstr_index;
	size_t i;
	int output_count;
	int result = -1;
	/* For shared libraries (ET_DYN) the load base is 0; the dynamic
	 * loader relocates the image at run time. */
	uint64_t base_addr = ctx->shared ? 0 : LD_BASE;
	/* Program header count: PT_LOAD + optional PT_TLS + (shared) PT_PHDR/PT_DYNAMIC. */
	int phnum;
	uint16_t phentsize = (ctx->target->elf_class == 1)
	                       ? MT_ELF32_PHDR_SIZE : MT_ELF64_PHDR_SIZE;

	entry_symbol = NULL;
	entry_address = 0;
	if (entry) {
		entry_symbol = find_global(ctx, entry);
		if (!entry_symbol || !entry_symbol->defined)
			return ld_errorf(ctx, "entry symbol not found", entry);
		entry_group = &ctx->groups[entry_symbol->group];
		entry_address = entry_group->address + entry_symbol->offset;
	}
	for (i = 0; i < ctx->group_count; ++i) {
		struct ld_group *group = &ctx->groups[i];
		if (group->rank < 2)
			rx_end = group->type == MT_SHT_NOBITS ? rx_end :
			          group->file_offset + group->size > rx_end ?
			          group->file_offset + group->size : rx_end;
		else if (rw_start == UINT64_MAX || group->file_offset < rw_start)
			rw_start = group->file_offset;
		if ((group->flags & MT_SHF_ALLOC) &&
		    group->type != MT_SHT_NOBITS &&
		    file_end < group->file_offset + group->size)
			file_end = group->file_offset + group->size;
		if ((group->flags & MT_SHF_ALLOC) &&
		    memory_end < group->file_offset + group->size)
			memory_end = group->file_offset + group->size;
	}
	for (i = 0; i < ctx->group_count; ++i) {
		if ((ctx->groups[i].flags & MT_SHF_ALLOC) &&
		    ctx->groups[i].type == MT_SHT_NOBITS &&
		    memory_end < ctx->groups[i].file_offset + ctx->groups[i].size)
			memory_end = ctx->groups[i].file_offset + ctx->groups[i].size;
	}
	output_count = (int)ctx->group_count + 2; /* null + groups + shstrtab */
	if (ctx->build_id)
		output_count++; /* +1 for .note.gnu.build-id */
	if (ctx->eh_frame_hdr) {
		/* Just check if .eh_frame exists to decide section count.
		 * Actual hdr content is generated later once address is known. */
		int ehg = find_group(ctx, ".eh_frame");
		if (ehg >= 0 && ctx->groups[ehg].size > 0 &&
		    ctx->groups[ehg].type != MT_SHT_NOBITS)
			output_count++; /* +1 for .eh_frame_hdr */
	}
	sections = (struct ld_output_section *)calloc(output_count,
	                                               sizeof(*sections));
	name_offsets = (uint32_t *)calloc(output_count, sizeof(*name_offsets));
	if (!sections || !name_offsets)
		goto out;
	if (strings_init(&shstr) != 0)
		goto out;
	for (i = 0; i < ctx->group_count; ++i) {
		const char *gn = ctx->groups[i].name;
		int is_elf64 = (ctx->target->elf_class == MT_ELFCLASS64);
		uint64_t es = 0; /* sh_entsize */
		if (gn && strcmp(gn, ".dynsym") == 0)
			es = is_elf64 ? MT_ELF64_SYM_SIZE : (uint64_t)MT_ELF32_SYM_SIZE;
		else if (gn && strcmp(gn, ".rela.dyn") == 0)
			es = is_elf64 ? 24 : 12;
		else if (gn && strcmp(gn, ".rela.plt") == 0)
			es = is_elf64 ? 24 : 12;
		else if (gn && strcmp(gn, ".hash") == 0)
			es = 4;
		sections[i + 1] = (struct ld_output_section){
			.name = gn,
			.type = ctx->groups[i].type,
			.flags = ctx->groups[i].flags,
			.address = ctx->groups[i].address,
			.offset = ctx->groups[i].file_offset,
			.size = ctx->groups[i].size,
			.align = ctx->groups[i].align,
			.link = 0,
			.info = 0,
			.entry_size = es
		};
		if (gn && strings_add(&shstr, sections[i + 1].name, &name_offsets[i + 1]) != 0)
			goto out_strings;
	}
	/* Set sh_link / sh_info for the dynamic symbol table and the .dynamic,
	 * .hash and .rela.dyn sections.  Valid for -shared and -pie (both emit
	 * .dynstr / .dynsym / .dynamic / .hash, and .rela.dyn when relocations
	 * exist).  Per ELF spec, .hash and .rela.dyn's sh_link points to the
	 * dynamic symbol table (.dynsym). */
	if (ctx->shared || ctx->pie) {
		/* Find .dynstr section index */
		uint32_t dynstr_sec = 0;
		uint32_t dynsym_sec = 0;
		uint32_t dynamic_sec = 0;
		uint32_t hash_sec = 0;
		uint32_t reladyn_sec = 0;
		uint32_t relaplt_sec = 0;
		for (i = 0; i < ctx->group_count; ++i) {
			const char *gn = ctx->groups[i].name;
			if (strcmp(gn, ".dynstr") == 0)
				dynstr_sec = (uint32_t)(i + 1);
			if (strcmp(gn, ".dynsym") == 0)
				dynsym_sec = (uint32_t)(i + 1);
			if (strcmp(gn, ".dynamic") == 0)
				dynamic_sec = (uint32_t)(i + 1);
			if (strcmp(gn, ".hash") == 0)
				hash_sec = (uint32_t)(i + 1);
			if (strcmp(gn, ".rela.dyn") == 0)
				reladyn_sec = (uint32_t)(i + 1);
			if (strcmp(gn, ".rela.plt") == 0)
				relaplt_sec = (uint32_t)(i + 1);
		}
		if (dynsym_sec && dynstr_sec) {
			sections[dynsym_sec].link = dynstr_sec;
			sections[dynsym_sec].info = 1; /* first non-local symbol index */
		}
		if (dynamic_sec && dynstr_sec)
			sections[dynamic_sec].link = dynstr_sec;
		/* sh_link of .hash / .rela.dyn / .rela.plt -> .dynsym */
		if (hash_sec && dynsym_sec)
			sections[hash_sec].link = dynsym_sec;
		if (reladyn_sec && dynsym_sec)
			sections[reladyn_sec].link = dynsym_sec;
		if (relaplt_sec && dynsym_sec)
			sections[relaplt_sec].link = dynsym_sec;
	}
	/* ---- .note.gnu.build-id section (if --build-id) ---- */
	if (ctx->build_id) {
		unsigned char note_data[24];
		memset(note_data, 0, sizeof(note_data));
		write32(note_data + 0, 4);
		write32(note_data + 4, 8);
		write32(note_data + 8, 3);
		memcpy(note_data + 12, "GNU", 4);
		write64(note_data + 16, 0); /* placeholder; recomputed at write time */
		uint32_t bid_sec_idx = (uint32_t)(ctx->group_count + 1);
		sections[bid_sec_idx].name = ".note.gnu.build-id";
		sections[bid_sec_idx].type = MT_SHT_NOTE;
		sections[bid_sec_idx].flags = LD_SHF_ALLOC;
		sections[bid_sec_idx].offset = file_end;
		sections[bid_sec_idx].address = base_addr + file_end;
		sections[bid_sec_idx].size = sizeof(note_data);
		sections[bid_sec_idx].align = 4;
		sections[bid_sec_idx].link = 0;
		sections[bid_sec_idx].info = 0;
		if (strings_add(&shstr, ".note.gnu.build-id",
		                &name_offsets[bid_sec_idx]) != 0)
			goto out_strings;
		file_end += sizeof(note_data);
	}
	/* ---- .eh_frame_hdr section (if --eh-frame-hdr and .eh_frame exists) ---- */
	if (ctx->eh_frame_hdr) {
		int ehg = find_group(ctx, ".eh_frame");
		if (ehg >= 0 && ctx->groups[ehg].size > 0 &&
		    ctx->groups[ehg].type != MT_SHT_NOBITS) {
			uint64_t eh_addr = base_addr + file_end;
			uint32_t eh_idx = (uint32_t)(ctx->group_count + 1 +
			                             (ctx->build_id ? 1 : 0));
			/* Generate eh_frame_hdr data directly with final encoded values */
			if (build_eh_frame_hdr(ctx, &eh_frame_hdr_data,
			                       &eh_frame_hdr_size, eh_addr) != 0)
				goto out_strings;
			sections[eh_idx].name = ".eh_frame_hdr";
			sections[eh_idx].type = MT_SHT_PROGBITS;
			sections[eh_idx].flags = LD_SHF_ALLOC;
			sections[eh_idx].offset = file_end;
			sections[eh_idx].address = eh_addr;
			sections[eh_idx].size = eh_frame_hdr_size;
			sections[eh_idx].align = 4;
			sections[eh_idx].link = 0;
			sections[eh_idx].info = 0;
			if (strings_add(&shstr, ".eh_frame_hdr",
			                &name_offsets[eh_idx]) != 0)
				goto out_strings;
			file_end += eh_frame_hdr_size;
		}
	}
	alloc_file_end = file_end;
	/* .shstrtab must not overlap non-allocatable sections (.debug*,
	 * .comment, .note) that layout places after the ALLOC data:
	 * extend file_end past every PROGBITS group before placing it.
	 * alloc_file_end above keeps the ALLOC-only end for PT_LOAD fsz. */
	for (i = 0; i < ctx->group_count; ++i) {
		struct ld_group *group = &ctx->groups[i];
		if (group->type != MT_SHT_NOBITS &&
		    group->file_offset + group->size > file_end)
			file_end = group->file_offset + group->size;
	}
	shstr_index = (uint32_t)(output_count - 1);
	sections[shstr_index].name = ".shstrtab";
	sections[shstr_index].type = MT_SHT_STRTAB;
	sections[shstr_index].flags = 0;
	sections[shstr_index].align = 1;
	sections[shstr_index].offset = file_end;
	sections[shstr_index].size = shstr.size;
	if (strings_add(&shstr, ".shstrtab", &name_offsets[shstr_index]) != 0)
		goto out_strings;
	sections[shstr_index].size = shstr.size;
	file_end = align_up(file_end, 1) + shstr.size;
	section_offset = align_up(file_end, 8);
	file = fopen(path, "wb+");
	if (!file)
		goto out_strings;
	/* ELF header — set fields from target descriptor. */
	if (target->elf_class == 1) {
		unsigned char h[52] = {0x7f, 'E', 'L', 'F',
		                       target->elf_class, target->elf_endian, 1, 0};
		uint16_t e_type = (ctx->shared || ctx->pie) ? MT_ET_DYN : MT_ET_EXEC;
		write16(h + 16, e_type);
		write16(h + 18, target->emachine);
		write32(h + 20, 1);
		write32(h + 24, (uint32_t)entry_address);
		write32(h + 28, target->ehdr_size);     /* e_phoff after fixed header */
		write32(h + 32, (uint32_t)section_offset);
		write32(h + 36, target->e_flags);
		write16(h + 40, target->ehdr_size);
		write16(h + 42, MT_ELF32_PHDR_SIZE);
		phnum = 1; /* PT_LOAD */
		if (memory_end > rx_end) phnum++; /* second LOAD for RW */
		if (ctx->tls_size) phnum++;
	phnum++; /* PT_GNU_STACK */
		if (ctx->shared || ctx->pie) phnum += 2;
		if (ctx->pie) phnum += 1;
		if ((ctx->shared || ctx->pie) &&
		    (find_group(ctx, ".dynamic") >= 0 || find_group(ctx, ".got") >= 0))
			phnum++; /* PT_GNU_RELRO */
		write16(h + 44, (uint16_t)phnum);
		write16(h + 46, target->shdr_size);
		write16(h + 48, (uint16_t)output_count);
		write16(h + 50, (uint16_t)shstr_index);
		if (fwrite(h, 1, sizeof(h), file) != sizeof(h))
			goto out_file;
	} else {
		unsigned char h[64] = {0x7f, 'E', 'L', 'F',
		                       target->elf_class, target->elf_endian, 1, 0};
		uint16_t e_type = (ctx->shared || ctx->pie) ? MT_ET_DYN : MT_ET_EXEC;
		write16(h + 16, e_type);
		write16(h + 18, target->emachine);
		write32(h + 20, 1);
		write64(h + 24, entry_address);
		write64(h + 32, target->ehdr_size);
		write64(h + 40, section_offset);
		write32(h + 48, target->e_flags);
		write16(h + 52, target->ehdr_size);
		write16(h + 54, MT_ELF64_PHDR_SIZE);
		phnum = 1; /* PT_LOAD */
		if (memory_end > rx_end) phnum++; /* second LOAD for RW */
		if (ctx->tls_size) phnum++;
	phnum++; /* PT_GNU_STACK */
		if (ctx->shared || ctx->pie) phnum += 2; /* PT_PHDR + PT_DYNAMIC */
		if (ctx->pie) phnum += 1;    /* PT_INTERP */
		if ((ctx->shared || ctx->pie) &&
		    (find_group(ctx, ".dynamic") >= 0 || find_group(ctx, ".got") >= 0))
			phnum++; /* PT_GNU_RELRO */
		write16(h + 56, (uint16_t)phnum);
		write16(h + 58, target->shdr_size);
		write16(h + 60, (uint16_t)output_count);
		write16(h + 62, (uint16_t)shstr_index);
		if (fwrite(h, 1, sizeof(h), file) != sizeof(h))
			goto out_file;
	}
	/* Program header order per ELF spec: PHDR (if present) before LOAD.
	 *
	 * Note: QEMU 7.2 loongarch64 user-mode loader had a bug that crashed
	 * (SEGV_ACCERR) when executing code from the first LOAD segment if a
	 * second non-empty PT_LOAD existed; the old workaround collapsed both
	 * segments into one RWX LOAD.  The current layout emits the standard
	 * two-segment form below (1st LOAD R|X, 2nd LOAD R|W), which is what
	 * the QEMU/kernel loaders used in tests accept. */
	if (ctx->shared || ctx->pie) {
		uint64_t phoff = target->ehdr_size;
		if (write_program_header_type(file, MT_PT_PHDR, LD_PF_R,
		                             phoff, base_addr + phoff,
		                             (uint64_t)phnum * phentsize,
		                             (uint64_t)phnum * phentsize, 8,
		                             target->elf_class == 1) != 0)
			goto out_file;
	}
	{
		/* ---- 1st LOAD: read-execute (code + rodata) ---- */
		/* rx_end, memory_end 都跟踪 FILE OFFSETS。
		 * LOAD 段将文件偏移 0 映射到 base_addr (0x400000)。
		 * 所以文件偏移 F 的组，虚拟地址为 base_addr + F。 */
		uint64_t seg1_fsz = rx_end > LD_PAGE ? rx_end : LD_PAGE;
		uint64_t seg1_msz = seg1_fsz;
		if (write_program_header(file, LD_PF_R | LD_PF_X,
		                         0, base_addr, seg1_fsz, seg1_msz,
		                         target->elf_class == 1) != 0)
			goto out_file;
		/* ---- 2nd LOAD: read-write (data + bss), when present ---- */
		if (memory_end > rx_end) {
			/* Align RW segment start to page boundary for QEMU/kernel. */
			uint64_t rw_page = align_up(rx_end, LD_PAGE);
			uint64_t rw_off = rw_page;
			uint64_t rw_vaddr = base_addr + rw_page;
			/* file data covers from rw_page to max(rx_end, alloc_file_end) */
			uint64_t alloc_end = rx_end > alloc_file_end ?
			                     rx_end : alloc_file_end;
			uint64_t rw_fsz = alloc_end > rw_page ?
			                  alloc_end - rw_page : 0;
			/* memory may extend further for BSS (.bss, .tbss) */
			uint64_t rw_msz = memory_end > rw_page ?
			                  memory_end - rw_page : 0;
			if (write_program_header(file, LD_PF_R | LD_PF_W,
			                         rw_off, rw_vaddr,
			                         rw_fsz, rw_msz,
			                         target->elf_class == 1) != 0)
				goto out_file;
		}
	}
	/* PT_DYNAMIC for shared libraries and PIE executables (ET_DYN) */
	if (ctx->shared || ctx->pie) {
		int dg = find_group(ctx, ".dynamic");
		if (dg >= 0) {
			struct ld_group *dyn = &ctx->groups[dg];
			if (write_program_header_type(file, MT_PT_DYNAMIC,
			                             LD_PF_R | LD_PF_W,
			                             dyn->file_offset, dyn->address,
			                             dyn->size, dyn->size, 8,
			                             target->elf_class == 1) != 0)
				goto out_file;
		}
	}
	/* PT_INTERP for PIE executables */
	if (ctx->pie) {
		int ig = find_group(ctx, ".interp");
		if (ig >= 0) {
			struct ld_group *interp = &ctx->groups[ig];
			if (write_program_header_type(file, MT_PT_INTERP, LD_PF_R,
			                             interp->file_offset, interp->address,
			                             interp->size, interp->size, 1,
			                             target->elf_class == 1) != 0)
				goto out_file;
		}
	}
	if (ctx->tls_size) {
		uint64_t tls_addr = 0, tls_off = 0, tls_filesz = 0;
		if (ctx->tls_tdata_group >= 0) {
			tls_addr = ctx->groups[ctx->tls_tdata_group].address;
			tls_off = ctx->groups[ctx->tls_tdata_group].file_offset;
			tls_filesz = ctx->groups[ctx->tls_tdata_group].size;
		} else if (ctx->tls_tbss_group >= 0) {
			/* .tbss-only TLS: emit PT_TLS with filesz=0.
			 * The .tbss group has a valid address beyond the LOAD
			 * segments (layout pass allocates it at LD_BASE+offset),
			 * so PT_TLS does NOT overlap PT_LOAD.  The old workaround
			 * (goto skip_tls) was for QEMU 7.2 loongarch64 which
			 * rejected overlapping PT_TLS with PT_LOAD; with proper
			 * .tbss address assignment this no longer occurs. */
			tls_addr = ctx->groups[ctx->tls_tbss_group].address;
			tls_off = ctx->groups[ctx->tls_tbss_group].file_offset;
		}
		if (write_program_header_type(file, LD_PT_TLS, LD_PF_R, tls_off,
		                               tls_addr, tls_filesz, ctx->tls_size,
		                               ctx->tls_align,
		                               target->elf_class == 1) != 0)
			goto out_file;
	}
	/* PT_GNU_STACK: mark stack as non-executable (QEMU and some kernels
	 * require this program header for ELF binaries). */
	{
		int stack_flags = LD_PF_R | LD_PF_W;
		if (write_program_header_type(file, MT_PT_GNU_STACK, stack_flags,
		                               0, 0, 0, 0, 16,
		                               target->elf_class == 1) != 0)
			goto out_file;
	}
	/* PT_GNU_RELRO: mark .dynamic and .got as read-only after relocations.
	 * This is an optional program header; emit it when both sections exist. */
	if (ctx->shared || ctx->pie) {
		int dg_relro = find_group(ctx, ".dynamic");
		int gg_relro = find_group(ctx, ".got");
		uint64_t relro_start = ~0ULL, relro_end = 0;
		if (dg_relro >= 0) {
			relro_start = ctx->groups[dg_relro].address;
			relro_end = ctx->groups[dg_relro].address + ctx->groups[dg_relro].size;
		}
		if (gg_relro >= 0) {
			uint64_t ga = ctx->groups[gg_relro].address;
			uint64_t ge = ga + ctx->groups[gg_relro].size;
			if (ga < relro_start) relro_start = ga;
			if (ge > relro_end) relro_end = ge;
		}
		if (relro_end > 0) {
			uint64_t rstart = relro_start & ~(uint64_t)(LD_PAGE - 1);
			uint64_t rend = (relro_end + LD_PAGE - 1) & ~(uint64_t)(LD_PAGE - 1);
			/* File offset = rstart - base_addr (p_vaddr mapping) */
			uint64_t base_va = ctx->shared ? 0 : LD_BASE;
			uint64_t roff = rstart - base_va;
			if (write_program_header_type(file, MT_PT_GNU_RELRO, LD_PF_R,
			                             roff, rstart, rend - rstart,
			                             rend - rstart, 1,
			                             target->elf_class == 1) != 0)
				goto out_file;
		}
	}
	for (i = 0; i < ctx->group_count; ++i) {
		struct ld_group *group = &ctx->groups[i];
		if (group->type == MT_SHT_NOBITS || group->size == 0)
			continue;
		if (fseek(file, (long)group->file_offset, SEEK_SET) != 0 ||
		    fwrite(group->data, 1, group->size, file) != group->size)
			goto out_file;
	}
	if (ctx->build_id) {
		unsigned char note_data[24];
		memset(note_data, 0, sizeof(note_data));
		/* Compute FNV-1a hash over output data for build-id */
		uint64_t hash = 0xcbf29ce484222325ULL;
		const uint64_t prime = 0x100000001b3ULL;
		uint64_t j;
		for (j = 0; j < ctx->group_count; ++j) {
			struct ld_group *g = &ctx->groups[j];
			if (g->type == MT_SHT_NOBITS || g->size == 0) continue;
			uint64_t k;
			for (k = 0; k < g->size; ++k) {
				hash ^= g->data[k];
				hash *= prime;
			}
		}
		write32(note_data + 0, 4);
		write32(note_data + 4, 8);
		write32(note_data + 8, 3);
		memcpy(note_data + 12, "GNU", 4);
		write64(note_data + 16, hash);
		uint32_t bid_idx = (uint32_t)(ctx->group_count + 1);
		if (fseek(file, (long)sections[bid_idx].offset, SEEK_SET) != 0 ||
		    fwrite(note_data, 1, 24, file) != 24)
			goto out_file;
	}
	/* Write .eh_frame_hdr data (pre-encoded, no patching needed) */
	if (eh_frame_hdr_data) {
		uint32_t eh_idx = (uint32_t)(ctx->group_count + 1 +
		                             (ctx->build_id ? 1 : 0));
		if (fseek(file, (long)sections[eh_idx].offset, SEEK_SET) != 0 ||
		    fwrite(eh_frame_hdr_data, 1, eh_frame_hdr_size, file) != eh_frame_hdr_size)
			goto out_file;
	}
	if (fseek(file, (long)sections[shstr_index].offset, SEEK_SET) != 0 ||
	    fwrite(shstr.data, 1, shstr.size, file) != shstr.size)
		goto out_file;
	if (fseek(file, (long)section_offset, SEEK_SET) != 0)
		goto out_file;
	/* NULL section header: write one zeroed header of the correct size.
	 * ELF32 = 40 bytes, ELF64 = 64 bytes.  All zeros is correct for the
	 * NULL section (sh_type=0, sh_flags=0, sh_addr=0, etc.). */
	unsigned char null_sh[64] = {0};
	size_t sh_size = (size_t)(target->elf_class == 1 ? 40 : 64);
	if (fwrite(null_sh, 1, sh_size, file) != sh_size)
		goto out_file;
	for (i = 1; i < (size_t)output_count; ++i) {
		if (target->elf_class == 1) {
			unsigned char sh[40] = {0};
			write32(sh + 0, name_offsets[i]);
			write32(sh + 4, sections[i].type);
			write32(sh + 8, (uint32_t)sections[i].flags);
			write32(sh + 12, (uint32_t)sections[i].address);
			write32(sh + 16, (uint32_t)sections[i].offset);
			write32(sh + 20, (uint32_t)sections[i].size);
			write32(sh + 24, sections[i].link);
			write32(sh + 28, sections[i].info);
			write32(sh + 32, (uint32_t)(sections[i].align ? sections[i].align : 1));
			write32(sh + 36, (uint32_t)sections[i].entry_size);
			if (fwrite(sh, 1, sizeof(sh), file) != sizeof(sh))
				goto out_file;
		} else {
			unsigned char sh[64] = {0};
			write32(sh + 0, name_offsets[i]);
			write32(sh + 4, sections[i].type);
			write64(sh + 8, sections[i].flags);
			write64(sh + 16, sections[i].address);
			write64(sh + 24, sections[i].offset);
			write64(sh + 32, sections[i].size);
			write32(sh + 40, sections[i].link);
			write32(sh + 44, sections[i].info);
			write64(sh + 48, sections[i].align ? sections[i].align : 1);
			write64(sh + 56, sections[i].entry_size);
			if (fwrite(sh, 1, sizeof(sh), file) != sizeof(sh))
				goto out_file;
		}
	}
	if (fclose(file) != 0) {
		file = NULL;
		goto out_strings;
	}
	file = NULL;
	if (chmod(path, 0755) != 0)
		goto out_strings;
	result = 0;
out_file:
	if (file)
		fclose(file);
out_strings:
	free(shstr.data);
out:
	free(eh_frame_hdr_data);
	free(sections);
	free(name_offsets);
	return result;
}
