/* symbol.c - mt/ld symbol collection, definition and resolution.
 *
 * Collects object/shared/archive symbols (collect_symbols / archives /
 * collect_one_object_symbols / collect_shared_object_symbols), applies
 * --defsym and --wrap (apply_defsym / apply_wrap), allocates COMMON
 * symbols, and resolves a symbol's value (symbol_value).  Extracted from
 * link.c (per-stage module split).
 */
#include "ld_internal.h"

static int
object_get_strtab_string(struct ld_object *object, uint32_t offset,
                         const char **value)
{
	if (object->elf_class == 1)
		return mt_elf32_get_string(object->data, object->size,
		                            &object->strtab.v32, offset, value);
	return mt_elf64_get_string(object->data, object->size,
	                            &object->strtab.v64, offset, value);
}

static int
archive_member_needed_impl(struct ld_context *ctx, const unsigned char *data,
                      size_t size, int elf_class)
{
	union {
		struct mt_elf64_view v64;
		struct mt_elf32_view v32;
	} view;
	union {
		struct mt_elf64_section v64;
		struct mt_elf32_section v32;
	} symtab, strtab;
	struct mt_elf64_symbol symbol;
	const char *name;
	uint16_t i;
	uint64_t j;
	uint16_t section_count;
	unsigned binding;

	/* Parse based on elf_class */
	if (elf_class == 1) {
		if (mt_elf32_parse(data, size, &view.v32) != MT_ELF_OK ||
		    view.v32.type != MT_ET_REL ||
		    view.v32.machine != ctx->target->emachine)
			return 0;
		section_count = view.v32.section_count;
	} else {
		if (mt_elf64_parse(data, size, &view.v64) != MT_ELF_OK ||
		    view.v64.type != MT_ET_REL ||
		    view.v64.machine != ctx->target->emachine)
			return 0;
		section_count = view.v64.section_count;
	}

	for (i = 0; i < section_count; ++i) {
		if (elf_class == 1) {
			struct mt_elf32_section s32;
			if (mt_elf32_get_section(data, size, &view.v32, i, &s32) != MT_ELF_OK)
				return -1;
			if (s32.type != MT_SHT_SYMTAB)
				continue;
			symtab.v32 = s32;
			if (s32.link >= section_count ||
			    mt_elf32_get_section(data, size, &view.v32,
			                         (uint16_t)s32.link, &strtab.v32) != MT_ELF_OK)
				return -1;
			for (j = 0; s32.entry_size > 0 && j < s32.size / s32.entry_size; ++j) {
				struct mt_elf32_symbol sym32;
				struct ld_global *global;
				if (mt_elf32_get_symbol(data, size, &symtab.v32, j,
				                        &sym32) != MT_ELF_OK)
					return -1;
				binding = sym32.info >> LD_STB_SHIFT;
				if (binding == LD_STB_LOCAL || sym32.section == MT_SHN_UNDEF ||
				    sym32.name == 0 ||
				    mt_elf32_get_string(data, size, &strtab.v32, sym32.name,
				                       &name) != MT_ELF_OK)
					continue;
				global = find_global(ctx, name);
				if (global && !global->defined)
					return 1;
			}
		} else {
			struct mt_elf64_section s64;
			if (mt_elf64_get_section(data, size, &view.v64, i, &s64) != MT_ELF_OK)
				return -1;
			if (s64.type != MT_SHT_SYMTAB)
				continue;
			symtab.v64 = s64;
			if (s64.link >= section_count ||
			    mt_elf64_get_section(data, size, &view.v64,
			                         (uint16_t)s64.link, &strtab.v64) != MT_ELF_OK)
				return -1;
			for (j = 0; s64.entry_size > 0 && j < s64.size / s64.entry_size; ++j) {
				struct ld_global *global;
				if (mt_elf64_get_symbol(data, size, &symtab.v64, j,
				                        &symbol) != MT_ELF_OK)
					return -1;
				binding = symbol.info >> LD_STB_SHIFT;
				if (binding == LD_STB_LOCAL || symbol.section == MT_SHN_UNDEF ||
				    symbol.name == 0 ||
				    mt_elf64_get_string(data, size, &strtab.v64, symbol.name,
				                       &name) != MT_ELF_OK)
					continue;
				global = find_global(ctx, name);
				if (global && !global->defined)
					return 1;
			}
		}
		break;
	}
	return 0;
}

static int
collect_one_object_sections(struct ld_context *ctx, size_t object_index)
{
	struct ld_object *object = &ctx->objects.items[object_index];
	struct mt_elf64_section section;
	const char *name;
	struct ld_group *out;
	uint16_t j;
	int group;
	uint64_t offset;
	if (object->is_shared)
		return 0;
	for (j = 0; j < object_section_count(object); ++j) {
		if (object_get_section(object, j, &section) != 0)
			return ld_errorf(ctx, "invalid section in object", object->name);
		if (section.type != MT_SHT_PROGBITS && section.type != MT_SHT_NOBITS &&
		    section.type != MT_SHT_INIT_ARRAY &&
		    section.type != MT_SHT_FINI_ARRAY &&
		    section.type != MT_SHT_PREINIT_ARRAY)
			continue;
		name = object_section_name(object, j);
		if (!name || !*name)
			return ld_errorf(ctx, "section has no name", object->name);
		group = get_group(ctx, name, section.type, section.flags,
		                  section.alignment ? section.alignment : 1);
		if (group < 0)
			return ld_error(ctx, "out of memory");
		out = &ctx->groups[group];
		/* Set up group rank and flags based on section attributes */
		if (section.flags & LD_SHF_ALLOC) {
			/* Allocatable sections keep their original attributes */
		} else {
			/* Non-allocatable sections (debug, comments, etc.) go at
			 * the end of the file, after all loadable content.  They
			 * are NOT included in PT_LOAD segments. */
			ctx->groups[group].flags = section.flags;
		}
		if (section.type == MT_SHT_NOBITS) {
			if (append_group_data(ctx, out, NULL, (size_t)section.size,
			                      section.alignment ? section.alignment : 1,
			                      &offset) != 0)
				return -1;
		} else if (append_group_data(ctx, out,
		                      object->data + section.offset,
		                      (size_t)section.size,
		                      section.alignment ? section.alignment : 1,
		                      &offset) != 0)
			return -1;
		object->maps[j].group = group;
		object->maps[j].offset = offset;
	}
	return 0;
}


static int
prepare_object_symbol(struct ld_context *ctx, struct ld_object *object)
{
	struct mt_elf64_section section;
	uint16_t i;
	uint16_t sec_count = object_section_count(object);
	object->has_symtab = 0;
	for (i = 0; i < sec_count; ++i) {
		if (object_get_section(object, i, &section) != 0)
			return ld_errorf(ctx, "invalid symbol section", object->name);
		if (section.type != MT_SHT_SYMTAB)
			continue;
		if (section.link >= sec_count)
			return ld_errorf(ctx, "invalid symbol table", object->name);
		if (object->elf_class == 1) {
			struct mt_elf32_section strtab32;
			if (mt_elf32_get_section(object->data, object->size,
			                         &object->view.v32,
			                         (uint16_t)section.link,
			                         &strtab32) != MT_ELF_OK ||
			    strtab32.type != MT_SHT_STRTAB ||
			    section.entry_size < MT_ELF32_SYM_SIZE ||
			    section.size % section.entry_size != 0)
				return ld_errorf(ctx, "invalid symbol table", object->name);
			object->strtab.v32 = strtab32;
			/* Convert the 64-bit section fields back to 32-bit for storage */
			{
				struct mt_elf32_section sym32;
				sym32.name = section.name;
				sym32.type = section.type;
				sym32.flags = (uint32_t)section.flags;
				sym32.address = (uint32_t)section.address;
				sym32.offset = (uint32_t)section.offset;
				sym32.size = (uint32_t)section.size;
				sym32.link = section.link;
				sym32.info = section.info;
				sym32.alignment = (uint32_t)section.alignment;
				sym32.entry_size = (uint32_t)section.entry_size;
				object->symtab.v32 = sym32;
			}
		} else {
			if (mt_elf64_get_section(object->data, object->size,
			                         &object->view.v64,
			                         (uint16_t)section.link,
			                         &object->strtab.v64) != MT_ELF_OK ||
			    object->strtab.v64.type != MT_SHT_STRTAB ||
			    section.entry_size < MT_ELF64_SYM_SIZE ||
			    section.size % section.entry_size != 0)
				return ld_errorf(ctx, "invalid symbol table", object->name);
			object->symtab.v64 = section;
		}
		object->symtab_index = i;
		object->has_symtab = 1;
		break;
	}
	return 0;
}

static int
register_global_symbol(struct ld_context *ctx, struct ld_object *object,
                       uint64_t index, const struct mt_elf64_symbol *symbol,
                       const char *name)
{
	struct ld_global *global;
	unsigned binding = symbol->info >> LD_STB_SHIFT;
	int defined = symbol->section != MT_SHN_UNDEF &&
	              symbol->section != LD_SHN_COMMON;
	int common = symbol->section == LD_SHN_COMMON;

	if (!name || !*name || binding == LD_STB_LOCAL)
		return 0;
	global = get_global(ctx, name);
	if (!global)
		return ld_error(ctx, "out of memory");
	if (defined) {
		if (symbol->section >= object_section_count(object) ||
		    object->maps[symbol->section].group < 0)
			return ld_errorf(ctx, "symbol points to discarded section", name);
		if (global->defined && !global->weak && binding != LD_STB_WEAK)
			return ld_errorf(ctx, "multiple definition", name);
		if (!global->defined || (global->weak && binding != LD_STB_WEAK)) {
			global->object = object;
			global->symbol_index = index;
			global->group = object->maps[symbol->section].group;
			global->offset = object->maps[symbol->section].offset + symbol->value;
			global->size = symbol->size;
			global->align = 1;
			global->defined = 1;
			global->weak = binding == LD_STB_WEAK;
			global->common = 0;
		}
	} else if (common) {
		if (!global->defined) {
			global->common = 1;
			if (global->size < symbol->size)
				global->size = symbol->size;
			if (global->align < symbol->value)
				global->align = symbol->value;
		}
	} else if (!global->defined) {
		global->weak = binding == LD_STB_WEAK;
	}
	return 0;
}

static int
collect_one_object_symbols(struct ld_context *ctx, size_t object_index)
{
	struct ld_object *object = &ctx->objects.items[object_index];
	struct mt_elf64_symbol symbol;
	const char *name;
	uint64_t j, sym_count;
	if (object->is_shared)
		return 0;
	if (prepare_object_symbol(ctx, object) != 0)
		return -1;
	if (!object->has_symtab)
		return 0;
	/* Copy symtab/strtab to 64-bit local vars for iteration */
	if (object->elf_class == 1) {
		struct mt_elf32_section *s32 = &object->symtab.v32;
		sym_count = s32->size / s32->entry_size;
	} else {
		struct mt_elf64_section *s64 = &object->symtab.v64;
		sym_count = s64->size / s64->entry_size;
	}
	for (j = 0; j < sym_count; ++j) {
		if (object_get_symbol(object, j, &symbol) != 0)
			return ld_errorf(ctx, "invalid symbol", object->name);
		if (symbol.name == 0 ||
		    object_get_strtab_string(object, symbol.name, &name) != 0)
			continue;
		if (register_global_symbol(ctx, object, j, &symbol, name) != 0)
			return -1;
	}
	return 0;
}

/* Extract exported symbols from a shared library (.so) object.
 * .so files have SHT_DYNSYM (not SHT_SYMTAB) for exported symbols.
 * Symbols are added to globals as "defined" to satisfy references
 * from other objects, but without a group mapping (.so sections
 * are not merged into the output). */
static int
collect_shared_object_symbols(struct ld_context *ctx, size_t object_index)
{
	struct ld_object *object = &ctx->objects.items[object_index];
	struct mt_elf64_section section;
	uint16_t j;
	int dso_dynsym_sec = -1, dso_dynstr_sec = -1;

	for (j = 0; j < object_section_count(object); ++j) {
		if (object_get_section(object, j, &section) != 0) continue;
		if (section.type == MT_SHT_DYNSYM) {
			dso_dynsym_sec = j;
			if (section.link < object_section_count(object))
				dso_dynstr_sec = (int)section.link;
			break;
		}
	}
	if (dso_dynsym_sec < 0) return 0;

	size_t entry_size = (size_t)section.entry_size;
	size_t sym_size = (object->elf_class == 1) ? 16 : 24;
	if (entry_size < sym_size || section.size < entry_size) return 0;
	size_t sym_count = section.size / entry_size;

	for (size_t si = 1; si < sym_count; si++) {
		struct mt_elf64_section dynsym_sec;
		struct mt_elf64_symbol sym;
		if (object_get_section(object, (uint16_t)dso_dynsym_sec,
		                       &dynsym_sec) != 0)
			continue;
		if (mt_elf64_get_symbol(object->data, object->size,
		                        &dynsym_sec, si, &sym) != MT_ELF_OK)
			continue;
		if (sym.section == 0 || sym.name == 0) continue;
		int binding = (sym.info >> 4) & 0xf;
		if (binding == 0) continue;

		/* Get name from .dynstr */
		const char *symname = NULL;
		if (dso_dynstr_sec >= 0) {
			struct mt_elf64_section ds;
			if (object_get_section(object, (uint16_t)dso_dynstr_sec, &ds) == 0) {
				if (mt_elf64_get_string(object->data, object->size,
				                        &ds, sym.name, &symname) != MT_ELF_OK)
					symname = NULL;
			}
		}
		if (!symname || !*symname) continue;

		struct ld_global *g = get_global(ctx, symname);
		if (!g) return ld_error(ctx, "out of memory");
		if (!g->defined) {
			g->defined = 1;
			g->weak = (binding == 2);
			g->offset = sym.value;
			g->size = sym.size;
			g->group = -1;
		}
	}
	return 0;
}

int
collect_symbols(struct ld_context *ctx)
{
	size_t i;
	for (i = 0; i < ctx->objects.count; ++i) {
		struct ld_object *obj = &ctx->objects.items[i];
		if (obj->is_shared) {
			if (collect_shared_object_symbols(ctx, i) != 0)
				return -1;
		} else {
			if (collect_one_object_symbols(ctx, i) != 0)
				return -1;
		}
	}
	return 0;
}

/* Apply --defsym: create or override a symbol at a fixed absolute address.
 * Format: SYM=VAL (VAL may use a 0x prefix for hexadecimal).
 * The new global is marked absolute so symbol_value() returns its raw
 * address without consulting any output section. */
int
apply_defsym(struct ld_context *ctx, const char *defsym)
{
	char symname[256];
	const char *eq, *valstr;
	size_t namelen;
	uint64_t val;
	struct ld_global *global;

	if (!defsym || !*defsym)
		return 0;
	eq = strchr(defsym, '=');
	if (!eq) {
		ld_errorf(ctx, "invalid --defsym (expect SYM=VAL)", defsym);
		return -1;
	}
	namelen = (size_t)(eq - defsym);
	if (namelen == 0 || namelen >= sizeof(symname)) {
		ld_errorf(ctx, "invalid --defsym symbol name", defsym);
		return -1;
	}
	memcpy(symname, defsym, namelen);
	symname[namelen] = '\0';

	valstr = eq + 1;
	if (valstr[0] == '0' && (valstr[1] == 'x' || valstr[1] == 'X')) {
		if (sscanf(valstr + 2, "%llx", (unsigned long long *)&val) != 1) {
			ld_errorf(ctx, "invalid --defsym hexadecimal value", defsym);
			return -1;
		}
	} else {
		val = (uint64_t)atol(valstr);
	}

	global = get_global(ctx, symname);
	if (!global)
		return ld_error(ctx, "out of memory");
	global->defined = 1;
	global->absolute = 1;
	global->weak = 0;
	global->common = 0;
	global->group = -1;
	global->offset = val;
	return 0;
}

/* Apply --wrap: redirect references to SYM to __wrap_SYM, and make
 * __real_SYM point at the original SYM definition (if any).
 *
 * The implementation reuses struct ld_global's alias field:
 *   SYM->alias = __wrap_SYM
 * so that any lookup of SYM via symbol_value() (and related resolvers)
 * follows the alias to __wrap_SYM.
 *
 * If SYM was defined, __real_SYM is set up as a copy of SYM's definition,
 * giving the user access to the original function/data. */
int
apply_wrap(struct ld_context *ctx, const char *sym)
{
	char wrap_name[256];
	char real_name[256];
	struct ld_global *g, *gw, *gr;

	if (!sym || !*sym)
		return 0;

	snprintf(wrap_name, sizeof wrap_name, "__wrap_%s", sym);
	snprintf(real_name, sizeof real_name, "__real_%s", sym);

	g  = get_global(ctx, sym);
	gw = get_global(ctx, wrap_name);
	gr = get_global(ctx, real_name);
	if (!g || !gw || !gr)
		return ld_error(ctx, "out of memory");

	/* If SYM has a definition, copy it to __real_SYM so the wrapped
	 * function can call __real_foo to access the original. */
	if (g->defined) {
		gr->defined   = 1;
		gr->absolute  = g->absolute;
		gr->weak      = g->weak;
		gr->common    = g->common;
		gr->group     = g->group;
		gr->offset    = g->offset;
		gr->size      = g->size;
		gr->align     = g->align;
		gr->object    = g->object;
		gr->symbol_index = g->symbol_index;
	}

	/* Redirect SYM to __wrap_SYM: any UNDEF reference to SYM will now
	 * resolve to __wrap_SYM (via the alias field in symbol_value()). */
	g->alias = gw;
	return 0;
}

struct archive_extract_context {
	struct ld_context *ctx;
	const char *archive;
	int added;
};

static int
archive_member_needed(struct ld_context *ctx, const unsigned char *data,
                      size_t size)
{
	return archive_member_needed_impl(ctx, data, size, ctx->target->elf_class);
}

static int
extract_archive_member(const struct mt_ar_member *member,
                       const unsigned char *data, void *context)
{
	struct archive_extract_context *extract =
	    (struct archive_extract_context *)context;
	struct ld_context *ctx = extract->ctx;
	char display[512];
	size_t index;
	int needed = ctx->whole_archive ? 1 :
	             archive_member_needed(ctx, data, (size_t)member->size);
	if (needed < 0)
		return ld_errorf(ctx, "invalid archive member", member->name);
	if (!needed)
		return 0;
	snprintf(display, sizeof(display), "%s(%s)", extract->archive, member->name);
	index = ctx->objects.count;

	/* With --whole-archive, skip already-extracted members to avoid
	 * infinite re-extraction in the do-while loop. */
	if (ctx->whole_archive) {
		size_t oi;
		for (oi = 0; oi < ctx->objects.count; oi++) {
			if (strcmp(ctx->objects.items[oi].name, display) == 0)
				return 0; /* already extracted */
		}
	}

	if (append_object(ctx, display, data, (size_t)member->size) != 0)
		return -1;
	if (collect_one_object_sections(ctx, index) != 0)
		return -1;
	if (collect_one_object_symbols(ctx, index) != 0)
		return -1;
	extract->added = 1;
	return 0;
}

int
extract_archives(struct ld_context *ctx)
{
	int changed;
	size_t i;
	do {
		changed = 0;
		for (i = 0; i < ctx->archives.count; ++i) {
			struct archive_extract_context extract = {
				.ctx = ctx, .archive = ctx->archives.paths[i], .added = 0
			};
			/* Use in-memory archive data if available (VFS .msys) */
			if (i < ctx->archive_mem_count &&
			    ctx->archive_mem_data[i] != NULL) {
				if (mt_ar_foreach_mem(ctx->archive_mem_data[i],
				                      ctx->archive_mem_size[i],
				                      extract_archive_member,
				                      &extract) != 0)
					return ld_errorf(ctx, "cannot extract archive",
					                 ctx->archives.paths[i]);
			} else {
				if (mt_ar_foreach(ctx->archives.paths[i],
				                  extract_archive_member,
				                  &extract) != 0)
					return ld_errorf(ctx, "cannot extract archive",
					                 ctx->archives.paths[i]);
			}
			if (extract.added)
				changed = 1;
		}
	} while (changed);
	return 0;
}

int
allocate_common(struct ld_context *ctx)
{
	struct ld_group *bss;
	size_t i;
	uint64_t offset;
	if (find_group(ctx, ".bss") < 0) {
		if (get_group(ctx, ".bss", MT_SHT_NOBITS,
		              LD_SHF_ALLOC | LD_SHF_WRITE, 8) < 0)
			return ld_error(ctx, "out of memory");
	}
	bss = &ctx->groups[find_group(ctx, ".bss")];
	for (i = 0; i < ctx->globals.count; ++i) {
		struct ld_global *global = &ctx->globals.items[i];
		if (!global->common || global->defined)
			continue;
		if (append_group_data(ctx, bss, NULL, (size_t)global->size,
		                      global->align ? global->align : 1,
		                      &offset) != 0)
			return -1;
		global->group = find_group(ctx, ".bss");
		global->offset = offset;
		global->defined = 1;
	}
	return 0;
}

int
get_symbol_by_index(struct ld_context *ctx, struct ld_object *object,
                    uint64_t index, struct mt_elf64_symbol *symbol,
                    const char **name)
{
	if (!object->has_symtab ||
	    object_get_symbol(object, index, symbol) != 0)
		return ld_errorf(ctx, "relocation symbol is invalid", object->name);
	if (symbol->name == 0) {
		*name = "";
		return 0;
	}
	if (object_get_strtab_string(object, symbol->name, name) != 0)
		return ld_errorf(ctx, "relocation string is invalid", object->name);
	return 0;
}

int
symbol_value(struct ld_context *ctx, struct ld_object *object,
             uint64_t symbol_index, uint64_t *value, const char **name_out)
{
	struct mt_elf64_symbol symbol;
	const char *name;
	struct ld_global *global;
	struct ld_section_map *map;
	if (get_symbol_by_index(ctx, object, symbol_index, &symbol, &name) != 0)
		return -1;
	if (name_out)
		*name_out = name;
	/* Skip nameless undefined symbols (symbol index 0 in RISCV,
	 * relaxation markers with no associated symbol name). */
	if (symbol.section == MT_SHN_UNDEF && (!name || !name[0])) {
		if (value) *value = 0;
		return 0;
	}
	if (symbol.section == MT_SHN_UNDEF) {
		global = find_global(ctx, name);
		if (!global)
			return ld_errorf(ctx, "undefined symbol", name);
		/* Follow alias chain (from --wrap) */
		while (global->alias)
			global = global->alias;
		if (!global->defined) {
			/* For shared/PIE output (ET_DYN), undefined external
			 * symbols are resolved at load time by ld.so.  Return 0
			 * as the placeholder value; the PLT32/GOTPCREL relocation
			 * handler will allocate a GOT entry and emit a JUMP_SLOT /
			 * GLOB_DAT dynamic relocation so ld.so fills the correct
			 * address. */
			if (ctx->shared || ctx->pie) {
				*value = 0;
				return 0;
			}
			return ld_errorf(ctx, "undefined symbol", name);
		}
		if (global->absolute)
			*value = global->offset + symbol.value;
		else if (global->group < 0) {
			/* Symbol defined only in an input shared library (its
			 * global has no output-section group).  It is external to
			 * this ET_DYN and must be resolved by ld.so at load time,
			 * so return 0 and let the PLT32/GOTPCREL dynamic-relocation
			 * path emit the GLOB_DAT/JUMP_SLOT entry.  Returning a
			 * baked-in vaddr from the .so (groups[-1] junk) would leave
			 * a static self-reference that crashes at runtime. */
			if (ctx->shared || ctx->pie) {
				*value = 0;
				return 0;
			}
			*value = 0;
		} else
			*value = ctx->groups[global->group].address +
			         global->offset + symbol.value;
		return 0;
	}
	if (symbol.section == LD_SHN_COMMON) {
		global = find_global(ctx, name);
		if (!global || !global->defined)
			return ld_errorf(ctx, "undefined common symbol", name);
		*value = ctx->groups[global->group].address + global->offset;
		return 0;
	}
	if (symbol.section >= object_section_count(object) ||
	    object->maps[symbol.section].group < 0)
		return ld_errorf(ctx, "symbol section was discarded", name);
	map = &object->maps[symbol.section];
	*value = ctx->groups[map->group].address + map->offset + symbol.value;
	return 0;
}
