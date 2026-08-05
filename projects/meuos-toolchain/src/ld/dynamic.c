/* dynamic.c - mt/ld dynamic symbol table and .dynamic construction.
 *
 * Builds .dynsym/.dynstr/.dynamic/.rela.dyn (build_dynamic_tables) and
 * back-fills the dynamic addresses after layout (fill_dynamic_addresses),
 * plus .rela.dyn incremental fill (build_rela_dyn / rela_dyn_add) and the
 * dynamic/pie section ensure helpers.  Extracted from link.c (per-stage
 * module split; shared state lives in struct ld_context via ld_internal.h).
 */
#include "ld_internal.h"



int
rela_dyn_add(struct ld_context *ctx, uint64_t offset, uint64_t info, int64_t addend)
{
	int rg = find_group(ctx, ".rela.dyn");
	int elf64 = (ctx->target->elf_class == MT_ELFCLASS64);
	size_t entry_size = elf64 ? 24 : 12;
	if (rg < 0) return -1;
	/* Reject overflow: the section buffer was pre-reserved in
	 * build_dynamic_tables to hold rela_capacity_entries entries. */
	if (ctx->rela_count >= ctx->rela_capacity_entries)
		return ld_error(ctx, "internal: .rela.dyn overflow");
	unsigned char *dst = ctx->groups[rg].data +
	                     ctx->rela_count * entry_size;
	if (elf64) {
		write64(dst, offset);
		write64(dst + 8, info);
		write64(dst + 16, (uint64_t)addend);
	} else {
		write32(dst, (uint32_t)offset);
		write32(dst + 4, (uint32_t)info);
		write32(dst + 8, (uint32_t)addend);
	}
	ctx->rela_count++;
	return 0;
}

/* Add a JUMP_SLOT relocation entry to .rela.plt.  Separate from .rela.dyn
 * so that the dynamic loader can process DT_JMPREL (PLT relocations) before
 * DT_RELA (regular GOT/data relocations) if needed. */
int
rela_plt_add(struct ld_context *ctx, uint64_t offset, uint64_t info, int64_t addend)
{
	int rg = find_group(ctx, ".rela.plt");
	int elf64 = (ctx->target->elf_class == MT_ELFCLASS64);
	size_t entry_size = elf64 ? 24 : 12;
	if (rg < 0) return -1;
	if (ctx->rela_plt_count >= ctx->rela_plt_capacity_entries)
		return ld_error(ctx, "internal: .rela.plt overflow");
	unsigned char *dst = ctx->groups[rg].data +
	                     ctx->rela_plt_count * entry_size;
	if (elf64) {
		write64(dst, offset);
		write64(dst + 8, info);
		write64(dst + 16, (uint64_t)addend);
	} else {
		write32(dst, (uint32_t)offset);
		write32(dst + 4, (uint32_t)info);
		write32(dst + 8, (uint32_t)addend);
	}
	ctx->rela_plt_count++;
	return 0;
}

int
build_rela_dyn(struct ld_context *ctx)
{
	size_t i;
	if ((!ctx->shared && !ctx->pie) || (ctx->got.count == 0 && ctx->rela_capacity_entries == 0))
		return 0;
	int rg = find_group(ctx, ".rela.dyn");
	int rpg = find_group(ctx, ".rela.plt");
	ctx->rela_count = 0;
	ctx->rela_plt_count = 0;
	uint64_t got_addr = ctx->groups[ctx->got.group].address;
	for (i = 0; i < ctx->got.count; ++i) {
		uint64_t got_entry = got_addr + ctx->got.items[i].offset;
		if (ctx->got.items[i].tls) {
			/* TLS GOT slots are filled at load time by ld.so.
			 * GD/LD pairs get a DTPMOD64 + DTPOFF64 reloc pair; IE
			 * gets a single DTPOFF64 (TP-relative offset). */
			uint32_t sym_idx = 0;
			for (size_t j = 0; j < ctx->dynsym_count; ++j) {
				if (strcmp(ctx->dynsym_entries[j].global->name,
				           ctx->got.items[i].name) == 0) {
					sym_idx = (uint32_t)(j + 1); /* +1 for STN_UNDEF */
					break;
				}
			}
			if (ctx->got.items[i].slots >= 2) {
				uint64_t dtpmod = ((uint64_t)sym_idx << 32) | MT_R_X86_64_DTPMOD64;
				uint64_t dtpoff = ((uint64_t)sym_idx << 32) | MT_R_X86_64_DTPOFF64;
				if (rela_dyn_add(ctx, got_entry, dtpmod, 0) != 0 ||
				    rela_dyn_add(ctx, got_entry + 8, dtpoff, 0) != 0)
					return -1;
			} else {
				/* IE: GOT slot holds the symbol's TP-relative offset. */
				uint64_t tpoff = ((uint64_t)sym_idx << 32) | MT_R_X86_64_TPOFF64;
				if (rela_dyn_add(ctx, got_entry, tpoff, 0) != 0)
					return -1;
			}
			continue;
		}
		if (ctx->got.items[i].reloc_type == MT_R_X86_64_JUMP_SLOT) {
			/* Undefined symbol import: emit JUMP_SLOT dynamic
			 * relocation so ld.so looks up the symbol by name
			 * and fills the GOT slot.  These go into .rela.plt
			 * (DT_JMPREL) rather than .rela.dyn. */
			uint32_t sym_idx = 0;
			for (size_t j = 0; j < ctx->dynsym_count; ++j) {
				if (strcmp(ctx->dynsym_entries[j].global->name,
				           ctx->got.items[i].name) == 0) {
					sym_idx = (uint32_t)(j + 1); /* +1 for STN_UNDEF */
					break;
				}
			}
			uint64_t info;
			if (strcmp(ctx->target->name, "arm") == 0)
				info = (sym_idx << 8) | MT_R_ARM_JUMP_SLOT;
			else if (ctx->target->elf_class == MT_ELFCLASS32)
				info = (sym_idx << 8) | MT_R_386_JUMP_SLOT;
			else
				info = ((uint64_t)sym_idx << 32) | MT_R_X86_64_JUMP_SLOT;
			if (rela_plt_add(ctx, got_entry, info, 0) != 0)
				return -1;
		} else {
			/* The GOT entry references a symbol.  If it is defined in
			 * THIS link, emit a RELATIVE (load-base + addend) so the
			 * slot points at the symbol directly.  If it is an external
			 * (undefined) symbol — e.g. a PIE importing data from a
			 * shared library — emit GLOB_DAT so ld.so resolves it by
			 * name across DSOs at load time; emitting RELATIVE would
			 * bake in a bogus addend and leave the slot garbage. */
			struct ld_global *g = find_global(ctx, ctx->got.items[i].name);
			if (g && g->defined && g->group >= 0) {
				uint64_t sym_value = ctx->groups[g->group].address + g->offset;
				uint64_t info;
				if (strcmp(ctx->target->name, "arm") == 0)
					info = (uint64_t)MT_R_ARM_RELATIVE;
				else if (ctx->target->elf_class == MT_ELFCLASS32)
					info = (uint64_t)MT_R_X86_64_RELATIVE;
				else
					info = (0ULL << 32) | MT_R_X86_64_RELATIVE;
				if (rela_dyn_add(ctx, got_entry, info, (int64_t)sym_value) != 0)
					return -1;
			} else if ((ctx->shared || ctx->pie) &&
			           strcmp(ctx->target->name, "x86_64") == 0) {
				/* External/undefined symbol: GLOB_DAT so ld.so fills
				 * the GOT slot with the defining DSO's symbol address. */
				uint32_t sym_idx = 0;
				for (size_t j = 0; j < ctx->dynsym_count; ++j) {
					if (ctx->dynsym_entries[j].global->name &&
					    strcmp(ctx->dynsym_entries[j].global->name,
					           ctx->got.items[i].name) == 0) {
						sym_idx = (uint32_t)(j + 1); /* +1 for STN_UNDEF */
						break;
					}
				}
				uint64_t info = ((uint64_t)sym_idx << 32) | MT_R_X86_64_GLOB_DAT;
				if (rela_dyn_add(ctx, got_entry, info, 0) != 0)
					return -1;
			}
		}
	}
	/* Generate RELATIVE entries for data-section absolute relocations
	 * (R_X86_64_64/32/32S) in PIE/shared mode.  These are needed so the
	 * dynamic loader can adjust the absolute address by the load base.
	 * Without them, data-section pointer initializers (e.g.
	 * `int *p = &bss_var[4]`) carry stale addresses → SIGSEGV. */
	if (ctx->shared || ctx->pie) {
		for (i = 0; i < ctx->objects.count; ++i) {
			struct ld_object *object = &ctx->objects.items[i];
			if (object->is_shared) continue;
			for (uint16_t si = 0; si < object_section_count(object); ++si) {
				struct mt_elf64_section sec;
				if (object_get_section(object, si, &sec) != 0) continue;
				if (sec.type != MT_SHT_RELA && sec.type != MT_SHT_REL) continue;
				if (sec.entry_size == 0 || sec.size % sec.entry_size != 0) continue;
				/* Skip if the relocation targets a discarded section */
				if (sec.info >= object_section_count(object)) continue;
				int target_group = object->maps[sec.info].group;
				if (target_group < 0) continue;
				uint64_t target_addr = ctx->groups[target_group].address;
				for (uint64_t ri = 0; ri < sec.size / sec.entry_size; ++ri) {
					const unsigned char *p = object->data + sec.offset + ri * sec.entry_size;
					uint64_t r_offset;
					uint64_t sym_idx;
					int type;
					int64_t addend;
					if (object->elf_class == 1) {
						r_offset = read32(p + 0);
						uint32_t info32 = read32(p + 4);
						type = (int)(info32 & 0xff);
						sym_idx = info32 >> 8;
						addend = (sec.type == MT_SHT_RELA) ? (int32_t)read32(p + 8) : 0;
					} else {
						r_offset = read64(p + 0);
						uint64_t info64 = read64(p + 8);
						type = (int)(info64 & 0xffffffffu);
						sym_idx = info64 >> 32;
						addend = (int64_t)read64(p + 16);
					}
					if (type != LD_R_X86_64_64 &&
					    type != LD_R_X86_64_32 &&
					    type != LD_R_X86_64_32S)
						continue;
					struct mt_elf64_symbol sym;
					const char *sym_name;
					struct ld_global *g;
					if (get_symbol_by_index(ctx, object, sym_idx, &sym, &sym_name) != 0)
						continue;
					g = find_global(ctx, sym_name);
					if (!g || !g->defined || g->group < 0) continue;
					/* Skip TLS symbols (handled separately). */
					const char *gname = ctx->groups[g->group].name;
					if (gname && (strcmp(gname, ".tdata") == 0 ||
					              strcmp(gname, ".tbss") == 0))
						continue;
					/* Compute the address of the location being relocated. */
					uint64_t reloc_offset = object->maps[sec.info].offset;
					uint64_t place = target_addr + reloc_offset + r_offset;
					uint64_t sym_value = ctx->groups[g->group].address + g->offset;
					/* The RELATIVE entry's addend must match the value that
					 * apply_relocations wrote into the data slot, which is
					 * (sym_value + addend) for R_X86_64_64/32/32S.  At
					 * runtime, ld.so computes: slot = base + addend. */
					uint64_t info;
					if (strcmp(ctx->target->name, "arm") == 0)
						info = (uint64_t)MT_R_ARM_RELATIVE;
					else if (ctx->target->elf_class == MT_ELFCLASS32)
						info = (uint64_t)MT_R_X86_64_RELATIVE;
					else
						info = (0ULL << 32) | MT_R_X86_64_RELATIVE;
					/* The addend is the full linked address of the target
					 * symbol (the value that the relocation patched in).
					 * At runtime, ld.so will compute: slot = base + addend. */
					if (rela_dyn_add(ctx, place, info, (int64_t)sym_value + addend) != 0)
						return -1;
				}
			}
		}
	}
	/* Shrink .rela.dyn to the actual number of entries written (a skipped
	 * undefined-symbol RELATIVE may leave the reserved buffer under-filled),
	 * then fix up DT_RELASZ, which fill_dynamic_addresses could only
	 * approximate before the entry list was finalised. */
	if (rg >= 0) {
		int rela64 = (ctx->target->elf_class == MT_ELFCLASS64);
		size_t rsz = ctx->rela_count * (rela64 ? 24 : 12);
		ctx->groups[rg].size = rsz;
		int dgn = find_group(ctx, ".dynamic");
		if (dgn >= 0) {
			struct ld_group *dyn = &ctx->groups[dgn];
			size_t ndyn = dyn->size / (rela64 ? 16 : 8);
			for (size_t di = 0; di < ndyn; ++di) {
				uint64_t tag = rela64 ? read64(dyn->data + di * 16)
				                   : read32(dyn->data + di * 8);
				if (tag == MT_DT_RELASZ) {
					if (rela64)
						write64(dyn->data + di * 16 + 8, rsz);
					else
						write32(dyn->data + di * 8 + 4, (uint32_t)rsz);
					break;
				}
			}
		}
	}
	/* Similarly shrink .rela.plt and fix up DT_PLTRELSZ. */
	if (rpg >= 0) {
		int rela64 = (ctx->target->elf_class == MT_ELFCLASS64);
		size_t rsz = ctx->rela_plt_count * (rela64 ? 24 : 12);
		ctx->groups[rpg].size = rsz;
		int dgn = find_group(ctx, ".dynamic");
		if (dgn >= 0) {
			struct ld_group *dyn = &ctx->groups[dgn];
			size_t ndyn = dyn->size / (rela64 ? 16 : 8);
			for (size_t di = 0; di < ndyn; ++di) {
				uint64_t tag = rela64 ? read64(dyn->data + di * 16)
				                   : read32(dyn->data + di * 8);
				if (tag == MT_DT_PLTRELSZ) {
					if (rela64)
						write64(dyn->data + di * 16 + 8, rsz);
					else
						write32(dyn->data + di * 8 + 4, (uint32_t)rsz);
					break;
				}
			}
		}
	}
	return 0;
}

static uint32_t
next_prime32(uint32_t n)
{
	uint32_t p, i;
	for (p = n < 2 ? 2 : n; ; ++p) {
		int isp = 1;
		for (i = 2; i * i <= p; ++i) {
			if (p % i == 0) { isp = 0; break; }
		}
		if (isp) return p;
	}
}

static uint32_t
elf_hash32(const char *name)
{
	uint32_t h = 0, g;
	while (*name) {
		h = (h << 4) + (unsigned char)*name++;
		if ((g = h & 0xf0000000u))
			h ^= g >> 24;
		h &= ~g;
	}
	return h;
}

static void
write_dynsym_entry(unsigned char *p, int elf64,
                   uint32_t name, uint8_t info, uint8_t other,
                   uint16_t shndx, uint64_t value, uint64_t size)
{
	if (elf64) {
		write32(p + 0, name);
		p[4] = info;
		p[5] = other;
		write16(p + 6, shndx);
		write64(p + 8, value);
		write64(p + 16, size);
	} else {
		write32(p + 0, name);
		write32(p + 4, (uint32_t)value);
		write32(p + 8, (uint32_t)size);
		p[12] = info;
		p[13] = other;
		write16(p + 14, shndx);
	}
}

int
build_dynamic_tables(struct ld_context *ctx)
{
	size_t i;
	size_t nsym = 0;

	if (!ctx->shared && !ctx->pie)
		return 0;

	/* Count exported (defined non-weak) globals with names */
	for (i = 0; i < ctx->globals.count; ++i) {
		struct ld_global *g = &ctx->globals.items[i];
		/* --version-script filter: skip symbols not in the export list */
		if (ctx->version_script && g->defined && !g->weak && g->name) {
			int keep = 0;
			for (size_t vi = 0; vi < ctx->version_script_count; vi++) {
				if (strcmp(ctx->version_script[vi], g->name) == 0) {
					keep = 1; break;
				}
			}
			if (!keep) continue;
		}
		if (g->defined && !g->weak && g->name)
			++nsym;
	}

	/* Also count imported (undefined) symbols that have GOT entries
	 * with JUMP_SLOT relocations (needed for ld.so symbol resolution). */
	for (i = 0; i < ctx->got.count; ++i) {
		if (ctx->got.items[i].reloc_type == MT_R_X86_64_JUMP_SLOT) {
			/* Avoid double-counting if already exported */
			int already = 0;
			for (size_t j = 0; j < ctx->globals.count; ++j) {
				if (ctx->globals.items[j].defined &&
				    ctx->globals.items[j].name &&
				    strcmp(ctx->globals.items[j].name, ctx->got.items[i].name) == 0) {
					already = 1; break;
				}
			}
			if (!already) ++nsym;
		}
	}

	/* Allocate local dynsym metadata array */
	if (nsym > ctx->dynsym_capacity) {
		size_t cap = ctx->dynsym_capacity ? ctx->dynsym_capacity * 2 : 64;
		while (cap < nsym) cap *= 2;
		struct ld_dynsym_entry *e = (struct ld_dynsym_entry *)ld_realloc(
		    ctx->dynsym_entries, cap * sizeof(*e));
		if (!e) return ld_error(ctx, "out of memory");
		ctx->dynsym_entries = e;
		ctx->dynsym_capacity = cap;
	}
	ctx->dynsym_count = nsym;

	int elf64 = (ctx->target->elf_class == MT_ELFCLASS64);
	size_t symsize = elf64 ? MT_ELF64_SYM_SIZE : MT_ELF32_SYM_SIZE;

	/* ---- .dynstr ---- */
	int dg_str = get_group(ctx, ".dynstr", MT_SHT_STRTAB,
	                       LD_SHF_ALLOC, 1);
	if (dg_str < 0) return ld_error(ctx, "out of memory");
	/* Leading NUL byte (string table index 0 = empty string) */
	uint64_t stroff;
	if (append_group_data(ctx, &ctx->groups[dg_str], (const unsigned char *)"", 1, 1, &stroff) != 0)
		return -1;

	/* Store soname string (if any) and append its name to .dynstr */
	ctx->soname_dynstr_offset = 0;
	if (ctx->soname) {
		uint64_t so_off;
		if (append_group_data(ctx, &ctx->groups[dg_str],
		                      (const unsigned char *)ctx->soname,
		                      strlen(ctx->soname) + 1, 1, &so_off) != 0)
			return -1;
		ctx->soname_dynstr_offset = (uint32_t)so_off;
	}

	/* Append --add-needed sonames to .dynstr and record their offsets.
	 * Also auto-generate DT_NEEDED entries for input .so files. */
	ctx->needed_dynstr_offsets = NULL;
	/* Count total needed entries: CLI --add-needed + input .so files */
	size_t needed_from_cli = ctx->add_needed_count;
	size_t needed_from_dso = 0;
	for (i = 0; i < ctx->objects.count; i++)
		if (ctx->objects.items[i].is_shared)
			needed_from_dso++;
	size_t total_needed = needed_from_cli + needed_from_dso;

	if (total_needed > 0) {
		ctx->needed_dynstr_offsets = (uint32_t *)ld_malloc(
		    total_needed * sizeof(uint32_t));
		if (!ctx->needed_dynstr_offsets)
			return ld_error(ctx, "out of memory");
		memset(ctx->needed_dynstr_offsets, 0,
		       total_needed * sizeof(uint32_t));
		size_t needed_idx = 0;
		/* --add-needed entries */
		for (i = 0; i < ctx->add_needed_count; ++i) {
			uint64_t no_off;
			if (append_group_data(ctx, &ctx->groups[dg_str],
			                      (const unsigned char *)ctx->add_needed[i],
			                      strlen(ctx->add_needed[i]) + 1, 1, &no_off) != 0)
				return -1;
			ctx->needed_dynstr_offsets[needed_idx++] = (uint32_t)no_off;
		}
		/* Auto-generated entries from input .so files */
		for (i = 0; i < ctx->objects.count; i++) {
			struct ld_object *obj = &ctx->objects.items[i];
			if (!obj->is_shared) continue;
			/* Use SONAME from .so's dynamic section if available */
			uint64_t no_off;
			if (append_group_data(ctx, &ctx->groups[dg_str],
			                      (const unsigned char *)obj->name,
			                      strlen(obj->name) + 1, 1, &no_off) != 0)
				return -1;
			ctx->needed_dynstr_offsets[needed_idx++] = (uint32_t)no_off;
		}
	}

	/* ---- .dynsym (placeholder, filled in pass 2) ---- */
	int dg_sym = get_group(ctx, ".dynsym", MT_SHT_DYNSYM,
	                       LD_SHF_ALLOC, 8);
	if (dg_sym < 0) return ld_error(ctx, "out of memory");
	size_t nsym_total = nsym + 1; /* index 0 = null symbol */
	size_t dysize = nsym_total * symsize;
	/* Zero-fill the entire .dynsym (null symbol at 0 stays zero) */
	unsigned char *dz = (unsigned char *)ld_malloc(dysize ? dysize : 1);
	if (!dz) return ld_error(ctx, "out of memory");
	memset(dz, 0, dysize);
	uint64_t sym_data_off;
	if (append_group_data(ctx, &ctx->groups[dg_sym], dz, dysize, 8, &sym_data_off) != 0) {
		free(dz);
		return -1;
	}
	free(dz);
	ctx->dynsym_data_offset = sym_data_off;

	/* Now populate the dynsym_entries metadata (name → dynstr_offset).
	 *
	 * IMPORTANT: this MUST happen BEFORE building the .hash table,
	 * because the hash loop accesses e->global->name. */
	size_t ei = 0;
	for (i = 0; i < ctx->globals.count; ++i) {
		struct ld_global *g = &ctx->globals.items[i];
		if (!(g->defined && !g->weak) || !g->name)
			continue;
		/* --version-script filter: skip symbols not in the export list */
		if (ctx->version_script) {
			int keep = 0;
			for (size_t vi = 0; vi < ctx->version_script_count; vi++) {
				if (strcmp(ctx->version_script[vi], g->name) == 0) {
					keep = 1; break;
				}
			}
			if (!keep) continue;
		}
		/* Determine st_type */
		int stt = MT_STT_OBJECT;
		if (g->group >= 0) {
			const char *gname = ctx->groups[g->group].name;
			if (gname && strcmp(gname, ".text") == 0)
				stt = MT_STT_FUNC;
			else if (gname &&
			         (strcmp(gname, ".tdata") == 0 ||
			          strcmp(gname, ".tbss") == 0))
				stt = MT_STT_TLS;
		}
		uint32_t dynstr_off;
		if (append_group_data(ctx, &ctx->groups[dg_str],
		                      (const unsigned char *)g->name,
		                      strlen(g->name) + 1, 1, &stroff) != 0)
			return -1;
		dynstr_off = (uint32_t)stroff;
		ctx->dynsym_entries[ei].global = g;
		ctx->dynsym_entries[ei].dynstr_offset = dynstr_off;
		ctx->dynsym_entries[ei].stt = stt;
		++ei;
	}

	/* Add imported (undefined) symbols: symbols referenced via JUMP_SLOT
	 * GOT entries that were not already exported above.  These appear in
	 * .dynsym as STT_FUNC with SHN_UNDEF (section 0), so ld.so can
	 * resolve them by name at load time. */
	for (i = 0; i < ctx->got.count; ++i) {
		if (ctx->got.items[i].reloc_type != MT_R_X86_64_JUMP_SLOT)
			continue;
		/* Check if already exported above */
		int already = 0;
		for (size_t j = 0; j < ctx->globals.count; ++j) {
			if (ctx->globals.items[j].defined &&
			    ctx->globals.items[j].name &&
			    strcmp(ctx->globals.items[j].name, ctx->got.items[i].name) == 0) {
				already = 1; break;
			}
		}
		if (already) continue;
		/* Create a fake ld_global entry for this import */
		struct ld_global *g = find_global(ctx, ctx->got.items[i].name);
		if (!g) continue;
		uint32_t dynstr_off;
		if (append_group_data(ctx, &ctx->groups[dg_str],
		                      (const unsigned char *)g->name,
		                      strlen(g->name) + 1, 1, &stroff) != 0)
			return -1;
		dynstr_off = (uint32_t)stroff;
		ctx->dynsym_entries[ei].global = g;
		ctx->dynsym_entries[ei].dynstr_offset = dynstr_off;
		ctx->dynsym_entries[ei].stt = MT_STT_FUNC;
		++ei;
	}

	/* ---- .hash (SysV format, 32-bit entries) ---- */
	int dg_hash = get_group(ctx, ".hash", MT_SHT_HASH,
	                        LD_SHF_ALLOC, 4);
	if (dg_hash < 0) return ld_error(ctx, "out of memory");
	uint32_t nbucket = next_prime32((uint32_t)(nsym_total > 1 ? nsym_total : 3));
	uint32_t nchain = (uint32_t)nsym_total;
	size_t hash_size = (size_t)(2 + nbucket + nchain) * 4;
	unsigned char *hb = (unsigned char *)ld_malloc(hash_size);
	if (!hb) return ld_error(ctx, "out of memory");
	memset(hb, 0, hash_size);
	write32(hb, nbucket);
	write32(hb + 4, nchain);
	uint32_t *bucket = (uint32_t *)(hb + 8);
	uint32_t *chain = (uint32_t *)(hb + 8 + (size_t)nbucket * 4);
	/* For each exported symbol (index 1..nsym in .dynsym) */
	for (i = 0; i < nsym; ++i) {
		struct ld_dynsym_entry *e = &ctx->dynsym_entries[i];
		uint32_t h = elf_hash32(e->global->name);
		uint32_t bi = h % nbucket;
		uint32_t sym_ndx = (uint32_t)(i + 1);
		chain[sym_ndx] = bucket[bi];
		bucket[bi] = sym_ndx;
	}
	uint64_t hash_off;
	if (append_group_data(ctx, &ctx->groups[dg_hash], hb, hash_size, 4, &hash_off) != 0) {
		free(hb);
		return -1;
	}
	free(hb);

	/* ---- .dynamic (placeholder with correct size, filled in pass 2) ---- */
	size_t ntags = 5; /* SYMTAB, SYMENT, STRTAB, STRSZ, HASH */
	if (ctx->soname) ntags++;

	/* Count JUMP_SLOT entries separately — they go into .rela.plt. */
	size_t n_jumpslots = 0;
	size_t n_regular_rela = 0;
	for (i = 0; i < ctx->got.count; ++i) {
		if (ctx->got.items[i].reloc_type == MT_R_X86_64_JUMP_SLOT) {
			n_jumpslots++;
		} else if (ctx->got.items[i].tls) {
			n_regular_rela += (ctx->got.items[i].slots >= 2) ? 2 : 1;
		} else {
			n_regular_rela++;
		}
	}

	/* Count data-section RELATIVE entries needed for PIE/shared mode.
	 * In PIE mode, every R_X86_64_64/32/32S relocation that targets a
	 * defined symbol in this link needs a RELATIVE entry so the dynamic
	 * loader can adjust the absolute address by the load base at runtime.
	 * Without this, data-section pointer initializers (e.g.
	 * `int *p = &bss_var[4]`) carry stale addresses → SIGSEGV. */
	size_t data_rela_count = 0;
	if (ctx->shared || ctx->pie) {
		for (i = 0; i < ctx->objects.count; ++i) {
			struct ld_object *object = &ctx->objects.items[i];
			if (object->is_shared) continue;
			for (uint16_t si = 0; si < object_section_count(object); ++si) {
				struct mt_elf64_section sec;
				if (object_get_section(object, si, &sec) != 0) continue;
				if (sec.type != MT_SHT_RELA && sec.type != MT_SHT_REL) continue;
				if (sec.entry_size == 0 || sec.size % sec.entry_size != 0) continue;
				for (uint64_t ri = 0; ri < sec.size / sec.entry_size; ++ri) {
					const unsigned char *p = object->data + sec.offset + ri * sec.entry_size;
					uint64_t info64;
					int type;
					uint64_t sym_idx;
					struct mt_elf64_symbol sym;
					const char *sym_name;
					struct ld_global *g;
					if (object->elf_class == 1) {
						uint32_t info32 = read32(p + 4);
						type = (int)(info32 & 0xff);
						sym_idx = info32 >> 8;
					} else {
						info64 = read64(p + 8);
						type = (int)(info64 & 0xffffffffu);
						sym_idx = info64 >> 32;
					}
					/* Only count absolute data relocations targeting
					 * defined symbols in this link. */
					if (type != LD_R_X86_64_64 &&
					    type != LD_R_X86_64_32 &&
					    type != LD_R_X86_64_32S)
						continue;
					if (get_symbol_by_index(ctx, object, sym_idx, &sym, &sym_name) != 0)
						continue;
					g = find_global(ctx, sym_name);
					if (!g || !g->defined || g->group < 0) continue;
					/* Skip TLS symbols (handled separately). */
					const char *gname = ctx->groups[g->group].name;
					if (gname && (strcmp(gname, ".tdata") == 0 ||
					              strcmp(gname, ".tbss") == 0))
						continue;
					data_rela_count++;
				}
			}
		}
	}
	n_regular_rela += data_rela_count;

	/* Pre-create .rela.dyn group for non-PLT dynamic relocations (must exist
	 * before layout_output assigns addresses).  Content is filled by
	 * build_rela_dyn() after layout. */
	int have_rela = ((ctx->shared || ctx->pie) && n_regular_rela > 0);
	if (have_rela) {
		ntags += 3; /* DT_RELA, DT_RELASZ, DT_RELAENT */
		int rg = get_group(ctx, ".rela.dyn", MT_SHT_RELA,
		                   LD_SHF_ALLOC, 8);
		if (rg < 0)
			return ld_error(ctx, "out of memory");
		ctx->groups[rg].rank = 1; /* same rank as .rodata/dynstr/dynsym */
		ctx->rela_capacity_entries = n_regular_rela;
		size_t rela_es = elf64 ? 24 : 12;
		uint64_t rdyn_off;
		if (append_group_data(ctx, &ctx->groups[rg], NULL,
		                      n_regular_rela * rela_es, 8, &rdyn_off) != 0)
			return -1;
		ctx->rela_count = 0;
	} else {
		ctx->rela_capacity_entries = 0;
		ctx->rela_count = 0;
	}

	/* Pre-create .rela.plt group for JUMP_SLOT relocations (DT_JMPREL).
	 * These are separate so the dynamic loader can process PLT relocations
	 * via DT_JMPREL rather than mixing them into .rela.dyn. */
	int have_rela_plt = ((ctx->shared || ctx->pie) && n_jumpslots > 0);
	if (have_rela_plt) {
		ntags += 3; /* DT_JMPREL, DT_PLTRELSZ, DT_PLTREL */
		int rpg = get_group(ctx, ".rela.plt", MT_SHT_RELA,
		                    LD_SHF_ALLOC, 8);
		if (rpg < 0)
			return ld_error(ctx, "out of memory");
		ctx->groups[rpg].rank = 1; /* same rank as .rela.dyn */
		ctx->rela_plt_capacity_entries = n_jumpslots;
		size_t rela_es = elf64 ? 24 : 12;
		uint64_t rplt_off;
		if (append_group_data(ctx, &ctx->groups[rpg], NULL,
		                      n_jumpslots * rela_es, 8, &rplt_off) != 0)
			return -1;
		ctx->rela_plt_count = 0;
	} else {
		ctx->rela_plt_capacity_entries = 0;
		ctx->rela_plt_count = 0;
	}

	/* ---- .plt stubs (one per JUMP_SLOT GOT entry) ----
	 * mt/ld has no lazy-binding PLT machinery, so each PLT32 function
	 * import gets a tiny 6-byte stub `jmp *GOT_slot(%rip)` in an RX .plt
	 * section.  The caller's `call foo@PLT` (a direct rel32 call) targets
	 * the stub, which then jumps through the GOT slot that ld.so fills
	 * with the resolved function address.  Without the stub, the call
	 * would target the GOT slot's own address and execute the 8-byte
	 * pointer as code (SIGSEGV).  The disp32 is filled in after layout,
	 * once both the stub and the GOT slot addresses are known. */
	{
		size_t njumps = 0;
		for (i = 0; i < ctx->got.count; ++i)
			if (ctx->got.items[i].reloc_type == MT_R_X86_64_JUMP_SLOT)
				njumps++;
		if (njumps > 0) {
			int pg = get_group(ctx, ".plt", MT_SHT_PROGBITS,
			                   LD_SHF_ALLOC | LD_SHF_EXECINSTR, 16);
			if (pg < 0)
				return ld_error(ctx, "out of memory");
			ctx->groups[pg].rank = 0; /* RX, like .text */
			static const unsigned char stub[6] = {
				0xff, 0x25, 0x00, 0x00, 0x00, 0x00
			};
			uint64_t stub_off = 0;
			for (i = 0; i < ctx->got.count; ++i) {
				if (ctx->got.items[i].reloc_type != MT_R_X86_64_JUMP_SLOT)
					continue;
				if (append_group_data(ctx, &ctx->groups[pg], stub,
				                      sizeof(stub), 16, &stub_off) != 0)
					return -1;
				ctx->got.items[i].plt_offset = stub_off;
			}
		}
	}
	/* .init / .fini / .init_array / .fini_array / .preinit_array */
	int have_init = (find_group(ctx, ".init") >= 0);
	int have_fini = (find_group(ctx, ".fini") >= 0);
	int have_init_arr = (find_group(ctx, ".init_array") >= 0);
	int have_fini_arr = (find_group(ctx, ".fini_array") >= 0);
	int have_preinit_arr = (find_group(ctx, ".preinit_array") >= 0);
	if (have_init) ntags++;      /* DT_INIT */
	if (have_fini) ntags++;      /* DT_FINI */
	if (have_init_arr) ntags += 2;  /* DT_INIT_ARRAY + DT_INIT_ARRAYSZ */
	if (have_fini_arr) ntags += 2;  /* DT_FINI_ARRAY + DT_FINI_ARRAYSZ */
	if (have_preinit_arr) ntags += 2; /* DT_PREINIT_ARRAY + DT_PREINIT_ARRAYSZ */
	/* Count DT_NEEDED entries: CLI --add-needed + input .so files */
	{
		size_t dso_needed = 0;
		for (i = 0; i < ctx->objects.count; i++)
			if (ctx->objects.items[i].is_shared)
				dso_needed++;
		if (ctx->add_needed_count > 0 || dso_needed > 0)
			ntags += ctx->add_needed_count + dso_needed;
	}
	ntags++; /* DT_NULL terminator */
	size_t dynent = elf64 ? 16 : 8;
	size_t dyn_total = ntags * dynent;
	int dg_dyn = find_group(ctx, ".dynamic"); /* created by ensure_dynamic_section */
	if (dg_dyn < 0) return ld_error(ctx, "out of memory");
	unsigned char *dd = (unsigned char *)ld_malloc(dyn_total ? dyn_total : 1);
	if (!dd) return ld_error(ctx, "out of memory");
	memset(dd, 0, dyn_total);
	uint64_t dyn_off;
	if (append_group_data(ctx, &ctx->groups[dg_dyn], dd, dyn_total, 8, &dyn_off) != 0) {
		free(dd);
		return -1;
	}
	free(dd);

	return 0;
}

static int
write_dt_addr(unsigned char *dp, int elf64, size_t *k,
              uint64_t tag, uint64_t val)
{
	if (elf64) {
		write64(dp + *k * 16 + 0, tag);
		write64(dp + *k * 16 + 8, val);
	} else {
		write32(dp + *k * 8 + 0, (uint32_t)tag);
		write32(dp + *k * 8 + 4, (uint32_t)val);
	}
	++*k;
	return 0;
}

int
fill_dynamic_addresses(struct ld_context *ctx)
{
	size_t i;

	if (!ctx->shared && !ctx->pie)
		return 0;

	int elf64 = (ctx->target->elf_class == MT_ELFCLASS64);
	size_t symsize = elf64 ? MT_ELF64_SYM_SIZE : MT_ELF32_SYM_SIZE;

	int dg_sym = find_group(ctx, ".dynsym");
	int dg_str = find_group(ctx, ".dynstr");
	int dg_hash = find_group(ctx, ".hash");
	int dg_dyn = find_group(ctx, ".dynamic");
	if (dg_sym < 0 || dg_str < 0 || dg_hash < 0 || dg_dyn < 0)
		return ld_error(ctx, "internal: dynamic sections not created");

	struct ld_group *gsym = &ctx->groups[dg_sym];
	struct ld_group *gstr = &ctx->groups[dg_str];
	struct ld_group *ghash = &ctx->groups[dg_hash];
	struct ld_group *gdyn = &ctx->groups[dg_dyn];

	uint64_t sym_addr   = gsym->address;
	uint64_t str_addr   = gstr->address;
	uint64_t hash_addr  = ghash->address;
	(void)sym_addr; (void)str_addr; (void)hash_addr;
	(void)gdyn;

	/* Fill .dynsym entries (skip null symbol at index 0) */
	for (i = 0; i < ctx->dynsym_count; ++i) {
		struct ld_dynsym_entry *e = &ctx->dynsym_entries[i];
		struct ld_global *g = e->global;
		uint64_t st_value;
		uint16_t st_shndx;
		uint8_t st_bind = LD_STB_GLOBAL;
		if (g->defined && g->group >= 0) {
			if (e->stt == MT_STT_TLS) {
				/* TLS symbols carry a block-relative st_value: the offset
				 * of the variable within the module's TLS block (.tdata
				 * base).  Using the .tdata runtime address here would make
				 * rtld's DTPOFF resolution bake in the vaddr and break
				 * __tls_get_addr (the dynamic loader does block-relative
				 * offset arithmetic). */
				st_value = g->offset;
				st_shndx = (uint16_t)(g->group + 1);
			} else {
				st_value = ctx->groups[g->group].address + g->offset;
				st_shndx = (uint16_t)(g->group + 1);
			}
		} else {
			st_value = 0;
			st_shndx = 0;
			st_bind = g->weak ? LD_STB_WEAK : LD_STB_GLOBAL;
		}
		uint8_t info = (uint8_t)((st_bind << LD_STB_SHIFT) | e->stt);
		unsigned char *p = gsym->data + ctx->dynsym_data_offset +
		                   (i + 1) * symsize;
		write_dynsym_entry(p, elf64, e->dynstr_offset, info, 0,
		                   st_shndx, st_value, g->size);
	}

	/* Fill .dynamic DT entries in the order reserved by build_dynamic_tables:
	 *   SYMTAB, SYMENT, STRTAB, STRSZ, HASH, [SONAME,] NULL */
	size_t k = 0;
	unsigned char *dp = gdyn->data;

	/* DT_SYMTAB */
	if (elf64) {
		write64(dp + k * 16 + 0, MT_DT_SYMTAB);
		write64(dp + k * 16 + 8, sym_addr);
	} else {
		write32(dp + k * 8 + 0, MT_DT_SYMTAB);
		write32(dp + k * 8 + 4, (uint32_t)sym_addr);
	}
	++k;

	/* DT_SYMENT */
	if (elf64) {
		write64(dp + k * 16 + 0, MT_DT_SYMENT);
		write64(dp + k * 16 + 8, (uint64_t)symsize);
	} else {
		write32(dp + k * 8 + 0, MT_DT_SYMENT);
		write32(dp + k * 8 + 4, (uint32_t)symsize);
	}
	++k;

	/* DT_STRTAB */
	if (elf64) {
		write64(dp + k * 16 + 0, MT_DT_STRTAB);
		write64(dp + k * 16 + 8, str_addr);
	} else {
		write32(dp + k * 8 + 0, MT_DT_STRTAB);
		write32(dp + k * 8 + 4, (uint32_t)str_addr);
	}
	++k;

	/* DT_STRSZ */
	if (elf64) {
		write64(dp + k * 16 + 0, MT_DT_STRSZ);
		write64(dp + k * 16 + 8, (uint64_t)gstr->size);
	} else {
		write32(dp + k * 8 + 0, MT_DT_STRSZ);
		write32(dp + k * 8 + 4, (uint32_t)gstr->size);
	}
	++k;

	/* DT_HASH */
	if (elf64) {
		write64(dp + k * 16 + 0, MT_DT_HASH);
		write64(dp + k * 16 + 8, hash_addr);
	} else {
		write32(dp + k * 8 + 0, MT_DT_HASH);
		write32(dp + k * 8 + 4, (uint32_t)hash_addr);
	}
	++k;

	/* DT_SONAME (optional) */
	if (ctx->soname) {
		if (elf64) {
			write64(dp + k * 16 + 0, MT_DT_SONAME);
			write64(dp + k * 16 + 8, ctx->soname_dynstr_offset);
		} else {
			write32(dp + k * 8 + 0, MT_DT_SONAME);
			write32(dp + k * 8 + 4, ctx->soname_dynstr_offset);
		}
		++k;
	}

	/* DT_NEEDED entries from --add-needed and input .so files */
	if (ctx->needed_dynstr_offsets) {
		size_t needed_total = ctx->add_needed_count;
		for (i = 0; i < ctx->objects.count; i++)
			if (ctx->objects.items[i].is_shared)
				needed_total++;
		for (i = 0; i < needed_total; ++i) {
			if (elf64) {
				write64(dp + k * 16 + 0, MT_DT_NEEDED);
				write64(dp + k * 16 + 8, ctx->needed_dynstr_offsets[i]);
			} else {
				write32(dp + k * 8 + 0, MT_DT_NEEDED);
				write32(dp + k * 8 + 4, ctx->needed_dynstr_offsets[i]);
			}
			++k;
		}
	}

	/* DT_INIT / DT_FINI / DT_INIT_ARRAY / DT_FINI_ARRAY / DT_PREINIT_ARRAY */
	{
		int dg;
		dg = find_group(ctx, ".init");
		if (dg >= 0 && write_dt_addr(dp, elf64, &k, MT_DT_INIT,
		                             ctx->groups[dg].address) != 0) return -1;
		dg = find_group(ctx, ".fini");
		if (dg >= 0 && write_dt_addr(dp, elf64, &k, MT_DT_FINI,
		                             ctx->groups[dg].address) != 0) return -1;
		dg = find_group(ctx, ".init_array");
		if (dg >= 0) {
			if (write_dt_addr(dp, elf64, &k, MT_DT_INIT_ARRAY,
			                  ctx->groups[dg].address) != 0) return -1;
			if (write_dt_addr(dp, elf64, &k, MT_DT_INIT_ARRAYSZ,
			                  ctx->groups[dg].size) != 0) return -1;
		}
		dg = find_group(ctx, ".fini_array");
		if (dg >= 0) {
			if (write_dt_addr(dp, elf64, &k, MT_DT_FINI_ARRAY,
			                  ctx->groups[dg].address) != 0) return -1;
			if (write_dt_addr(dp, elf64, &k, MT_DT_FINI_ARRAYSZ,
			                  ctx->groups[dg].size) != 0) return -1;
		}
		dg = find_group(ctx, ".preinit_array");
		if (dg >= 0) {
			if (write_dt_addr(dp, elf64, &k, MT_DT_PREINIT_ARRAY,
			                  ctx->groups[dg].address) != 0) return -1;
			if (write_dt_addr(dp, elf64, &k, MT_DT_PREINIT_ARRAYSZ,
			                  ctx->groups[dg].size) != 0) return -1;
		}
	}

	/* DT_RELA / DT_RELASZ / DT_RELAENT (for non-JUMP_SLOT dynamic relocs) */
	int dg_rela = find_group(ctx, ".rela.dyn");
	if (dg_rela >= 0) {
		struct ld_group *grela = &ctx->groups[dg_rela];
		uint64_t rela_entsize = elf64 ? 24 : 12;
		uint64_t rela_sz = ctx->rela_capacity_entries * rela_entsize;
		/* DT_RELA: address of .rela.dyn */
		if (elf64) {
			write64(dp + k * 16 + 0, MT_DT_RELA);
			write64(dp + k * 16 + 8, grela->address);
		} else {
			write32(dp + k * 8 + 0, MT_DT_RELA);
			write32(dp + k * 8 + 4, (uint32_t)grela->address);
		}
		++k;
		/* DT_RELASZ: size of .rela.dyn */
		if (elf64) {
			write64(dp + k * 16 + 0, MT_DT_RELASZ);
			write64(dp + k * 16 + 8, rela_sz);
		} else {
			write32(dp + k * 8 + 0, MT_DT_RELASZ);
			write32(dp + k * 8 + 4, (uint32_t)rela_sz);
		}
		++k;
		/* DT_RELAENT: size of each RELA entry */
		if (elf64) {
			write64(dp + k * 16 + 0, MT_DT_RELAENT);
			write64(dp + k * 16 + 8, rela_entsize);
		} else {
			write32(dp + k * 8 + 0, MT_DT_RELAENT);
			write32(dp + k * 8 + 4, (uint32_t)rela_entsize);
		}
		++k;
	}

	/* DT_JMPREL / DT_PLTRELSZ / DT_PLTREL (for JUMP_SLOT PLT relocations) */
	int dg_rplt = find_group(ctx, ".rela.plt");
	if (dg_rplt >= 0) {
		struct ld_group *grplt = &ctx->groups[dg_rplt];
		uint64_t rela_entsize = elf64 ? 24 : 12;
		uint64_t pltrel_sz = ctx->rela_plt_capacity_entries * rela_entsize;
		/* DT_JMPREL: address of .rela.plt */
		if (elf64) {
			write64(dp + k * 16 + 0, MT_DT_JMPREL);
			write64(dp + k * 16 + 8, grplt->address);
		} else {
			write32(dp + k * 8 + 0, MT_DT_JMPREL);
			write32(dp + k * 8 + 4, (uint32_t)grplt->address);
		}
		++k;
		/* DT_PLTRELSZ: size of .rela.plt */
		if (elf64) {
			write64(dp + k * 16 + 0, MT_DT_PLTRELSZ);
			write64(dp + k * 16 + 8, pltrel_sz);
		} else {
			write32(dp + k * 8 + 0, MT_DT_PLTRELSZ);
			write32(dp + k * 8 + 4, (uint32_t)pltrel_sz);
		}
		++k;
		/* DT_PLTREL: indicates the type of PLT relocations (DT_RELA) */
		if (elf64) {
			write64(dp + k * 16 + 0, MT_DT_PLTREL);
			write64(dp + k * 16 + 8, MT_DT_RELA);
		} else {
			write32(dp + k * 8 + 0, MT_DT_PLTREL);
			write32(dp + k * 8 + 4, MT_DT_RELA);
		}
		++k;
	}

	/* DT_NULL terminator is already zeroed in the placeholder — but write
	 * it explicitly for clarity. */
	if (elf64) {
		write64(dp + k * 16 + 0, MT_DT_NULL);
		write64(dp + k * 16 + 8, 0);
	} else {
		write32(dp + k * 8 + 0, MT_DT_NULL);
		write32(dp + k * 8 + 4, 0);
	}
	return 0;
}

int
ensure_dynamic_section(struct ld_context *ctx)
{
	if (!ctx->shared && !ctx->pie)
		return 0;
	int g = get_group(ctx, ".dynamic", MT_SHT_DYNAMIC,
	                  LD_SHF_ALLOC | LD_SHF_WRITE, 8);
	if (g < 0)
		return ld_error(ctx, "out of memory");
	ctx->groups[g].rank = 2; /* same rank as .got, inside the RW region */
	return 0;
}

int
ensure_pie_section(struct ld_context *ctx)
{
	if (!ctx->pie)
		return 0;
	int g = get_group(ctx, ".interp", MT_SHT_PROGBITS,
	                  LD_SHF_ALLOC, 1);
	if (g < 0)
		return ld_error(ctx, "out of memory");
	ctx->groups[g].rank = 1; /* read-only region */
	const char *interp = ctx->dynamic_linker
	                     ? ctx->dynamic_linker : "/lib/ld-meuos.so.1";
	uint64_t off;
	if (append_group_data(ctx, &ctx->groups[g],
	                       (const unsigned char *)interp,
	                       strlen(interp) + 1, 1, &off) != 0)
		return -1;
	return 0;
}